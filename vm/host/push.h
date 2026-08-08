/* Receiving a pushed program over TCP.
 *
 * Framing is deliberately trivial: "MPSH", a u32 little-endian length, then
 * the blob. The VM verifies whatever arrives before running it, so a bad or
 * hostile push is refused rather than trusted — see docs/BYTECODE.md.
 *
 * Polled rather than threaded: the host already gets control once a frame,
 * and a second thread touching the VM would need locking for no benefit.
 */
#ifndef MOTH_PUSH_H
#define MOTH_PUSH_H

#include <stddef.h>
#include <stdint.h>

typedef struct moth_push moth_push;

/* Returns NULL if the port cannot be listened on. */
moth_push *moth_push_listen(int port);

/* Non-blocking. Returns a complete blob and its length once one has fully
 * arrived, otherwise NULL. The caller owns the returned buffer. */
uint8_t *moth_push_poll(moth_push *p, size_t *len_out);

void moth_push_close(moth_push *p);

#endif /* MOTH_PUSH_H */
