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

/* Registers every ui* native. Call after mr_init and before moth_load, so a
 * program needing the display fails at load on a board without one. */
void moth_ui_register(moth_vm *vm);

/* Called from inside uiCommit, which is the only point the host gets control
 * while a Dart program's own loop is running: present the framebuffer when
 * `repainted`, and pump input every time so the window stays responsive. */
typedef void (*moth_ui_frame_fn)(bool repainted, void *user);
void moth_ui_set_frame_hook(moth_ui_frame_fn hook, void *user);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_UI_H */
