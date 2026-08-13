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

/* The authenticated frame: same header, then a 32-byte HMAC-SHA256 computed
 * with the pairing key over nonce (little-endian) || blob, then the blob.
 *
 * The HMAC, not a bare token, because a token crosses the network in the
 * clear on every push — one sniff and any machine on the LAN can push
 * arbitrary programs forever. With the HMAC the secret never travels: an
 * observer can at most REPLAY a frame it saw, re-installing a program the
 * owner already chose to run. That residual is accepted for v0.1 and
 * recorded in docs/DECISIONS.md; closing it needs a challenge-response
 * round-trip.
 *
 * A paired TCP receiver accepts only this frame. An unpaired one accepts
 * both (out of the box nothing is provisioned, and the first-run experience
 * must not demand a secret). Serial receivers accept both and never check
 * the HMAC — plugging in the cable is the pairing. */
#define MPH2_MAGIC "MPH2"
#define MPH2_HMAC_LEN 32
#define MPH2_HEADER_LEN (MPSH_HEADER_LEN + MPH2_HMAC_LEN)

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

/* The length and nonce fields sit at the same offsets in both frames, so
 * the MPSH decoders above apply; the MPH2 header just carries the HMAC
 * after them. This helper writes the nonce in the exact byte order the
 * HMAC's first segment must use — the frame's little-endian encoding, so
 * both sides authenticate the bytes that actually crossed the wire. */
static inline void mpsh_nonce_le(uint32_t nonce, uint8_t out[4]) {
  out[0] = (uint8_t)(nonce & 0xFF);
  out[1] = (uint8_t)((nonce >> 8) & 0xFF);
  out[2] = (uint8_t)((nonce >> 16) & 0xFF);
  out[3] = (uint8_t)((nonce >> 24) & 0xFF);
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
