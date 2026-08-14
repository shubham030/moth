/* Binds the moth_render backend contract to the VM as native functions.
 *
 * The surface is deliberately flat and numeric — it is the boundary a
 * bytecode VM calls efficiently, not the API anyone writes by hand. The Dart
 * side wraps it in classes (ADR-009).
 */
#ifndef MOTH_UI_H
#define MOTH_UI_H

#include "moth_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers every ui* native and takes ownership of the render event sink
 * and queue. Call after mr_init and before moth_load, so a program needing
 * the display fails at load on a board without one. */
void moth_ui_register(moth_vm *vm);

/* Hands the loaded program's embedded images to the renderer. Call after
 * moth_load and after mr_reset on a swap — the renderer's registry died
 * with the old program (its pixels lived in the freed blob), and the new
 * program's images do not exist until this runs. */
void moth_ui_register_assets(moth_vm *vm);

/* Registers the ui* natives only, touching no shared state. For probe VMs:
 * verifying a pushed blob means loading it against the same native names,
 * and the full register on a throwaway VM was resetting the live program's
 * event queue — a tap delivered during verification simply vanished. */
void moth_ui_register_natives(moth_vm *vm);

/* Called from inside uiCommit, which is the only point the host gets control
 * while a Dart program's own loop is running: present the framebuffer when
 * `repainted`, and pump input every time so the window stays responsive. */
typedef void (*moth_ui_frame_fn)(bool repainted, void *user);
void moth_ui_set_frame_hook(moth_ui_frame_fn hook, void *user);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_UI_H */
