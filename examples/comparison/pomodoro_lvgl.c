/* The pomodoro from examples/ui/pomodoro.dart, written against LVGL 9's C
 * API — the same UI, twice, so the programming models can be compared
 * honestly. See docs/lvgl-comparison.md for the side-by-side.
 *
 * This file is not built by moth's build. It compiles against LVGL
 * release/v9.2 (MIT C API only — ADR-002's XML prohibition is about the
 * spec format, not this) and was verified in LVGL's SDL simulator; the
 * screenshot in the docs came from that run. Board bring-up (lv_init,
 * display/touch drivers, tick integration) is assumed done elsewhere, the
 * way moth's firmware is assumed flashed.
 */
#include "lvgl.h"

#define WORK_MS (25 * 60 * 1000)
#define BREAK_MS (5 * 60 * 1000)

/* State lives in globals; the widgets that display it live in globals too,
 * because callbacks need to reach both. */
static bool running;
static bool on_break;
static int32_t remaining_ms = WORK_MS;
static uint32_t end_ms;

static lv_obj_t *arc;
static lv_obj_t *phase_label;
static lv_obj_t *time_label;
static lv_obj_t *hint_label;

static lv_color_t accent(void) {
  return lv_color_hex(on_break ? 0x33AA66 : 0xE8A33D);
}

/* Every piece of state that changes must be manually pushed into each
 * widget that shows it — this is the retained-mode contract. Forget one
 * call here and that widget silently shows stale state. */
static void refresh(void) {
  int32_t total = on_break ? BREAK_MS : WORK_MS;
  int32_t sec = remaining_ms / 1000;

  lv_label_set_text(phase_label, on_break ? "BREAK" : "FOCUS");
  lv_obj_set_style_text_color(phase_label, accent(), 0);
  lv_label_set_text_fmt(time_label, "%ld:%02ld", (long)(sec / 60),
                        (long)(sec % 60));
  lv_label_set_text(hint_label, running ? "TAP TO PAUSE" : "TAP TO START");
  lv_arc_set_value(arc, (int32_t)(((int64_t)remaining_ms * 100) / total));
  lv_obj_set_style_arc_color(arc, accent(), LV_PART_INDICATOR);
}

static void on_tap(lv_event_t *e) {
  LV_UNUSED(e);
  running = !running;
  if (running) end_ms = lv_tick_get() + (uint32_t)remaining_ms;
  refresh();
}

static void tick_cb(lv_timer_t *t) {
  LV_UNUSED(t);
  if (!running) return;
  remaining_ms = (int32_t)(end_ms - lv_tick_get());
  if (remaining_ms <= 0) {
    on_break = !on_break;
    remaining_ms = on_break ? BREAK_MS : WORK_MS;
    running = false;
  }
  refresh();
}

void pomodoro_create(void) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E0E12), 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, on_tap, LV_EVENT_CLICKED, NULL);

  /* The ring: an arc styled into a progress indicator — rotate so zero
   * sits at twelve o'clock, strip the knob, make it non-interactive so it
   * does not swallow the screen's taps. */
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x1C1C21), LV_PART_MAIN);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

  /* The centered column. Sized to the round panel's inscribed square by
   * hand — there is no safe-area helper. */
  lv_obj_t *col = lv_obj_create(scr);
  lv_obj_set_size(col, 329, 329);
  lv_obj_center(col);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 14, 0);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE); /* let taps reach scr */

  phase_label = lv_label_create(col);
  lv_obj_set_style_text_font(phase_label, &lv_font_montserrat_20, 0);

  time_label = lv_label_create(col);
  /* Requires LV_FONT_MONTSERRAT_48 enabled in lv_conf.h and a rebuild —
   * and 48 is the largest stock font, so a true 72px clock means running
   * the offline font converter and shipping the output. */
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xF2EFE7), 0);

  lv_obj_t *rule = lv_obj_create(col);
  lv_obj_set_size(rule, 180, 2);
  lv_obj_set_style_bg_color(rule, lv_color_hex(0x2A2A31), 0);
  lv_obj_set_style_border_width(rule, 0, 0);

  hint_label = lv_label_create(col);
  lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint_label, lv_color_hex(0x6B6B76), 0);

  refresh();
  lv_timer_create(tick_cb, 250, NULL);
}
