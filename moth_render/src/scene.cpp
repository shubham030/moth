/* Contract implementation: tree ops, props, events, animation, frame. */
#include "scene_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace mr {

Scene &scene() {
  static Scene s;
  return s;
}

static void node_defaults(Node &n, mr_node_kind kind) {
  n = Node{};
  n.alive = true;
  n.kind = kind;
  n.f[MR_PROP_WIDTH] = MR_AUTO;
  n.f[MR_PROP_HEIGHT] = MR_AUTO;
  n.u[MR_PROP_FLEX_DIRECTION] = MR_COLUMN;
  n.u[MR_PROP_MAIN_ALIGN] = MR_ALIGN_START;
  n.u[MR_PROP_CROSS_ALIGN] = MR_ALIGN_STRETCH;
  n.f[MR_PROP_OPACITY] = 1.0f;
  n.f[MR_PROP_FONT_SIZE] = 14.0f;
  n.u[MR_PROP_TEXT_COLOR] = 0xFF000000;
  n.u[MR_PROP_BG_COLOR] = 0x00000000;
  n.f[MR_PROP_MAX] = 100.0f;
}

static float ease(mr_easing e, float t) {
  switch (e) {
    case MR_EASE_OUT: return 1.0f - (1.0f - t) * (1.0f - t);
    case MR_EASE_IN_OUT: return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
    default: return t;
  }
}

/* Sets a slider's value from a pointer x, emitting VALUE_CHANGED only when
 * it actually changed. Geometry comes from paint's slider_geometry, so the
 * value under the finger is the value under the thumb. */
static void slider_drag(Scene &s, mr_node_id id, float px) {
  Node *n = s.get(id);
  if (!n || n->kind != MR_NODE_SLIDER) return;
  const float lo = n->f[MR_PROP_MIN], hi = n->f[MR_PROP_MAX];
  if (hi <= lo) return;
  float x0, x1, r;
  slider_geometry(*n, &x0, &x1, &r);
  if (x1 <= x0) return;
  float t = (px - x0) / (x1 - x0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  const float v = lo + t * (hi - lo);
  /* Dedupe against what the FINGER last produced, not what the node holds
   * (see Scene::drag_value): the app may have written back something else,
   * and honoring that must not re-trigger an emit while the finger is
   * still. */
  if (s.drag_valued && v == s.drag_value) return;
  s.drag_value = v;
  s.drag_valued = true;
  if (v == n->f[MR_PROP_VALUE]) return;
  mr_set_f32(id, MR_PROP_VALUE, v);
  s.emit({id, MR_EV_VALUE_CHANGED, v, px, 0});
}

/* Flips a switch and reports it. The contract has the backend own control
 * gestures: a switch emits 0/1, and the widget layer decides whether the
 * new state sticks (a controlled component may set it right back). */
static void switch_toggle(Scene &s, mr_node_id id, float px, float py) {
  Node *n = s.get(id);
  if (!n || n->kind != MR_NODE_SWITCH) return;
  const float v = n->f[MR_PROP_VALUE] >= 0.5f ? 0.0f : 1.0f;
  mr_set_f32(id, MR_PROP_VALUE, v);
  s.emit({id, MR_EV_VALUE_CHANGED, v, px, py});
}

static mr_node_id hit_test(Scene &s, mr_node_id id, float px, float py) {
  Node *n = s.get(id);
  if (!n || n->f[MR_PROP_OPACITY] <= 0.0f) return MR_NODE_NONE;
  /* children above parent; later siblings above earlier */
  for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
    mr_node_id hit = hit_test(s, *it, px, py);
    if (hit != MR_NODE_NONE) return hit;
  }
  /* An arc is a stroke, not a surface: its box covers the whole ring, so
   * testing that box would make a decorative overlay swallow every tap
   * beneath it. Ask the geometry instead. */
  if (n->kind == MR_NODE_ARC) return arc_hit(*n, px, py) ? id : MR_NODE_NONE;

  /* A finger is not a mouse. The first on-glass test of the 24px slider
   * logged thirty touches around it and almost none inside it — one landed
   * seven pixels off. Controls accept touches out to a 48px-tall band
   * (Flutter's minimum touch target) and a little past each end; their
   * PAINTED box is unchanged. */
  float pad_x = 0.0f, pad_y = 0.0f;
  if (n->kind == MR_NODE_SLIDER || n->kind == MR_NODE_SWITCH) {
    if (n->kind == MR_NODE_SLIDER) {
      /* A slider too narrow for its thumb travel (w < 2r) paints nothing
       * and cannot drag — it must not sit invisibly on top of whatever is
       * behind it, eating touches, either. */
      float x0, x1, r;
      slider_geometry(*n, &x0, &x1, &r);
      if (x1 <= x0) return MR_NODE_NONE;
    }
    if (n->h < 48.0f) pad_y = (48.0f - n->h) * 0.5f;
    pad_x = 8.0f;
  }
  bool inside = px >= n->x - pad_x && px < n->x + n->w + pad_x &&
                py >= n->y - pad_y && py < n->y + n->h + pad_y;
  return inside ? id : MR_NODE_NONE;
}

} // namespace mr

using namespace mr;

extern "C" {

bool mr_init(const mr_config *cfg) {
  if (!cfg || cfg->width <= 0 || cfg->height <= 0) return false;
  Scene &s = scene();
  s = Scene{};
  s.cfg = *cfg;
  s.nodes.resize(2); /* slot 0 unused, slot 1 = root */
  node_defaults(s.nodes[1], MR_NODE_BOX);
  s.nodes[1].f[MR_PROP_WIDTH] = (float)cfg->width;
  s.nodes[1].f[MR_PROP_HEIGHT] = (float)cfg->height;
  /* Also fill the computed frame, so the display size can be queried before
   * the first commit has run layout. */
  s.nodes[1].w = (float)cfg->width;
  s.nodes[1].h = (float)cfg->height;
  s.framebuffer.assign((size_t)cfg->width * cfg->height, 0xFF000000);
  return true;
}

void mr_safe_area(int *x, int *y, int *w, int *h) {
  Scene &s = scene();
  int w_px = s.cfg.width, h_px = s.cfg.height;
  if (s.cfg.shape == MR_SHAPE_ROUND) {
    /* Inscribed square of the circle: side = diameter / sqrt(2). */
    int diameter = w_px < h_px ? w_px : h_px;
    int side = (int)((float)diameter * 0.70710678f);
    if (x) *x = (w_px - side) / 2;
    if (y) *y = (h_px - side) / 2;
    if (w) *w = side;
    if (h) *h = side;
    return;
  }
  if (x) *x = 0;
  if (y) *y = 0;
  if (w) *w = w_px;
  if (h) *h = h_px;
}

void mr_reset(void) {
  Scene &s = scene();
  mr_config cfg = s.cfg;
  /* The sink belongs to the host, not the program, so it survives a swap. */
  mr_event_cb sink = s.sink;
  void *sink_user = s.sink_user;
  mr_init(&cfg);
  s.sink = sink;
  s.sink_user = sink_user;
}

void mr_shutdown(void) { scene() = Scene{}; }

uint32_t mr_contract_version(void) { return MR_CONTRACT_VERSION; }

mr_node_id mr_root(void) { return (mr_node_id)1; }

mr_node_id mr_node_create(mr_node_kind kind) {
  Scene &s = scene();
  /* ids are never reused (contract §3): always append */
  s.nodes.emplace_back();
  node_defaults(s.nodes.back(), kind);
  s.dirty = true;
  return (mr_node_id)(s.nodes.size() - 1);
}

/* Rows a node last occupied, so what it leaves behind is repainted. */
static void damage_previous(Scene &s, mr_node_id id) {
  Node *n = s.get(id);
  if (!n) return;
  if (n->painted && n->prev_h > 0.0f) {
    if (s.damage_y1 <= s.damage_y0) {
      s.damage_y0 = n->prev_y;
      s.damage_y1 = n->prev_y + n->prev_h;
    } else {
      s.damage_y0 = std::min(s.damage_y0, n->prev_y);
      s.damage_y1 = std::max(s.damage_y1, n->prev_y + n->prev_h);
    }
  }
  for (mr_node_id k : n->children) damage_previous(s, k);
}

void mr_detach(mr_node_id child) {
  Scene &s = scene();
  Node *c = s.get(child);
  if (!c || c->parent == MR_NODE_NONE) return;
  /* Where it was has to be repainted, and after detaching nobody remembers. */
  damage_previous(s, child);
  Node *p = s.get(c->parent);
  if (p) {
    auto &v = p->children;
    for (size_t i = 0; i < v.size(); i++)
      if (v[i] == child) { v.erase(v.begin() + (long)i); break; }
  }
  c->parent = MR_NODE_NONE;
  s.dirty = true;
}

void mr_node_destroy(mr_node_id node) {
  Scene &s = scene();
  Node *n = s.get(node);
  if (!n || node == mr_root()) return;
  damage_previous(s, node);
  mr_detach(node);
  /* copy: children vector mutates during recursion */
  std::vector<mr_node_id> kids = n->children;
  for (mr_node_id k : kids) mr_node_destroy(k);
  s.nodes[node].alive = false;
  if (s.pressed_node == node) s.pressed_node = MR_NODE_NONE;
  for (auto &a : s.anims)
    if (a.node == node) a.alive = false;
  s.dirty = true;
}

void mr_attach(mr_node_id parent, mr_node_id child, int index) {
  Scene &s = scene();
  Node *p = s.get(parent);
  Node *c = s.get(child);
  if (!p || !c || p->kind != MR_NODE_BOX || child == mr_root()) return;
  mr_detach(child);
  if (index < 0 || (size_t)index >= p->children.size())
    p->children.push_back(child);
  else
    p->children.insert(p->children.begin() + index, child);
  c->parent = parent;
  c->touched = true;
  s.dirty = true;
}

/* A rebuild re-applies every property of every widget, so without comparing
 * first, every node is "changed" every frame and the damage band is always
 * the whole screen — which makes damage tracking do nothing at all. */
void mr_set_f32(mr_node_id node, mr_prop prop, float v) {
  Node *n = scene().get(node);
  if (!n || prop >= MR_PROP_COUNT) return;
  if (n->f[prop] == v) return;
  n->f[prop] = v;
  n->touched = true;
  scene().dirty = true;
}

void mr_set_u32(mr_node_id node, mr_prop prop, uint32_t v) {
  Node *n = scene().get(node);
  if (!n || prop >= MR_PROP_COUNT) return;
  if (n->u[prop] == v) return;
  n->u[prop] = v;
  n->touched = true;
  scene().dirty = true;
}

void mr_set_str(mr_node_id node, mr_prop prop, const char *utf8) {
  Node *n = scene().get(node);
  if (!n || !utf8) return;
  if (prop == MR_PROP_TEXT) {
    /* The wrapped lines are ranges into this string. Reusing them against
     * different text indexes past the end of a shorter one — 'NO GPS'
     * becoming 'GPS' is enough — so the cache dies with the text it
     * described. */
    if (n->text == utf8) return;
    {
      n->text = utf8;
      n->touched = true;
      n->lines.clear();
      n->lines_width = -1.0f;
      n->lines_font = nullptr;
      /* The hint is arrange's answer for the old text. Wrapping the new text
       * against it ratchets: a wider text wraps at the old width, measures
       * narrower for it, and the hint shrinks to match — a clock that ticked
       * past its first width stayed two lines forever. */
      n->wrap_hint = -1.0f;
    }
  }
  else if (prop == MR_PROP_IMAGE_SRC) {
    if (n->image_src == utf8) return;
    n->image_src = utf8;
  } else {
    return;
  }
  n->touched = true;
  scene().dirty = true;
}

void mr_set_event_sink(mr_event_cb cb, void *user) {
  scene().sink = cb;
  scene().sink_user = user;
}

void mr_pointer(int x, int y, bool down) {
  Scene &s = scene();
  float px = (float)x, py = (float)y;
  if (down && !s.pointer_down) {
    s.pressed_node = hit_test(s, mr_root(), px, py);
    s.drag_valued = false; /* a new gesture owes its first value an emit */
    if (s.pressed_node != MR_NODE_NONE) {
      s.emit({s.pressed_node, MR_EV_PRESSED, 0, px, py});
      /* Pressing a slider jumps the thumb to the finger, as Flutter's
       * does; the same call then tracks every move while held. */
      slider_drag(s, s.pressed_node, px);
    }
  } else if (down && s.pointer_down) {
    slider_drag(s, s.pressed_node, px);
  } else if (!down && s.pointer_down) {
    if (s.pressed_node != MR_NODE_NONE) {
      s.emit({s.pressed_node, MR_EV_RELEASED, 0, px, py});
      if (hit_test(s, mr_root(), px, py) == s.pressed_node) {
        /* Controls claim their gesture, as Flutter's do: a completed slider
         * drag or switch tap already reported itself as VALUE_CHANGED, and
         * a CLICKED here would bubble to any tappable ancestor — a slider
         * inside a tappable card would fire the card on every drag. */
        const Node *rn = s.get(s.pressed_node);
        const bool claims = rn && (rn->kind == MR_NODE_SLIDER ||
                                   rn->kind == MR_NODE_SWITCH);
        if (!claims) s.emit({s.pressed_node, MR_EV_CLICKED, 0, px, py});
        switch_toggle(s, s.pressed_node, px, py);
      }
    }
    s.pressed_node = MR_NODE_NONE;
  }
  s.pointer_down = down;
}

uint32_t mr_anim_start(mr_node_id node, mr_prop prop, float from, float to,
                       uint32_t duration_ms, mr_easing easing) {
  Scene &s = scene();
  if (!s.get(node) || duration_ms == 0) return 0;
  Anim a;
  a.alive = true;
  a.id = s.next_anim_id++;
  a.node = node;
  a.prop = prop;
  a.from = from;
  a.to = to;
  a.duration_ms = duration_ms;
  a.easing = easing;
  s.anims.push_back(a);
  return a.id;
}

void mr_anim_stop(uint32_t anim_id) {
  for (auto &a : scene().anims)
    if (a.id == anim_id) a.alive = false;
}

void mr_tick(uint32_t dt_ms) {
  Scene &s = scene();
  for (auto &a : s.anims) {
    if (!a.alive) continue;
    a.elapsed_ms += dt_ms;
    float t = a.elapsed_ms >= a.duration_ms
                  ? 1.0f
                  : (float)a.elapsed_ms / (float)a.duration_ms;
    /* TODO(R2): color-space interpolation for BG_COLOR/OPACITY-as-u32 props */
    mr_set_f32(a.node, a.prop, a.from + (a.to - a.from) * ease(a.easing, t));
    if (t >= 1.0f) {
      a.alive = false;
      s.emit({a.node, MR_EV_ANIM_COMPLETED, (float)a.id, 0, 0});
    }
  }
  s.anims.erase(
      std::remove_if(s.anims.begin(), s.anims.end(), [](const Anim &a) { return !a.alive; }),
      s.anims.end());
}

/* Widens the damage band to cover a node, both where it is and where it was.
 * A node is repainted when something about it changed, or when layout moved
 * it — the second case is why the previous bounds are kept. */
static void collect_damage(Scene &s, mr_node_id id) {
  Node *n = s.get(id);
  if (!n) return;

  const bool moved = !n->painted || n->prev_y != n->y || n->prev_h != n->h;
  if (n->touched || moved) {
    float y0 = n->y, y1 = n->y + n->h;
    if (n->painted) {
      y0 = std::min(y0, n->prev_y);
      y1 = std::max(y1, n->prev_y + n->prev_h);
    }
    if (s.damage_y1 <= s.damage_y0) {
      s.damage_y0 = y0;
      s.damage_y1 = y1;
    } else {
      s.damage_y0 = std::min(s.damage_y0, y0);
      s.damage_y1 = std::max(s.damage_y1, y1);
    }
  }
  for (mr_node_id k : n->children) collect_damage(s, k);
}

static void record_painted(Scene &s, mr_node_id id) {
  Node *n = s.get(id);
  if (!n) return;
  n->prev_y = n->y;
  n->prev_h = n->h;
  n->painted = true;
  n->touched = false;
  for (mr_node_id k : n->children) record_painted(s, k);
}

bool mr_commit(void) {
  Scene &s = scene();
  if (!s.dirty) return false;
  MR_PROF_START(t_layout);
  layout_run(s);
  MR_PROF_ADD(t_layout, mr_prof_layout_us);

  /* Start empty. Without this the band only ever grows, so the first frame —
   * where every node is unpainted and the whole screen is damaged — pins it
   * at full frame forever and the tracking does nothing. Detach and destroy
   * widen it before layout, so this resets only what this frame collects. */
  const float carried_y0 = s.damage_y0, carried_y1 = s.damage_y1;
  s.damage_y0 = 0.0f;
  s.damage_y1 = 0.0f;
  if (carried_y1 > carried_y0) {
    s.damage_y0 = carried_y0;
    s.damage_y1 = carried_y1;
  }

  collect_damage(s, mr_root());

  /* Clamp to the frame. An empty band means the tree changed in a way that
   * moved nothing — nothing to draw, and nothing to push. */
  s.clip_y0 = std::max(0, (int)std::floor(s.damage_y0));
  s.clip_y1 = std::min(s.cfg.height, (int)std::ceil(s.damage_y1));
  const bool anything = s.clip_y1 > s.clip_y0;

  if (anything) paint_run(s);

  record_painted(s, mr_root());
  s.damage_y0 = 0.0f;
  s.damage_y1 = 0.0f;
  s.dirty = false;
  return anything;
}

const uint32_t *mr_framebuffer(void) { return scene().framebuffer.data(); }

void mr_damage(int *x, int *y, int *w, int *h) {
  /* Full width by design: a band cannot be partially covered by a sibling,
   * which is what keeps overlap and translucency from mattering. */
  Scene &s = scene();
  if (x) *x = 0;
  if (w) *w = s.cfg.width;
  if (y) *y = s.clip_y0;
  if (h) *h = std::max(0, s.clip_y1 - s.clip_y0);
}

void mr_frame_of(mr_node_id node, float *x, float *y, float *w, float *h) {
  Node *n = scene().get(node);
  if (x) *x = n ? n->x : 0;
  if (y) *y = n ? n->y : 0;
  if (w) *w = n ? n->w : 0;
  if (h) *h = n ? n->h : 0;
}

} /* extern "C" */
