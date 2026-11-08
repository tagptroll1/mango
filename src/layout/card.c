#include "mango/layout/card.h"
#include "mango/common/scene_node.h"
#include "mango/common/server.h"
#include "mango/common/util.h"
#include "mango/manage/client.h"
#include "mango/manage/layer.h"
#include "mango/manage/monitor.h"
#include <math.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>

/* Card surface node: each surface (including subsurfaces) maps to a
 * scene_surface node in the card tree; sx/sy are its coordinates relative to
 * the root surface. */
struct card_surface {
	Client *c;
	struct wlr_surface *surface;
	struct wlr_scene_surface *scene_surface;
	struct wlr_scene_buffer *buffer;
	int sx, sy; /* Coordinates relative to the root surface */
	struct wl_list link;
	struct wl_listener commit; /* Recomputes scaling after commit (commit resets
								  dest/source). */
	struct wl_listener
		destroy; /* Removes the node when the surface is destroyed. */
};

static void card_surface_add(struct wlr_surface *surface, int sx, int sy,
							 void *data);
static void card_layout_popups(Client *c);

// Recomputes layout after surface commit (scene_surface commit resets
// dest/source and needs to be reapplied).
static void card_surface_handle_commit(struct wl_listener *listener,
									   void *data) {
	struct card_surface *entry = wl_container_of(listener, entry, commit);
	if (entry->c && entry->c->card.tree)
		card_layout(entry->c);
}

static void card_surface_free(struct card_surface *entry) {
	wl_list_remove(&entry->link);
	wl_list_remove(&entry->commit.link);
	wl_list_remove(&entry->destroy.link);
	free(entry);
}

static void card_surface_handle_destroy(struct wl_listener *listener,
										void *data) {
	struct card_surface *entry = wl_container_of(listener, entry, destroy);
	card_surface_free(entry);
}

static struct card_surface *card_entry_for_surface(Client *c,
												   struct wlr_surface *s) {
	struct card_surface *entry;
	wl_list_for_each(entry, &c->card.surfaces, link) {
		if (entry->surface == s)
			return entry;
	}
	return NULL;
}

// Card buffers show scaled content, so hit tests map the card-local point back
// to surface-local coordinates before asking the client's input region.
static bool card_buffer_point_accepts_input(struct wlr_scene_buffer *buffer,
											double *sx, double *sy) {
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	MangoSceneNode *data = mango_scene_node_find(&buffer->node);
	Client *c = data && (data->type == XDGShell || data->type == X11)
					? data->owner
					: NULL;
	if (!scene_surface || !c || !c->card.tree)
		return false;

	struct card_surface *entry =
		card_entry_for_surface(c, scene_surface->surface);
	if (!entry || c->card.scale_x <= 0.f || c->card.scale_y <= 0.f)
		return false;

	*sx = (buffer->node.x + *sx) / c->card.scale_x + c->card.clip_x - entry->sx;
	*sy = (buffer->node.y + *sy) / c->card.scale_y + c->card.clip_y - entry->sy;
	return wlr_surface_point_accepts_input(scene_surface->surface, *sx, *sy);
}

bool card_owns_buffer(struct wlr_scene_buffer *buffer) {
	return buffer->point_accepts_input == card_buffer_point_accepts_input;
}

bool card_surface_coords(Client *c, struct wlr_surface *s, double lx, double ly,
						 double *sx, double *sy) {
	if (!c || !c->card.tree)
		return false;
	struct card_surface *entry = card_entry_for_surface(c, s);
	if (!entry || c->card.scale_x <= 0.f || c->card.scale_y <= 0.f)
		return false;

	int32_t ox, oy;
	wlr_scene_node_coords(&c->card.tree->node, &ox, &oy);
	*sx = (lx - ox) / c->card.scale_x + c->card.clip_x - entry->sx;
	*sy = (ly - oy) / c->card.scale_y + c->card.clip_y - entry->sy;
	return true;
}

static void card_sync_surface(struct wlr_surface *surface, int sx, int sy,
							  void *data) {
	Client *c = data;
	struct card_surface *entry = card_entry_for_surface(c, surface);
	if (!entry) {
		card_surface_add(surface, sx, sy, c);
		entry = card_entry_for_surface(c, surface);
		if (!entry)
			return;
	}
	entry->sx = sx;
	entry->sy = sy;
	// Iteration runs bottom to top, so raising in order restores stacking.
	wlr_scene_node_raise_to_top(&entry->buffer->node);
}

// Creates a card scene_surface node for every surface (including subsurfaces).
static void card_surface_add(struct wlr_surface *surface, int sx, int sy,
							 void *data) {
	Client *c = data;
	if (!c->card.tree)
		return;

	struct card_surface *entry = ecalloc(1, sizeof(*entry));
	entry->c = c;
	entry->surface = surface;
	entry->sx = sx;
	entry->sy = sy;

	entry->scene_surface = wlr_scene_surface_create(c->card.tree, surface);
	if (!entry->scene_surface) {
		free(entry);
		return;
	}
	entry->buffer = entry->scene_surface->buffer;
	wlr_scene_buffer_set_filter_mode(entry->buffer, WLR_SCALE_FILTER_BILINEAR);
	entry->buffer->point_accepts_input = card_buffer_point_accepts_input;

	entry->commit.notify = card_surface_handle_commit;
	wl_signal_add(&surface->events.commit, &entry->commit);
	entry->destroy.notify = card_surface_handle_destroy;
	wl_signal_add(&surface->events.destroy, &entry->destroy);

	wl_list_insert(&c->card.surfaces, &entry->link);
}

// Content origin uses the client_get_clip geometry offset.
void card_layout(Client *c) {
	if (!c->card.tree)
		return;

	struct wlr_box geo = c->animation.current;
	if (geo.width <= 0 || geo.height <= 0)
		client_get_geometry(c, &geo);
	int32_t bw = (int32_t)c->bw;
	int32_t w = geo.width - 2 * bw;
	int32_t h = geo.height - 2 * bw;
	if (w <= 0 || h <= 0)
		return;

	wlr_scene_node_set_position(&c->card.tree->node, bw, bw);

	// Long-lived cards see subsurfaces appear and move.
	wlr_surface_for_each_surface(client_surface(c), card_sync_surface, c);

	// Content origin (geometry offset) and card content size.
	struct wlr_box clip;
	client_get_clip(c, &clip);

	float content_w, content_h;
#ifdef XWAYLAND
	struct wlr_surface *s = client_surface(c);
	if (client_is_x11(c)) {
		content_w = s->current.width;
		content_h = s->current.height;
	} else
#endif
	{
		content_w = c->surface.xdg->geometry.width;
		content_h = c->surface.xdg->geometry.height;
	}
	if (content_w <= 0 || content_h <= 0)
		return;

	double scale_x = (double)w / content_w;
	double scale_y = (double)h / content_h;
	c->card.scale_x = scale_x;
	c->card.scale_y = scale_y;
	c->card.clip_x = clip.x;
	c->card.clip_y = clip.y;

	int32_t vx = 0, vy = 0, vw = w, vh = h;
	if (c->mon) {
		struct wlr_box content_box = {
			.x = geo.x + bw,
			.y = geo.y + bw,
			.width = w,
			.height = h,
		};
		struct wlr_box vis;
		if (wlr_box_intersection(&vis, &content_box, &c->mon->m)) {
			vx = vis.x - content_box.x;
			vy = vis.y - content_box.y;
			vw = vis.width;
			vh = vis.height;
		} else {
			vw = 0;
			vh = 0;
		}
	}

	struct card_surface *entry;
	wl_list_for_each(entry, &c->card.surfaces, link) {
		struct wlr_surface *es = entry->surface;
		/* current.width/height are logical coordinates; buffer_width/height are
		 * pixel coordinates. */
		int32_t lw = es->current.width;
		int32_t lh = es->current.height;
		// A 0x0 destination means native buffer size in SceneFX, so an
		// empty extent must disable the node instead.
		if (lw <= 0 || lh <= 0) {
			wlr_scene_node_set_enabled(&entry->buffer->node, false);
			continue;
		}

		/*
		 * Map each surface's own (0,0) into the card, then clip the result to
		 * the card and monitor. The source offset is derived from that
		 * intersection, so an xdg geometry origin outside the root buffer never
		 * becomes a negative buffer coordinate. The source offset comes from
		 * the unrounded origin, or a fractional offset shifts the content.
		 */
		double fx = (entry->sx - clip.x) * scale_x;
		double fy = (entry->sy - clip.y) * scale_y;
		int px = (int)floor(fx);
		int py = (int)floor(fy);
		int dw = (int)(lw * scale_x);
		int dh = (int)(lh * scale_y);

		int cx0 = MANGO_MAX(px, vx);
		int cy0 = MANGO_MAX(py, vy);
		int cx1 = MANGO_MIN(px + dw, vx + vw);
		int cy1 = MANGO_MIN(py + dh, vy + vh);
		int cw = cx1 - cx0;
		int ch = cy1 - cy0;
		if (cw <= 0 || ch <= 0) {
			wlr_scene_node_set_enabled(&entry->buffer->node, false);
			continue;
		}

		double ox = fmax(0.0, (cx0 - fx) / scale_x);
		double oy = fmax(0.0, (cy0 - fy) / scale_y);
		struct wlr_fbox surface_box = {
			.x = ox,
			.y = oy,
			.width = (double)cw / scale_x,
			.height = (double)ch / scale_y,
		};
		struct wlr_fbox src;
		client_surface_get_source_box(es, &surface_box, &src);
		wlr_scene_node_set_enabled(&entry->buffer->node, true);
		wlr_scene_node_set_position(&entry->buffer->node, cx0, cy0);
		wlr_scene_buffer_set_dest_size(entry->buffer, cw, ch);
		wlr_scene_buffer_set_source_box(entry->buffer, &src);
	}

	card_layout_popups(c);
}

void card_set_radii(Client *c, struct fx_corner_radii corners) {
	if (!c->card.tree)
		return;
	struct card_surface *entry;
	wl_list_for_each(entry, &c->card.surfaces, link)
		wlr_scene_buffer_set_corner_radii(entry->buffer, corners);
}

void card_create(Client *c) {
	if (c->card.tree)
		return;
	if (!client_surface(c) || !client_surface(c)->mapped)
		return;

	c->card.tree = wlr_scene_tree_create(c->scene);
	if (!c->card.tree)
		return;
	wl_list_init(&c->card.surfaces);
	wl_list_init(&c->card.popups);
	// The tree now exists, so this hides the real surface tree.
	client_update_visibility(c);

	wlr_surface_for_each_surface(client_surface(c), card_surface_add, c);
	card_layout(c);

	// Feeds one frame to start the render loop (later driven by scene_surface
	// frame-done).
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	client_send_frame_done(c, &now);
}

void card_destroy(Client *c) {
	if (!c->card.tree)
		return;

	// Their carriers are placed from the card mapping, gone after this.
	Popup *popup, *ptmp;
	wl_list_for_each_safe(popup, ptmp, &c->card.popups, card.link)
		wlr_xdg_popup_destroy(popup->wlr_popup);

	struct card_surface *entry, *tmp;
	wl_list_for_each_safe(entry, tmp, &c->card.surfaces, link)
		card_surface_free(entry);

	wlr_scene_node_destroy(&c->card.tree->node);
	c->card.tree = NULL;
	client_update_visibility(c);
}

static void card_popup_handle_commit(struct wl_listener *listener, void *data) {
	Popup *popup = wl_container_of(listener, popup, card.commit);
	if (popup->card.client)
		card_layout_popups(popup->card.client);
}

// Parents direct popups of a card client to their own node so they escape
// the hidden real surface tree and can follow the scaled card.
bool card_popup_create(Popup *popup) {
	struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;
	Client *c = NULL;
	if (!wlr_popup->parent ||
		toplevel_from_wlr_surface(wlr_popup->parent, &c, NULL) < 0 || !c ||
		!c->card.tree || client_is_x11(c) ||
		wlr_popup->parent != client_surface(c))
		return false;

	popup->card.client = c;
	popup->card.carrier = wlr_scene_tree_create(c->scene);
	wlr_popup->base->surface->data =
		wlr_scene_xdg_surface_create(popup->card.carrier, wlr_popup->base);
	wl_list_insert(&c->card.popups, &popup->card.link);
	popup->card.commit.notify = card_popup_handle_commit;
	wl_signal_add(&wlr_popup->base->surface->events.commit,
				  &popup->card.commit);
	card_layout_popups(c);
	return true;
}

void card_popup_destroy(Popup *popup) {
	if (!popup->card.client)
		return;
	wl_list_remove(&popup->card.link);
	wl_list_remove(&popup->card.commit.link);
	wlr_scene_node_destroy(&popup->card.carrier->node);
	popup->card.client = NULL;
}

static void card_layout_popups(Client *c) {
	if (!c->card.tree || !c->mon)
		return;

	Popup *popup;
	wl_list_for_each(popup, &c->card.popups, card.link) {
		struct wlr_box g = popup->wlr_popup->current.geometry;
		// Popup geometry already excludes the parent's surface margins.
		int32_t ax = (int32_t)c->bw + (int32_t)roundf(g.x * c->card.scale_x);
		int32_t ay = (int32_t)c->bw + (int32_t)roundf(g.y * c->card.scale_y);

		// Slide back onto the usable area instead of flipping.
		int32_t lx = c->animation.current.x + ax;
		int32_t ly = c->animation.current.y + ay;
		struct wlr_box *u = &c->mon->w;
		if (lx + g.width > u->x + u->width)
			ax -= lx + g.width - (u->x + u->width);
		if (ly + g.height > u->y + u->height)
			ay -= ly + g.height - (u->y + u->height);
		if (lx < u->x)
			ax += u->x - lx;
		if (ly < u->y)
			ay += u->y - ly;

		// wlroots places the popup tree at g inside the carrier.
		wlr_scene_node_set_position(&popup->card.carrier->node, ax - g.x,
									ay - g.y);
		wlr_scene_node_raise_to_top(&popup->card.carrier->node);
	}
}
