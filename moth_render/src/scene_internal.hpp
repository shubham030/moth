#pragma once
#include "moth_font.h"
#include "moth_render.h"

#include <string>
#include <vector>

namespace mr {

struct Node {
  bool alive = false;
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

struct Scene {
  mr_config cfg{};
  std::vector<Node> nodes;     /* index == id; slot 0 unused */
  std::vector<Anim> anims;
  uint32_t next_anim_id = 1;
  bool dirty = true;

  mr_event_cb sink = nullptr;
  void *sink_user = nullptr;

  std::vector<uint32_t> framebuffer; /* ARGB8888 */

  /* pointer state for hit-testing */
  bool pointer_down = false;
  mr_node_id pressed_node = MR_NODE_NONE;

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

/* The nearest generated face at or below `size`, never null. */
const moth_font *font_for(float size);

/* Pen advance for a run of bytes, in pixels. */
float text_advance(const moth_font *f, const char *s, size_t len);

/* One wrapped line, as a range into the original string. */
struct TextLine {
  uint32_t start, len;
  float width;
};

/* Breaks `text` into lines that fit `max_w`, at spaces where it can and
 * mid-word only when a single word cannot fit. A `max_w` of zero or less
 * means no wrapping, and the result is one line. */
void wrap_text(const moth_font *f, const std::string &text, float max_w,
               std::vector<TextLine> &out);

/* The size a label wants: its widest line, and one line height per line. */
void measure_text(const std::string &text, float font_size, float max_w,
                  float &out_w, float &out_h);

/* paint.cpp — true when a point lands on an arc's stroke, as drawn. An arc's
 * box spans the whole ring, so testing that box would have a decorative
 * overlay swallow every tap inside it. */
bool arc_hit(const Node &n, float px, float py);

/* layout.cpp — implements docs/BACKEND.md §4 against the node tree */
void layout_run(Scene &s);

/* paint.cpp — rasterizes the tree into s.framebuffer.
 * v0: flat-color software fill. TODO(R2): ThorVG canvas. */
void paint_run(Scene &s);

} // namespace mr
