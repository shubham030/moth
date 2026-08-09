/* Fonts, measurement and line breaking.
 *
 * Layout reserves space for text and paint draws it, and the two must agree to
 * the pixel — so both come here rather than each doing its own arithmetic. A
 * label whose height was measured for two lines and then painted as three
 * would overflow its box with nothing to blame.
 */
#include "scene_internal.hpp"

#include <algorithm>

#include "fonts/registry.h"

namespace mr {

/* A glyph the face does not carry. fontgen emits a zero entry for anything
 * outside the subset, and a real space has an advance, so this is unambiguous. */
static bool is_absent(const moth_glyph *g) {
  return g->adv_w == 0 && g->box_w == 0;
}

static const moth_glyph *glyph_for(const moth_font *f, unsigned char ch) {
  if (ch < f->first || ch > f->last) return nullptr;
  const moth_glyph *g = &f->glyphs[ch - f->first];
  return is_absent(g) ? nullptr : g;
}

/* Width to advance for a character this face cannot draw. A space if there is
 * one, otherwise half the line height — the point is that missing text takes
 * up room and is visible, rather than silently closing up. */
static float notdef_advance(const moth_font *f) {
  const moth_glyph *space = glyph_for(f, ' ');
  return space ? (float)space->adv_w : (float)f->line_height * 0.5f;
}

static bool covers(const moth_font *f, const std::string &text) {
  for (unsigned char ch : text) {
    if (ch == '\n') continue;
    if (!glyph_for(f, ch)) return false;
  }
  return true;
}

const moth_font *font_for(float size, const std::string &text) {
  /* Largest face at or below the requested size that can render the string.
   * Ranking by size alone hands a word to a digits-only face, which measures
   * zero and draws nothing — the caller gets no hint that it happened. */
  const moth_font *best = nullptr;
  for (int i = 0; i < kFaceCount; i++) {
    if ((float)kFaces[i]->size <= size + 0.5f && covers(kFaces[i], text)) {
      best = kFaces[i];
    }
  }
  if (best) return best;

  /* Nothing small enough covers it. A covering face at the wrong size is far
   * better than a correctly sized blank, so take the largest that covers. */
  for (int i = 0; i < kFaceCount; i++) {
    if (covers(kFaces[i], text)) best = kFaces[i];
  }
  if (best) return best;

  /* Nothing covers it at all — fall back on size and let the missing glyphs
   * draw as notdef boxes, which at least shows up. */
  best = kFaces[0];
  for (int i = 0; i < kFaceCount; i++) {
    if ((float)kFaces[i]->size <= size + 0.5f) best = kFaces[i];
  }
  return best;
}

float glyph_advance(const moth_font *f, unsigned char ch) {
  const moth_glyph *g = glyph_for(f, ch);
  return g ? (float)g->adv_w : notdef_advance(f);
}

const moth_glyph *glyph_or_null(const moth_font *f, unsigned char ch) {
  return glyph_for(f, ch);
}

void wrap_text(const moth_font *f, const std::string &text, float max_w,
               std::vector<TextLine> &out) {
  out.clear();
  if (text.empty()) {
    out.push_back({0, 0, 0.0f, 0.0f});
    return;
  }
  /* No width means no wrapping — but a newline still breaks, so the scan
   * runs either way with the limit pushed out of reach. */
  const float limit = max_w > 0.0f ? max_w : 1e9f;

  size_t line_start = 0;
  size_t last_break = std::string::npos; /* the space we could fall back to */
  float width = 0.0f, width_at_break = 0.0f;
  float ink = 0.0f, ink_at_break = 0.0f;

  for (size_t i = 0; i < text.size(); i++) {
    const unsigned char ch = (unsigned char)text[i];
    if (ch == '\n') {
      out.push_back({(uint32_t)line_start, (uint32_t)(i - line_start), width,
                     std::max(ink, width)});
      line_start = i + 1;
      last_break = std::string::npos;
      width = 0.0f;
      ink = 0.0f;
      continue;
    }

    const float adv = glyph_advance(f, ch);

    if (ch == ' ') {
      last_break = i;
      width_at_break = width;
      ink_at_break = ink;
    }

    if (width + adv > limit && i > line_start) {
      if (last_break != std::string::npos && last_break > line_start) {
        /* Break at the space, and drop it rather than leading the next line. */
        out.push_back({(uint32_t)line_start,
                       (uint32_t)(last_break - line_start), width_at_break,
                       std::max(ink_at_break, width_at_break)});
        line_start = last_break + 1;
        i = last_break; /* the loop's ++ resumes after the space */
      } else {
        /* One word wider than the box: break inside it, since the
         * alternative is drawing past the edge. */
        out.push_back({(uint32_t)line_start, (uint32_t)(i - line_start), width,
                       std::max(ink, width)});
        line_start = i;
        i -= 1;
      }
      last_break = std::string::npos;
      width = 0.0f;
      ink = 0.0f;
      continue;
    }

    /* Ink reaches ofs_x + box_w from the pen, which for many glyphs is past
     * the advance the pen will move by. */
    const moth_glyph *g = glyph_or_null(f, ch);
    if (g) ink = std::max(ink, width + (float)g->ofs_x + (float)g->box_w);
    width += adv;
  }

  /* Only a real tail. Text ending in a newline has already had its line
   * pushed, and adding another reports a blank line that layout then reserves
   * room for. */
  if (line_start < text.size() || out.empty()) {
    out.push_back({(uint32_t)line_start, (uint32_t)(text.size() - line_start),
                   width, std::max(ink, width)});
  }
}

void layout_text(Node &n, float max_w, float &out_w, float &out_h) {
  const moth_font *f = font_for(n.f[MR_PROP_FONT_SIZE], n.text);

  /* Wrapping the same string twice a frame is how layout and paint end up
   * disagreeing, so it happens once and the result lives on the node. */
  if (n.lines_font != f || n.lines_width != max_w || n.lines.empty()) {
    wrap_text(f, n.text, max_w, n.lines);
    n.lines_width = max_w;
    n.lines_font = f;
  }

  /* Size the box by ink, not by where the pen stops, or the last glyph on a
   * line paints outside its own layout and gets clipped. */
  float widest = 0.0f;
  for (const TextLine &l : n.lines) widest = std::max(widest, l.ink);

  out_w = widest;
  out_h = (float)n.lines.size() * (float)f->line_height;
}

} // namespace mr
