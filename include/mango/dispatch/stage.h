#ifndef __DISPATCH_STAGE_H__
#define __DISPATCH_STAGE_H__ 1

#include "mango/common/types.h"
#include <stdint.h>

// Stage dispatches. Each reports its effect in ipc_dispatch_result; the return
// value is the bind convention (0 keeps matching binds), not success.

// arg->i is LEFT, RIGHT, UNDIR for the nearer edge, or -1 for an unknown edge
// name; the target is arg->tc, else the focused window.
int32_t stage_dock(const Arg *arg);
int32_t stage_undock(const Arg *arg);
// Between a stashed window's card and a dock on the nearer edge.
int32_t stage_dock_toggle(const Arg *arg);
// arg->tc's dock to arg->i along its edge, for the shell's tab drag.
int32_t stage_dock_move(const Arg *arg);
int32_t stage_flip_toggle(const Arg *arg);

#endif
