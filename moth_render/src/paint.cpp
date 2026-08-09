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

/* The ring an arc describes: the centre line of its stroke, and half its
 * width. Everything the stroke can touch lies within `half + 1` of that
 * circle. */
struct ArcRing {
  float cx, cy, mid, half;
};

static bool arc_ring(const Node &n, ArcRing &out) {
  float thickness = n.f[MR_PROP_THICKNESS];
  if (thickness <= 0.0f) thickness = 4.0f;

  const float outer = (n.w < n.h ? n.w : n.h) * 0.5f;
  out.cx = n.x + n.w * 0.5f;
  out.cy = n.y + n.h * 0.5f;
  out.mid = outer - thickness * 0.5f;
  out.half = thickness * 0.5f;
  return out.mid > 0.0f;
}

/* Degrees clockwise from twelve o'clock — how a progress ring is described,
 * rather than how atan2 reports angles. */
static float arc_angle_at(float dx, float dy) {
  const float kDeg = 3.14159265358979f / 180.0f;
  return std::atan2(dx, -dy) / kDeg;
}

static bool arc_within_sweep(const Node &n, float dx, float dy, float sweep) {
  float rel = arc_angle_at(dx, dy) - n.f[MR_PROP_ARC_START];
  while (rel < 0.0f) rel += 360.0f;
  while (rel >= 360.0f) rel -= 360.0f;
  return rel <= sweep;
}

/* Coverage from the round cap at one end of the sweep, or 0 if the pixel is
 * nowhere near it. */
static float arc_cap_coverage(float px, float py, float capx, float capy,
                              float half) {
  const float dx = px - capx, dy = py - capy;
  const float q = dx * dx + dy * dy;
  const float reach = half + 1.0f;
  if (q > reach * reach) return 0.0f;
  return clamp01(half + 0.5f - std::sqrt(q));
}

/* A stroked ring segment with round caps, inscribed in the node's box.
 *
 * Coverage comes from the distance to the stroke's centre line, so the edge is
 * smooth without supersampling — an aliased arc on a round panel looks broken
 * in a way an aliased rectangle does not. */
static void draw_arc(Scene &s, const Node &n, float opacity) {
  ArcRing r;
  if (!arc_ring(n, r)) return;

  float sweep = n.f[MR_PROP_ARC_SWEEP];
  if (sweep <= 0.0f) return;
  const bool closed = sweep >= 360.0f;

  const float kDeg = 3.14159265358979f / 180.0f;
  const float a0 = n.f[MR_PROP_ARC_START] * kDeg;
  const float a1 = (n.f[MR_PROP_ARC_START] + sweep) * kDeg;
  const float cap0x = r.cx + r.mid * std::sin(a0), cap0y = r.cy - r.mid * std::cos(a0);
  const float cap1x = r.cx + r.mid * std::sin(a1), cap1y = r.cy - r.mid * std::cos(a1);

  /* Everything the stroke can touch lies in an annulus a pixel wider than the
   * stroke on each side. Rejecting against its squared radii costs a multiply
   * where taking the distance costs a square root, and nearly all of the box
   * is rejected — on a 466px panel that is ~20k roots instead of ~217k. */
  const float r_in = r.mid - r.half - 1.0f;
  const float r_out = r.mid + r.half + 1.0f;
  const float r_in2 = r_in > 0.0f ? r_in * r_in : 0.0f;
  const float r_out2 = r_out * r_out;

  const float outer = (n.w < n.h ? n.w : n.h) * 0.5f;
  const int x0 = std::max(0, (int)std::floor(r.cx - outer - 1.0f));
  const int y0 = std::max(0, (int)std::floor(r.cy - outer - 1.0f));
  const int x1 = std::min(s.cfg.width, (int)std::ceil(r.cx + outer + 1.0f));
  const int y1 = std::min(s.cfg.height, (int)std::ceil(r.cy + outer + 1.0f));

  const uint32_t argb = n.u[MR_PROP_BG_COLOR];

  for (int y = y0; y < y1; y++) {
    const float dy = (float)y + 0.5f - r.cy;
    const float dy2 = dy * dy;
    if (dy2 > r_out2) continue;

    uint32_t *row = s.framebuffer.data() + (size_t)y * s.cfg.width;
    for (int x = x0; x < x1; x++) {
      const float dx = (float)x + 0.5f - r.cx;
      const float d2 = dx * dx + dy2;
      if (d2 > r_out2 || d2 < r_in2) continue;

      float cov = clamp01(r.half + 0.5f - std::fabs(std::sqrt(d2) - r.mid));
      if (!closed) {
        if (cov > 0.0f && !arc_within_sweep(n, dx, dy, sweep)) cov = 0.0f;
        if (cov < 1.0f) {
          const float px = (float)x + 0.5f, py = (float)y + 0.5f;
          cov = std::max(cov, arc_cap_coverage(px, py, cap0x, cap0y, r.half));
          cov = std::max(cov, arc_cap_coverage(px, py, cap1x, cap1y, r.half));
        }
      }

      if (cov <= 0.0f) continue;
      row[x] = blend(row[x], argb, opacity * cov);
    }
  }
}

/* Draws a label from the lines layout already wrapped.
 *
 * The font stores alpha rather than on/off pixels, so this is the whole of
 * antialiasing — no rasterizer, just the stored value scaling the blend.
 * Nothing is re-wrapped here: doing that independently is how a label
 * reserves room for one line and then draws three. */
static void draw_text(Scene &s, const Node &n, float opacity) {
  if (!n.lines_font || n.lines.empty()) return;
  const moth_font *f = n.lines_font;
  const uint32_t color = n.u[MR_PROP_TEXT_COLOR];

  const int box_x0 = (int)std::floor(n.x);
  const int box_x1 = (int)std::ceil(n.x + n.w);
  const int box_y0 = (int)std::floor(n.y);
  const int box_y1 = (int)std::ceil(n.y + n.h);

  float line_y = n.y;
  for (const TextLine &line : n.lines) {
    float pen = n.x;
    for (uint32_t i = 0; i < line.len; i++) {
      const unsigned char ch = (unsigned char)n.text[line.start + i];
      const moth_glyph *g = glyph_or_null(f, ch);

      if (!g) {
        /* A character this face cannot draw. Showing a hollow box beats
         * closing the gap silently, which turns "12.5" into "125". */
        const float nd = glyph_advance(f, ch);
        const float top = line_y + (float)f->line_height * 0.25f;
        const float hgt = (float)f->line_height * 0.5f;
        stroke_round_rect(s, pen + 1.0f, top, nd - 2.0f, hgt, 1.0f, 1.0f,
                          color, opacity * 0.55f);
        pen += nd;
        continue;
      }

      if (g->box_w > 0 && g->box_h > 0) {
        const float gx = pen + (float)g->ofs_x;
        const float gy = line_y + (float)g->ofs_y;

        /* Clamp the glyph's own range once rather than testing every pixel
         * against the box. The left edge is clamped against the node box, not
         * the pen: a glyph with a negative ofs_x — 'f' and 'j' in Inter —
         * legitimately starts a hair before the pen, and clipping there ate
         * an ink column off any line beginning with one. */
        int c0 = 0, c1 = g->box_w;
        int r0 = 0, r1 = g->box_h;
        if (gx < (float)box_x0) c0 = (int)((float)box_x0 - gx);
        if (gx + (float)g->box_w > (float)box_x1) c1 = (int)((float)box_x1 - gx);
        if (gy < (float)box_y0) r0 = (int)((float)box_y0 - gy);
        if (gy + (float)g->box_h > (float)box_y1) r1 = (int)((float)box_y1 - gy);

        for (int row = r0; row < r1; row++) {
          const int y = (int)(gy + (float)row);
          if (y < 0 || y >= s.cfg.height) continue;
          uint32_t *dst = s.framebuffer.data() + (size_t)y * s.cfg.width;
          for (int col = c0; col < c1; col++) {
            const int x = (int)(gx + (float)col);
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
  ArcRing r;
  if (!arc_ring(n, r)) return false;

  const float dx = px - r.cx, dy = py - r.cy;
  if (std::fabs(std::sqrt(dx * dx + dy * dy) - r.mid) > r.half) return false;

  const float sweep = n.f[MR_PROP_ARC_SWEEP];
  if (sweep <= 0.0f) return false;
  if (sweep >= 360.0f) return true;
  return arc_within_sweep(n, dx, dy, sweep);
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
