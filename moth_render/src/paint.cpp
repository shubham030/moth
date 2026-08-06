/* v0 paint: flat-color software fill, full-frame repaint.
 * Enough to see layout on screen and to write the SDL harness against.
 *
 * TODO(R2): replace fills with a ThorVG SwCanvas over s.framebuffer —
 *           rounded rects, borders, text (real metrics), images.
 * TODO(R2): slider/switch composite rendering (track + knob from VALUE).
 * TODO(R3): dirty-rect damage tracking; only re-rasterize damaged nodes.
 */
#include "scene_internal.hpp"

#include <algorithm>
#include <cmath>

namespace mr {

static uint32_t blend(uint32_t dst, uint32_t src, float extra_opacity) {
  float sa = ((src >> 24) & 0xFF) / 255.0f * extra_opacity;
  if (sa <= 0.0f) return dst;
  if (sa >= 1.0f) return 0xFF000000u | (src & 0x00FFFFFFu);
  auto ch = [&](int shift) {
    float d = (float)((dst >> shift) & 0xFF);
    float s2 = (float)((src >> shift) & 0xFF);
    return (uint32_t)(s2 * sa + d * (1.0f - sa)) << shift;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

static void fill_rect(Scene &s, float fx, float fy, float fw, float fh,
                      uint32_t argb, float opacity) {
  int x0 = std::max(0, (int)std::floor(fx));
  int y0 = std::max(0, (int)std::floor(fy));
  int x1 = std::min(s.cfg.width, (int)std::ceil(fx + fw));
  int y1 = std::min(s.cfg.height, (int)std::ceil(fy + fh));
  for (int y = y0; y < y1; y++) {
    uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
    for (int x = x0; x < x1; x++) row[x] = blend(row[x], argb, opacity);
  }
}

static void paint_node(Scene &s, mr_node_id id, float opacity) {
  Node *n = s.get(id);
  if (!n) return;
  opacity *= n->f[MR_PROP_OPACITY];
  if (opacity <= 0.0f) return;

  fill_rect(s, n->x, n->y, n->w, n->h, n->u[MR_PROP_BG_COLOR], opacity);

  if (n->kind == MR_NODE_LABEL && !n->text.empty()) {
    /* placeholder: text renders as an underline bar until ThorVG lands */
    float fs = n->f[MR_PROP_FONT_SIZE];
    fill_rect(s, n->x, n->y + n->h - 2, std::min(n->w, 0.55f * fs * (float)n->text.size()),
              2, n->u[MR_PROP_TEXT_COLOR], opacity);
  }

  for (mr_node_id c : n->children) paint_node(s, c, opacity);
}

void paint_run(Scene &s) {
  std::fill(s.framebuffer.begin(), s.framebuffer.end(), 0xFF000000u);
  paint_node(s, (mr_node_id)1, 1.0f);
}

} // namespace mr
