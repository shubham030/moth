/* v0 paint: flat-color software fill, full-frame repaint.
 * Enough to see layout on screen and to write the SDL harness against.
 *
 * TODO(R2): replace fills with a ThorVG SwCanvas over s.framebuffer —
 *           rounded rects, borders, text (real metrics), images.
 * TODO(R2): slider/switch composite rendering (track + knob from VALUE).
 * TODO(R3): dirty-rect damage tracking; only re-rasterize damaged nodes.
 */
#include "scene_internal.hpp"

#include "font8x8_basic.h"

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

/* Blits the 8x8 glyphs, each pixel expanded to a scale x scale block.
 * Clipped to the node's box, so a label never paints outside its layout. */
static void draw_text(Scene &s, const Node &n, float opacity) {
  int scale = text_scale(n.f[MR_PROP_FONT_SIZE]);
  uint32_t color = n.u[MR_PROP_TEXT_COLOR];
  float pen_x = n.x;

  for (size_t i = 0; i < n.text.size(); i++) {
    unsigned char ch = (unsigned char)n.text[i];
    if (ch > 127) ch = '?'; /* the font covers ASCII only */
    const unsigned char *glyph = font8x8_basic[ch];

    for (int row = 0; row < MOTH_GLYPH_PX; row++) {
      for (int col = 0; col < MOTH_GLYPH_PX; col++) {
        if (!((glyph[row] >> col) & 1)) continue;
        float px = pen_x + (float)(col * scale);
        float py = n.y + (float)(row * scale);
        if (px + scale <= n.x || px >= n.x + n.w) continue; /* clip to the box */
        fill_rect(s, px, py, (float)scale, (float)scale, color, opacity);
      }
    }
    pen_x += (float)(MOTH_GLYPH_PX * scale);
  }
}

static void paint_node(Scene &s, mr_node_id id, float opacity) {
  Node *n = s.get(id);
  if (!n) return;
  opacity *= n->f[MR_PROP_OPACITY];
  if (opacity <= 0.0f) return;

  fill_rect(s, n->x, n->y, n->w, n->h, n->u[MR_PROP_BG_COLOR], opacity);

  if (n->kind == MR_NODE_LABEL && !n->text.empty()) {
    draw_text(s, *n, opacity);
  }

  for (mr_node_id c : n->children) paint_node(s, c, opacity);
}

void paint_run(Scene &s) {
  std::fill(s.framebuffer.begin(), s.framebuffer.end(), 0xFF000000u);
  paint_node(s, (mr_node_id)1, 1.0f);
}

} // namespace mr
