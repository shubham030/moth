/* Hot push over the USB cable — no network required.
 *
 * The same USB-Serial-JTAG console the board logs through is a bidirectional
 * byte pipe, and the push protocol is just MPSH + length + blob over any byte
 * stream. A background task watches the console's receive side for that
 * framing; completed frames are handed to the same verify-then-swap path the
 * WiFi listener feeds. Plug in, push, done — WiFi is the upgrade, not the
 * prerequisite.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Installs the USB-Serial-JTAG driver and starts watching for pushes. */
void serialpush_start(void);

/* A completed frame if one has arrived, malloc'd and owned by the caller;
 * NULL otherwise. Non-blocking; call it from the frame hook alongside the
 * TCP poll. */
uint8_t *serialpush_poll(size_t *len_out);
