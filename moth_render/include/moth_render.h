/* moth backend contract — C API.
 * Semantics are normative in docs/BACKEND.md; this header is the normative API.
 * Implemented by: moth_render (this component), lvgl backend (planned).
 */
#ifndef MOTH_RENDER_H
#define MOTH_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MR_CONTRACT_VERSION 2

typedef uint32_t mr_node_id; /* opaque; 0 is never a valid node */
#define MR_NODE_NONE ((mr_node_id)0)

typedef enum {
  MR_NODE_BOX,
  MR_NODE_LABEL,
  MR_NODE_IMAGE,
  MR_NODE_SLIDER,
  MR_NODE_SWITCH,
  MR_NODE_ARC, /* a stroked ring segment, for gauges and progress */
} mr_node_kind;

typedef enum {
  /* layout (f32 unless noted) */
  MR_PROP_WIDTH,          /* px; MR_AUTO for content size */
  MR_PROP_HEIGHT,         /* px; MR_AUTO for content size */
  MR_PROP_FLEX_DIRECTION, /* u32: mr_direction */
  MR_PROP_MAIN_ALIGN,     /* u32: mr_align */
  MR_PROP_CROSS_ALIGN,    /* u32: mr_align (space_between invalid here) */
  MR_PROP_FLEX_GROW,
  MR_PROP_GAP,
  MR_PROP_PADDING,        /* uniform inset */
  MR_PROP_POSITION,       /* u32: mr_position */
  MR_PROP_LEFT,           /* absolute only */
  MR_PROP_TOP,            /* absolute only */
  /* style */
  MR_PROP_BG_COLOR,       /* u32 ARGB8888 */
  MR_PROP_RADIUS,
  MR_PROP_BORDER_WIDTH,
  MR_PROP_BORDER_COLOR,   /* u32 ARGB8888 */
  MR_PROP_OPACITY,        /* 0..1, multiplies subtree */
  /* text */
  MR_PROP_TEXT,           /* str */
  MR_PROP_FONT_SIZE,
  MR_PROP_TEXT_COLOR,     /* u32 ARGB8888 */
  /* image */
  MR_PROP_IMAGE_SRC,      /* str asset key */
  /* controls */
  MR_PROP_VALUE,
  MR_PROP_MIN,
  MR_PROP_MAX,
  /* arc — appended, so every index above keeps its number */
  MR_PROP_ARC_START, /* degrees, 0 at twelve o'clock, clockwise */
  MR_PROP_ARC_SWEEP, /* degrees; >= 360 draws a closed ring */
  MR_PROP_THICKNESS, /* stroke width, px */
  /* Where the stroke sits relative to the nominal circle, as Flutter's
   * strokeAlign: -1 inside, 0 centred, 1 outside. */
  MR_PROP_STROKE_ALIGN,
  MR_PROP_STROKE_CAP, /* u32: mr_stroke_cap */
  /* The unswept remainder of the ring, drawn in the same pass. Zero alpha
   * leaves it undrawn, which is a plain arc. */
  MR_PROP_ARC_TRACK_COLOR,
  MR_PROP_COUNT
} mr_prop;

#define MR_AUTO (-1.0f) /* sentinel for width/height */

/* MR_STACK lays every child at the container's origin rather than in a
 * sequence, so they overlap. Absolutely positioned children already overlay,
 * but expressing an overlay as a column that happens to skip them is a lie
 * about the layout. */
typedef enum { MR_COLUMN, MR_ROW, MR_STACK } mr_direction;
/* space_between: main axis only; stretch: cross axis only (auto-sized children) */
typedef enum { MR_ALIGN_START, MR_ALIGN_CENTER, MR_ALIGN_END, MR_ALIGN_SPACE_BETWEEN, MR_ALIGN_STRETCH } mr_align;
typedef enum { MR_FLOW, MR_ABSOLUTE } mr_position;

/* Butt ends the stroke square at the sweep; round adds a half-disc, which is
 * what a gauge usually wants. */
typedef enum { MR_CAP_BUTT, MR_CAP_ROUND } mr_stroke_cap;

/* ---- lifecycle -------------------------------------------------------- */

/* A round panel still has a rectangular framebuffer, but its corners are
 * behind the bezel. Layout stays rectangular; the safe area is what an app
 * should keep its content inside. */
typedef enum { MR_SHAPE_RECT, MR_SHAPE_ROUND } mr_shape;

typedef struct {
  int width;  /* display px */
  int height;
  mr_shape shape; /* MR_SHAPE_RECT when zero-initialised */
} mr_config;

bool mr_init(const mr_config *cfg);

/* Clears the tree back to a bare root, keeping the display configuration and
 * the event sink. A host swapping in a new program calls this so the old
 * program's nodes do not linger underneath the new UI. */
void mr_reset(void);

void mr_shutdown(void);
uint32_t mr_contract_version(void);
mr_node_id mr_root(void); /* box spanning the display; owned by the backend */

/* The largest rectangle guaranteed to be visible. On a rectangular panel
 * that is the whole display; on a round one it is the inscribed square,
 * which is why a round 466px panel gives about 330px of usable width. */
void mr_safe_area(int *x, int *y, int *w, int *h);

/* ---- tree ------------------------------------------------------------- */

mr_node_id mr_node_create(mr_node_kind kind);
void mr_node_destroy(mr_node_id node); /* recursive; detaches first */
void mr_attach(mr_node_id parent, mr_node_id child, int index); /* -1 = append */
void mr_detach(mr_node_id child);

/* ---- properties (deferred until mr_commit) ---------------------------- */

void mr_set_f32(mr_node_id node, mr_prop prop, float v);
void mr_set_u32(mr_node_id node, mr_prop prop, uint32_t v);
void mr_set_str(mr_node_id node, mr_prop prop, const char *utf8);

/* ---- events ----------------------------------------------------------- */

typedef enum {
  MR_EV_PRESSED,
  MR_EV_RELEASED,
  MR_EV_CLICKED,
  MR_EV_VALUE_CHANGED,
  MR_EV_ANIM_COMPLETED,
} mr_event_kind;

typedef struct {
  mr_node_id node;
  mr_event_kind kind;
  float value; /* VALUE_CHANGED payload; anim id for ANIM_COMPLETED */
  float x, y;  /* pointer position, root-relative */
} mr_event;

/* Single sink (the VM event loop). Must not call back into this API
 * synchronously — queue and return. */
typedef void (*mr_event_cb)(const mr_event *ev, void *user);
void mr_set_event_sink(mr_event_cb cb, void *user);

/* Platform feeds pointer state; backend does hit-testing and gestures. */
void mr_pointer(int x, int y, bool down);

/* ---- animation (native; the only path for continuous motion) ---------- */

typedef enum { MR_EASE_LINEAR, MR_EASE_OUT, MR_EASE_IN_OUT } mr_easing;

uint32_t mr_anim_start(mr_node_id node, mr_prop prop, float from, float to,
                       uint32_t duration_ms, mr_easing easing);
void mr_anim_stop(uint32_t anim_id);

/* ---- frame ------------------------------------------------------------ */

void mr_tick(uint32_t dt_ms);              /* advance animations */
bool mr_commit(void);                      /* layout -> damage -> paint; true if repainted */
const uint32_t *mr_framebuffer(void);      /* ARGB8888, width*height */
void mr_damage(int *x, int *y, int *w, int *h); /* region repainted by last commit */

/* ---- introspection (conformance suite) -------------------------------- */

void mr_frame_of(mr_node_id node, float *x, float *y, float *w, float *h);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_RENDER_H */
