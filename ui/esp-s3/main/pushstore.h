/* Persistence for pushed programs, so a push survives a reboot.
 *
 * The blob lives in the `mothb` data partition (partitions.csv) behind a
 * small header with a CRC. Loading maps the flash directly — the VM runs the
 * blob in place, the way it runs the embedded one — so a stored program
 * costs no RAM.
 *
 * The strike counter is the crash-loop guard, driven by reset ground truth:
 * on boot, a panic/watchdog reset that happened while the stored program was
 * running (pushstore_boot_was_store) adds a strike, and any clean reset
 * clears the record. A blob that crashes the chip — at boot or an hour in —
 * accumulates strikes until the caller falls back to the embedded program.
 * (An earlier design refunded a strike after ten stable seconds of uptime;
 * a program panicking after the refund defeated it forever, so it was
 * replaced. Brownouts are power faults and never count.)
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

/* True when the previous boot was running a pushed program — so a crash
 * reset can be charged to it rather than to the embedded one. */
bool pushstore_boot_was_store(void);
void pushstore_set_boot_source(bool store);
