#include "mango/overview/overview.h"
#include "mango/animation/client.h"
#include "mango/common/server.h"
#include "mango/common/util.h"
#include "mango/draw/text-node.h"
#include "mango/layout/card.h"
#include "mango/layout/layout.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"
#include "mango/manage/tab.h"
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

// Returns 0 when the target window shares its tag with other windows.
uint32_t want_restore_fullscreen(Client *target_client) {
	Client *c = NULL;
	wl_list_for_each(c, &server.clients, link) {
		if (c && c != target_client && c->tags == target_client->tags &&
			c == server.selected_monitor->sel && c->mon &&
			c->mon->pertag->ltidxs[get_tags_first_tag_num(c->tags)]->id !=
				SCROLLER &&
			c->mon->pertag->ltidxs[get_tags_first_tag_num(c->tags)]->id !=
				VERTICAL_SCROLLER) {
			return 0;
		}
	}

	return 1;
}

void overview_update_jump_label(Client *c) {
	if (!c || !c->mon || !c->card.tree || !c->mon->isoverview ||
		!c->mon->is_jump_mode || !c->jump_char)
		return;

	struct wlr_scene_node *label = &c->jump_label_node->scene->node;
	int32_t lw = c->jump_label_node->logical_width;
	int32_t lh = c->jump_label_node->logical_height;
	if (lw <= 0 || lh <= 0) {
		if (label->enabled)
			wlr_scene_node_set_enabled(label, false);
		return;
	}

	struct wlr_box cur = c->animation.current;
	int32_t lx = cur.x + (cur.width - lw) / 2;
	int32_t ly = cur.y + (cur.height - lh) / 2;
	if (lx < c->mon->m.x || ly < c->mon->m.y ||
		lx + lw > c->mon->m.x + c->mon->m.width ||
		ly + lh > c->mon->m.y + c->mon->m.height) {
		if (label->enabled)
			wlr_scene_node_set_enabled(label, false);
		return;
	}

	wlr_scene_node_set_position(label, (cur.width - lw) / 2,
								(cur.height - lh) / 2);
	if (!label->enabled) {
		wlr_scene_node_set_enabled(label, true);
		wlr_scene_node_raise_to_top(label);
	}
}

// Entering overview: every tag window shows its card and must not be disabled
// by the subtree hiding logic.
void overview_backup_surface(Client *c) {
	if (c->card.tree)
		return;
	if (!client_surface(c) || !client_surface(c)->mapped)
		return;

	c->is_clip_to_hide = false;
	client_update_visibility(c);

	card_create(c);

	// The card tree is created at the scene top; enabled jump labels are raised
	// above the cards.
	if (c->jump_label_node->scene->node.enabled)
		wlr_scene_node_raise_to_top(&c->jump_label_node->scene->node);
	overview_update_jump_label(c);
}
// Saves the window old state when switching from the normal view to overview.
void overview_backup(Client *c) {
	c->overview_isfloatingbak = c->isfloating;
	c->overview_isfullscreenbak = c->isfullscreen;
	c->overview_ismaximizescreenbak = c->ismaximizescreen;
	c->overview_isfullscreenbak = c->isfullscreen;
	c->animation.tagining = false;
	c->animation.tagouted = false;
	c->animation.tagouting = false;
	c->overview_backup_geom = c->geom;
	c->overview_backup_bw = c->bw;
	c->overview_member = true;
	if (c->isfloating) {
		c->isfloating = 0;
	}

	overview_backup_surface(c);

	if (c->isfullscreen || c->ismaximizescreen) {
		client_pending_fullscreen_state(
			c, 0); // Clears the window fullscreen flag.
		client_pending_maximized_state(c, 0);
	}
	c->bw = c->no_border ? 0 : config.borderpx;

	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT |
							WLR_EDGE_RIGHT);
}
// Restores window state when switching back from overview to the normal view.
void overview_restore(Client *c, const Arg *arg) {
	// A card tree alone may be a stage card with nothing backed up.
	if (!c->overview_member)
		return;

	c->isfloating = c->overview_isfloatingbak;
	c->isfullscreen = c->overview_isfullscreenbak;
	c->ismaximizescreen = c->overview_ismaximizescreenbak;
	c->overview_isfloatingbak = 0;
	c->overview_isfullscreenbak = 0;
	c->overview_ismaximizescreenbak = 0;
	c->geom = c->overview_backup_geom;
	c->bw = c->overview_backup_bw;
	c->animation.tagining = false;
	c->is_restoring_from_ov = (arg->ui & c->tags & TAGMASK) == 0 ? true : false;

	// Staged windows keep their card: the stage draws them as cards too, and a
	// rebuild would flash the live surface.
	if (!c->isstaged)
		card_destroy(c);

	if (c->isfloating) {
		// XRaiseWindow(display, c->win); // Raise the floating window to the
		// top
		resize(c, c->overview_backup_geom, (ResizeOpts){.interact = 0});
	} else if (c->isfullscreen || c->ismaximizescreen) {
		if (want_restore_fullscreen(c) && c->ismaximizescreen) {
			client_set_maximize_screen(c, 1, false);
		} else if (want_restore_fullscreen(c) && c->isfullscreen) {
			client_apply_fullscreen(c, 1, false);
		} else {
			client_pending_fullscreen_state(c, 0);
			client_pending_maximized_state(c, 0);
			client_apply_fullscreen(c, false, false);
		}
	} else {
		if (c->is_restoring_from_ov) {
			c->is_restoring_from_ov = false;
			resize(c, c->overview_backup_geom, (ResizeOpts){.interact = 0});
		}
	}

	if (c->bw == 0 && !c->isfullscreen) { // Windows created while in overview
										  // mode have no bw record.
		c->bw = c->no_border ? 0 : config.borderpx;
	}

	if (c->isfloating && !c->force_tiled_state) {
		client_set_tiled(c, WLR_EDGE_NONE);
	}

	c->overview_member = false;
}

// Overview state belongs to one monitor: hand it back before the client
// leaves, and back up again if the new monitor is in overview too.
void overview_change_mon(Client *c, Monitor *m) {
	if (!c->overview_member || !c->mon || !m || m == c->mon)
		return;
	overview_restore(c, &(Arg){.ui = c->tags});
	if (m->isoverview)
		overview_backup(c);
}
