/* Contract implementation: tree ops, props, events, animation, frame. */
#include "scene_internal.hpp"

#include <algorithm>
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

static mr_node_id hit_test(Scene &s, mr_node_id id, float px, float py) {
  Node *n = s.get(id);
  if (!n || n->f[MR_PROP_OPACITY] <= 0.0f) return MR_NODE_NONE;
  /* children above parent; later siblings above earlier */
  for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
    mr_node_id hit = hit_test(s, *it, px, py);
    if (hit != MR_NODE_NONE) return hit;
  }
  bool inside = px >= n->x && px < n->x + n->w && py >= n->y && py < n->y + n->h;
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
  s.framebuffer.assign((size_t)cfg->width * cfg->height, 0xFF000000);
  return true;
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

void mr_detach(mr_node_id child) {
  Scene &s = scene();
  Node *c = s.get(child);
  if (!c || c->parent == MR_NODE_NONE) return;
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
  s.dirty = true;
}

void mr_set_f32(mr_node_id node, mr_prop prop, float v) {
  Node *n = scene().get(node);
  if (!n || prop >= MR_PROP_COUNT) return;
  n->f[prop] = v;
  scene().dirty = true;
}

void mr_set_u32(mr_node_id node, mr_prop prop, uint32_t v) {
  Node *n = scene().get(node);
  if (!n || prop >= MR_PROP_COUNT) return;
  n->u[prop] = v;
  scene().dirty = true;
}

void mr_set_str(mr_node_id node, mr_prop prop, const char *utf8) {
  Node *n = scene().get(node);
  if (!n || !utf8) return;
  if (prop == MR_PROP_TEXT) n->text = utf8;
  else if (prop == MR_PROP_IMAGE_SRC) n->image_src = utf8;
  else return;
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
    if (s.pressed_node != MR_NODE_NONE)
      s.emit({s.pressed_node, MR_EV_PRESSED, 0, px, py});
  } else if (!down && s.pointer_down) {
    if (s.pressed_node != MR_NODE_NONE) {
      s.emit({s.pressed_node, MR_EV_RELEASED, 0, px, py});
      if (hit_test(s, mr_root(), px, py) == s.pressed_node)
        s.emit({s.pressed_node, MR_EV_CLICKED, 0, px, py});
    }
    s.pressed_node = MR_NODE_NONE;
  }
  /* TODO(R1): slider drag gesture -> MR_EV_VALUE_CHANGED */
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

void mr_commit(void) {
  Scene &s = scene();
  if (!s.dirty) return;
  layout_run(s);
  paint_run(s);
  s.dirty = false;
}

const uint32_t *mr_framebuffer(void) { return scene().framebuffer.data(); }

void mr_damage(int *x, int *y, int *w, int *h) {
  /* TODO(R3): real dirty-rect tracking; v0 reports full frame */
  Scene &s = scene();
  if (x) *x = 0;
  if (y) *y = 0;
  if (w) *w = s.cfg.width;
  if (h) *h = s.cfg.height;
}

void mr_frame_of(mr_node_id node, float *x, float *y, float *w, float *h) {
  Node *n = scene().get(node);
  if (x) *x = n ? n->x : 0;
  if (y) *y = n ? n->y : 0;
  if (w) *w = n ? n->w : 0;
  if (h) *h = n ? n->h : 0;
}

} /* extern "C" */
