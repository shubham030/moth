/* Receiving a pushed program over TCP.
 *
 * Framing lives in push_proto.h: "MPSH", a u32 length, a u32 nonce, then the
 * blob. The VM verifies whatever arrives before running it, so a bad or
 * hostile push is refused rather than trusted — see docs/BYTECODE.md.
 *
 * Polled rather than threaded: the host already gets control once a frame,
 * and a second thread touching the VM would need locking for no benefit.
 */
#ifndef MOTH_PUSH_H
#define MOTH_PUSH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct moth_push moth_push;

/* Returns NULL if the port cannot be listened on. */
moth_push *moth_push_listen(int port);

/* Non-blocking. Returns a complete blob and its length once one has fully
 * arrived, otherwise NULL. The caller owns the returned buffer, and the
 * client connection stays open until moth_push_respond delivers the
 * verification verdict — respond after every non-NULL return. */
uint8_t *moth_push_poll(moth_push *p, size_t *len_out);

/* Sends the framed verdict (MPOK/MPRJ + the frame's nonce) for the blob the
 * last poll returned, and closes that client. Best-effort: a peer that
 * vanished just misses its reply. */
void moth_push_respond(moth_push *p, bool ok);

/* Closes the client WITHOUT a verdict, for when none truthfully exists —
 * the verifier could not run, which says nothing about the blob. The sender
 * times out and retries; MPRJ would tell it the program was bad. */
void moth_push_abandon(moth_push *p);

void moth_push_close(moth_push *p);

#endif /* MOTH_PUSH_H */
