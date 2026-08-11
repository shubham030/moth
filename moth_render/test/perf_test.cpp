/* Paint-cost regression suite.
 *
 * Wall time depends on the machine, so what is asserted is pixels VISITED per
 * frame — every framebuffer slot a paint loop touched, counted by the
 * MR_PROFILE instrumentation. That number is exact and identical everywhere,
 * and every regression this suite guards against was a 2-10x jump in it:
 *
 *  - transparent wrapper boxes taking the blend path (95ms/frame on an
 *    ESP32-S3): rect_px explodes to N full bands instead of one
 *  - painting under an opaque cover: rect_px counts two backgrounds, and
 *    clear_px fires when the cover made clearing unnecessary
 *  - arcs scanned full-width instead of by annulus span: arc_px ~15x
 *  - the wrap_hint ratchet (a ticking label locked at two lines): the label's
 *    height and the damage band double
 *
 * Wall time is printed for the record but never asserted.
 *
 * Budgets are 1.5-2x the measured steady-state cost, far below any of the
 * regressions. If a legitimate change moves a number past its budget, the new
 * cost is a decision to make consciously — update the budget in the same
 * commit and say why.
 */
#include "moth_render.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* MR_PROFILE's clock, normally provided by the firmware. */
extern "C" int64_t mr_prof_now_us(void) {
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}
extern "C" {
extern int64_t mr_prof_clear_px, mr_prof_rect_px, mr_prof_arc_px,
    mr_prof_text_px;
}

static int failures = 0;

static void check(bool ok, const char *what, long long got, long long limit) {
  if (ok) return;
  fprintf(stderr, "FAIL: %s — got %lld, limit %lld\n", what, got, limit);
  failures++;
}

static mr_node_id box(mr_node_id parent, float w, float h, uint32_t bg) {
  mr_node_id n = mr_node_create(MR_NODE_BOX);
  mr_set_f32(n, MR_PROP_WIDTH, w);
  mr_set_f32(n, MR_PROP_HEIGHT, h);
  mr_set_u32(n, MR_PROP_BG_COLOR, bg);
  mr_attach(parent, n, -1);
  return n;
}

static mr_node_id label(mr_node_id parent, const char *text, float size) {
  mr_node_id n = mr_node_create(MR_NODE_LABEL);
  mr_set_str(n, MR_PROP_TEXT, text);
  mr_set_f32(n, MR_PROP_FONT_SIZE, size);
  mr_set_u32(n, MR_PROP_TEXT_COLOR, 0xFFF2EFE7);
  mr_attach(parent, n, -1);
  return n;
}

struct PxDelta {
  int64_t clear, rect, arc, text;
};

static PxDelta commit_and_measure(int *band_h) {
  const int64_t c0 = mr_prof_clear_px, r0 = mr_prof_rect_px;
  const int64_t a0 = mr_prof_arc_px, t0 = mr_prof_text_px;
  mr_commit();
  int x, y, w, h;
  mr_damage(&x, &y, &w, &h);
  if (band_h) *band_h = h;
  return {mr_prof_clear_px - c0, mr_prof_rect_px - r0, mr_prof_arc_px - a0,
          mr_prof_text_px - t0};
}

/* The frame_bench shape: an opaque cover over the root, transparent wrappers
 * the way Stack/Column/Padding produce them, a ticking 72px clock, a static
 * label, and a full-screen progress ring. Steady state should paint one
 * background band, the clock's glyphs, and the slice of ring the band
 * crosses — nothing else. */
static void ticking_clock_scene(void) {
  const int W = 466, H = 466;
  mr_config cfg = {W, H, MR_SHAPE_ROUND};
  mr_init(&cfg);

  mr_node_id root = mr_root();
  /* A stack, so the ring overlays the content instead of being pushed below
   * the full-height cover — a column would place it at y=466, off-screen,
   * and the arc budget would be asserting against an arc that never draws. */
  mr_set_u32(root, MR_PROP_FLEX_DIRECTION, MR_STACK);
  mr_set_u32(root, MR_PROP_BG_COLOR, 0xFF101014);

  /* The cover: painting the root's background under this is pure waste. */
  mr_node_id inner = box(root, (float)W, (float)H, 0xFF000000);

  /* Wrappers with nothing to draw, nested the way a widget tree nests. */
  mr_node_id wrap = inner;
  for (int i = 0; i < 4; i++) wrap = box(wrap, (float)W, (float)H, 0x00000000);

  /* Centered, like the benchmark's Column: a centered label shrink-wraps, so
   * arrange hands it its own measured width — which is exactly the feedback
   * loop the wrap_hint ratchet lived in. A stretched label never trips it. */
  mr_set_u32(wrap, MR_PROP_MAIN_ALIGN, MR_ALIGN_CENTER);
  mr_set_u32(wrap, MR_PROP_CROSS_ALIGN, MR_ALIGN_CENTER);

  mr_node_id clock = label(wrap, "14:0", 72);
  label(wrap, "FRI 8 AUG", 20);

  mr_node_id ring = mr_node_create(MR_NODE_ARC);
  mr_set_f32(ring, MR_PROP_WIDTH, (float)W);
  mr_set_f32(ring, MR_PROP_HEIGHT, (float)H);
  mr_set_f32(ring, MR_PROP_THICKNESS, 6);
  mr_set_f32(ring, MR_PROP_ARC_SWEEP, 200);
  mr_set_u32(ring, MR_PROP_BG_COLOR, 0xFFE8A33D);
  mr_set_u32(ring, MR_PROP_ARC_TRACK_COLOR, 0xFF1C1C21);
  mr_attach(root, ring, -1);

  mr_commit(); /* first frame legitimately paints everything */

  float lx, ly, lw, lh;
  mr_frame_of(clock, &lx, &ly, &lw, &lh);
  const int line_h = (int)lh; /* one 72px line, whatever the face reports */
  check(line_h > 0 && line_h < 110, "clock label is one line after first paint",
        line_h, 110);

  const auto t0 = mr_prof_now_us();
  int frames = 0;
  /* Tick through widths both growing and shrinking — '14:9' to '14:10' is
   * the transition the wrap_hint ratchet used to lose. */
  for (int n = 1; n <= 40; n++, frames++) {
    char text[16];
    snprintf(text, sizeof text, "14:%d", n);
    mr_set_str(clock, MR_PROP_TEXT, text);

    int band_h = 0;
    PxDelta d = commit_and_measure(&band_h);

    check(band_h <= line_h + 8, "damage band is the label, not double it",
          band_h, line_h + 8);
    float x, y, w, h;
    mr_frame_of(clock, &x, &y, &w, &h);
    check((int)h <= line_h, "clock stays one line (wrap_hint ratchet)", (int)h,
          line_h);
    /* One cover fill over the band; wrappers and the skipped root are free. */
    const int64_t band_px = (int64_t)band_h * W;
    check(d.rect <= band_px * 3 / 2, "rect px: one background fill per frame",
          d.rect, band_px * 3 / 2);
    check(d.clear == 0, "no clear when a cover spans the band", d.clear, 0);
    check(d.arc <= 20000, "arc px: annulus spans, not full-width scan", d.arc,
          20000);
    check(d.arc > 0, "arc painted at all — a zero means the scene is wrong",
          d.arc, 1);
    check(d.text <= 30000, "text px: the clock's glyphs, roughly", d.text,
          30000);
    if (n == 1) {
      printf("steady state: band %d, px/frame rect %lld arc %lld text %lld\n",
             band_h, (long long)d.rect, (long long)d.arc, (long long)d.text);
    }
  }
  const auto us = mr_prof_now_us() - t0;
  printf("ticking_clock: %d frames, %.2fms each host wall-clock (not asserted)\n",
         frames, (double)us / frames / 1000.0);
}

/* No opaque cover anywhere: the clear has to fire, exactly once per band. */
static void uncovered_scene(void) {
  const int W = 466, H = 466;
  mr_config cfg = {W, H, MR_SHAPE_ROUND};
  mr_init(&cfg);

  mr_set_u32(mr_root(), MR_PROP_BG_COLOR, 0x00000000);
  mr_node_id dot = box(mr_root(), 40, 40, 0xFF3366AA);
  mr_commit();

  /* n starts at 1: n = 0 would re-apply the color the box already has, which
   * compare-before-store rightly ignores — nothing commits, and the test
   * would be reading the previous frame's band. */
  for (int n = 1; n <= 8; n++) {
    mr_set_u32(dot, MR_PROP_BG_COLOR, 0xFF3366AA + (uint32_t)n);
    int band_h = 0;
    PxDelta d = commit_and_measure(&band_h);
    const int64_t band_px = (int64_t)band_h * W;
    check(band_h <= 41, "band is the changed box's rows", band_h, 41);
    check(d.clear == band_px, "clear covers exactly the band", d.clear,
          band_px);
    check(d.rect <= 40 * 41, "rect px: the box alone", d.rect, 40 * 41);
  }
}

int main(void) {
  ticking_clock_scene();
  uncovered_scene();
  if (failures) {
    fprintf(stderr, "%d perf regression(s)\n", failures);
    return 1;
  }
  printf("render perf: all budgets held\n");
  return 0;
}
