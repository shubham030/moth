/* The accept/reject POLICY of the TCP push receiver, driven over real
 * sockets on the loopback. hmac_test pins the primitive; this pins the
 * decision the primitive exists for — which frames a paired and an unpaired
 * receiver hand to their caller, and which verdict the sender hears. The
 * on-device run proved this once; a test keeps the header_want /
 * discard_left state machine honest through refactors.
 */
#include "../host/hmac_sha256.h"
#include "../host/push.h"
#include "../host/push_proto.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 57621

static int failures;

static void check(int ok, const char *what) {
  if (ok) return;
  fprintf(stderr, "FAIL: %s\n", what);
  failures++;
}

/* Connects, sends the whole buffer, and polls the receiver until it either
 * yields a blob or replies. Returns the blob (caller frees) or NULL, and
 * fills reply[] with up to MPSH_REPLY_LEN bytes read back (zeroed first). */
static uint8_t *roundtrip(moth_push *p, const uint8_t *frame, size_t frame_len,
                          bool respond_ok_if_blob, uint8_t reply[MPSH_REPLY_LEN]) {
  memset(reply, 0, MPSH_REPLY_LEN);
  int c = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(c, (struct sockaddr *)&a, sizeof a) != 0) {
    close(c);
    check(0, "connect");
    return NULL;
  }
  size_t sent = 0;
  while (sent < frame_len) {
    ssize_t n = send(c, frame + sent, frame_len - sent, 0);
    if (n <= 0) break;
    sent += (size_t)n;
  }
  uint8_t *blob = NULL;
  size_t blob_len = 0;
  /* Generous bound; each iteration is a non-blocking poll + 1ms sleep. */
  for (int i = 0; i < 2000 && !blob; i++) {
    blob = moth_push_poll(p, &blob_len);
    if (!blob) usleep(1000);
    /* A reply may already be on the wire (reject paths). */
    ssize_t r = recv(c, reply, MPSH_REPLY_LEN, MSG_DONTWAIT);
    if (r == MPSH_REPLY_LEN) break;
  }
  if (blob && respond_ok_if_blob) {
    moth_push_respond(p, true);
    ssize_t got = 0;
    for (int i = 0; i < 2000 && got < MPSH_REPLY_LEN; i++) {
      ssize_t r = recv(c, reply + got, (size_t)(MPSH_REPLY_LEN - got), MSG_DONTWAIT);
      if (r > 0) got += r;
      else usleep(1000);
    }
  }
  close(c);
  return blob;
}

static size_t plain_frame(uint8_t *out, const uint8_t *blob, size_t len,
                          uint32_t nonce) {
  memcpy(out, MPSH_MAGIC, 4);
  out[4] = (uint8_t)len; out[5] = out[6] = out[7] = 0;
  mpsh_nonce_le(nonce, out + 8);
  memcpy(out + MPSH_HEADER_LEN, blob, len);
  return MPSH_HEADER_LEN + len;
}

static size_t authed_frame(uint8_t *out, const uint8_t *blob, size_t len,
                           uint32_t nonce, const uint8_t key[32]) {
  memcpy(out, MPH2_MAGIC, 4);
  out[4] = (uint8_t)len; out[5] = out[6] = out[7] = 0;
  mpsh_nonce_le(nonce, out + 8);
  uint8_t nl[4];
  mpsh_nonce_le(nonce, nl);
  hmac_sha256_2(key, 32, nl, 4, blob, len, out + MPSH_HEADER_LEN);
  memcpy(out + MPH2_HEADER_LEN, blob, len);
  return MPH2_HEADER_LEN + len;
}

static int reply_is(const uint8_t reply[MPSH_REPLY_LEN], const char *cc,
                    uint32_t nonce) {
  uint8_t want[MPSH_REPLY_LEN];
  mpsh_make_reply(want, cc[2] == 'O', nonce);
  (void)cc;
  return memcmp(reply, want, MPSH_REPLY_LEN) == 0;
}

int main(void) {
  const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
  uint8_t key[32];
  sha256("policy-test-key", 15, key); /* any 32 bytes; KDF is not under test */
  uint8_t frame[MPH2_HEADER_LEN + sizeof payload];
  uint8_t reply[MPSH_REPLY_LEN];

  moth_push *p = moth_push_listen(PORT);
  if (!p) {
    fprintf(stderr, "SKIP: port %d unavailable\n", PORT);
    return 0; /* an occupied port is an environment problem, not a failure */
  }
  moth_push_set_key(p, key);

  /* Paired + valid MPH2: the blob reaches the caller intact, MPOK echoes. */
  size_t n = authed_frame(frame, payload, sizeof payload, 0x11111111, key);
  uint8_t *blob = roundtrip(p, frame, n, true, reply);
  check(blob != NULL, "paired accepts a signed frame");
  if (blob) {
    check(memcmp(blob, payload, sizeof payload) == 0,
          "the delivered blob is the sent blob");
    free(blob);
  }
  check(reply_is(reply, "MPOK", 0x11111111), "MPOK echoes the nonce");

  /* Paired + plain MPSH: rejected, and the sender hears a real MPRJ. */
  n = plain_frame(frame, payload, sizeof payload, 0x22222222);
  blob = roundtrip(p, frame, n, true, reply);
  check(blob == NULL, "paired refuses an unsigned frame");
  check(reply_is(reply, "MPRJ", 0x22222222), "the refusal is a framed MPRJ");

  /* Paired + tampered blob: the MAC no longer matches. */
  n = authed_frame(frame, payload, sizeof payload, 0x33333333, key);
  frame[MPH2_HEADER_LEN] ^= 0x01;
  blob = roundtrip(p, frame, n, true, reply);
  check(blob == NULL, "paired refuses a tampered blob");
  check(reply_is(reply, "MPRJ", 0x33333333), "tampering earns MPRJ");

  /* Paired + wrong key: same refusal. */
  uint8_t wrong[32];
  sha256("some-other-key", 14, wrong);
  n = authed_frame(frame, payload, sizeof payload, 0x44444444, wrong);
  blob = roundtrip(p, frame, n, true, reply);
  check(blob == NULL, "paired refuses a wrong-key signature");

  /* Unpaired: both frames are accepted. */
  moth_push_set_key(p, NULL);
  n = plain_frame(frame, payload, sizeof payload, 0x55555555);
  blob = roundtrip(p, frame, n, true, reply);
  check(blob != NULL, "unpaired accepts a plain frame");
  free(blob);
  check(reply_is(reply, "MPOK", 0x55555555), "unpaired MPOK echoes");
  n = authed_frame(frame, payload, sizeof payload, 0x66666666, key);
  blob = roundtrip(p, frame, n, true, reply);
  check(blob != NULL, "unpaired accepts a signed frame without checking");
  free(blob);

  moth_push_close(p);
  if (failures) {
    fprintf(stderr, "%d push policy failure(s)\n", failures);
    return 1;
  }
  printf("push policy: paired and unpaired receivers behave\n");
  return 0;
}
