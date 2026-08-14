/* The image node at the contract: register pixels, build a node, commit,
 * and read the framebuffer. Pins the blit (exact pixels at 1:1), alpha
 * blending, nearest-neighbour scaling, intrinsic sizing, the
 * missing-asset behavior (nothing painted, layout placeholder), and the
 * lifetime rule that matters most — mr_reset clears the registry, because
 * a program swap frees the blob the pixels live in. */
#include "moth_render.h"

#include <cstdio>
#include <cstring>

static int failures;

static void check(bool ok, const char *what) {
  if (ok) return;
  fprintf(stderr, "FAIL: %s\n", what);
  failures++;
}

static uint32_t pixel(int x, int y) { return mr_framebuffer()[y * 466 + x]; }

int main(void) {
  mr_config cfg = {466, 466, MR_SHAPE_RECT};
  mr_init(&cfg);
  /* The root's default cross-axis STRETCH would inflate an auto-width
   * image to the full display and hide the intrinsic-size behavior. */
  mr_set_u32(mr_root(), MR_PROP_CROSS_ALIGN, MR_ALIGN_START);

  /* A 2x2 asset: opaque red, opaque green / 50% blue, transparent. */
  static const uint32_t px[4] = {0xFFFF0000u, 0xFF00FF00u, 0x800000FFu,
                                 0x00000000u};
  mr_asset_set("tiny", 4, 2, 2, px);

  /* Black backdrop, image at the origin at intrinsic size. */
  mr_node_id img = mr_node_create(MR_NODE_IMAGE);
  mr_set_str(img, MR_PROP_IMAGE_SRC, "tiny");
  mr_attach(mr_root(), img, -1);
  mr_commit();

  float w = 0, h = 0, x = 0, y = 0;
  mr_frame_of(img, &x, &y, &w, &h);
  check(w == 2.0f && h == 2.0f, "intrinsic size is the asset's pixels");

  check(pixel(0, 0) == 0xFFFF0000u, "opaque pixel lands exactly");
  check(pixel(1, 0) == 0xFF00FF00u, "second opaque pixel lands exactly");
  /* 50% blue over black: roughly half-intensity blue, nothing else. */
  uint32_t b = pixel(0, 1);
  check((b & 0x00FFFF00u) == 0 && ((b & 0xFF) > 100 && (b & 0xFF) < 160),
        "half-alpha pixel blends toward the backdrop");
  check(pixel(1, 1) == 0xFF000000u, "transparent pixel leaves the backdrop");

  /* Explicit size scales nearest-neighbour: 2x2 stretched to 8x8 puts the
   * red source pixel across the whole top-left quadrant. */
  mr_set_f32(img, MR_PROP_WIDTH, 8);
  mr_set_f32(img, MR_PROP_HEIGHT, 8);
  mr_commit();
  check(pixel(1, 1) == 0xFFFF0000u && pixel(3, 3) == 0xFFFF0000u,
        "scaling repeats source pixels, no filtering");
  check(pixel(5, 1) == 0xFF00FF00u, "right quadrant is the green source");

  /* A key nobody registered: a placeholder box in layout, no paint. */
  mr_node_id ghost = mr_node_create(MR_NODE_IMAGE);
  mr_set_str(ghost, MR_PROP_IMAGE_SRC, "nope");
  mr_attach(mr_root(), ghost, -1);
  mr_commit();
  mr_frame_of(ghost, &x, &y, &w, &h);
  check(w == 32.0f && h == 32.0f, "unknown asset gets the placeholder size");
  check(pixel(2, (int)y + 2) == 0xFF000000u, "unknown asset paints nothing");

  /* THE lifetime rule: reset clears the registry. If this fails, a hot
   * push leaves image nodes blitting from the freed old blob. */
  mr_reset();
  mr_set_u32(mr_root(), MR_PROP_CROSS_ALIGN, MR_ALIGN_START);
  mr_node_id after = mr_node_create(MR_NODE_IMAGE);
  mr_set_str(after, MR_PROP_IMAGE_SRC, "tiny");
  mr_attach(mr_root(), after, -1);
  mr_commit();
  mr_frame_of(after, &x, &y, &w, &h);
  check(w == 32.0f && h == 32.0f,
        "after reset the old registration is gone (placeholder size)");
  check(pixel(0, 0) == 0xFF000000u, "after reset nothing blits");

  if (failures) {
    fprintf(stderr, "%d image contract failure(s)\n", failures);
    return 1;
  }
  printf("image: assets blit, blend, scale, and die with the program\n");
  return 0;
}
