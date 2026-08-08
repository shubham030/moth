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

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Signed distance to a rounded rectangle: negative inside, and crossing zero
 * exactly at the edge. Coverage is then just how far a pixel's centre sits
 * inside that boundary, which is what gives a smooth edge for free. */
static float sd_round_rect(float px, float py, float cx, float cy,
                           float hw, float hh, float r) {
  if (r > hw) r = hw;
  if (r > hh) r = hh;
  float qx = std::fabs(px - cx) - (hw - r);
  float qy = std::fabs(py - cy) - (hh - r);
  float ax = qx > 0.0f ? qx : 0.0f;
  float ay = qy > 0.0f ? qy : 0.0f;
  float outside = std::sqrt(ax * ax + ay * ay);
  float inside = qx > qy ? qx : qy;
  if (inside > 0.0f) inside = 0.0f;
  return outside + inside - r;
}

static void fill_rect(Scene &s, float fx, float fy, float fw, float fh,
                      uint32_t argb, float opacity) {
  int x0 = std::max(0, (int)std::floor(fx));
  int y0 = std::max(0, (int)std::floor(fy));
  int x1 = std::min(s.cfg.width, (int)std::ceil(fx + fw));
  int y1 = std::min(s.cfg.height, (int)std::ceil(fy + fh));
  if (x1 <= x0 || y1 <= y0) return;

  /* An opaque fill has nothing to blend with, and backgrounds are the biggest
   * rectangles on screen — a full-screen panel is a fifth of a million pixels,
   * and taking the general path for each cost more than everything else in a
   * frame put together. */
  if (((argb >> 24) & 0xFF) == 0xFF && opacity >= 1.0f) {
    const uint32_t solid = 0xFF000000u | (argb & 0x00FFFFFFu);
    for (int y = y0; y < y1; y++) {
      uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
      std::fill(row + x0, row + x1, solid);
    }
    return;
  }

  for (int y = y0; y < y1; y++) {
    uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
    for (int x = x0; x < x1; x++) row[x] = blend(row[x], argb, opacity);
  }
}

/* Fills a rectangle whose corners are rounded, antialiased. Falls back to the
 * square version when there is no radius, which is the common case and much
 * cheaper. */
static void fill_round_rect(Scene &s, float fx, float fy, float fw, float fh,
                            float radius, uint32_t argb, float opacity) {
  if (radius <= 0.5f || fw <= 0.0f || fh <= 0.0f) {
    fill_rect(s, fx, fy, fw, fh, argb, opacity);
    return;
  }
  int x0 = std::max(0, (int)std::floor(fx));
  int y0 = std::max(0, (int)std::floor(fy));
  int x1 = std::min(s.cfg.width, (int)std::ceil(fx + fw));
  int y1 = std::min(s.cfg.height, (int)std::ceil(fy + fh));

  const float cx = fx + fw * 0.5f, cy = fy + fh * 0.5f;
  const float hw = fw * 0.5f, hh = fh * 0.5f;

  for (int y = y0; y < y1; y++) {
    uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
    for (int x = x0; x < x1; x++) {
      float d = sd_round_rect((float)x + 0.5f, (float)y + 0.5f, cx, cy, hw, hh, radius);
      float cov = clamp01(0.5f - d);
      if (cov <= 0.0f) continue;
      row[x] = blend(row[x], argb, opacity * cov);
    }
  }
}

/* Draws only the outline of a rounded rectangle, [width] px thick, inside the
 * node's box. */
static void stroke_round_rect(Scene &s, float fx, float fy, float fw, float fh,
                              float radius, float width, uint32_t argb,
                              float opacity) {
  if (width <= 0.0f || fw <= 0.0f || fh <= 0.0f) return;
  int x0 = std::max(0, (int)std::floor(fx));
  int y0 = std::max(0, (int)std::floor(fy));
  int x1 = std::min(s.cfg.width, (int)std::ceil(fx + fw));
  int y1 = std::min(s.cfg.height, (int)std::ceil(fy + fh));

  const float cx = fx + fw * 0.5f, cy = fy + fh * 0.5f;
  const float hw = fw * 0.5f, hh = fh * 0.5f;

  for (int y = y0; y < y1; y++) {
    uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
    for (int x = x0; x < x1; x++) {
      float d = sd_round_rect((float)x + 0.5f, (float)y + 0.5f, cx, cy, hw, hh, radius);
      /* The band between the edge and `width` inside it. */
      float cov = clamp01(0.5f - d) - clamp01(0.5f - (d + width));
      if (cov <= 0.0f) continue;
      row[x] = blend(row[x], argb, opacity * cov);
    }
  }
}

/* A stroked ring segment with round caps, inscribed in the node's box.
 *
 * Angles are degrees clockwise from twelve o'clock, because that is how a
 * progress ring is described rather than how atan2 reports them. Coverage is
 * computed from the distance to the ring's centre line, so the edge is smooth
 * without supersampling — an aliased arc on a round panel looks broken in a
 * way an aliased rectangle does not. */
static void draw_arc(Scene &s, const Node &n, float opacity) {
  float thickness = n.f[MR_PROP_THICKNESS];
  if (thickness <= 0.0f) thickness = 4.0f;

  const float cx = n.x + n.w * 0.5f, cy = n.y + n.h * 0.5f;
  const float outer = (n.w < n.h ? n.w : n.h) * 0.5f;
  const float mid = outer - thickness * 0.5f; /* the centre line of the stroke */
  if (mid <= 0.0f) return;

  const float half = thickness * 0.5f;
  float sweep = n.f[MR_PROP_ARC_SWEEP];
  if (sweep <= 0.0f) return;
  const bool closed = sweep >= 360.0f;
  const float start = n.f[MR_PROP_ARC_START];

  const float kDeg = 3.14159265358979f / 180.0f;
  /* Cap centres sit on the centre line at each end of the sweep. */
  const float a0 = start * kDeg, a1 = (start + sweep) * kDeg;
  const float cap0x = cx + mid * std::sin(a0), cap0y = cy - mid * std::cos(a0);
  const float cap1x = cx + mid * std::sin(a1), cap1y = cy - mid * std::cos(a1);

  /* Everything the stroke can touch lies in an annulus a pixel wider than the
   * stroke on each side. Rejecting against its squared radii costs a multiply
   * where taking the distance costs a square root, and nearly all of the
   * bounding box is rejected — on a 466px panel that is the difference
   * between ~217k square roots per arc and ~20k. */
  const float r_in = mid - half - 1.0f;
  const float r_out = mid + half + 1.0f;
  const float r_in2 = r_in > 0.0f ? r_in * r_in : 0.0f;
  const float r_out2 = r_out * r_out;
  const float cap_r2 = (half + 1.0f) * (half + 1.0f);

  int x0 = std::max(0, (int)std::floor(cx - outer - 1.0f));
  int y0 = std::max(0, (int)std::floor(cy - outer - 1.0f));
  int x1 = std::min(s.cfg.width, (int)std::ceil(cx + outer + 1.0f));
  int y1 = std::min(s.cfg.height, (int)std::ceil(cy + outer + 1.0f));

  const uint32_t argb = n.u[MR_PROP_BG_COLOR];

  for (int y = y0; y < y1; y++) {
    const float dy = (float)y + 0.5f - cy;
    const float dy2 = dy * dy;
    if (dy2 > r_out2) continue; /* this row misses the ring entirely */

    uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
    for (int x = x0; x < x1; x++) {
      const float dx = (float)x + 0.5f - cx;
      const float d2 = dx * dx + dy2;
      if (d2 > r_out2 || d2 < r_in2) continue; /* outside the stroke: no sqrt */

      const float dist = std::sqrt(d2);
      float cov = clamp01(half + 0.5f - std::fabs(dist - mid));

      if (!closed) {
        if (cov > 0.0f) {
          float ang = std::atan2(dx, -dy) / kDeg; /* 0 at twelve, clockwise */
          float rel = ang - start;
          while (rel < 0.0f) rel += 360.0f;
          while (rel >= 360.0f) rel -= 360.0f;
          if (rel > sweep) cov = 0.0f; /* past the end; a cap may still cover it */
        }

        /* Round caps, as discs on the ends of the centre line. They lie inside
         * the annulus by construction, so this only sees pixels that already
         * got past the radial check, and only while coverage is still to gain. */
        if (cov < 1.0f) {
          const float d0x = (float)x + 0.5f - cap0x, d0y = (float)y + 0.5f - cap0y;
          const float q0 = d0x * d0x + d0y * d0y;
          if (q0 <= cap_r2) {
            const float c0 = clamp01(half + 0.5f - std::sqrt(q0));
            if (c0 > cov) cov = c0;
          }
          const float d1x = (float)x + 0.5f - cap1x, d1y = (float)y + 0.5f - cap1y;
          const float q1 = d1x * d1x + d1y * d1y;
          if (q1 <= cap_r2) {
            const float c1 = clamp01(half + 0.5f - std::sqrt(q1));
            if (c1 > cov) cov = c1;
          }
        }
      }

      if (cov <= 0.0f) continue;
      row[x] = blend(row[x], argb, opacity * cov);
    }
  }
}

/* Draws a label: wrapped into lines, each glyph blended by its own coverage.
 *
 * The font stores alpha rather than on/off pixels, so this is the whole of
 * antialiasing — no rasterizer, just the stored value scaling the blend. Text
 * is clipped to the node's box on both axes so a label can never paint over
 * a sibling. */
static void draw_text(Scene &s, const Node &n, float opacity) {
  const moth_font *f = font_for(n.f[MR_PROP_FONT_SIZE]);
  const uint32_t color = n.u[MR_PROP_TEXT_COLOR];

  std::vector<TextLine> lines;
  wrap_text(f, n.text, n.w > 0.0f ? n.w : 0.0f, lines);

  const float clip_x0 = n.x, clip_x1 = n.x + n.w;
  const float clip_y0 = n.y, clip_y1 = n.y + n.h;

  float line_y = n.y;
  for (const TextLine &line : lines) {
    float pen = n.x;
    for (uint32_t i = 0; i < line.len; i++) {
      const unsigned char ch = (unsigned char)n.text[line.start + i];
      if (ch < f->first || ch > f->last) continue;
      const moth_glyph *g = &f->glyphs[ch - f->first];

      if (g->box_w > 0 && g->box_h > 0) {
        /* ofs_y is measured from the top of the line, as the generator
         * recorded it from the same origin the metrics use. */
        const float gx = pen + (float)g->ofs_x;
        const float gy = line_y + (float)g->ofs_y;

        for (int row = 0; row < g->box_h; row++) {
          const float py = gy + (float)row;
          if (py < clip_y0 || py >= clip_y1) continue;
          const int y = (int)py;
          if (y < 0 || y >= s.cfg.height) continue;
          uint32_t *dst = s.framebuffer.data() + (size_t)y * s.cfg.width;

          for (int col = 0; col < g->box_w; col++) {
            const float px = gx + (float)col;
            if (px < clip_x0 || px >= clip_x1) continue;
            const int x = (int)px;
            if (x < 0 || x >= s.cfg.width) continue;

            const uint8_t a = moth_glyph_alpha(f, g, col, row);
            if (!a) continue;
            dst[x] = blend(dst[x], color, opacity * (float)a / 255.0f);
          }
        }
      }
      pen += (float)g->adv_w;
    }
    line_y += (float)f->line_height;
  }
}

/* Whether a point lies on the stroke — the same geometry draw_arc uses, so
 * what you can touch is exactly what you can see. Without this an arc laid
 * over content takes every tap in its bounding box, which for a ring around
 * a display is the entire display. */
bool arc_hit(const Node &n, float px, float py) {
  float thickness = n.f[MR_PROP_THICKNESS];
  if (thickness <= 0.0f) thickness = 4.0f;

  const float cx = n.x + n.w * 0.5f, cy = n.y + n.h * 0.5f;
  const float outer = (n.w < n.h ? n.w : n.h) * 0.5f;
  const float mid = outer - thickness * 0.5f;
  if (mid <= 0.0f) return false;

  const float half = thickness * 0.5f;
  const float dx = px - cx, dy = py - cy;
  const float dist = std::sqrt(dx * dx + dy * dy);
  if (std::fabs(dist - mid) > half) return false;

  float sweep = n.f[MR_PROP_ARC_SWEEP];
  if (sweep <= 0.0f) return false;
  if (sweep >= 360.0f) return true;

  const float kDeg = 3.14159265358979f / 180.0f;
  float rel = std::atan2(dx, -dy) / kDeg - n.f[MR_PROP_ARC_START];
  while (rel < 0.0f) rel += 360.0f;
  while (rel >= 360.0f) rel -= 360.0f;
  return rel <= sweep;
}

static void paint_node(Scene &s, mr_node_id id, float opacity) {
  Node *n = s.get(id);
  if (!n) return;
  opacity *= n->f[MR_PROP_OPACITY];
  if (opacity <= 0.0f) return;

  if (n->kind == MR_NODE_ARC) {
    draw_arc(s, *n, opacity);
  } else {
    fill_round_rect(s, n->x, n->y, n->w, n->h, n->f[MR_PROP_RADIUS],
                    n->u[MR_PROP_BG_COLOR], opacity);
    if (n->f[MR_PROP_BORDER_WIDTH] > 0.0f) {
      stroke_round_rect(s, n->x, n->y, n->w, n->h, n->f[MR_PROP_RADIUS],
                        n->f[MR_PROP_BORDER_WIDTH], n->u[MR_PROP_BORDER_COLOR],
                        opacity);
    }
    if (n->kind == MR_NODE_LABEL && !n->text.empty()) {
      draw_text(s, *n, opacity);
    }
  }

  for (mr_node_id c : n->children) paint_node(s, c, opacity);
}

void paint_run(Scene &s) {
  std::fill(s.framebuffer.begin(), s.framebuffer.end(), 0xFF000000u);
  paint_node(s, (mr_node_id)1, 1.0f);
}

} // namespace mr
