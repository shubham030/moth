# Reviewing render changes for performance

Every item on this list is something that actually happened in this codebase
and cost real frames. Review any change to `moth_render/src/`,
`ui/src/moth_ui.c`, or `moth_render/esp-s3/main/panel.c` against it. The numbers
that anchor it: 466x466 at 4 bytes/px, writing one 177-row band through PSRAM
costs ~9.5ms on the ESP32-S3, and a 30fps frame is a 33ms budget — three
band-writes and it's gone.

## Painting

- **Does any loop touch pixels it cannot change?** A fully transparent fill
  took the blend path, and `row[x] = blend(row[x], ...)` read and wrote every
  pixel back unchanged. Every Stack/Column/Padding wrapper and every label
  background did this over the full band — 95ms of the 138ms paint. Skips
  belong *before* the pixel loop (`invisible()` in paint.cpp), not inside it.
- **Is work proportional to what's drawn, or to the bounding box?** The arc
  scanned its full-width box and rejected pixel by pixel; the ring itself was
  ~3% of the box. Same trap available in rounded rects (SDF runs over the
  whole box) and any future primitive with a bounding box much larger than
  its ink.
- **Is anything painted only to be painted over?** Backgrounds under an
  opaque full-band cover are dead writes; `find_band_cover` skips them. A new
  widget that adds an opaque full-bleed layer gets this for free — but only
  if it is a BOX with radius 0 and alpha FF; check the conditions still match
  what the widget produces.
- **Float math per pixel?** Float blending cost the same in internal RAM as
  in PSRAM (34ms vs 13ms a band) — the conversions were the cost, not the
  memory. Per-pixel work should be integer; float is fine per-node or
  per-row.

## Caching and damage

- **Does a cached value outlive the thing it described?** Twice: wrapped
  lines keyed on font+width survived a text change (read past the end of the
  new string); `wrap_hint` survived a text change (wrapped the new text at
  the old width, ratcheted the clock to two lines *on screen*). Any cache on
  a Node must be invalidated in the `mr_set_*` that changes its inputs.
- **Are properties compared before stored?** A rebuild re-applies every
  property of every widget. Without compare-before-store in `mr_set_*`,
  every node is touched every frame and the damage band is the whole screen
  — damage tracking measured exactly zero until this was fixed.
- **Is the band reset each commit?** It accumulates across detach/destroy on
  purpose, but pinning it (the first frame damages everything) made tracking
  a no-op the first time around.
- **Does damage match paint?** Paint clamps y to the band and does not clamp
  x — that asymmetry is why bands are full-width rows. Anything that clips x
  in paint without narrowing `mr_damage`'s contract (or vice versa) ships
  stale pixels to the panel.

## Measuring

- **fps claims come from `make fps` on hardware, nothing else.** Host paint
  is a different workload (no PSRAM, different FPU, different compiler). The
  wall-clock number includes VM rebuild, layout, paint, convert, and the QSPI
  transfer *landing* — `panel_present_argb` blocks on the semaphore, so
  enqueue time cannot masquerade as transfer time again.
- **Time the transfer, not the enqueue.** The QSPI push measured 2ms once;
  the transfer was 25.6ms. If a number looks free, it is probably being
  measured around the wrong thing.
- **Keep `delay()` out of means.** Wall-clock between presents once swept the
  program's own delay into the frame time. Phase timers live inside
  `uiCommit`/`panel.c` for this reason.
- **A measured zero improvement means instrument, don't ship.** Damage
  tracking measured zero twice; both times the mechanism was broken (touched
  avalanche, band never reset), not the idea. The per-primitive split
  (`MR_PROFILE`) exists so "paint is slow" becomes "rect is 115ms and rect
  should be 19ms".
- **Beware same-second rebuilds when A/B-ing.** Editing a source file within
  the same second as the previous build can leave make convinced nothing
  changed; the "comparison" then runs the old binary twice. Confirm the
  rebuild actually compiled something.

## Verifying

- **Pure optimizations are byte-identical.** Render a probe scene against the
  previous commit (a git worktree keeps it clean) and `cmp` the framebuffers.
  The arc span rewrite and the cover skip were both verified this way.
- **Precision changes are bounded, not eyeballed.** The integer blend was
  accepted because the diff was ≤2/255 per channel, on blended pixels only,
  with opaque-stays-opaque exact (alpha widened 0..256). "Looks the same" is not
  evidence; pixels get compared.
- **Check the probe actually exercises the path.** A probe scene placed a
  full-screen ring under a full-height sibling in a column — the ring
  laid out off-screen and a "verified" claim is hollow until a pixel
  count says arc > 0. Assert the path ran, then assert what it did.
- **`make render-test` budgets are part of the change.** They are pixel
  counts, deterministic everywhere. A legitimate cost increase moves a budget
  *in the same commit, with the reasoning* — never silently, and never by
  loosening a budget to make a regression fit.
