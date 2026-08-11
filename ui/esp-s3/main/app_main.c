/* moth on the ESP32-S3 with a display: the VM runs a Dart program, the same
 * moth_render code draws it, and the panel shows the result.
 *
 * The Dart program owns the loop, so the host does its work from the frame
 * hook inside uiCommit — exactly as mothsim does on the desktop.
 */
#include "hotpush.h"
#include "moth_render.h"
#include "moth_ui.h"
#include "moth_vm.h"
#include "panel.h"
#include "push.h"
#include "pushstore.h"
#include "serialpush.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "moth";

/* When 1, logs where each frame's time goes. See docs/ROADMAP.md R3.
 * `make fps` (tools/fpsbench) turns it on from the build with
 * -DMOTH_FPSBENCH=1, which also gives moth_render's MR_PROFILE the clock and
 * counters the split needs; a normal build measures nothing. */
#ifndef MOTH_FRAME_PROFILE
#define MOTH_FRAME_PROFILE 0
#endif

extern const uint8_t program_start[] asm("_binary_program_mothb_start");
extern const uint8_t program_end[] asm("_binary_program_mothb_end");

#if MOTH_FRAME_PROFILE
/* moth_render's MR_PROFILE clock, and its per-primitive accumulators. */
int64_t mr_prof_now_us(void) { return esp_timer_get_time(); }
extern int64_t mr_prof_layout_us, mr_prof_clear_us, mr_prof_rect_us,
    mr_prof_arc_us, mr_prof_text_us;

/* ---- boot microbench: what touching the damage band's pixels costs at all.
 * 177 rows is the band frame_bench damages, so these numbers subtract
 * directly from the measured frame. -------------------------------------- */
#define BENCH_W 466
#define BENCH_ROWS 177

/* The exact arithmetic paint's blend() does on its general path. */
static uint32_t bench_blend_f(uint32_t dst, uint32_t src) {
  float sa = ((src >> 24) & 0xFF) / 255.0f;
  uint32_t out = 0xFF000000u;
  for (int shift = 0; shift <= 16; shift += 8) {
    float d = (float)((dst >> shift) & 0xFF);
    float s2 = (float)((src >> shift) & 0xFF);
    out |= (uint32_t)(s2 * sa + d * (1.0f - sa)) << shift;
  }
  return out;
}

/* The integer replacement being considered: 8-bit alpha, two channels at a
 * time, no floats. */
static uint32_t bench_blend_i(uint32_t dst, uint32_t src, uint32_t a) {
  const uint32_t inv = 255u - a;
  uint32_t rb = (((dst & 0x00FF00FFu) * inv + (src & 0x00FF00FFu) * a) >> 8) &
                0x00FF00FFu;
  uint32_t g = (((dst & 0x0000FF00u) * inv + (src & 0x0000FF00u) * a) >> 8) &
               0x0000FF00u;
  return 0xFF000000u | rb | g;
}

static int64_t bench_pass(uint32_t *buf, size_t px, int reps, int kind) {
  int64_t best = INT64_MAX;
  for (int r = 0; r < reps; r++) {
    const int64_t t0 = esp_timer_get_time();
    switch (kind) {
      case 0: /* write only — what std::fill costs */
        for (size_t i = 0; i < px; i++) buf[i] = 0xFF112233u;
        break;
      case 1: /* read-modify-write, trivial op — the traffic floor for blending */
        for (size_t i = 0; i < px; i++) buf[i] ^= 0x00010101u;
        break;
      case 2: /* float blend, as paint does today */
        for (size_t i = 0; i < px; i++) buf[i] = bench_blend_f(buf[i], 0x80E8A33Du);
        break;
      case 3: /* integer blend, as paint could */
        for (size_t i = 0; i < px; i++) buf[i] = bench_blend_i(buf[i], 0x80E8A33Du, 0x80);
        break;
    }
    const int64_t dt = esp_timer_get_time() - t0;
    if (dt < best) best = dt;
  }
  return best;
}

static void membench(void) {
  const size_t px = (size_t)BENCH_W * BENCH_ROWS;
  uint32_t *ps = heap_caps_malloc(px * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  /* An eighth of the band fits internal RAM; loop it 8x for the same work. */
  const size_t in_px = px / 8;
  uint32_t *in = heap_caps_malloc(in_px * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!ps || !in) {
    ESP_LOGE(TAG, "membench: alloc failed (psram %p internal %p)", ps, in);
    free(ps);
    free(in);
    return;
  }
  memset(ps, 0x42, px * 4);
  memset(in, 0x42, in_px * 4);

  ESP_LOGI(TAG, "MEMBENCH %dx%d band, us per pass (best of 4):", BENCH_W, BENCH_ROWS);
  ESP_LOGI(TAG, "  psram    write %6lld  rmw %6lld  blend_f %6lld  blend_i %6lld",
           bench_pass(ps, px, 4, 0), bench_pass(ps, px, 4, 1),
           bench_pass(ps, px, 4, 2), bench_pass(ps, px, 4, 3));
  int64_t w = 0, m = 0, bf = 0, bi = 0;
  for (int r = 0; r < 8; r++) {
    w += bench_pass(in, in_px, 4, 0);
    m += bench_pass(in, in_px, 4, 1);
    bf += bench_pass(in, in_px, 4, 2);
    bi += bench_pass(in, in_px, 4, 3);
  }
  ESP_LOGI(TAG, "  internal write %6lld  rmw %6lld  blend_f %6lld  blend_i %6lld",
           w, m, bf, bi);
  free(ps);
  free(in);
}
#endif

/* ---- hot push: a new program arriving over WiFi ------------------------ */

static moth_push *s_push;      /* listener; NULL until WiFi is up */
static uint8_t *s_pending;     /* verified blob waiting to be swapped in */
static size_t s_pending_len;
static moth_vm *s_vm;          /* the running VM, so a push can halt it */

static void register_host_natives(moth_vm *vm);

/* What verification concluded — and "could not run the verifier" is not
 * "the blob is bad". Treating them alike let a transient out-of-memory at
 * boot erase a perfectly good stored program. */
typedef enum { BLOB_OK, BLOB_BAD, BLOB_UNVERIFIABLE } blob_verdict;

/* Loads a blob into a throwaway VM to find out whether it would run, without
 * touching the one that is running. The natives must match what the real VM
 * offers by NAME only — moth_ui_register_natives, not the full register,
 * which would reset the live program's event queue and eat any tap queued
 * while the probe ran. Logging is the caller's: a bad boot-time store and a
 * bad live push are different messages (and mothc greps for the latter). */
static blob_verdict verify_blob(const uint8_t *b, size_t len, char *why,
                                size_t why_len) {
  moth_vm *probe = moth_new();
  if (!probe) {
    snprintf(why, why_len, "out of memory for the verifier");
    return BLOB_UNVERIFIABLE;
  }
  register_host_natives(probe);
  moth_ui_register_natives(probe);
  moth_status st = moth_load(probe, b, len);
  if (st != MOTH_OK) snprintf(why, why_len, "%s", moth_error(probe));
  moth_free(probe);
  if (st == MOTH_OK) return BLOB_OK;
  /* Out of memory DURING the load is as transient as failing to create the
   * probe — it says nothing about the blob. Only a verdict about the blob
   * itself may erase a stored program. */
  return st == MOTH_ERR_OOM ? BLOB_UNVERIFIABLE : BLOB_BAD;
}

/* Takes a pushed blob if one has arrived and it survives verification.
 * Rejecting here rather than after the swap is the whole point: a bad blob
 * must not be able to take the running program down with it. */
typedef enum { PUSH_SRC_TCP, PUSH_SRC_SERIAL } push_src;

/* Routes the framed verdict back over whichever transport delivered the
 * frame. The reply carries the sender's nonce (push_proto.h), so the log
 * lines here are for the human watching the console — mothc no longer
 * reads them. */
static void respond_push(push_src src, uint32_t nonce, bool ok) {
  if (src == PUSH_SRC_TCP) moth_push_respond(s_push, ok);
  else serialpush_respond(nonce, ok);
}

static bool accept_push(void) {
  if (s_push == NULL && hotpush_net_connected()) {
    /* Throttled: this runs on the frame hook, and a bind that keeps failing
     * (port held after a soft restart, say) must not add a socket syscall
     * burst to every frame forever. */
    static int64_t next_listen_us;
    if (esp_timer_get_time() >= next_listen_us) {
      s_push = moth_push_listen(HOTPUSH_PORT);
      if (s_push) {
        ESP_LOGI(TAG, "hot push ready: mothc app.dart --push %s:%d",
                 hotpush_net_ip(), HOTPUSH_PORT);
      } else {
        next_listen_us = esp_timer_get_time() + 2 * 1000 * 1000;
      }
    }
  }
  if (s_pending != NULL) return false;

  /* Two transports, one path from here on: WiFi when it is up, and the USB
   * console always. */
  size_t len = 0;
  uint32_t nonce = 0;
  push_src src = PUSH_SRC_TCP;
  uint8_t *blob = s_push ? moth_push_poll(s_push, &len) : NULL;
  if (!blob) {
    blob = serialpush_poll(&len, &nonce);
    src = PUSH_SRC_SERIAL;
  }
  if (!blob) return false;
  char why[128] = {0};
  if (verify_blob(blob, len, why, sizeof why) != BLOB_OK) {
    ESP_LOGE(TAG, "push rejected: %s", why);
    respond_push(src, nonce, false);
    free(blob); /* the running program never noticed */
    return false;
  }
  s_pending = blob;
  s_pending_len = len;
  respond_push(src, nonce, true);
  ESP_LOGI(TAG, "push: %u bytes received, restarting", (unsigned)len);
  return true;
}

static void on_frame(bool repainted, void *user) {
  (void)user;

  /* Touch is polled here rather than on a task, so events reach the VM
   * between its own loop iterations and never mid-instruction. */
  int x, y;
  bool down = panel_touch_read(&x, &y);
  static int last_x, last_y;
  if (down) {
    last_x = x;
    last_y = y;
  }
  mr_pointer(down ? x : last_x, down ? y : last_y, down);

  /* A push asks the running program to stop; the display stays up, so the
   * replacement draws over a live screen rather than a blank one. */
  if (accept_push() && s_vm) moth_request_halt(s_vm);

  if (repainted) {
#if MOTH_FRAME_PROFILE
    /* Splits a frame three ways, all as per-repaint means over the same
     * window so the three numbers can be compared and added.
     *
     * Layout and paint is timed inside uiCommit, not as wall-clock around
     * this hook: the program's own delay() and every pump that found nothing
     * dirty would otherwise land in that bucket and dominate it. */
    extern int64_t panel_convert_us, panel_push_us;
    extern int64_t g_moth_ui_commit_us;
    extern int g_moth_ui_commits;

    static int64_t base_convert, base_push, base_commit;
    static int64_t base_layout, base_clear, base_rect, base_arc, base_text;
    static int base_commits;
    static int frames;

    int dx, dy, dw, dh;
    mr_damage(&dx, &dy, &dw, &dh);
    panel_present_argb(mr_framebuffer(), dy, dh);

    if (++frames % 20 == 0) {
      const int n = g_moth_ui_commits - base_commits;
      if (n > 0) {
        ESP_LOGI("moth",
                 "PHASES, mean per repaint over %d repaints: "
                 "layout+paint %lld.%01lldms  convert %lld.%01lldms  "
                 "qspi %lld.%01lldms",
                 n,
                 (g_moth_ui_commit_us - base_commit) / n / 1000,
                 ((g_moth_ui_commit_us - base_commit) / n / 100) % 10,
                 (panel_convert_us - base_convert) / n / 1000,
                 ((panel_convert_us - base_convert) / n / 100) % 10,
                 (panel_push_us - base_push) / n / 1000,
                 ((panel_push_us - base_push) / n / 100) % 10);
        /* Where the layout+paint number above goes, in us per repaint.
         * `other` is damage collection, record_painted and everything else
         * mr_commit does around the timed pieces. */
        const int64_t layout = (mr_prof_layout_us - base_layout) / n;
        const int64_t clear = (mr_prof_clear_us - base_clear) / n;
        const int64_t rect = (mr_prof_rect_us - base_rect) / n;
        const int64_t arc = (mr_prof_arc_us - base_arc) / n;
        const int64_t text = (mr_prof_text_us - base_text) / n;
        const int64_t other = (g_moth_ui_commit_us - base_commit) / n - layout -
                              clear - rect - arc - text;
        ESP_LOGI("moth",
                 "SPLIT, us per repaint: layout %lld  clear %lld  rect %lld  "
                 "arc %lld  text %lld  other %lld",
                 layout, clear, rect, arc, text, other);
      }
      base_convert = panel_convert_us;
      base_push = panel_push_us;
      base_commit = g_moth_ui_commit_us;
      base_layout = mr_prof_layout_us;
      base_clear = mr_prof_clear_us;
      base_rect = mr_prof_rect_us;
      base_arc = mr_prof_arc_us;
      base_text = mr_prof_text_us;
      base_commits = g_moth_ui_commits;
    }
#else
    /* Only the rows the renderer says changed — conversion and transfer both
     * scale with the band. */
    int dx, dy, dw, dh;
    mr_damage(&dx, &dy, &dw, &dh);
    panel_present_argb(mr_framebuffer(), dy, dh);
#endif
  }
}

static moth_value n_print(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  moth_value text = moth_to_string(vm, argv[0]);
  int len = 0;
  const char *chars = moth_string_chars(text, &len);
  ESP_LOGI(TAG, "%.*s", len, chars ? chars : "");
  return moth_null();
}

static moth_value n_delay(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) vTaskDelay(pdMS_TO_TICKS(argv[0].as.i));
  return moth_null();
}

static moth_value n_millis(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_int(esp_timer_get_time() / 1000);
}

static void register_host_natives(moth_vm *vm) {
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "millis", n_millis, NULL);
}

/* What the VM is currently executing, and how to let go of it. Both flash
 * sources — the embedded blob and the mapped store — are freed by doing
 * nothing; only a fresh push lives on the heap until the next boot. */
typedef struct {
  const uint8_t *blob;
  size_t len;
  bool heap;       /* free(blob) when swapped out */
  bool from_store; /* executing from the mothb partition's mapped flash */
} program_src;

static program_src embedded_program(void) {
  program_src p = {program_start, (size_t)(program_end - program_start),
                   false, false};
  return p;
}

/* The one teardown ordering, used by every path that stops a program. The
 * failure path and the swap path each having their own copy is how the
 * failure path came to erase the store while the VM still held pointers
 * into its mapped flash. */
static void teardown_vm(moth_vm *vm) {
  s_vm = NULL;
  mr_reset(); /* the program's nodes go with it */
  moth_free(vm);
}

/* Crash accounting by ground truth rather than by timer. The previous
 * design refunded a strike after ten stable seconds, which a program that
 * panics at t=30s defeated forever — it earned its refund every boot and
 * the guard never fired. esp_reset_reason() says what actually ended the
 * last boot: a panic or watchdog while the stored program ran is a strike,
 * and any clean reset clears the record. */
static bool crash_reset(void) {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

/* Picks what to run at boot: the last pushed program if one is stored, still
 * verifies, and has not crashed the chip PUSHSTORE_MAX_STRIKES boots in a
 * row — the embedded program otherwise. */
static program_src choose_boot_blob(void) {
  /* Account for how the LAST boot ended before deciding this one. */
  if (crash_reset() && pushstore_boot_was_store()) {
    pushstore_add_strike();
    ESP_LOGW(TAG, "last boot crashed while running the pushed program "
                  "(strike %d of %d)", pushstore_strikes(),
             PUSHSTORE_MAX_STRIKES);
  } else if (!crash_reset()) {
    pushstore_clear_strikes();
  }

  size_t stored_len = 0;
  const uint8_t *stored = pushstore_load(&stored_len);
  if (!stored) return embedded_program();

  if (pushstore_strikes() >= PUSHSTORE_MAX_STRIKES) {
    ESP_LOGW(TAG, "stored program crashed %d boots in a row; falling back "
                  "to the embedded one", pushstore_strikes());
    pushstore_invalidate();
    pushstore_clear_strikes();
    return embedded_program();
  }
  char why[128] = {0};
  const blob_verdict verdict = verify_blob(stored, stored_len, why, sizeof why);
  if (verdict == BLOB_BAD) {
    ESP_LOGW(TAG, "stored program rejected: %s — dropping it", why);
    pushstore_invalidate();
    return embedded_program();
  }
  if (verdict == BLOB_UNVERIFIABLE) {
    /* Could not check — which says nothing about the blob. Run the embedded
     * program this boot and leave the store intact for the next; erasing on
     * a transient out-of-memory would destroy a good program forever. */
    ESP_LOGW(TAG, "cannot verify the stored program (%s) — "
                  "running the embedded one this boot", why);
    pushstore_release();
    return embedded_program();
  }

  ESP_LOGI(TAG, "booting the last pushed program (%u bytes)",
           (unsigned)stored_len);
  program_src p = {stored, stored_len, false, true};
  return p;
}

/* Creates a VM for `p` and runs it to completion, halt, or failure. */
static moth_status run_program(const program_src *p, moth_vm **vm_out) {
  moth_vm *vm = moth_new();
  *vm_out = vm;
  if (!vm) {
    ESP_LOGE(TAG, "out of memory");
    return MOTH_ERR_OOM;
  }
  register_host_natives(vm);
  moth_ui_register(vm);
  moth_ui_set_frame_hook(on_frame, NULL);
  s_vm = vm;

  moth_status st = moth_load(vm, p->blob, p->len);
  if (st != MOTH_OK) {
    ESP_LOGE(TAG, "load failed (%d): %s", st, moth_error(vm));
    return st;
  }
  ESP_LOGI(TAG, "running Dart UI");
  return moth_run(vm);
}

/* Blocks until a verified push is waiting. The panel keeps its last frame:
 * a push aimed at a finished program should still take, exactly as mothsim
 * behaves on the desktop. */
static void wait_for_pending(void) {
  while (s_pending == NULL) {
    accept_push();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/* Shared persistent storage, initialized before anything that needs it.
 * This was owned by the WiFi bring-up once — which meant a wifi failure took
 * the crash-loop strike counter down with it, in the code whose whole job is
 * surviving failures. Failure here degrades (no credentials, no strikes)
 * but never stops the UI. */
static void storage_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* A layout upgrade wipes NVS — and the wifi credentials with it. Say
     * so, or the board silently stops connecting after an IDF bump. */
    ESP_LOGW(TAG, "NVS layout changed; erasing — wifi needs re-provisioning");
    if (nvs_flash_erase() == ESP_OK) err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS unavailable (%s) — no wifi credentials and no "
                  "crash-loop strike counter this boot", esp_err_to_name(err));
  }
}

/* Claims the pending push as the program to run. Called with the old VM
 * already torn down: the outgoing program may have been executing from the
 * very flash pushstore_save erases. */
static program_src take_pending(void) {
  if (!pushstore_save(s_pending, s_pending_len)) {
    ESP_LOGW(TAG, "push not persisted — it runs now, and a reboot returns "
                  "to the embedded program");
  }
  pushstore_clear_strikes(); /* a new program starts with a clean record */

  /* Runs from the RAM copy until the next boot picks it up from flash —
   * but a crash while it runs is still the pushed program's crash. */
  pushstore_set_boot_source(true);
  program_src p = {s_pending, s_pending_len, true, false};
  s_pending = NULL;
  return p;
}

void app_main(void) {
  ESP_ERROR_CHECK(panel_init());

#if MOTH_FRAME_PROFILE
  membench();
#endif

  /* 1.75" CO5300 is circular: corners sit behind the bezel. */
  mr_config cfg = {PANEL_W, PANEL_H, MR_SHAPE_ROUND};
  if (!mr_init(&cfg)) {
    ESP_LOGE(TAG, "renderer init failed");
    return;
  }

  /* NVS first — the strike counter and the wifi credentials both live
   * there. Then the transports, in the background; the UI never waits for
   * them and neither one failing may stop it. */
  storage_init();
  hotpush_net_start();
  serialpush_start();

  program_src cur = choose_boot_blob();
  pushstore_set_boot_source(cur.from_store);
  ESP_LOGI(TAG, "loading %u bytes of Dart bytecode for a %dx%d display",
           (unsigned)cur.len, PANEL_W, PANEL_H);

  for (;;) {
    moth_vm *vm = NULL;
    moth_status st = run_program(&cur, &vm);
    if (!vm) {
      /* Returning here would leave a live-looking board that can never be
       * pushed again — the one state hot push exists to prevent. Heap may
       * recover (the failed program's allocations are gone); keep trying. */
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (st == MOTH_HALTED) {
      ESP_LOGI(TAG, "program stopped for a push");
    } else if (st != MOTH_OK) {
      ESP_LOGE(TAG, "program failed (%d): %s", st, moth_error(vm));
      /* A stored program that fails at runtime is not worth keeping: fall
       * back to the embedded one now rather than showing a dead screen and
       * failing the same way on every boot. Teardown strictly before
       * invalidate — the erase must not pull flash out from under a VM that
       * still points into it. */
      if (cur.from_store && s_pending == NULL) {
        ESP_LOGW(TAG, "dropping the stored program");
        teardown_vm(vm);
        pushstore_invalidate();
        pushstore_clear_strikes();
        cur = embedded_program();
        pushstore_set_boot_source(false);
        continue;
      }
    } else {
      ESP_LOGI(TAG, "program finished: ok");
    }

    wait_for_pending();
    teardown_vm(vm);
    if (cur.heap) free((void *)cur.blob);
    cur = take_pending();
  }
}
