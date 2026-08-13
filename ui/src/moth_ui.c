#include "moth_ui.h"

#include "moth_render.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Events are drained by polling rather than delivered by callback: the VM has
 * no closures or event loop yet, so a Dart program asks for them in its own
 * loop. This becomes a real queue-to-callback path once closures land. */
#define EVENT_QUEUE 32

static struct {
  mr_event items[EVENT_QUEUE];
  int head, count;
  float last_value;
} g_events;

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
static int64_t moth_ui_now_us(void) { return esp_timer_get_time(); }
#else
#include <time.h>
static int64_t moth_ui_now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#endif

/* How long mr_commit has spent, and over how many repaints — read by the
 * host's frame profiler. */
int64_t g_moth_ui_commit_us;
int g_moth_ui_commits;

static moth_ui_frame_fn g_frame_hook;
static void *g_frame_user;

void moth_ui_set_frame_hook(moth_ui_frame_fn hook, void *user) {
  g_frame_hook = hook;
  g_frame_user = user;
}

static void on_event(const mr_event *ev, void *user) {
  (void)user;
  /* On overflow, drop the OLDEST. For a value stream the newest sample is
   * the one that matters: the node already holds the new value, so losing
   * the tail would leave the program with a stale value that the next
   * rebuild writes back, snapping a dragged thumb backward. */
  if (g_events.count == EVENT_QUEUE) {
    g_events.head = (g_events.head + 1) % EVENT_QUEUE;
    g_events.count--;
  }
  int slot = (g_events.head + g_events.count) % EVENT_QUEUE;
  g_events.items[slot] = *ev;
  g_events.count++;
}

static int64_t want_int(moth_value v, int64_t fallback) {
  if (v.type == MV_INT) return v.as.i;
  if (v.type == MV_DOUBLE) return (int64_t)v.as.d;
  return fallback;
}

static float want_num(moth_value v, float fallback) {
  if (v.type == MV_INT) return (float)v.as.i;
  if (v.type == MV_DOUBLE) return (float)v.as.d;
  return fallback;
}

/* ---- geometry ---------------------------------------------------------- */

static moth_value n_ui_width(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  float x, y, w, h;
  mr_frame_of(mr_root(), &x, &y, &w, &h);
  return moth_int((int64_t)w);
}

static moth_value n_ui_height(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  float x, y, w, h;
  mr_frame_of(mr_root(), &x, &y, &w, &h);
  return moth_int((int64_t)h);
}

static moth_value n_ui_root(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_int(mr_root());
}

static moth_value n_ui_frame_of(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  float f[4];
  mr_frame_of((mr_node_id)want_int(argv[0], 0), &f[0], &f[1], &f[2], &f[3]);
  int64_t which = want_int(argv[1], 0);
  if (which < 0 || which > 3) return moth_int(0);
  return moth_int((int64_t)f[which]);
}

/* On a round panel the corners are behind the bezel; this is the rectangle
 * an app can safely fill. */
static moth_value n_ui_safe_area(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int f[4];
  mr_safe_area(&f[0], &f[1], &f[2], &f[3]);
  int64_t which = want_int(argv[0], 0);
  if (which < 0 || which > 3) return moth_int(0);
  return moth_int(f[which]);
}

/* ---- tree -------------------------------------------------------------- */

static moth_value n_ui_create(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t kind = want_int(argv[0], MR_NODE_BOX);
  if (kind < MR_NODE_BOX || kind > MR_NODE_ARC) kind = MR_NODE_BOX;
  return moth_int(mr_node_create((mr_node_kind)kind));
}

static moth_value n_ui_destroy(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  mr_node_destroy((mr_node_id)want_int(argv[0], 0));
  return moth_null();
}

static moth_value n_ui_attach(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  mr_attach((mr_node_id)want_int(argv[0], 0), (mr_node_id)want_int(argv[1], 0),
            (int)want_int(argv[2], -1));
  return moth_null();
}

static moth_value n_ui_detach(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  mr_detach((mr_node_id)want_int(argv[0], 0));
  return moth_null();
}

/* ---- properties -------------------------------------------------------- */

static bool valid_prop(int64_t prop) { return prop >= 0 && prop < MR_PROP_COUNT; }

static moth_value n_ui_set_num(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t prop = want_int(argv[1], -1);
  if (valid_prop(prop)) {
    mr_set_f32((mr_node_id)want_int(argv[0], 0), (mr_prop)prop, want_num(argv[2], 0));
  }
  return moth_null();
}

static moth_value n_ui_set_int(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t prop = want_int(argv[1], -1);
  if (valid_prop(prop)) {
    mr_set_u32((mr_node_id)want_int(argv[0], 0), (mr_prop)prop,
               (uint32_t)want_int(argv[2], 0));
  }
  return moth_null();
}

static moth_value n_ui_set_text(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t prop = want_int(argv[1], -1);
  if (!valid_prop(prop)) return moth_null();

  /* moth strings are not NUL-terminated; mr_set_str needs a C string. */
  int len = 0;
  const char *chars = moth_string_chars(argv[2], &len);
  char stack_buf[128];
  char *text = stack_buf;
  if (!chars) { len = 0; chars = ""; }
  if ((size_t)len >= sizeof stack_buf) len = (int)sizeof stack_buf - 1;
  memcpy(text, chars, (size_t)len);
  text[len] = '\0';

  mr_set_str((mr_node_id)want_int(argv[0], 0), (mr_prop)prop, text);
  return moth_null();
}

/* ---- animation, frame, events ------------------------------------------ */

static moth_value n_ui_animate(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t prop = want_int(argv[1], -1);
  if (!valid_prop(prop)) return moth_int(0);
  int64_t easing = want_int(argv[5], MR_EASE_LINEAR);
  if (easing < MR_EASE_LINEAR || easing > MR_EASE_IN_OUT) easing = MR_EASE_LINEAR;
  return moth_int(mr_anim_start((mr_node_id)want_int(argv[0], 0), (mr_prop)prop,
                                want_num(argv[2], 0), want_num(argv[3], 0),
                                (uint32_t)want_int(argv[4], 0), (mr_easing)easing));
}

static moth_value n_ui_tick(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  mr_tick((uint32_t)want_int(argv[0], 0));
  return moth_null();
}

static moth_value n_ui_commit(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  /* Timed here rather than by the host, because only this call is layout and
   * paint. Measuring wall-clock between presents in the frame hook swept up
   * the program's own delay() and every non-dirty pump. */
  const int64_t commit_start = moth_ui_now_us();
  bool repainted = mr_commit();
  g_moth_ui_commit_us += moth_ui_now_us() - commit_start;
  if (repainted) g_moth_ui_commits++;
  if (g_frame_hook) g_frame_hook(repainted, g_frame_user);
  return moth_bool(repainted);
}

static moth_value n_ui_poll(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  if (g_events.count == 0) return moth_int(-1);
  mr_event ev = g_events.items[g_events.head];
  g_events.head = (g_events.head + 1) % EVENT_QUEUE;
  g_events.count--;
  g_events.last_value = ev.value;
  /* One integer carries both, so polling needs no allocation. */
  return moth_int((int64_t)ev.node * 8 + (int64_t)ev.kind);
}

static moth_value n_ui_event_value(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_double(g_events.last_value);
}

void moth_ui_register(moth_vm *vm) {
  g_events.head = 0;
  g_events.count = 0;
  mr_set_event_sink(on_event, NULL);
  moth_ui_register_natives(vm);
}

void moth_ui_register_natives(moth_vm *vm) {
  moth_register(vm, "uiWidth", n_ui_width, NULL);
  moth_register(vm, "uiHeight", n_ui_height, NULL);
  moth_register(vm, "uiRoot", n_ui_root, NULL);
  moth_register(vm, "uiFrameOf", n_ui_frame_of, NULL);
  moth_register(vm, "uiSafeArea", n_ui_safe_area, NULL);
  moth_register(vm, "uiCreate", n_ui_create, NULL);
  moth_register(vm, "uiDestroy", n_ui_destroy, NULL);
  moth_register(vm, "uiAttach", n_ui_attach, NULL);
  moth_register(vm, "uiDetach", n_ui_detach, NULL);
  moth_register(vm, "uiSetNum", n_ui_set_num, NULL);
  moth_register(vm, "uiSetInt", n_ui_set_int, NULL);
  moth_register(vm, "uiSetText", n_ui_set_text, NULL);
  moth_register(vm, "uiAnimate", n_ui_animate, NULL);
  moth_register(vm, "uiTick", n_ui_tick, NULL);
  moth_register(vm, "uiCommit", n_ui_commit, NULL);
  moth_register(vm, "uiPoll", n_ui_poll, NULL);
  moth_register(vm, "uiEventValue", n_ui_event_value, NULL);
}
