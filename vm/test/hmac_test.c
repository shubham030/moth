/* The crypto in vm/host/hmac_sha256.c is hand-rolled (see the header for
 * why), so it is held to published vectors: FIPS 180-4 for SHA-256, RFC 4231
 * for HMAC. The incremental and two-segment paths are checked against the
 * one-shot, because those are the shapes the push receiver actually uses. */
#include "../host/hmac_sha256.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const uint8_t got[32], const char *hex, const char *what) {
  uint8_t want[32];
  for (int i = 0; i < 32; i++) {
    unsigned b;
    sscanf(hex + i * 2, "%2x", &b);
    want[i] = (uint8_t)b;
  }
  if (memcmp(got, want, 32) != 0) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

int main(void) {
  uint8_t d[32];

  /* FIPS 180-4 */
  sha256("", 0, d);
  expect(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
         "sha256 of empty");
  sha256("abc", 3, d);
  expect(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "sha256 of abc");
  const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256(two, strlen(two), d);
  expect(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
         "sha256 two-block");

  /* The same message fed a byte at a time must match the one-shot: the
   * receiver hashes a blob in whatever chunk sizes the socket produced. */
  sha256_ctx c;
  sha256_init(&c);
  for (size_t i = 0; i < strlen(two); i++) sha256_update(&c, two + i, 1);
  sha256_final(&c, d);
  expect(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
         "sha256 byte-at-a-time");

  /* RFC 4231 case 1 */
  uint8_t key1[20];
  memset(key1, 0x0b, sizeof key1);
  hmac_sha256_2(key1, sizeof key1, "Hi There", 8, NULL, 0, d);
  expect(d, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
         "hmac case 1");

  /* RFC 4231 case 2 */
  hmac_sha256_2((const uint8_t *)"Jefe", 4,
                "what do ya want for nothing?", 28, NULL, 0, d);
  expect(d, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
         "hmac case 2");

  /* RFC 4231 case 6 — a key longer than the block, hashed first */
  uint8_t key6[131];
  memset(key6, 0xaa, sizeof key6);
  const char *msg6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  hmac_sha256_2(key6, sizeof key6, msg6, strlen(msg6), NULL, 0, d);
  expect(d, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
         "hmac long key");

  /* Splitting the message across the two segments equals the one-shot —
   * this is exactly HMAC(key, nonce || blob) as the frame check computes. */
  uint8_t whole[32], split[32];
  hmac_sha256_2(key1, sizeof key1, "Hi There", 8, NULL, 0, whole);
  hmac_sha256_2(key1, sizeof key1, "Hi ", 3, "There", 5, split);
  if (!hmac_sha256_eq(whole, split)) {
    fprintf(stderr, "FAIL: two-segment split\n");
    failures++;
  }

  /* And the constant-time comparison rejects a one-bit difference. */
  split[31] ^= 1;
  if (hmac_sha256_eq(whole, split)) {
    fprintf(stderr, "FAIL: eq accepted a differing digest\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%d hmac failure(s)\n", failures);
    return 1;
  }
  printf("hmac: all published vectors match\n");
  return 0;
}
