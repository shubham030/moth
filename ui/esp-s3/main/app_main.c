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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
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

static void register_natives(moth_vm *vm);

/* Loads a blob into a throwaway VM to find out whether it would run, without
 * touching the one that is running. The natives must match what the real VM
 * offers, or a valid program would look unresolvable here. */
static bool blob_is_loadable(const uint8_t *b, size_t len) {
  moth_vm *probe = moth_new();
  if (!probe) return false;
  register_natives(probe);
  moth_status st = moth_load(probe, b, len);
  if (st != MOTH_OK) ESP_LOGE(TAG, "push rejected: %s", moth_error(probe));
  moth_free(probe);
  return st == MOTH_OK;
}

/* Takes a pushed blob if one has arrived and it survives verification.
 * Rejecting here rather than after the swap is the whole point: a bad blob
 * must not be able to take the running program down with it. */
static bool accept_push(void) {
  if (s_push == NULL && hotpush_net_connected()) {
    s_push = moth_push_listen(HOTPUSH_PORT);
    if (s_push) {
      ESP_LOGI(TAG, "hot push ready: mothc app.dart --push %s:%d",
               hotpush_net_ip(), HOTPUSH_PORT);
    }
  }
  if (s_push == NULL || s_pending != NULL) return false;

  size_t len = 0;
  uint8_t *blob = moth_push_poll(s_push, &len);
  if (!blob) return false;
  if (!blob_is_loadable(blob, len)) {
    free(blob); /* the running program never noticed */
    return false;
  }
  s_pending = blob;
  s_pending_len = len;
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

static void register_natives(moth_vm *vm) {
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "millis", n_millis, NULL);
  moth_ui_register(vm);
}

/* Clears the strike counter once a stored program has stayed up long enough
 * to count as working. Armed only when booting from the store. */
static void on_stable(void *arg) {
  (void)arg;
  pushstore_clear_strikes();
  ESP_LOGI(TAG, "stored program stable; strike counter cleared");
}

/* Picks what to run at boot: the last pushed program if one is stored, still
 * verifies, and has not been striking out — the embedded program otherwise.
 * Booting from the store costs a strike up front; on_stable refunds it. */
static const uint8_t *choose_boot_blob(size_t *len, bool *from_store) {
  *from_store = false;
  size_t stored_len = 0;
  const uint8_t *stored = pushstore_load(&stored_len);
  if (!stored) goto embedded;

  if (pushstore_strikes() >= PUSHSTORE_MAX_STRIKES) {
    ESP_LOGW(TAG, "stored program crashed %d boots running; falling back to "
                  "the embedded one", PUSHSTORE_MAX_STRIKES);
    pushstore_invalidate();
    pushstore_clear_strikes();
    goto embedded;
  }
  if (!blob_is_loadable(stored, stored_len)) {
    pushstore_invalidate();
    goto embedded;
  }

  pushstore_add_strike();
  const esp_timer_create_args_t args = {.callback = on_stable, .name = "stable"};
  esp_timer_handle_t t;
  if (esp_timer_create(&args, &t) == ESP_OK) {
    esp_timer_start_once(t, 10 * 1000 * 1000); /* 10s of uptime = working */
  }
  *len = stored_len;
  *from_store = true;
  ESP_LOGI(TAG, "booting the last pushed program (%u bytes)",
           (unsigned)stored_len);
  return stored;

embedded:
  *len = (size_t)(program_end - program_start);
  return program_start;
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

  /* WiFi comes up in the background; the UI never waits for it. This also
   * initializes NVS, which the pushstore strike counter needs. */
  hotpush_net_start();

  bool from_store = false;
  size_t current_len = 0;
  const uint8_t *current = choose_boot_blob(&current_len, &from_store);
  bool current_is_heap = false; /* embedded and stored blobs live in flash */

  ESP_LOGI(TAG, "loading %u bytes of Dart bytecode for a %dx%d display",
           (unsigned)current_len, PANEL_W, PANEL_H);

  for (;;) {
    moth_vm *vm = moth_new();
    if (!vm) {
      ESP_LOGE(TAG, "out of memory");
      return;
    }
    register_natives(vm);
    moth_ui_set_frame_hook(on_frame, NULL);
    s_vm = vm;

    moth_status st = moth_load(vm, current, current_len);
    if (st == MOTH_OK) {
      ESP_LOGI(TAG, "running Dart UI");
      st = moth_run(vm);
    }
    if (st == MOTH_HALTED) {
      ESP_LOGI(TAG, "program stopped for a push");
    } else if (st != MOTH_OK) {
      ESP_LOGE(TAG, "program failed (%d): %s", st, moth_error(vm));
      /* A stored program that fails at runtime is not worth keeping: fall
       * back to the embedded one now rather than showing a dead screen and
       * failing the same way on every boot. */
      if (from_store && s_pending == NULL) {
        ESP_LOGW(TAG, "dropping the stored program");
        pushstore_invalidate();
        pushstore_clear_strikes();
        s_vm = NULL;
        mr_reset();
        moth_free(vm);
        current = program_start;
        current_len = (size_t)(program_end - program_start);
        current_is_heap = false;
        from_store = false;
        continue;
      }
    } else {
      ESP_LOGI(TAG, "program finished: ok");
    }

    /* The program ended on its own. Keep the panel showing its last frame
     * and keep listening — a push aimed at a finished program should still
     * take, exactly as mothsim does on the desktop. */
    while (s_pending == NULL) {
      accept_push();
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Swap in the pushed program. The old program's nodes go with it —
     * otherwise the new UI draws on top of a tree it does not own. The old
     * VM is torn down before the store is written: the outgoing program may
     * be running from the very flash the save erases. */
    s_vm = NULL;
    mr_reset();
    moth_free(vm);
    if (current_is_heap) free((void *)current);

    if (!pushstore_save(s_pending, s_pending_len)) {
      ESP_LOGW(TAG, "push not persisted — it runs now but a reboot loses it");
    }
    pushstore_clear_strikes(); /* a new program starts with a clean record */

    current = s_pending;
    current_len = s_pending_len;
    current_is_heap = true; /* runs from the RAM copy until the next boot */
    from_store = false;
    s_pending = NULL;
  }
}
