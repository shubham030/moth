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

const moth_font *font_for(float size) {
  /* Rank by the size a face was generated at, not by line_height — the
   * latter is ascent + descent and runs well above the em size, which would
   * quietly hand back a face one step too small. */
  const moth_font *best = kFaces[0];
  for (int i = 0; i < kFaceCount; i++) {
    if ((float)kFaces[i]->size <= size + 0.5f) best = kFaces[i];
  }
  return best;
}

static const moth_glyph *glyph_for(const moth_font *f, unsigned char ch) {
  if (ch < f->first || ch > f->last) return nullptr;
  return &f->glyphs[ch - f->first];
}

float text_advance(const moth_font *f, const char *s, size_t len) {
  float w = 0;
  for (size_t i = 0; i < len; i++) {
    const moth_glyph *g = glyph_for(f, (unsigned char)s[i]);
    if (g) w += (float)g->adv_w;
  }
  return w;
}

void wrap_text(const moth_font *f, const std::string &text, float max_w,
               std::vector<TextLine> &out) {
  out.clear();
  if (text.empty()) {
    out.push_back({0, 0, 0.0f});
    return;
  }
  /* No width means no wrapping — but a newline still breaks, so the scan
   * runs either way with the limit pushed out of reach. */
  const float limit = max_w > 0.0f ? max_w : 1e9f;

  size_t line_start = 0;
  size_t last_break = std::string::npos; /* the space we could fall back to */
  float width = 0.0f, width_at_break = 0.0f;

  for (size_t i = 0; i < text.size(); i++) {
    const unsigned char ch = (unsigned char)text[i];
    if (ch == '\n') {
      out.push_back({(uint32_t)line_start, (uint32_t)(i - line_start), width});
      line_start = i + 1;
      last_break = std::string::npos;
      width = 0.0f;
      continue;
    }

    const moth_glyph *g = glyph_for(f, ch);
    const float adv = g ? (float)g->adv_w : 0.0f;

    if (ch == ' ') {
      last_break = i;
      width_at_break = width;
    }

    if (width + adv > limit && i > line_start) {
      if (last_break != std::string::npos && last_break > line_start) {
        /* Break at the space, and drop it rather than leading the next line. */
        out.push_back({(uint32_t)line_start,
                       (uint32_t)(last_break - line_start), width_at_break});
        line_start = last_break + 1;
        i = last_break; /* the loop's ++ resumes after the space */
      } else {
        /* One word wider than the box: break inside it, since the
         * alternative is drawing past the edge. */
        out.push_back({(uint32_t)line_start, (uint32_t)(i - line_start), width});
        line_start = i;
        i -= 1;
      }
      last_break = std::string::npos;
      width = 0.0f;
      continue;
    }

    width += adv;
  }

  out.push_back({(uint32_t)line_start, (uint32_t)(text.size() - line_start), width});
}

void measure_text(const std::string &text, float font_size, float max_w,
                  float &out_w, float &out_h) {
  const moth_font *f = font_for(font_size);
  std::vector<TextLine> lines;
  wrap_text(f, text, max_w, lines);

  float widest = 0.0f;
  for (const TextLine &l : lines) widest = std::max(widest, l.width);

  out_w = widest;
  out_h = (float)lines.size() * (float)f->line_height;
}

} // namespace mr
