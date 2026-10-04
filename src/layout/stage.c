#include "mango/layout/stage.h"
#include "mango/animation/client.h"
#include "mango/common/server.h"
#include "mango/common/util.h"
#include "mango/ipc/ipc.h"
#include "mango/layout/arrange.h"
#include "mango/layout/card.h"
#include "mango/layout/layout.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"
#include "stage-internal.h"
#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

// Layout pass and stage state transitions. Geometry is in stage-geometry.c,
// pointer interaction in stage-drag.c.

bool is_stage_layout(Monitor *m) {
	return m && !m->isoverview &&
		   m->pertag->ltidxs[get_mon_curtag(m)]->id == STAGE;
}

struct stage_frame stage_frame_of(Monitor *m) {
	bool gaps = server.enable_gaps;
	uint32_t tag = get_mon_curtag(m);
	return (struct stage_frame){
		.mon = m->m,
		.usable = m->w,
		.gap_in = gaps ? m->gappih : 0,
		.gap_out_h = gaps ? m->gappoh : 0,
		.gap_out_v = gaps ? m->gappov : 0,
		.split_x = m->pertag->stage_split_x[tag],
		.split_y = m->pertag->stage_split_y[tag],
		.flip = m->pertag->stage_flip[tag],
		.borderpx = (int32_t)config.borderpx,
		.scale_max = config.stage_scale_max,
		.scale_min = config.stage_scale_min,
		.scale_curve = config.stage_scale_curve,
		.shrink_zone = config.stage_shrink_zone,
		.dock_tiny = config.stage_dock_tiny,
		.dock_zone = config.stage_dock_zone,
		.dock_mini_zone = config.stage_dock_mini_zone,
		.overview_dock_ratio = config.stage_overview_dock_ratio,
		.overview_gap_in = config.overviewgappi,
		.overview_gap_out = config.overviewgappo,
	};
}

struct stage_card stage_card_of(Client *c) {
	return (struct stage_card){c->stage_lw, c->stage_lh, (int32_t)c->bw};
}

// Configuring a stashed client smaller than its card's logical size makes the
// card draw it that much larger, so its text stays readable off-stage.
void stage_content_size(Client *c, int32_t *w, int32_t *h) {
	if (c->isstaged && c->stage_lw > 0 && c->stage_lh > 0) {
		float z = config.stage_text_zoom;
		*w = MANGO_MAX(1, (int32_t)roundf(c->stage_lw / z));
		*h = MANGO_MAX(1, (int32_t)roundf(c->stage_lh / z));
		return;
	}
	*w = c->geom.width - 2 * (int32_t)c->bw;
	*h = c->geom.height - 2 * (int32_t)c->bw;
}

static float stage_current_scale(Client *c) {
	return c->stage_scale > 0.f ? c->stage_scale : config.stage_scale_max;
}

// The box on screen is logical size times scale. Box changes that did not come
// from a scale change (resize grabs, keyboard resizes) resize the client.
void stage_sync_logical(Client *c) {
	if (!c->isstaged || c->stage_scale <= 0.f)
		return;
	int32_t bw2 = 2 * (int32_t)c->bw;
	float s = c->stage_scale;
	// Compared on screen: at tiny scales a rounded pixel is many logical ones,
	// and treating it as a resize would reconfigure the client mid-drag.
	int32_t ew = (int32_t)roundf(c->stage_lw * s) + bw2;
	int32_t eh = (int32_t)roundf(c->stage_lh * s) + bw2;
	if (abs(c->geom.width - ew) > 1)
		c->stage_lw = (int32_t)roundf((c->geom.width - bw2) / s);
	if (abs(c->geom.height - eh) > 1)
		c->stage_lh = (int32_t)roundf((c->geom.height - bw2) / s);
}

// Popups are anchored to content that is about to change scale; closing them
// matches what toolkits do when their window moves.
static void stage_close_popups(Client *c) {
	if (client_is_x11(c))
		return;
	struct wlr_xdg_popup *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &c->surface.xdg->popups, link)
		wlr_xdg_popup_destroy(popup);
}

// Stage fields changed outside an arrange. Only c's own watchers need its
// update, so skip IPC_WATCH_CLIENT, which rebuilds every client.
void stage_notify(Client *c) {
	printstatus(IPC_WATCH_ALL_CLIENTS);
	ipc_notify_client(c);
}

// Stage state changes only through the transitions (stash, unstash, dock,
// undock, forget). Each does the whole change in one order: close popups,
// fields, minimized state, focus, card, arrange, IPC. Stash and unstash also
// run inside the layout pass, so their callers arrange, and arrange notifies;
// the rest end by notifying themselves, after the final placement.

// Stashes c at box, or moves it there when already stashed.
void stage_stash(Client *c, struct wlr_box box) {
	int32_t lw = c->stage_lw, lh = c->stage_lh;
	if (!c->isstaged) {
		struct wlr_box content;
		client_get_geometry(c, &content);
		if (content.width <= 0 || content.height <= 0)
			stage_content_size(c, &content.width, &content.height);
		lw = content.width;
		lh = content.height;
	}
	if (c->mon) {
		struct stage_frame f = stage_frame_of(c->mon);
		stage_clamp(&f, &box);
	}
	// Checked before any field changes, so a box that could not be drawn or
	// grabbed never leaves a half-stashed client.
	int32_t bw2 = 2 * (int32_t)c->bw;
	if (lw <= 0 || lh <= 0 || box.width <= bw2 || box.height <= bw2)
		return;

	if (!c->isstaged) {
		stage_close_popups(c);
		c->stage_lw = lw;
		c->stage_lh = lh;
		c->isstaged = true;
		c->isfloating = 1;
		// The stage owns it now; a scratchpad toggle would fight the card.
		c->is_in_scratchpad = 0;
		c->isnamedscratchpad = 0;
		c->iscustomsize = 1;
		// Tiled, since the stage owns the size: untiled GTK clients may
		// resize themselves to their default size mid-drag.
		client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT |
								WLR_EDGE_RIGHT);
		card_create(c);
	}
	c->stage_scale = (float)(box.width - bw2) / (float)c->stage_lw;
	c->float_geom = box;
	resize(c, box, (ResizeOpts){.interact = 0});
}

void stage_unstash(Client *c) {
	if (!c->isstaged)
		return;
	stage_close_popups(c);
	c->isstaged = false;
	c->isfloating = 0;
	// A dock is a stashed window, so it cannot outlive the stash.
	if (c->stage_docked) {
		c->stage_docked = false;
		client_pending_minimized_state(c, 0);
	}
	if (!c->overview_member)
		card_destroy(c);
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT |
							WLR_EDGE_RIGHT);
}

void stage_forget(Client *c) {
	if (c->isstaged)
		stage_close_popups(c);
	stage_drag_forget(c);
	c->stage_previewing = false;
	c->stage_preview_mon = NULL;
	c->stage_docked = false;
	c->isstaged = false;
	card_destroy(c);
}

void stage_adopt_size(Client *c, int32_t w, int32_t h) {
	if (!c->isstaged || w <= 0 || h <= 0)
		return;
	int32_t cw, ch;
	stage_content_size(c, &cw, &ch);
	if (w == cw && h == ch)
		return;
	float s = stage_current_scale(c);
	float z = config.stage_text_zoom;
	c->stage_lw = (int32_t)roundf(w * z);
	c->stage_lh = (int32_t)roundf(h * z);
	struct wlr_box box =
		stage_box_at(stage_card_of(c), c->geom.x + c->geom.width / 2.0,
					 c->geom.y + c->geom.height / 2.0, s);
	if (c == server.grab_client) {
		server.grab_client->float_geom = box;
		return;
	}
	c->float_geom = box;
	resize(c, box, (ResizeOpts){.interact = 0});
}

void stage_release_all(Monitor *m) {
	Client *c;
	wl_list_for_each(c, &server.clients, link) {
		// Docks outlive tag switches but not the stage leaving their own tag.
		bool docked_here =
			c->stage_docked && c->mon == m && (c->tags & m->tagset[m->seltags]);
		if (docked_here || (c->isstaged && VISIBLEON(c, m)))
			stage_unstash(c);
	}
}

int32_t stage_center_count(Monitor *m, Client *skip) {
	Client *c;
	int32_t n = 0;
	wl_list_for_each(c, &server.clients, link) {
		if (c != skip && VISIBLEON(c, m) && ISTILED(c))
			n++;
	}
	return n;
}

// Side a stashed window belongs to: where it sits, or the emptier side while it
// is still inside the center square.
static bool stage_on_left(const struct stage_frame *f, Client *c, int32_t nleft,
						  int32_t nright) {
	struct wlr_box s = stage_center_box(f);
	double cx = c->geom.x + c->geom.width / 2.0;
	if (cx < s.x)
		return true;
	if (cx > s.x + s.width)
		return false;
	return nleft <= nright;
}

static void stage_align_column(const struct stage_frame *f, Client **list,
							   int32_t k, bool left) {
	if (k <= 0)
		return;
	struct stage_card *cards = ecalloc(k, sizeof(*cards));
	struct wlr_box *boxes = ecalloc(k, sizeof(*boxes));
	for (int32_t i = 0; i < k; i++)
		cards[i] = stage_card_of(list[i]);
	stage_column_boxes(f, cards, k, left, boxes);
	for (int32_t i = 0; i < k; i++)
		stage_stash(list[i], boxes[i]);
	free(cards);
	free(boxes);
}

static int cmp_center_y(const void *a, const void *b) {
	const Client *ca = *(Client *const *)a, *cb = *(Client *const *)b;
	int32_t ya = ca->geom.y + ca->geom.height / 2;
	int32_t yb = cb->geom.y + cb->geom.height / 2;
	return (ya > yb) - (ya < yb);
}

// Lines every stashed window up in two tidy columns, one per side.
void stage_align_sides(Monitor *m, Client *skip) {
	Client *c;
	int32_t n = 0, nl = 0, nr = 0;
	wl_list_for_each(c, &server.clients, link) {
		if (c != skip && c->isstaged && VISIBLEON(c, m))
			n++;
	}
	if (n == 0)
		return;

	struct stage_frame f = stage_frame_of(m);
	Client **left = ecalloc(n, sizeof(*left));
	Client **right = ecalloc(n, sizeof(*right));
	wl_list_for_each(c, &server.clients, link) {
		if (c == skip || !c->isstaged || !VISIBLEON(c, m))
			continue;
		if (stage_on_left(&f, c, nl, nr))
			left[nl++] = c;
		else
			right[nr++] = c;
	}
	qsort(left, nl, sizeof(*left), cmp_center_y);
	qsort(right, nr, sizeof(*right), cmp_center_y);
	stage_align_column(&f, left, nl, true);
	stage_align_column(&f, right, nr, false);
	free(left);
	free(right);
}

// Stashes a window without disturbing the ones already on the sides: below the
// last one on the emptier side, against the square's edge.
void stage_stash_free(Monitor *m, Client *c) {
	struct stage_frame f = stage_frame_of(m);
	struct wlr_box s = stage_center_box(&f);
	int32_t nl = 0, nr = 0, below_l = INT32_MIN, below_r = INT32_MIN;
	Client *o;

	wl_list_for_each(o, &server.clients, link) {
		if (o == c || !o->isstaged || !VISIBLEON(o, m))
			continue;
		int32_t below = o->geom.y + o->geom.height + f.gap_in;
		if (o->geom.x + o->geom.width / 2 < s.x) {
			nl++;
			below_l = MANGO_MAX(below_l, below);
		} else {
			nr++;
			below_r = MANGO_MAX(below_r, below);
		}
	}

	bool left = nl <= nr;
	int32_t bw2 = 2 * (int32_t)c->bw;
	struct stage_card k = {
		.lw = c->isstaged ? c->stage_lw : c->geom.width - bw2,
		.lh = c->isstaged ? c->stage_lh : c->geom.height - bw2,
		.bw = (int32_t)c->bw,
	};
	if (k.lw <= 0 || k.lh <= 0)
		return;
	stage_stash(c, stage_free_box(&f, k, left, left ? below_l : below_r));
}

static void stage_tile(Monitor *m, Client **tiles, int32_t n) {
	struct stage_frame f = stage_frame_of(m);
	struct wlr_box boxes[STAGE_CENTER_MAX];
	stage_tile_boxes(&f, n, boxes);
	for (int32_t i = 0; i < n; i++)
		client_tile_resize(tiles[i], boxes[i], 0, NULL);
}

void stage_flip(Monitor *m) {
	if (!is_stage_layout(m))
		return;
	uint32_t tag = get_mon_curtag(m);
	Pertag *p = m->pertag;
	p->stage_flip[tag] = !p->stage_flip[tag];
	// Swapping the dividers keeps each tile's share through the flip.
	float x = p->stage_split_x[tag];
	p->stage_split_x[tag] = p->stage_split_y[tag];
	p->stage_split_y[tag] = x;
	arrange(m, false, false);
}

void stage(Monitor *m) {
	Client *c, *tiles[STAGE_CENTER_MAX];
	int32_t n = 0;

	wl_list_for_each(c, &server.clients, link) {
		// Restored by the taskbar or restore_minimized rather than undocked.
		if (c->stage_docked && !c->isminimized)
			c->stage_docked = false;
		if (!VISIBLEON(c, m) || c->iskilling)
			continue;
		if (c->isstaged && !c->isfloating)
			stage_unstash(c);
		if (c->isstaged) {
			// Staging skips the card while the surface is unmapped.
			if (!c->card.tree && client_surface(c)->mapped)
				card_create(c);
			continue;
		}
		if (!ISTILED(c))
			continue;
		if (n < STAGE_CENTER_MAX)
			tiles[n++] = c;
		else
			stage_stash_free(m, c);
	}

	if (n > 0)
		stage_tile(m, tiles, n);
}

// A docked window keeps its tags, so arrange takes it for a window staying
// across a tag switch and leaves it drawn; hide it the way a plain hide does.
static void stage_hide_docked(Client *c) {
	c->animation.running = false;
	c->animation.tagining = false;
	c->animation.tagouting = false;
	c->tag_visible = false;
	client_update_visibility(c);
	c->animainit_geom = c->current = c->pending = c->animation.current =
		c->geom;
}

// Docking hides the window like a minimize, minus the scratchpad, and leaves
// the shell to draw it as an edge widget from the IPC state. A window already
// docked only moves to edge and keeps its place along it.
void stage_dock_client(Client *c, int32_t edge, int32_t tier) {
	Monitor *m = c->mon;
	if (c->stage_docked) {
		c->stage_dock_edge = edge;
		c->stage_dock_tier = tier;
		// Overview strips are laid out per edge; outside it the dock stays
		// hidden and only the shell redraws.
		if (m->isoverview)
			arrange(m, false, false);
		else
			stage_notify(c);
		return;
	}
	stage_close_popups(c);
	c->stage_docked = true;
	c->stage_dock_edge = edge;
	c->stage_dock_tier = tier;
	c->stage_dock_y = c->geom.y + c->geom.height / 2 - m->m.y;
	client_pending_minimized_state(c, 1);
	client_focus(client_focus_top(m), 1);
	arrange(m, false, false);
	stage_hide_docked(c);
}

static void stage_undock_client(Client *c) {
	c->stage_docked = false;
	unminimize(c);

	Monitor *m = c->mon;
	if (!m || !c->isstaged || !is_stage_layout(m))
		return;
	struct stage_frame f = stage_frame_of(m);
	stage_stash(c, stage_undock_box(&f, stage_card_of(c),
									c->stage_dock_edge == STAGE_DOCK_LEFT,
									c->stage_dock_y));
	// unminimize already published, with the dock's box.
	stage_notify(c);
}

enum stage_result stage_dock_window(Client *c, int32_t edge) {
	if (!c || !c->mon)
		return STAGE_NO_CLIENT;
	if (c->stage_docked) {
		if (edge == STAGE_DOCK_NEAREST || edge == c->stage_dock_edge)
			return STAGE_UNCHANGED;
		stage_dock_client(c, edge, c->stage_dock_tier);
		return STAGE_CHANGED;
	}
	if (!c->isstaged || !is_stage_layout(c->mon))
		return STAGE_NOT_STAGED;
	if (edge == STAGE_DOCK_NEAREST) {
		double cx = c->geom.x + c->geom.width / 2.0;
		edge = cx < c->mon->m.x + c->mon->m.width / 2.0 ? STAGE_DOCK_LEFT
														: STAGE_DOCK_RIGHT;
	}
	stage_dock_client(c, edge, STAGE_DOCK_FULL);
	return STAGE_CHANGED;
}

enum stage_result stage_undock_window(Client *c) {
	if (!c || !c->mon)
		return STAGE_NO_CLIENT;
	if (!c->stage_docked)
		return STAGE_NOT_DOCKED;
	stage_undock_client(c);
	return STAGE_CHANGED;
}

// For the shell's tab drag, so y is the tab center, the same as the undock
// position.
enum stage_result stage_move_dock(Client *c, int32_t y) {
	if (!c || !c->mon)
		return STAGE_NO_CLIENT;
	if (!c->stage_docked)
		return STAGE_NOT_DOCKED;
	y = MANGO_MAX(0, MANGO_MIN(y, c->mon->m.height));
	bool changed = y != c->stage_dock_y;
	c->stage_dock_y = y;
	stage_notify(c);
	return changed ? STAGE_CHANGED : STAGE_UNCHANGED;
}

// Docked windows are the shell's to draw. Only overview shows them, as cards
// in its dock strips.
bool stage_arrange_hidden(Monitor *m, Client *c) {
	if (!c->stage_docked)
		return false;
	if (m->isoverview && c->overview_member &&
		(c->tags & m->tagset[m->seltags]))
		return false;
	stage_hide_docked(c);
	return true;
}

static bool stage_overview_listed(Monitor *m, Client *c, int32_t edge) {
	return c->mon == m && c->stage_docked && c->stage_dock_edge == edge &&
		   c->overview_member && (c->tags & m->tagset[m->seltags]);
}

static float stage_overview_aspect(Client *c) {
	struct wlr_box *g = &c->overview_backup_geom;
	return g->width > 0 && g->height > 0 ? (float)g->height / g->width : 1.0f;
}

// Stacks one edge's docked cards in a centered column; returns the strip
// width taken, or 0 when that edge has none.
static int32_t stage_overview_strip(Monitor *m, struct wlr_box area,
									int32_t edge) {
	Client *c, **listed;
	int32_t n = 0;
	wl_list_for_each(c, &server.clients, link) {
		n += stage_overview_listed(m, c, edge);
	}
	if (n == 0)
		return 0;

	listed = ecalloc(n, sizeof(*listed));
	float *aspects = ecalloc(n, sizeof(*aspects));
	struct wlr_box *boxes = ecalloc(n, sizeof(*boxes));
	int32_t i = 0;
	wl_list_for_each(c, &server.clients, link) {
		if (!stage_overview_listed(m, c, edge))
			continue;
		listed[i] = c;
		aspects[i++] = stage_overview_aspect(c);
	}
	struct stage_frame f = stage_frame_of(m);
	int32_t taken =
		stage_strip_boxes(&f, area, aspects, n, edge == STAGE_DOCK_LEFT, boxes);
	for (i = 0; i < n; i++)
		resize(listed[i], boxes[i], (ResizeOpts){.interact = 0});
	free(listed);
	free(aspects);
	free(boxes);
	return taken;
}

// Overview lists docked windows on their own edge, so one whose shell widget
// is missing can still be picked back; the regular overview lays out in what
// is left of the area.
struct wlr_box stage_overview_docks(Monitor *m, struct wlr_box area) {
	int32_t left = stage_overview_strip(m, area, STAGE_DOCK_LEFT);
	int32_t right = stage_overview_strip(m, area, STAGE_DOCK_RIGHT);
	area.x += left;
	area.width -= left + right;
	return area;
}
