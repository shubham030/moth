/* SHA-256 and HMAC-SHA256, self-contained.
 *
 * Both push hosts need them — the board authenticates WiFi pushes against
 * the pairing key, and mothsim must verify the same frames on the desktop,
 * where mbedtls is not in the build. One ~150-line file both link beats two
 * crypto stacks that can drift; the cost is that THIS implementation must be
 * right, which is what the RFC 4231 / FIPS 180-4 vectors in
 * vm/test/hmac_test.c are for.
 *
 * Not constant-time in general; the one secret-dependent comparison callers
 * make is provided here as hmac_sha256_eq so nobody reaches for memcmp.
 */
#ifndef MOTH_HMAC_SHA256_H
#define MOTH_HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_LEN 32

typedef struct {
  uint32_t state[8];
  uint64_t total;   /* message bytes seen */
  uint8_t buf[64];  /* partial block */
  size_t buf_len;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[SHA256_DIGEST_LEN]);

/* One-shot convenience. */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);

/* HMAC-SHA256 over two segments, because the push frame authenticates
 * nonce || blob and the caller should not have to concatenate a megabyte
 * to prove it. Either segment may be NULL when its length is 0. `out` MAY
 * alias either input segment — both are fully absorbed before `out` is
 * written; pbkdf2_hmac_sha256's inner loop depends on this, so a streaming
 * rework that writes `out` early would break the KDF silently. */
void hmac_sha256_2(const uint8_t *key, size_t key_len,
                   const void *seg1, size_t len1,
                   const void *seg2, size_t len2,
                   uint8_t out[SHA256_DIGEST_LEN]);

/* Constant-time digest comparison; nonzero when equal. */
int hmac_sha256_eq(const uint8_t a[SHA256_DIGEST_LEN],
                   const uint8_t b[SHA256_DIGEST_LEN]);

/* PBKDF2-HMAC-SHA256, one 32-byte block (RFC 2898). This is the pairing
 * KDF: a bare SHA-256 of a human phrase hands a passive observer an offline
 * dictionary attack — one captured frame is a complete verifier, and people
 * do not type 128-bit phrases. The iteration count multiplies the
 * attacker's per-guess cost by itself; the salt kills precomputation shared
 * across targets. Every deriver (provision.py, mothc, this) must agree on
 * salt and count, so they live here. */
#define MOTH_PAIR_SALT "moth-push-v1"
#define MOTH_PAIR_ITERS 600000
void pbkdf2_hmac_sha256(const uint8_t *pass, size_t pass_len,
                        const uint8_t *salt, size_t salt_len, uint32_t iters,
                        uint8_t out[SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_HMAC_SHA256_H */
