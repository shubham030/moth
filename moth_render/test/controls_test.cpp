/* Slider and switch, tested at the contract: build the nodes, drive
 * mr_pointer the way a finger would, and assert the VALUE_CHANGED events and
 * painted pixels that BACKEND.md §2/§6 promise. Runs anywhere; no hardware.
 *
 * The geometry assertions derive from slider_geometry's published rule (the
 * thumb travels between a thumb-radius inset at each end), so if paint and
 * the gesture ever disagree, the expected values here disagree with one of
 * them and the test says which.
 */
#include "moth_render.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what, double got, double want) {
  if (ok) return;
  fprintf(stderr, "FAIL: %s — got %f, want %f\n", what, got, want);
  failures++;
}

struct Ev {
  mr_node_id node;
  mr_event_kind kind;
  float value;
};
static std::vector<Ev> g_events;

static void sink(const mr_event *ev, void *user) {
  (void)user;
  g_events.push_back({ev->node, ev->kind, ev->value});
}

/* The VALUE_CHANGED events since the last call, oldest first. */
static std::vector<float> value_events(mr_node_id node) {
  std::vector<float> out;
  for (const Ev &e : g_events) {
    if (e.node == node && e.kind == MR_EV_VALUE_CHANGED) out.push_back(e.value);
  }
  g_events.clear();
  return out;
}

static uint32_t pixel(int x, int y) { return mr_framebuffer()[y * 466 + x]; }

/* A tap is a press and a release at the same spot. */
static void tap(int x, int y) {
  mr_pointer(x, y, true);
  mr_pointer(x, y, false);
}

int main(void) {
  mr_config cfg = {466, 466, MR_SHAPE_RECT};
  mr_init(&cfg);
  mr_set_event_sink(sink, nullptr);

  /* A 200x24 slider at the origin (fixed width, so cross-stretch cannot
   * resize it) and a 40x24 switch below it. */
  mr_node_id slider = mr_node_create(MR_NODE_SLIDER);
  mr_set_f32(slider, MR_PROP_WIDTH, 200);
  mr_set_f32(slider, MR_PROP_HEIGHT, 24);
  mr_set_f32(slider, MR_PROP_MIN, 0);
  mr_set_f32(slider, MR_PROP_MAX, 100);
  mr_set_f32(slider, MR_PROP_VALUE, 25);
  mr_set_u32(slider, MR_PROP_BG_COLOR, 0xFFE8A33D);
  mr_attach(mr_root(), slider, -1);

  mr_node_id sw = mr_node_create(MR_NODE_SWITCH);
  mr_set_f32(sw, MR_PROP_WIDTH, 40);
  mr_set_f32(sw, MR_PROP_HEIGHT, 24);
  mr_set_u32(sw, MR_PROP_BG_COLOR, 0xFFE8A33D);
  mr_attach(mr_root(), sw, -1);
  mr_commit();
  g_events.clear();

  /* Thumb geometry: radius = 24/2 - 2 = 10, travel from x=10 to x=190. */
  const float x0 = 10, x1 = 190;

  /* Pressing the track centre jumps the value there — Flutter's behavior. */
  mr_pointer(100, 12, true);
  auto vs = value_events(slider);
  check(vs.size() == 1, "press emits one VALUE_CHANGED", vs.size(), 1);
  if (!vs.empty()) {
    const float want = (100 - x0) / (x1 - x0) * 100.0f;
    check(std::fabs(vs[0] - want) < 0.75f, "press jumps to position", vs[0],
          want);
  }

  /* Dragging while held tracks the finger; releasing past the end clamps. */
  mr_pointer(145, 12, true);
  vs = value_events(slider);
  check(vs.size() == 1 && std::fabs(vs[0] - 75.0f) < 0.75f, "drag tracks",
        vs.empty() ? -1 : vs[0], 75);
  mr_pointer(400, 12, true);
  vs = value_events(slider);
  check(vs.size() == 1 && vs[0] == 100.0f, "drag past the end clamps to max",
        vs.empty() ? -1 : vs[0], 100);
  /* Same position again: no change, no event. */
  mr_pointer(400, 12, true);
  vs = value_events(slider);
  check(vs.empty(), "an unchanged value emits nothing", vs.size(), 0);
  mr_pointer(400, 12, false);
  g_events.clear();

  /* Paint agrees with the gesture: after commit, the thumb centre pixel is
   * the accent, and the track left of it too (filled portion). */
  mr_commit();
  check(pixel(188, 12) == 0xFFE8A33D, "thumb painted at max", pixel(188, 12),
        0xFFE8A33D);
  check(pixel(20, 12) == 0xFFE8A33D, "filled track is accent", pixel(20, 12),
        0xFFE8A33D);

  /* The switch: off pixel first, then tap toggles 0 -> 1 -> 0 with an
   * event each time, and the knob crosses the pill. */
  check(pixel(6, 36) != 0xFFE8A33D, "off switch is not accent", pixel(6, 36),
        0);
  tap(20, 36);
  vs = value_events(sw);
  check(vs.size() == 1 && vs[0] == 1.0f, "tap turns the switch on",
        vs.empty() ? -1 : vs[0], 1);
  mr_commit();
  check(pixel(6, 36) == 0xFFE8A33D, "on switch pill is accent", pixel(6, 36),
        0xFFE8A33D);
  tap(20, 36);
  vs = value_events(sw);
  check(vs.size() == 1 && vs[0] == 0.0f, "tap turns it back off",
        vs.empty() ? -1 : vs[0], 0);

  /* A press that releases OUTSIDE the switch is not a click — no toggle. */
  mr_pointer(20, 36, true);
  mr_pointer(300, 300, false);
  vs = value_events(sw);
  check(vs.empty(), "press-drag-away does not toggle", vs.size(), 0);

  if (failures) {
    fprintf(stderr, "%d control contract failure(s)\n", failures);
    return 1;
  }
  printf("controls: slider and switch honor the contract\n");
  return 0;
}
