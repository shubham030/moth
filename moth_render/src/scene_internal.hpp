#pragma once
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

/* Text is drawn from an 8x8 bitmap font at integer scale, so a glyph is
 * always a whole number of pixels and layout can measure it exactly. */
#define MOTH_GLYPH_PX 8

inline int text_scale(float font_size) {
  int scale = (int)(font_size / MOTH_GLYPH_PX + 0.5f);
  return scale < 1 ? 1 : scale;
}

inline float text_width(size_t chars, float font_size) {
  return (float)(chars * MOTH_GLYPH_PX * (size_t)text_scale(font_size));
}

inline float text_height(float font_size) {
  return (float)(MOTH_GLYPH_PX * text_scale(font_size));
}

/* layout.cpp — implements docs/BACKEND.md §4 against the node tree */
void layout_run(Scene &s);

/* paint.cpp — rasterizes the tree into s.framebuffer.
 * v0: flat-color software fill. TODO(R2): ThorVG canvas. */
void paint_run(Scene &s);

} // namespace mr
