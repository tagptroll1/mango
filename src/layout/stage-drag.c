#include "mango/animation/client.h"
#include "mango/animation/common.h"
#include "mango/common/server.h"
#include "mango/common/util.h"
#include "mango/layout/arrange.h"
#include "mango/layout/layout.h"
#include "mango/layout/stage.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"
#include "stage-internal.h"
#include <math.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>

// Pointer interaction with the stage: one move, shake and divider resize at a
// time, since one pointer grab spans every monitor. Its state is released
// through stage_drag_forget when a client goes away.

// A dock preview sliding along its edge reports at most this often, since each
// report is a full client list.
#define STAGE_PREVIEW_MOVE_MS 16

static struct {
	Client *c;
	double ext;
	int32_t dir;
	uint32_t flips[STAGE_SHAKE_FLIPS_MAX];
	int32_t nflips;
	bool fired;
} shake;

// Drag state that must outlive the begin-move call.
static struct {
	Client *c;
	bool was_staged;
	struct wlr_box origin;
	// Grab point as a share of the box, kept unrounded so it cannot drift.
	bool has_grab;
	double grab_rx, grab_ry;
	uint32_t preview_sent;
	// Pickup eases from origin to the drag box instead of snapping.
	bool morphing;
	uint32_t morph_start;
} drag;

// One drop hint for the whole center: the drop can land outside the tile under
// the cursor, where that tile's own drop area would sit under its neighbours.
static struct {
	struct wlr_scene_rect *rect;
	Client *owner;
} hint;

// Divider shares when a resize began.
static struct {
	float x, y;
} resize_origin;

static void stage_hint_hide(Client *c) {
	if (hint.owner != c)
		return;
	hint.owner = NULL;
	wlr_scene_node_set_enabled(&hint.rect->node, false);
}

void stage_drag_forget(Client *c) {
	stage_hint_hide(c);
	if (drag.c == c)
		drag.c = NULL;
	if (shake.c == c)
		shake.c = NULL;
}

static void stage_preview_clear(Client *c) {
	if (!c->stage_previewing)
		return;
	c->stage_previewing = false;
	c->stage_preview_mon = NULL;
	stage_notify(c);
}

void stage_monitor_close(Monitor *m) {
	Client *c;
	wl_list_for_each(c, &server.clients, link) {
		if (c->stage_preview_mon == m)
			stage_preview_clear(c);
	}
}

// Tracks the dock a release at b would make (none when m is NULL) and reports
// changes, so the shell shows which widget the drop will become.
static void stage_preview_dock(Client *c, Monitor *m, struct wlr_box b,
							   uint32_t time) {
	int32_t tier = STAGE_DOCK_FULL;
	int32_t edge = -1;
	if (m) {
		struct stage_frame f = stage_frame_of(m);
		edge = stage_dock_edge_at(&f, b, &tier);
	}
	if (edge < 0) {
		stage_preview_clear(c);
		return;
	}
	int32_t y = b.y + b.height / 2 - m->m.y;
	bool moved_only = c->stage_previewing && c->stage_preview_mon == m &&
					  c->stage_preview_edge == edge &&
					  c->stage_preview_tier == tier;
	if (moved_only && (y == c->stage_preview_y ||
					   time - drag.preview_sent < STAGE_PREVIEW_MOVE_MS))
		return;
	c->stage_previewing = true;
	c->stage_preview_mon = m;
	c->stage_preview_edge = edge;
	c->stage_preview_tier = tier;
	c->stage_preview_y = y;
	drag.preview_sent = time;
	stage_notify(c);
}

Client *stage_tile_at_cursor(Monitor *m, Client *skip) {
	Client *c;
	struct stage_frame f = stage_frame_of(m);
	if (!stage_in_center(&f, server.cursor->x, server.cursor->y))
		return NULL;
	wl_list_for_each(c, &server.clients, link) {
		if (c != skip && VISIBLEON(c, m) && ISTILED(c) &&
			wlr_box_contains_point(&c->geom, server.cursor->x,
								   server.cursor->y))
			return c;
	}
	return NULL;
}

// Moves the dividers the tile borders, each following the pointer (or the
// keyboard offset), like the master/stack split in tile.
void stage_resize_tile(Client *c, bool isdrag, int32_t offsetx, int32_t offsety,
					   uint32_t time) {
	Monitor *m = c->mon;
	uint32_t tag = get_mon_curtag(m);
	float *sx = &m->pertag->stage_split_x[tag];
	float *sy = &m->pertag->stage_split_y[tag];

	if (isdrag && !server.start_drag_window) {
		server.start_drag_window = true;
		server.drag_begin_cursor_x = server.cursor->x;
		server.drag_begin_cursor_y = server.cursor->y;
		resize_origin.x = stage_split(*sx);
		resize_origin.y = stage_split(*sy);
		c->drag_begin_geom = c->geom;
		return;
	}
	if (isdrag) {
		offsetx = server.cursor->x - server.drag_begin_cursor_x;
		offsety = server.cursor->y - server.drag_begin_cursor_y;
	} else {
		resize_origin.x = stage_split(*sx);
		resize_origin.y = stage_split(*sy);
		c->drag_begin_geom = c->geom;
	}

	struct stage_frame f = stage_frame_of(m);
	struct wlr_box in = stage_tile_area(&f);
	struct wlr_box g = c->drag_begin_geom;
	int32_t gi = f.gap_in;
	if (g.x > in.x + 1 || g.x + g.width < in.x + in.width - 1)
		*sx = fmaxf(0.1f, fminf(0.9f, resize_origin.x +
										  (float)offsetx / (in.width - gi)));
	if (g.y > in.y + 1 || g.y + g.height < in.y + in.height - 1)
		*sy = fmaxf(0.1f, fminf(0.9f, resize_origin.y +
										  (float)offsety / (in.height - gi)));

	if (!isdrag) {
		arrange(m, false, false);
		return;
	}
	if (server.last_apply_drag_time == 0 ||
		time - server.last_apply_drag_time >
			config.drag_tile_refresh_interval) {
		arrange(m, false, false);
		server.last_apply_drag_time = time;
	}
}

static void shake_reset(Client *c) {
	shake.c = c;
	shake.ext = server.cursor->x;
	shake.dir = 0;
	shake.nflips = 0;
	shake.fired = false;
}

// Shaking the dragged window pushes everything else out to tidy side columns.
static void stage_shake(Monitor *m, Client *grabbed) {
	Client *c;
	wl_list_for_each(c, &server.clients, link) {
		if (c != grabbed && VISIBLEON(c, m) && ISTILED(c))
			stage_stash(c, c->geom);
	}
	stage_align_sides(m, grabbed);
	arrange(m, false, false);
}

static void stage_track_shake(Client *c, uint32_t time) {
	double x = server.cursor->x;
	if (shake.c != c)
		shake_reset(c);
	if (shake.fired || time == 0)
		return;

	if (shake.dir == 0) {
		if (fabs(x - shake.ext) >= config.stage_shake_travel) {
			shake.dir = x > shake.ext ? 1 : -1;
			shake.ext = x;
		}
		return;
	}
	if ((x - shake.ext) * shake.dir > 0) {
		shake.ext = x;
		return;
	}
	if ((shake.ext - x) * shake.dir < config.stage_shake_travel)
		return;

	// The last n flips sit at the front of the array, oldest first.
	int32_t n = config.stage_shake_flips;
	shake.dir = -shake.dir;
	shake.ext = x;
	memmove(shake.flips, shake.flips + 1, sizeof(shake.flips[0]) * (n - 1));
	shake.flips[n - 1] = time;
	shake.nflips = MANGO_MIN(shake.nflips + 1, n);

	if (shake.nflips == n &&
		time - shake.flips[0] <= config.stage_shake_window_ms) {
		Monitor *m = monitor_at_point(server.cursor->x, server.cursor->y);
		if (is_stage_layout(m)) {
			shake.fired = true;
			stage_shake(m, c);
		}
	}
}

// Gives c the drag card's logical size: a square a quarter of the center.
static void stage_drag_size(Client *c, const struct stage_frame *f) {
	c->stage_lw = c->stage_lh = stage_drag_logical(f, (int32_t)c->bw);
}

bool stage_begin_move(Client *c, double x, double y) {
	// Recorded for every move: a tile from another monitor may land here.
	drag.c = c;
	drag.was_staged = c->isstaged;
	drag.origin = c->geom;
	drag.has_grab = false;
	drag.morphing = false;

	if (!is_stage_layout(c->mon))
		return false;
	if (!c->isstaged && !ISTILED(c))
		return false;

	shake_reset(c);

	struct stage_frame f = stage_frame_of(c->mon);
	double rx = (x - c->geom.x) / c->geom.width;
	double ry = (y - c->geom.y) / c->geom.height;
	if (!c->isstaged)
		stage_stash(c, c->geom);
	stage_drag_size(c, &f);
	struct stage_card k = stage_card_of(c);
	float s = stage_scale_at(&f, k, x, rx);
	struct wlr_box box = stage_box_at(k, 0, 0, s);
	box.x = (int32_t)round(x - rx * box.width);
	box.y = (int32_t)round(y - ry * box.height);
	drag.morphing = config.animations && config.animation_duration_move > 0;
	drag.morph_start = get_now_in_ms();
	stage_stash(c, box);
	arrange(c->mon, false, false);
	// Lets the regular drag code draw drop hints over center tiles.
	c->drag_to_tile = true;
	return true;
}

bool stage_drag_morph(Client *c) {
	if (!drag.morphing || drag.c != c || c != server.grab_client)
		return false;
	double p = (double)(get_now_in_ms() - drag.morph_start) /
			   config.animation_duration_move;
	struct wlr_box g = c->geom, box = g;
	if (p >= 1.0) {
		drag.morphing = false;
	} else {
		double f = find_animation_curve_at(p, MOVE);
		struct wlr_box o = drag.origin;
		box.width = (int32_t)round(o.width + (g.width - o.width) * f);
		box.height = (int32_t)round(o.height + (g.height - o.height) * f);
		// Scaled about the grab point, so the cursor stays on the same spot.
		double rx = g.width > 0 ? (double)server.grab_offset_x / g.width : 0.5;
		double ry =
			g.height > 0 ? (double)server.grab_offset_y / g.height : 0.5;
		box.x = (int32_t)round(g.x + rx * (g.width - box.width));
		box.y = (int32_t)round(g.y + ry * (g.height - box.height));
	}
	c->animation.current = box;
	wlr_scene_node_set_position(&c->scene->node, box.x, box.y);
	return true;
}

void stage_drag_motion(Client *c, uint32_t time) {
	Monitor *m = monitor_at_point(server.cursor->x, server.cursor->y);
	if (!is_stage_layout(m)) {
		stage_preview_dock(c, NULL, c->float_geom, time);
		return;
	}
	if (!c->isstaged) {
		// Same rule as the drop: only tiles dock unstaged.
		bool docks = drag.c == c && c->drag_to_tile;
		stage_preview_dock(c, docks ? m : NULL, c->float_geom, time);
		return;
	}

	stage_track_shake(c, time);

	if (!drag.has_grab) {
		struct wlr_box old = c->geom;
		drag.has_grab = true;
		drag.grab_rx =
			old.width > 0 ? (double)server.grab_offset_x / old.width : 0.5;
		drag.grab_ry =
			old.height > 0 ? (double)server.grab_offset_y / old.height : 0.5;
	}
	// Scale changes only the box on screen, so the client never re-renders.
	struct stage_frame f = stage_frame_of(m);
	struct stage_card k = stage_card_of(c);
	float s = stage_scale_at(&f, k, server.cursor->x, drag.grab_rx);
	c->stage_scale = s;
	struct wlr_box box = stage_box_at(k, 0, 0, s);
	server.grab_offset_x = (int32_t)round(drag.grab_rx * box.width);
	server.grab_offset_y = (int32_t)round(drag.grab_ry * box.height);
	box.x = (int32_t)round(server.cursor->x) - server.grab_offset_x;
	box.y = (int32_t)round(server.cursor->y) - server.grab_offset_y;
	stage_clamp(&f, &box);
	c->float_geom = box;
	stage_preview_dock(c, m, box, time);
}

// Center box g gets when dropped next to target: the same list insert and swap
// as stage_drop and the plain drag-tile drop. False when it would be stashed.
static bool stage_drop_box(Monitor *m, Client *target, Client *g, bool after,
						   struct wlr_box *box) {
	Client *o;
	int32_t n = 0, t = -1;
	wl_list_for_each(o, &server.clients, link) {
		if (o == g || !VISIBLEON(o, m) || !ISTILED(o))
			continue;
		if (o == target)
			t = n;
		n++;
	}
	if (t < 0)
		return false;
	int32_t at = after ? t + 1 : t, total = n + 1;
	// A staged card swaps with a full center; a tile from another monitor
	// pushes the last one out instead.
	if (g->isstaged && n >= STAGE_CENTER_MAX) {
		at = t;
		total = n;
	}
	if (at >= STAGE_CENTER_MAX)
		return false;
	struct stage_frame f = stage_frame_of(m);
	struct wlr_box boxes[STAGE_CENTER_MAX];
	stage_tile_boxes(&f, MANGO_MIN(total, STAGE_CENTER_MAX), boxes);
	*box = boxes[at];
	return true;
}

bool stage_set_drop_area(Client *c) {
	Client *g = server.grab_client;
	bool ours = is_stage_layout(c->mon);
	// Hidden even when the layout changed under the drag.
	if (!ours || !c->enable_drop_area_draw || !g) {
		stage_hint_hide(c);
		return ours;
	}

	// Nearest edge, as other layouts pick it; only before or after matters.
	double x = server.cursor->x - c->geom.x;
	double y = server.cursor->y - c->geom.y;
	double dl = x, dr = c->geom.width - x;
	double dt = y, db = c->geom.height - y;
	if (dl <= dr && dl <= dt && dl <= db)
		c->drop_direction = LEFT;
	else if (dr <= dt && dr <= db)
		c->drop_direction = RIGHT;
	else if (dt <= db)
		c->drop_direction = UP;
	else
		c->drop_direction = DOWN;
	bool after = c->drop_direction == RIGHT || c->drop_direction == DOWN;

	struct wlr_box box;
	if (!stage_drop_box(c->mon, c, g, after, &box)) {
		stage_hint_hide(c);
		return true;
	}
	if (!hint.rect)
		hint.rect = wlr_scene_rect_create(server.layers[LyrTile], 0, 0,
										  config.dropcolor);
	hint.owner = c;
	wlr_scene_rect_set_color(hint.rect, config.dropcolor);
	wlr_scene_rect_set_size(hint.rect, box.width, box.height);
	wlr_scene_node_set_position(&hint.rect->node, box.x, box.y);
	wlr_scene_node_raise_to_top(&hint.rect->node);
	wlr_scene_node_set_enabled(&hint.rect->node, true);
	return true;
}

bool stage_drop(Client *c) {
	Monitor *m = c->mon;
	bool ours = drag.c == c;
	drag.c = NULL;
	shake.c = NULL;
	stage_preview_clear(c);

	if (!is_stage_layout(m)) {
		if (!c->isstaged)
			return false;
		// Dropped on a monitor without the stage: join its tiling.
		stage_unstash(c);
		c->drag_to_tile = true;
		arrange(m, false, false);
		return false;
	}
	struct stage_frame f = stage_frame_of(m);
	int32_t tier;
	int32_t edge = stage_dock_edge_at(&f, c->geom, &tier);

	if (!c->isstaged) {
		// A tile dragged in from another monitor stashes where it lands; plain
		// floating windows stay out of the stage, docking just got in the way.
		bool from_tile = ours && c->drag_to_tile;
		if (!from_tile)
			return false;
		if (from_tile && edge < 0 &&
			stage_in_center(&f, server.cursor->x, server.cursor->y))
			return false;

		c->drag_to_tile = false;
		stage_stash(c, c->geom);
		if (from_tile)
			stage_drag_size(c, &f);
		struct stage_card k = stage_card_of(c);
		stage_stash(c,
					stage_box_at(k, server.cursor->x, server.cursor->y,
								 stage_scale_at(&f, k, server.cursor->x, 0.5)));
		if (edge >= 0)
			stage_dock_client(c, edge, tier);
		else
			arrange(m, false, false);
		return true;
	}

	c->drag_to_tile = false;
	if (edge >= 0) {
		stage_dock_client(c, edge, tier);
		return true;
	}
	if (!stage_in_center(&f, server.cursor->x, server.cursor->y)) {
		arrange(m, false, false);
		return true;
	}

	Client *target = stage_tile_at_cursor(m, c);
	int32_t n = stage_center_count(m, c);

	if (n >= STAGE_CENTER_MAX && !target) {
		// Full center and no tile under the cursor (a gap): back to the side.
		if (ours && drag.was_staged)
			stage_stash(c, drag.origin);
		else
			stage_stash_free(m, c);
		arrange(m, false, false);
		return true;
	}

	stage_unstash(c);
	if (target) {
		if (target->drop_direction == RIGHT || target->drop_direction == DOWN)
			wl_list_safe_reinsert_next(&target->link, &c->link);
		else
			wl_list_safe_reinsert_prev(&target->link, &c->link);
	}

	if (n >= STAGE_CENTER_MAX) {
		// Swap: the tile it landed on takes the dropped window's old spot.
		if (ours && drag.was_staged) {
			// Stashed in place first: that sets its logical size from what
			// it shows now, which the box below is solved for.
			stage_stash(target, target->geom);
			stage_stash(target, stage_box_beside(
									&f, stage_card_of(target),
									drag.origin.x + drag.origin.width / 2.0,
									drag.origin.y + drag.origin.height / 2.0));
		} else {
			stage_stash_free(m, target);
		}
	}

	arrange(m, false, false);
	return true;
}
