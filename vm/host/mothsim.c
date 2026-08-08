/* mothsim — runs a .mothb with a real display, in a desktop window.
 *
 * The VM, the renderer and the UI bindings are the same code the board runs;
 * only the panel differs. A Dart program's own loop drives everything, so the
 * host does its work from the frame hook inside uiCommit.
 */
#include "moth_render.h"
#include "moth_ui.h"
#include "moth_vm.h"
#include "push.h"

#include <SDL.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  int width, height;
  bool round;
  bool quit;

  /* Synthetic taps, so the click path can be exercised without a mouse —
   * the same reason mothrun can fake analog readings and I2C devices. */
  struct { int x, y, at_frame; } taps[8];
  int ntaps;
  long frame;
  long quit_after; /* 0 = run until the window closes */

  /* Hot push: a newly arrived program waiting to replace the running one. */
  moth_push *push;
  uint8_t *pending;
  size_t pending_len;
  moth_vm *vm;
} g_sim;

static void pump_input(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_QUIT:
        g_sim.quit = true;
        break;
      case SDL_MOUSEMOTION:
        mr_pointer(e.motion.x, e.motion.y, (e.motion.state & SDL_BUTTON_LMASK) != 0);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT) mr_pointer(e.button.x, e.button.y, true);
        break;
      case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) mr_pointer(e.button.x, e.button.y, false);
        break;
      default:
        break;
    }
  }
}

/* Called from uiCommit — the only moment the host gets control while the
 * Dart program's loop is running. */
static void on_frame(bool repainted, void *user) {
  (void)user;
  pump_input();

  /* A push asks the running program to stop; the display stays up, so the
   * replacement draws over a live screen rather than a blank one. */
  if (g_sim.push && !g_sim.pending) {
    size_t len = 0;
    uint8_t *blob = moth_push_poll(g_sim.push, &len);
    if (blob) {
      g_sim.pending = blob;
      g_sim.pending_len = len;
      printf("push: %zu bytes received, restarting\n", len);
      fflush(stdout);
      moth_request_halt(g_sim.vm);
    }
  }

  g_sim.frame++;
  for (int i = 0; i < g_sim.ntaps; i++) {
    if (g_sim.frame == g_sim.taps[i].at_frame) {
      mr_pointer(g_sim.taps[i].x, g_sim.taps[i].y, true);
    } else if (g_sim.frame == g_sim.taps[i].at_frame + 2) {
      mr_pointer(g_sim.taps[i].x, g_sim.taps[i].y, false);
    }
  }
  if (repainted) {
    SDL_UpdateTexture(g_sim.texture, NULL, mr_framebuffer(), g_sim.width * 4);
    SDL_RenderClear(g_sim.renderer);
    SDL_RenderCopy(g_sim.renderer, g_sim.texture, NULL, NULL);
    SDL_RenderPresent(g_sim.renderer);
  }
  if (g_sim.quit_after && g_sim.frame >= g_sim.quit_after) g_sim.quit = true;
  if (g_sim.quit) {
    /* Closing the window ends the program even mid-loop, the way pulling
     * power would on a board. */
    SDL_Quit();
    exit(0);
  }
}

/* ---- the small host natives a UI program still needs ------------------- */

static moth_value n_print(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  moth_value text = moth_to_string(vm, argv[0]);
  int len = 0;
  const char *chars = moth_string_chars(text, &len);
  if (chars) fwrite(chars, 1, (size_t)len, stdout);
  putchar('\n');
  fflush(stdout);
  return moth_null();
}

static moth_value n_delay(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) SDL_Delay((Uint32)argv[0].as.i);
  pump_input();
  if (g_sim.quit) { SDL_Quit(); exit(0); }
  return moth_null();
}

static moth_value n_millis(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_int(SDL_GetTicks());
}

static void register_host_natives(moth_vm *vm) {
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "millis", n_millis, NULL);
}

static const char *status_text(moth_status st) {
  switch (st) {
    case MOTH_OK: return "ok";
    case MOTH_ERR_FORMAT: return "bad program file";
    case MOTH_ERR_UNRESOLVED_NATIVE: return "missing built-in";
    case MOTH_ERR_TYPE: return "type error";
    case MOTH_ERR_DIV_ZERO: return "division by zero";
    case MOTH_ERR_STACK_OVERFLOW: return "stack overflow";
    case MOTH_ERR_OOM: return "out of memory";
    case MOTH_ERR_BAD_OP: return "corrupt program";
    case MOTH_HALTED: return "stopped";
  }
  return "error";
}

int main(int argc, char **argv) {
  const char *path = NULL;
  g_sim.width = 480;
  g_sim.height = 320;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      if (sscanf(argv[++i], "%dx%d", &g_sim.width, &g_sim.height) != 2) {
        fprintf(stderr, "mothsim: --size wants WIDTHxHEIGHT, e.g. 466x466\n");
        return 64;
      }
    } else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
      g_sim.push = moth_push_listen(atoi(argv[++i]));
      if (!g_sim.push) {
        fprintf(stderr, "mothsim: cannot listen on that port\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--round") == 0) {
      g_sim.round = true;
    } else if (strcmp(argv[i], "--tap") == 0 && i + 1 < argc) {
      if (g_sim.ntaps >= 8) { fprintf(stderr, "mothsim: at most 8 taps\n"); return 64; }
      int x, y;
      if (sscanf(argv[++i], "%d,%d", &x, &y) != 2) {
        fprintf(stderr, "mothsim: --tap wants X,Y\n");
        return 64;
      }
      g_sim.taps[g_sim.ntaps].x = x;
      g_sim.taps[g_sim.ntaps].y = y;
      g_sim.taps[g_sim.ntaps].at_frame = 20 + g_sim.ntaps * 20;
      g_sim.ntaps++;
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      g_sim.quit_after = atol(argv[++i]);
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "usage: mothsim <program.mothb> [--size WxH] [--round] [--listen PORT]\n"
              "                [--tap X,Y] [--frames N]\n");
      return 64;
    } else if (!path) {
      path = argv[i];
    } else {
      fprintf(stderr, "usage: mothsim <program.mothb> [--size WxH]\n");
      return 64;
    }
  }
  if (!path) {
    fprintf(stderr, "usage: mothsim <program.mothb> [--size WxH]\n");
    return 64;
  }

  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "mothsim: cannot open %s\n", path); return 66; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *blob = malloc((size_t)len);
  if (!blob || fread(blob, 1, (size_t)len, f) != (size_t)len) {
    fprintf(stderr, "mothsim: cannot read %s\n", path);
    return 66;
  }
  fclose(f);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "mothsim: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  g_sim.window = SDL_CreateWindow("moth", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  g_sim.width, g_sim.height, 0);
  g_sim.renderer = SDL_CreateRenderer(g_sim.window, -1, SDL_RENDERER_PRESENTVSYNC);
  g_sim.texture = SDL_CreateTexture(g_sim.renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, g_sim.width, g_sim.height);

  mr_config cfg = {g_sim.width, g_sim.height,
                   g_sim.round ? MR_SHAPE_ROUND : MR_SHAPE_RECT};
  if (!mr_init(&cfg)) {
    fprintf(stderr, "mothsim: renderer init failed\n");
    return 1;
  }

  moth_vm *vm = moth_new();
  register_host_natives(vm);
  moth_ui_register(vm);
  moth_ui_set_frame_hook(on_frame, NULL);

  uint8_t *current = blob;
  size_t current_len = (size_t)len;

  for (;;) {
    g_sim.vm = vm;
    moth_status st = moth_load(vm, current, current_len);
    if (st != MOTH_OK) {
      fprintf(stderr, "mothsim: %s: %s\n", status_text(st), moth_error(vm));
      if (!g_sim.pending) return 65;
    } else {
      st = moth_run(vm);
      if (st != MOTH_OK && st != MOTH_HALTED) {
        fprintf(stderr, "mothsim: %s: %s\n", status_text(st), moth_error(vm));
        if (!g_sim.pending) return 70;
      }
    }

    if (!g_sim.pending) break;

    /* Swap in the pushed program. The old program's nodes go with it —
     * otherwise the new UI draws on top of a tree it does not own. */
    mr_reset();
    moth_free(vm);
    free(current);
    current = g_sim.pending;
    current_len = g_sim.pending_len;
    g_sim.pending = NULL;

    vm = moth_new();
    register_host_natives(vm);
    moth_ui_register(vm);
    moth_ui_set_frame_hook(on_frame, NULL);
  }

  /* The program finished; hold the window so the result stays visible. */
  while (!g_sim.quit) {
    pump_input();
    SDL_Delay(16);
  }
  moth_free(vm);
  free(current);
  moth_push_close(g_sim.push);
  SDL_Quit();
  return 0;
}
