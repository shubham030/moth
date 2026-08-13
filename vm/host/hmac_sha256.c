/* See hmac_sha256.h for why this exists. FIPS 180-4 SHA-256, RFC 2104 HMAC. */
#include "hmac_sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_ctx *c, const uint8_t p[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
           ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->state[0], b = c->state[1], d = c->state[3], e = c->state[4];
  uint32_t f = c->state[5], g = c->state[6], h = c->state[7], cc = c->state[2];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + K[i] + w[i];
    uint32_t s0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
    uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = s0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
  c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

void sha256_init(sha256_ctx *c) {
  c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
  c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
  c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
  c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
  c->total = 0;
  c->buf_len = 0;
}

void sha256_update(sha256_ctx *c, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  c->total += len;
  if (c->buf_len) {
    size_t take = 64 - c->buf_len;
    if (take > len) take = len;
    memcpy(c->buf + c->buf_len, p, take);
    c->buf_len += take;
    p += take;
    len -= take;
    if (c->buf_len == 64) {
      sha256_block(c, c->buf);
      c->buf_len = 0;
    }
  }
  while (len >= 64) {
    sha256_block(c, p);
    p += 64;
    len -= 64;
  }
  if (len) {
    memcpy(c->buf, p, len);
    c->buf_len = len;
  }
}

void sha256_final(sha256_ctx *c, uint8_t out[SHA256_DIGEST_LEN]) {
  uint64_t bits = c->total * 8;
  uint8_t pad = 0x80;
  sha256_update(c, &pad, 1);
  uint8_t zero = 0;
  while (c->buf_len != 56) sha256_update(c, &zero, 1);
  uint8_t len_be[8];
  for (int i = 0; i < 8; i++) len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
  sha256_update(c, len_be, 8);
  for (int i = 0; i < 8; i++) {
    out[i * 4] = (uint8_t)(c->state[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
    out[i * 4 + 3] = (uint8_t)c->state[i];
  }
}

void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]) {
  sha256_ctx c;
  sha256_init(&c);
  sha256_update(&c, data, len);
  sha256_final(&c, out);
}

void hmac_sha256_2(const uint8_t *key, size_t key_len,
                   const void *seg1, size_t len1,
                   const void *seg2, size_t len2,
                   uint8_t out[SHA256_DIGEST_LEN]) {
  uint8_t k[64];
  memset(k, 0, sizeof k);
  if (key_len > 64) {
    sha256(key, key_len, k); /* long keys are hashed first, per RFC 2104 */
  } else {
    memcpy(k, key, key_len);
  }
  uint8_t pad[64];
  sha256_ctx c;

  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
  sha256_init(&c);
  sha256_update(&c, pad, 64);
  if (len1) sha256_update(&c, seg1, len1);
  if (len2) sha256_update(&c, seg2, len2);
  uint8_t inner[SHA256_DIGEST_LEN];
  sha256_final(&c, inner);

  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
  sha256_init(&c);
  sha256_update(&c, pad, 64);
  sha256_update(&c, inner, sizeof inner);
  sha256_final(&c, out);

  memset(k, 0, sizeof k);
  memset(pad, 0, sizeof pad);
}

int hmac_sha256_eq(const uint8_t a[SHA256_DIGEST_LEN],
                   const uint8_t b[SHA256_DIGEST_LEN]) {
  uint8_t d = 0;
  for (int i = 0; i < SHA256_DIGEST_LEN; i++) d |= a[i] ^ b[i];
  return d == 0;
}
