#include "push.h"
#include "hmac_sha256.h"
#include "push_proto.h"

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Sized for the larger frame; how much of it a given frame fills depends on
 * the magic in its first four bytes. */
#define HEADER_LEN MPH2_HEADER_LEN

/* A peer that disappears without a FIN or an RST — a board losing power, a
 * laptop closing its lid mid-push — leaves a socket that never reports an
 * error and never delivers a byte. Without a deadline it would hold the
 * channel forever and no later push could be accepted. Progress resets it,
 * so this only fires on a connection that has gone completely silent. */
#define PUSH_STALL_MS 5000

static uint32_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

struct moth_push {
  int listener;
  int client;
  uint8_t header[HEADER_LEN];
  size_t header_got;
  size_t header_want;     /* MPSH_ or MPH2_HEADER_LEN once the magic is known */
  uint8_t *blob;
  size_t blob_len, blob_got;
  size_t discard_left;    /* rejected frame's blob still owed by the wire */
  uint32_t nonce;         /* from the frame being received */
  bool frame_authed;      /* current frame is MPH2 */
  bool awaiting_verdict;  /* poll returned a blob; respond has not run */
  uint32_t last_progress_ms;
  uint8_t key[SHA256_DIGEST_LEN];
  bool key_set;
};

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* True when a failed read simply had nothing to give. Anything else — a reset
 * connection, a client killed mid-transfer — has to drop the client, or the
 * channel stays occupied by a peer that will never send another byte and no
 * further push is ever accepted. */
static bool read_would_block(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

static void drop_client(moth_push *p) {
  if (p->client >= 0) close(p->client);
  p->client = -1;
  p->header_got = 0;
  p->header_want = 0;
  p->frame_authed = false;
  p->discard_left = 0;
  free(p->blob);
  p->blob = NULL;
  p->blob_len = 0;
  p->blob_got = 0;
}

/* Best-effort framed reply to the current client. Shared by the verdict
 * path and the auth-reject path, so a rejection is a real MPRJ the sender's
 * scanner recognizes rather than a bare closed socket. */
static void send_reply(moth_push *p, bool ok) {
  if (p->client < 0) return;
  uint8_t reply[MPSH_REPLY_LEN];
  mpsh_make_reply(reply, ok, p->nonce);
#ifdef MSG_NOSIGNAL
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0;
#endif
  /* Nonblocking socket: one EAGAIN must not eat the verdict, since the
   * sender has no other way to learn it. Elapsed-time form, not an
   * absolute deadline — now_ms() wraps every 49.7 days and an absolute
   * `now + 500` near the wrap never admits even one iteration. Yield on
   * EAGAIN rather than spinning a core against a full buffer. */
  size_t off = 0;
  const uint32_t start = now_ms();
  while (off < sizeof reply && now_ms() - start < 500) {
    ssize_t n = send(p->client, reply + off, sizeof reply - off, flags);
    if (n > 0) {
      off += (size_t)n;
    } else if (n < 0 && !read_would_block()) {
      break; /* peer is gone; it just misses its reply */
    } else {
      usleep(1000);
    }
  }
}

moth_push *moth_push_listen(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return NULL;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 1) < 0) {
    close(fd);
    return NULL;
  }
  set_nonblocking(fd);

  moth_push *p = calloc(1, sizeof *p);
  if (!p) { close(fd); return NULL; }
  p->listener = fd;
  p->client = -1;
  return p;
}

uint8_t *moth_push_poll(moth_push *p, size_t *len_out) {
  if (!p || p->awaiting_verdict) return NULL;

  if (p->client < 0) {
    int c = accept(p->listener, NULL, NULL);
    if (c < 0) return NULL; /* nothing waiting, or a transient failure */
    set_nonblocking(c);
#ifdef SO_NOSIGPIPE
    /* The verdict reply is this file's only write; a peer that closed first
     * must cost an EPIPE errno, not a process-killing SIGPIPE. Linux and
     * lwIP get the same via MSG_NOSIGNAL at the send. */
    int nosig = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
#endif
    p->client = c;
    p->last_progress_ms = now_ms();
  } else if (now_ms() - p->last_progress_ms > PUSH_STALL_MS) {
    fprintf(stderr, "push: client stalled, dropping it\n");
    fflush(stderr);
    drop_client(p);
    return NULL;
  }

  if (p->header_want == 0 || p->header_got < p->header_want) {
    /* Until the magic is in, only its four bytes are owed; the magic then
     * decides whether an HMAC follows the nonce. Reading the short header
     * length first would steal blob bytes from an MPH2 frame. */
    size_t want = p->header_want ? p->header_want : (size_t)MPSH_MAGIC_LEN;
    ssize_t n = read(p->client, p->header + p->header_got, want - p->header_got);
    if (n == 0) { drop_client(p); return NULL; }
    if (n < 0) {
      if (!read_would_block()) drop_client(p);
      return NULL;
    }
    p->header_got += (size_t)n;
    p->last_progress_ms = now_ms();

    if (p->header_want == 0) {
      if (p->header_got < MPSH_MAGIC_LEN) return NULL;
      if (memcmp(p->header, MPH2_MAGIC, MPSH_MAGIC_LEN) == 0) {
        p->frame_authed = true;
        p->header_want = MPH2_HEADER_LEN;
      } else if (memcmp(p->header, MPSH_MAGIC, MPSH_MAGIC_LEN) == 0) {
        p->frame_authed = false;
        p->header_want = MPSH_HEADER_LEN;
      } else {
        drop_client(p);
        return NULL;
      }
    }
    if (p->header_got < p->header_want) return NULL;

    p->blob_len = mpsh_header_len(p->header);
    p->nonce = mpsh_header_nonce(p->header);
    if (!mpsh_len_ok(p->blob_len)) { drop_client(p); return NULL; }

    /* A paired receiver refuses a legacy frame at the header: no buffer is
     * allocated and the verifier never runs for it. The doomed blob is
     * still READ (into a scratch buffer, below) before the MPRJ goes out —
     * replying and closing mid-stream makes the peer's next write draw an
     * RST, and an RST discards the un-read reply on the sender's side, so
     * the honest "you need a token" verdict became a silent timeout. The
     * stall timer still bounds a peer that never finishes sending. */
    if (p->key_set && !p->frame_authed) {
      fprintf(stderr, "push: rejected — this board is paired and the push "
                      "carried no token\n");
      fflush(stderr);
      p->discard_left = p->blob_len;
    } else {
      p->blob = malloc(p->blob_len);
      if (!p->blob) { drop_client(p); return NULL; }
      p->blob_got = 0;
    }
  }

  if (p->discard_left > 0) {
    uint8_t scratch[512];
    while (p->discard_left > 0) {
      size_t want = p->discard_left < sizeof scratch ? p->discard_left
                                                     : sizeof scratch;
      ssize_t n = read(p->client, scratch, want);
      if (n == 0) { drop_client(p); return NULL; }
      if (n < 0) {
        if (!read_would_block()) drop_client(p);
        return NULL;
      }
      p->discard_left -= (size_t)n;
      p->last_progress_ms = now_ms();
    }
    send_reply(p, false);
    drop_client(p);
    return NULL;
  }

  while (p->blob_got < p->blob_len) {
    ssize_t n = read(p->client, p->blob + p->blob_got, p->blob_len - p->blob_got);
    if (n == 0) { drop_client(p); return NULL; }
    if (n < 0) {
      /* Partway through a blob: a dropped peer means the half-read blob is
       * junk, so the whole transfer is abandoned rather than resumed. */
      if (!read_would_block()) drop_client(p);
      return NULL;
    }
    p->blob_got += (size_t)n;
    p->last_progress_ms = now_ms();
  }

  /* The whole frame is in. A paired receiver now holds every byte the HMAC
   * covers; a frame that fails the check never reaches the caller, so the
   * bytecode verifier only ever sees blobs the key's holder sent. An MPH2
   * frame reaching an UNPAIRED receiver is accepted without a check — there
   * is no key to check against, and refusing it would make pairing the
   * sender before the board a hard ordering. */
  if (p->key_set && p->frame_authed) {
    uint8_t nonce_le[4], want[SHA256_DIGEST_LEN];
    mpsh_nonce_le(p->nonce, nonce_le);
    hmac_sha256_2(p->key, sizeof p->key, nonce_le, sizeof nonce_le,
                  p->blob, p->blob_len, want);
    if (!hmac_sha256_eq(want, p->header + MPSH_HEADER_LEN)) {
      fprintf(stderr, "push: rejected — token mismatch\n");
      fflush(stderr);
      send_reply(p, false);
      drop_client(p);
      return NULL;
    }
  }

  uint8_t *complete = p->blob;
  *len_out = p->blob_len;
  p->blob = NULL; /* ownership passes to the caller */
  /* The client stays open: the reply is the VERDICT, and the verdict does
   * not exist until the caller has verified the blob. An earlier version
   * acked on receipt, which told the sender "pushed" for a blob the host
   * then rejected. */
  p->awaiting_verdict = true;
  return complete;
}

void moth_push_respond(moth_push *p, bool ok) {
  if (!p || !p->awaiting_verdict) return;
  /* The verdict flag clears even when the peer is already gone — otherwise
   * a client that vanished mid-verify would leave the channel refusing every
   * future push. */
  send_reply(p, ok);
  p->awaiting_verdict = false;
  drop_client(p);
}

void moth_push_set_key(moth_push *p, const uint8_t *key32) {
  if (!p) return;
  if (key32) {
    memcpy(p->key, key32, sizeof p->key);
    p->key_set = true;
  } else {
    memset(p->key, 0, sizeof p->key);
    p->key_set = false;
  }
}

void moth_push_abandon(moth_push *p) {
  if (!p || !p->awaiting_verdict) return;
  p->awaiting_verdict = false;
  drop_client(p);
}

void moth_push_close(moth_push *p) {
  if (!p) return;
  drop_client(p);
  close(p->listener);
  free(p);
}
