/* The push wire format, in one place: "MPSH", a 32-bit little-endian length,
 * a 32-bit nonce, then the blob. The reply — sent AFTER the host verified
 * the blob — is 8 bytes: "MPOK" or "MPRJ" followed by the same nonce.
 *
 * The nonce is the whole design. Three review rounds each found a new way a
 * grepped log-line ack could be forged or missed: first by the receivers'
 * own log tags, then by a program calling print('push: ...') under the same
 * tag. Any text ack on a console shared with user output loses that game
 * eventually. A binary reply echoing a nonce the sender just invented cannot
 * be produced by log output, survives interleaving with it, and doubles as
 * proof the reply answers THIS push rather than a stale one — which is why
 * the receivers need no drain-the-backlog step at all.
 *
 * Both receivers — TCP (push.c) and the board's USB console (serialpush.c)
 * — decode against these, so the magic, the limit and the field encoding
 * cannot drift apart.
 *
 * The receivers' STATE MACHINES stay separate on purpose, and the difference
 * is semantic, not drift: TCP has connection framing, so a byte that breaks
 * the pattern means the peer is confused and the right move is to drop the
 * connection. A serial console has no connections and shares the wire with
 * log output and stray keystrokes, so the right move is to resynchronize on
 * the next byte that could start a frame.
 */
#ifndef MOTH_PUSH_PROTO_H
#define MOTH_PUSH_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define MPSH_MAGIC "MPSH"
#define MPSH_MAGIC_LEN 4
#define MPSH_HEADER_LEN 12 /* magic + u32 len + u32 nonce */

#define MPSH_REPLY_OK "MPOK"
#define MPSH_REPLY_REJECT "MPRJ"
#define MPSH_REPLY_LEN 8 /* 4CC + u32 nonce */

/* A megabyte is far beyond any real program. */
#define MPSH_MAX_BLOB (1u << 20)

static inline size_t mpsh_header_len(const uint8_t h[MPSH_HEADER_LEN]) {
  return (size_t)h[4] | ((size_t)h[5] << 8) | ((size_t)h[6] << 16) |
         ((size_t)h[7] << 24);
}

static inline uint32_t mpsh_header_nonce(const uint8_t h[MPSH_HEADER_LEN]) {
  return (uint32_t)h[8] | ((uint32_t)h[9] << 8) | ((uint32_t)h[10] << 16) |
         ((uint32_t)h[11] << 24);
}

/* True when a decoded length can be a real frame. */
static inline int mpsh_len_ok(size_t len) {
  return len > 0 && len <= MPSH_MAX_BLOB;
}

/* Fills an 8-byte reply. */
static inline void mpsh_make_reply(uint8_t out[MPSH_REPLY_LEN], int ok,
                                   uint32_t nonce) {
  const char *cc = ok ? MPSH_REPLY_OK : MPSH_REPLY_REJECT;
  out[0] = (uint8_t)cc[0];
  out[1] = (uint8_t)cc[1];
  out[2] = (uint8_t)cc[2];
  out[3] = (uint8_t)cc[3];
  out[4] = (uint8_t)(nonce & 0xFF);
  out[5] = (uint8_t)((nonce >> 8) & 0xFF);
  out[6] = (uint8_t)((nonce >> 16) & 0xFF);
  out[7] = (uint8_t)((nonce >> 24) & 0xFF);
}

#endif /* MOTH_PUSH_PROTO_H */
