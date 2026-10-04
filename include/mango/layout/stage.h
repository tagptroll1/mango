#ifndef __LAYOUT_STAGE_H__
#define __LAYOUT_STAGE_H__ 1

#include "mango/common/types.h"
#include <stdbool.h>
#include <stdint.h>
#include <wlr/util/box.h>

// Stage layout: a square in the middle of the monitor tiles up to four windows;
// everything else is stashed on the sides as live, scaled-down cards that
// shrink the further out they sit.

// Bounds stage_shake_flips; the shake history is a fixed array.
#define STAGE_SHAKE_FLIPS_MAX 8

enum { STAGE_DOCK_LEFT, STAGE_DOCK_RIGHT };
enum { STAGE_DOCK_MINI, STAGE_DOCK_FULL };
// Dock edge asking for whichever side the window is nearer.
#define STAGE_DOCK_NEAREST -1

// What a stage operation did; bind and IPC adapters translate it.
enum stage_result {
	STAGE_CHANGED,
	STAGE_UNCHANGED,
	STAGE_NO_CLIENT,
	STAGE_NOT_STAGED,
	STAGE_NOT_DOCKED,
};

void stage(Monitor *m);
bool is_stage_layout(Monitor *m);

// Size the client is configured to. Staged clients keep their logical size
// while their box on screen is scaled.
void stage_content_size(Client *c, int32_t *w, int32_t *h);
// The client picked its own size (min size, X11 configure request).
void stage_adopt_size(Client *c, int32_t w, int32_t h);
// Folds box changes that were not scale changes into the logical size.
void stage_sync_logical(Client *c);

// Unstashes one window without rearranging (fullscreen, maximize).
void stage_unstash(Client *c);
// The client is unmapping; drops every stage reference to it.
void stage_forget(Client *c);
// Returns stashed windows to tiling when the monitor leaves the stage layout.
void stage_release_all(Monitor *m);
// Drops dock previews aimed at a monitor that is going away.
void stage_monitor_close(Monitor *m);

// Docks a stashed window on edge (or STAGE_DOCK_NEAREST), or moves its dock
// there. Already docked with no edge asked for counts as unchanged.
enum stage_result stage_dock_window(Client *c, int32_t edge);
// Back to a card beside its edge; watchers get the final box.
enum stage_result stage_undock_window(Client *c);
// Moves a dock along its edge; y is from the monitor top, clamped to it.
enum stage_result stage_move_dock(Client *c, int32_t y);
// Center tiles side by side or stacked.
void stage_flip(Monitor *m);

// Docked windows stay hidden outside overview; set_arrange_hidden hook.
bool stage_arrange_hidden(Monitor *m, Client *c);
// Lays docked cards out in strips on the edges of area and returns what is
// left between them for the other overview cards.
struct wlr_box stage_overview_docks(Monitor *m, struct wlr_box area);

// Divider moves from pointer and keyboard resizes.
void stage_resize_tile(Client *c, bool isdrag, int32_t offsetx, int32_t offsety,
					   uint32_t time);

// Pointer move hooks.
bool stage_begin_move(Client *c, double x, double y);
void stage_drag_motion(Client *c, uint32_t time);
// Places the dragged card on its pickup ease toward the drag box; true while
// that frame needs drawing.
bool stage_drag_morph(Client *c);
bool stage_drop(Client *c);
Client *stage_tile_at_cursor(Monitor *m, Client *skip);
// Drop hint on target c: the box the dragged window will take, which is not
// always inside c. False leaves the hint to the regular drop area.
bool stage_set_drop_area(Client *c);

#endif
