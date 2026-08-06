/* Desktop harness: drives the contract from plain C, displays via SDL2.
 * This is the moth_render dev loop — no Dart, no VM, no hardware.
 */
#include "moth_render.h"

#include <SDL.h>
#include <stdio.h>

#define W 480
#define H 320

static void on_event(const mr_event *ev, void *user) {
  (void)user;
  printf("event: node=%u kind=%d value=%.1f at (%.0f,%.0f)\n",
         ev->node, ev->kind, ev->value, ev->x, ev->y);
}

/* A demo scene exercising column/row, gap, padding, grow, and animation. */
static void build_scene(void) {
  mr_node_id root = mr_root();
  mr_set_u32(root, MR_PROP_BG_COLOR, 0xFF1A1B26); /* tokyo night, obviously */
  mr_set_f32(root, MR_PROP_PADDING, 16);
  mr_set_f32(root, MR_PROP_GAP, 12);

  mr_node_id title = mr_node_create(MR_NODE_LABEL);
  mr_set_str(title, MR_PROP_TEXT, "moth_render harness");
  mr_set_f32(title, MR_PROP_FONT_SIZE, 20);
  mr_set_u32(title, MR_PROP_TEXT_COLOR, 0xFFC0CAF5);
  mr_attach(root, title, -1);

  mr_node_id row = mr_node_create(MR_NODE_BOX);
  mr_set_u32(row, MR_PROP_FLEX_DIRECTION, MR_ROW);
  mr_set_f32(row, MR_PROP_GAP, 12);
  mr_set_f32(row, MR_PROP_FLEX_GROW, 1);
  mr_attach(root, row, -1);

  uint32_t colors[] = {0xFF7AA2F7, 0xFFBB9AF7, 0xFF9ECE6A};
  for (int i = 0; i < 3; i++) {
    mr_node_id card = mr_node_create(MR_NODE_BOX);
    mr_set_u32(card, MR_PROP_BG_COLOR, colors[i]);
    mr_set_f32(card, MR_PROP_FLEX_GROW, (float)(i + 1));
    mr_set_f32(card, MR_PROP_RADIUS, 8); /* visible once ThorVG lands */
    mr_attach(row, card, -1);
  }

  mr_node_id pulse = mr_node_create(MR_NODE_BOX);
  mr_set_u32(pulse, MR_PROP_BG_COLOR, 0xFFF7768E);
  mr_set_f32(pulse, MR_PROP_HEIGHT, 24);
  mr_attach(root, pulse, -1);
  mr_anim_start(pulse, MR_PROP_OPACITY, 1.0f, 0.15f, 900, MR_EASE_IN_OUT);
}

int main(void) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *win = SDL_CreateWindow("moth_render", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, W, H, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, W, H);

  mr_config cfg = {W, H};
  if (!mr_init(&cfg)) return 1;
  mr_set_event_sink(on_event, NULL);
  build_scene();

  uint32_t prev = SDL_GetTicks();
  int running = 1;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = 0;
      else if (e.type == SDL_MOUSEMOTION)
        mr_pointer(e.motion.x, e.motion.y, (e.motion.state & SDL_BUTTON_LMASK) != 0);
      else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        mr_pointer(e.button.x, e.button.y, true);
      else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        mr_pointer(e.button.x, e.button.y, false);
    }

    uint32_t now = SDL_GetTicks();
    mr_tick(now - prev);
    prev = now;
    if (mr_commit())
      SDL_UpdateTexture(tex, NULL, mr_framebuffer(), W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
  }

  mr_shutdown();
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
