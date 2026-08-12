---
title: Backend contract
sidebar_position: 11
slug: /backend
---

# The backend contract

A **backend** turns moth's semantic node tree into pixels and input events.
Two implementations: `moth_render` (the native renderer — built, and what the
ESP32-S3 firmware ships today) and `lvgl` (planned, wraps LVGL 9; see
ADR-008). Dart code — the widget framework and apps — sees only this
contract, never a backend's internals.

The C header `moth_render/include/moth_render.h` is the normative API; this
document is the normative *semantics*. A backend is correct iff it passes the
conformance suite (§7).

## 1. Design rules

- **Semantic nodes, not primitives.** The contract speaks in `label`, `slider`,
  `box` — not rects and glyphs. This lets the LVGL backend use `lv_slider`
  (mature, accessible, themed) while moth_render composes primitives internally.
- **moth owns the semantics.** Layout (§4) and style (§5) behavior is defined
  *here*, in backend-neutral terms. Backends map moth semantics onto their
  engine — never the reverse. If LVGL flex and this spec disagree, the LVGL
  backend must compensate; the spec does not bend to an engine.
- **Batched frames.** Mutations (`create/attach/set_*`) are cheap and deferred;
  `commit()` applies them: layout → damage → paint, and returns whether a
  repaint occurred — platforms skip the display flush (expensive over SPI)
  when it didn't. One commit per event-loop tick, at most.
- **The VM is never in the frame loop.** Continuous animation runs inside the
  backend (§6); the contract only starts/stops it.

## 2. Node kinds

| kind     | purpose                          | notes |
| -------- | -------------------------------- | ----- |
| `box`    | container; background, layout    | the only node with children |
| `label`  | single run of text               | wraps within its bounds |
| `image`  | raster asset by key              | asset resolution is backend-supplied |
| `slider` | horizontal value control         | emits `value_changed` |
| `switch` | boolean toggle                   | emits `value_changed` (0/1) |

v1 deliberately small; `arc`, `textinput`, `canvas` are post-v1 additions and
require a contract version bump (§8).

## 2.5 Display shape

A panel declares itself rectangular or round. Layout stays rectangular
either way — a round panel still has a rectangular framebuffer — but the
corners of a round one sit behind the bezel.

`mr_safe_area()` reports the largest rectangle guaranteed to be visible: the
whole display when rectangular, the inscribed square when round. For a 466px
circular panel that is 329x329 at (68, 68), since the inscribed square of a
circle has side `diameter / sqrt(2)`.

Apps should keep content inside it rather than hardcoding a padding. Nothing
clips to the circle: a background may still cover the full framebuffer, which
is usually what you want.

## 3. Tree operations

`create(kind) → id`, `destroy(id)` (recursive, detaches first),
`attach(parent, child, index)`, `detach(child)`. Only `box` may have children.
Node ids are opaque `uint32`, never reused within a session. The root box is
created by `init` and spans the display.

## 4. Layout — the moth flex subset

Every `box` lays out children on one axis. Properties and exact meaning:

- `flex_direction`: `row` | `column` (default `column`)
- `width`, `height`: px, or `auto` (default). Auto = content size: sum of
  children (+ gaps) on the main axis, max child on the cross axis, plus padding.
  Leaf auto sizes: label = measured text; image = intrinsic; slider = 160×24;
  switch = 40×24 (px, before styling).
- `flex_grow`: float ≥ 0 (default 0). After fixed/auto sizing, remaining main-
  axis space is distributed proportionally to grow factors. No shrink in v1 —
  overflow simply clips.
- `main_align`: `start` | `center` | `end` | `space_between` (default `start`).
  Ignored when any child grows (leftover space is zero).
- `cross_align`: `start` | `center` | `end` | `stretch` (default `stretch`,
  matching CSS flexbox). Stretch makes *auto-sized* children fill the cross
  axis; children with a fixed cross size keep it and fall back to `start`
  placement. Without stretch, a row inside a column could never fill the width.
- `gap`: px between adjacent children (default 0)
- `padding`: uniform px inset (default 0; per-side is post-v1)
- `position`: `flow` (default) | `absolute`. Absolute nodes leave the flex flow
  and place at (`left`, `top`) relative to the parent's padding box.

No wrap, no percent sizes, no margins in v1 (margins = wrap in a padded box).
Coordinates are float px; backends may snap to physical pixels when painting
but must report unsnapped frames in `frame_of` (§7).

## 5. Style properties

| property | type | applies to | default |
| -------- | ---- | ---------- | ------- |
| `bg_color` | ARGB8888 | box, slider, switch | transparent |
| `radius` | px | box, image | 0 |
| `border_width` / `border_color` | px / ARGB | box | 0 |
| `opacity` | 0..1 | all (multiplies subtree) | 1 |
| `text`, `font_size`, `text_color` | utf8 / px / ARGB | label | "", 14, opaque black |
| `image_src` | asset key | image | — |
| `value`, `min`, `max` | float | slider, switch | 0, 0, 100 |

Setters are typed (`set_f32`, `set_u32`, `set_str`); setting a property a node
kind doesn't support is a no-op (logged in debug builds, never a crash).

## 6. Events and animation

**Events** flow backend → one registered sink: `{node, kind, value, x, y}` with
kinds `pressed`, `released`, `clicked`, `value_changed`. The backend performs
hit-testing and control gestures (slider drag) natively; the sink (the VM event
loop) only sees semantic results. The sink must never re-enter the contract
synchronously — it queues. Hit-testing for `slider` and `switch` extends to at
least a 48px-tall band around the painted control (finger-sized targets); a
touch that misses the pixels but lands in the band still hits.

**Animation**: `anim_start(node, prop, from, to, duration_ms, easing) → anim_id`,
`anim_stop(anim_id)`. Easing: `linear`, `ease_out`, `ease_in_out`. Animatable:
any f32 property plus `bg_color`/`opacity`. The backend interpolates every
frame natively; a `completed` event fires at the end. This is the *only*
sanctioned path for continuous motion.

**Time**: the platform drives `tick(dt_ms)` + `commit()`; the contract has no
internal clock (keeps the core deterministic and testable).

## 7. Conformance suite

Lives in `conformance/` (host-run, no hardware):

1. **Layout goldens** — scene scripts (JSON: build tree, set props, commit)
   with expected `frame_of(node)` rects for every node. Numeric, exact to
   0.5px. This is the primary gate; it encodes §4 completely.
2. **Op-trace goldens** — for the reconciler: widget tree in → expected
   contract-call sequence out (backend-independent; uses the recording "null
   backend").
3. **Pixel goldens** — screenshot comparisons with perceptual tolerance;
   advisory, not gating (backends legitimately render differently).

A backend passes ⇔ 1 is green. The suite is the spec's executable form —
behavior changes land as a golden change first.

## 8. Versioning

The contract carries a single integer `MR_CONTRACT_VERSION`. Additive changes
(new node kind, new prop) bump it; backends report the version they implement
and the framework refuses mismatches at init. Pre-1.0 there is no
compatibility promise, only honesty.
