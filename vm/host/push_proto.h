/* The push wire format, in one place: "MPSH", a 32-bit little-endian length,
 * then the blob. Both receivers — TCP (push.c) and the board's USB console
 * (serialpush.c) — decode against these, so the magic, the limit and the
 * length encoding cannot drift apart.
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
#define MPSH_HEADER_LEN 8

/* A megabyte is far beyond any real program. */
#define MPSH_MAX_BLOB (1u << 20)

/* The length field from a complete 8-byte header. */
static inline size_t mpsh_header_len(const uint8_t h[MPSH_HEADER_LEN]) {
  return (size_t)h[4] | ((size_t)h[5] << 8) | ((size_t)h[6] << 16) |
         ((size_t)h[7] << 24);
}

/* True when a decoded length can be a real frame. */
static inline int mpsh_len_ok(size_t len) {
  return len > 0 && len <= MPSH_MAX_BLOB;
}

#endif /* MOTH_PUSH_PROTO_H */
