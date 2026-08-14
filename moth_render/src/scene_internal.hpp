#pragma once
#include "moth_font.h"
#include "moth_render.h"

#include <string>
#include <vector>

/* Per-primitive frame timing, so optimization is aimed at where the
 * milliseconds actually go. Off by default and free when off. When on, the
 * embedder must provide mr_prof_now_us — a build that enables this without
 * linking a clock fails loudly rather than quietly measuring nothing. */
#ifndef MR_PROFILE
#define MR_PROFILE 0
#endif
#if MR_PROFILE
extern "C" {
int64_t mr_prof_now_us(void);
extern int64_t mr_prof_layout_us, mr_prof_clear_us, mr_prof_rect_us,
    mr_prof_arc_us, mr_prof_text_us;
/* Pixels *visited* per primitive — every framebuffer slot a loop touched,
 * whether or not it changed. Time varies by machine; these do not, which is
 * what makes them assertable in a test. The transparent-wrapper bug that cost
 * 95ms a frame was invisible in any image diff but is a 10x jump here. */
extern int64_t mr_prof_clear_px, mr_prof_rect_px, mr_prof_arc_px,
    mr_prof_text_px;
}
#define MR_PROF_START(t) const int64_t t = mr_prof_now_us()
#define MR_PROF_ADD(t, acc) ((acc) += mr_prof_now_us() - (t))
#define MR_PROF_PX(acc, n) ((acc) += (n))
#else
#define MR_PROF_START(t) (void)0
#define MR_PROF_ADD(t, acc) (void)0
#define MR_PROF_PX(acc, n) (void)0
#endif

namespace mr {

/* One wrapped line, as a range into the original string. */
struct TextLine {
  uint32_t start, len;

  /* Where the pen ends up: what wrapping decisions are made against. */
  float width;

  /* How far the ink actually reaches. A glyph may paint past its own advance
   * — Inter's '2' at 72px does — so a box sized by advance alone clips the
   * last character of a line down its right edge. */
  float ink;
};


struct Node {
  bool alive = false;

  /* Damage tracking. A node that changed must be repainted where it is now
   * and where it was, or it leaves a ghost behind. Bands are rows rather than
   * rectangles: a band spans the full width, so nothing can be partially
   * covered by a sibling and the awkward cases — overlap, translucency,
   * z-order — cannot arise. */
  bool touched = true;     /* a property changed since the last paint */
  bool painted = false;    /* prev_y/prev_h describe a real previous frame */
  float prev_y = 0.0f, prev_h = 0.0f;

  /* Wrapped lines, and the width they were wrapped at. Layout fills these and
   * paint reads them: two independent wraps of the same string is how a label
   * ends up reserving room for one line and drawing three. */
  std::vector<TextLine> lines;
  float lines_width = -1.0f;
  const moth_font *lines_font = nullptr;

  /* The width arrange last handed this label. A label with no width of its own
   * is measured before its parent decides how wide it gets, so the first pass
   * has nothing to wrap against; this carries that answer back so the second
   * pass measures against the width the label will really have. */
  float wrap_hint = -1.0f;
  mr_node_kind kind = MR_NODE_BOX;
  mr_node_id parent = MR_NODE_NONE;
  std::vector<mr_node_id> children;

  float f[MR_PROP_COUNT] = {};
  uint32_t u[MR_PROP_COUNT] = {};
  std::string text;
  std::string image_src;

  /* computed by layout, root-relative px */
  float x = 0, y = 0, w = 0, h = 0;
};

struct Anim {
  bool alive = false;
  uint32_t id = 0;
  mr_node_id node = MR_NODE_NONE;
  mr_prop prop = MR_PROP_OPACITY;
  float from = 0, to = 0;
  uint32_t duration_ms = 0, elapsed_ms = 0;
  mr_easing easing = MR_EASE_LINEAR;
};

/* One registered raster asset. Pixels are BORROWED — they belong to the
 * host's program blob and stay valid only while that program is loaded,
 * which is why the registry dies with the Scene on mr_reset: a swap frees
 * the old blob, and a surviving registration would be a dangling blit. */
struct Asset {
  std::string key;
  int w = 0, h = 0;
  const uint32_t *pixels = nullptr;
};

struct Scene {
  mr_config cfg{};
  std::vector<Node> nodes;     /* index == id; slot 0 unused */
  std::vector<Anim> anims;
  std::vector<Asset> assets;
  uint32_t next_anim_id = 1;
  bool dirty = true;

  /* The rows that must be repainted this frame, as [damage_y0, damage_y1).
   * Empty when nothing moved; the whole frame on the first paint. */
  float damage_y0 = 0.0f, damage_y1 = 0.0f;

  /* Painting is clamped to this band rather than to the framebuffer. */
  int clip_y0 = 0, clip_y1 = 0;

  mr_event_cb sink = nullptr;
  void *sink_user = nullptr;

  std::vector<uint32_t> framebuffer; /* ARGB8888 */

  /* pointer state for hit-testing */
  bool pointer_down = false;
  mr_node_id pressed_node = MR_NODE_NONE;

  /* The last value THIS drag derived from the finger, NaN when no drag has
   * emitted yet. slider_drag dedupes against this, not the node's value: the
   * widget layer rewrites the node every rebuild, so an app that clamps or
   * quantizes what it accepts (Flutter's `divisions` idiom) would defeat a
   * node-value dedupe and a stationary finger would emit — and repaint — at
   * frame rate forever. */
  float drag_value = 0.0f;
  bool drag_valued = false;

  Node *get(mr_node_id id) {
    if (id == MR_NODE_NONE || id >= nodes.size() || !nodes[id].alive) return nullptr;
    return &nodes[id];
  }
  void emit(const mr_event &ev) {
    if (sink) sink(&ev, sink_user);
  }
};

Scene &scene();

/* text.cpp — fonts, measurement and wrapping.
 *
 * Layout and paint have to agree exactly on where every glyph goes, so both
 * go through here rather than each doing its own arithmetic. That is also why
 * wrapping lives here: the height layout reserves and the lines paint draws
 * must come from the same call. */

/* The face to draw `text` at `size` with: the largest one at or below the
 * size that can actually render the string. Faces are subsetted, so ranking
 * by size alone hands back a digits-only face for a word and draws nothing.
 * Never null. */
const moth_font *font_for(float size, const std::string &text);

/* Advance for one character, including a stand-in for glyphs the face lacks. */
float glyph_advance(const moth_font *f, unsigned char ch);

/* The glyph, or null when this face cannot draw the character. */
const moth_glyph *glyph_or_null(const moth_font *f, unsigned char ch);

/* Breaks `text` into lines that fit `max_w`, at spaces where it can and
 * mid-word only when a single word cannot fit. A `max_w` of zero or less
 * means no wrapping, and the result is one line. */
void wrap_text(const moth_font *f, const std::string &text, float max_w,
               std::vector<TextLine> &out);

/* Wraps a label to `max_w` and caches the result on the node, so paint draws
 * exactly the lines layout reserved room for. Re-wraps only when the width it
 * was last wrapped at has changed. Fills n.w/n.h with what the text needs. */
void layout_text(Node &n, float max_w, float &out_w, float &out_h);

/* paint.cpp — true when a point lands on an arc's stroke, as drawn. An arc's
 * box spans the whole ring, so testing that box would have a decorative
 * overlay swallow every tap inside it. */
bool arc_hit(const Node &n, float px, float py);

/* paint.cpp — where a slider's thumb travels: inset a thumb radius from each
 * end of the box. Paint and the drag gesture must agree on this to the
 * pixel, or the thumb lands beside the finger — which is why both read it
 * from one function. */
void slider_geometry(const Node &n, float *x0, float *x1, float *radius);

/* nullptr when no asset with that key is registered. */
const Asset *find_asset(Scene &s, const std::string &key);

/* layout.cpp — implements docs/BACKEND.md §4 against the node tree */
void layout_run(Scene &s);

/* paint.cpp — rasterizes the tree into s.framebuffer, clamped to the damage
 * band in clip_y0/clip_y1. */
void paint_run(Scene &s);

} // namespace mr
