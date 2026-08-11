/* Persistence for pushed programs, so a push survives a reboot.
 *
 * The blob lives in the `mothb` data partition (partitions.csv) behind a
 * small header with a CRC. Loading maps the flash directly — the VM runs the
 * blob in place, the way it runs the embedded one — so a stored program
 * costs no RAM.
 *
 * The strike counter is the crash-loop guard: it is incremented before a
 * stored program runs and cleared once the program has stayed up for a
 * while. A blob that panics the chip on boot grows the counter each cycle;
 * at the limit the caller falls back to the embedded program instead of
 * rebooting forever.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PUSHSTORE_MAX_STRIKES 3

/* Writes a blob to the partition. False when there is no partition or the
 * save could not complete — and in the latter case the store is left
 * INVALID, never stale: a reboot after a failed save runs the embedded
 * program, not a program the user already replaced. Must not be called
 * while a pushstore_load'd blob is still running — the erase pulls the
 * flash out from under it. */
bool pushstore_save(const uint8_t *blob, size_t len);

/* Maps the stored blob and returns it, or NULL when nothing valid is
 * stored. The mapping lives until pushstore_release or _invalidate. */
const uint8_t *pushstore_load(size_t *len_out);

/* Unmaps a loaded blob without touching the stored bytes. For the path that
 * decides not to run the store this boot but wants it intact for the next. */
void pushstore_release(void);

/* Marks the stored blob invalid (releasing any mapping first), so the next
 * boot runs the embedded one. */
void pushstore_invalidate(void);

int pushstore_strikes(void);
void pushstore_add_strike(void);
void pushstore_clear_strikes(void);
