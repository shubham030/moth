/* moth flex subset — docs/BACKEND.md §4.
 * Measure pass (auto sizes, bottom-up) then arrange pass (positions, grow,
 * alignment, top-down). Coordinates are root-relative float px.
 */
#include "scene_internal.hpp"

#include <algorithm>
#include <cmath>

namespace mr {

static bool is_auto(float v) { return v < 0.0f; }

/* TODO(R2): real text measurement via ThorVG font metrics. Placeholder uses
 * a fixed advance ratio so layout goldens can be written against something
 * deterministic, then re-baselined once real metrics land. */
static void leaf_auto_size(const Node &n, float &w, float &h) {
  switch (n.kind) {
    case MR_NODE_LABEL: {
      float fs = n.f[MR_PROP_FONT_SIZE];
      w = 0.55f * fs * (float)n.text.size();
      h = 1.25f * fs;
      break;
    }
    case MR_NODE_IMAGE:  w = 32; h = 32; break; /* TODO(R2): intrinsic size */
    case MR_NODE_SLIDER: w = 160; h = 24; break;
    case MR_NODE_SWITCH: w = 40; h = 24; break;
    default:             w = 0; h = 0; break;
  }
}

static void measure(Scene &s, mr_node_id id, float &out_w, float &out_h) {
  Node *n = s.get(id);
  if (!n) { out_w = out_h = 0; return; }

  float cw = 0, ch = 0; /* content size */
  if (n->kind == MR_NODE_BOX) {
    bool row = n->u[MR_PROP_FLEX_DIRECTION] == MR_ROW;
    float main_sum = 0, cross_max = 0;
    int flow_count = 0;
    for (mr_node_id c : n->children) {
      Node *cn = s.get(c);
      if (!cn) continue;
      float w, h;
      measure(s, c, w, h);
      if (cn->u[MR_PROP_POSITION] == MR_ABSOLUTE) continue;
      flow_count++;
      main_sum += row ? w : h;
      cross_max = std::max(cross_max, row ? h : w);
    }
    if (flow_count > 1) main_sum += n->f[MR_PROP_GAP] * (float)(flow_count - 1);
    cw = row ? main_sum : cross_max;
    ch = row ? cross_max : main_sum;
    cw += 2 * n->f[MR_PROP_PADDING];
    ch += 2 * n->f[MR_PROP_PADDING];
  } else {
    leaf_auto_size(*n, cw, ch);
  }

  out_w = is_auto(n->f[MR_PROP_WIDTH]) ? cw : n->f[MR_PROP_WIDTH];
  out_h = is_auto(n->f[MR_PROP_HEIGHT]) ? ch : n->f[MR_PROP_HEIGHT];
  n->w = out_w;
  n->h = out_h;
}

static void arrange(Scene &s, mr_node_id id) {
  Node *n = s.get(id);
  if (!n || n->kind != MR_NODE_BOX) return;

  bool row = n->u[MR_PROP_FLEX_DIRECTION] == MR_ROW;
  float pad = n->f[MR_PROP_PADDING];
  float gap = n->f[MR_PROP_GAP];
  float inner_main = (row ? n->w : n->h) - 2 * pad;
  float inner_cross = (row ? n->h : n->w) - 2 * pad;

  /* flow children: used space and grow total */
  float used = 0, grow_total = 0;
  int flow_count = 0;
  for (mr_node_id c : n->children) {
    Node *cn = s.get(c);
    if (!cn || cn->u[MR_PROP_POSITION] == MR_ABSOLUTE) continue;
    flow_count++;
    used += row ? cn->w : cn->h;
    grow_total += cn->f[MR_PROP_FLEX_GROW];
  }
  if (flow_count > 1) used += gap * (float)(flow_count - 1);
  float leftover = std::max(0.0f, inner_main - used);

  /* distribute grow */
  if (grow_total > 0) {
    for (mr_node_id c : n->children) {
      Node *cn = s.get(c);
      if (!cn || cn->u[MR_PROP_POSITION] == MR_ABSOLUTE) continue;
      float extra = leftover * (cn->f[MR_PROP_FLEX_GROW] / grow_total);
      if (row) cn->w += extra; else cn->h += extra;
    }
    leftover = 0;
  }

  /* main-axis start offset and inter-child spacing (§4: main_align ignored
   * when any child grows — leftover is already 0 then) */
  float cursor = pad, spacing = gap;
  switch ((mr_align)n->u[MR_PROP_MAIN_ALIGN]) {
    case MR_ALIGN_CENTER: cursor += leftover / 2; break;
    case MR_ALIGN_END: cursor += leftover; break;
    case MR_ALIGN_SPACE_BETWEEN:
      if (flow_count > 1) spacing = gap + leftover / (float)(flow_count - 1);
      break;
    default: break;
  }

  for (mr_node_id c : n->children) {
    Node *cn = s.get(c);
    if (!cn) continue;

    if (cn->u[MR_PROP_POSITION] == MR_ABSOLUTE) {
      cn->x = n->x + pad + cn->f[MR_PROP_LEFT];
      cn->y = n->y + pad + cn->f[MR_PROP_TOP];
      arrange(s, c);
      continue;
    }

    mr_align cross_align = (mr_align)n->u[MR_PROP_CROSS_ALIGN];
    bool cross_auto = is_auto(row ? cn->f[MR_PROP_HEIGHT] : cn->f[MR_PROP_WIDTH]);
    if (cross_align == MR_ALIGN_STRETCH && cross_auto) {
      if (row) cn->h = inner_cross; else cn->w = inner_cross;
    }
    float cross_size = row ? cn->h : cn->w;
    float cross_off = pad;
    switch (cross_align) {
      case MR_ALIGN_CENTER: cross_off += (inner_cross - cross_size) / 2; break;
      case MR_ALIGN_END: cross_off += inner_cross - cross_size; break;
      default: break; /* start; stretch with fixed size falls back to start */
    }

    if (row) {
      cn->x = n->x + cursor;
      cn->y = n->y + cross_off;
      cursor += cn->w + spacing;
    } else {
      cn->x = n->x + cross_off;
      cn->y = n->y + cursor;
      cursor += cn->h + spacing;
    }
    arrange(s, c);
  }
}

void layout_run(Scene &s) {
  Node *root = s.get((mr_node_id)1);
  if (!root) return;
  float w, h;
  measure(s, (mr_node_id)1, w, h);
  root->x = 0;
  root->y = 0;
  root->w = (float)s.cfg.width;
  root->h = (float)s.cfg.height;
  arrange(s, (mr_node_id)1);
}

} // namespace mr
