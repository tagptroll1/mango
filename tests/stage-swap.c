/* A staged card dropped on a full center swaps with the tile under it. */
#include "../src/layout/stage-drag.c"
#include "../src/layout/stage-geometry.c"
#include "../src/layout/stage.c"
#include <stdio.h>

struct MangoServer server;
Config config;

#define NCLIENTS 6
static Client clients[NCLIENTS];
static struct wlr_xdg_surface xdg[NCLIENTS];
/* What an X11 client reports; Xwayland may still report nothing. */
static struct wlr_box x11_geometry[NCLIENTS];
static Monitor mon;
/* Geometry inputs as the layout reads them from mon. */
static const struct stage_frame *frame(void) {
	static struct stage_frame f;
	f = stage_frame_of(&mon);
	return &f;
}
static Pertag pertag;
static struct wlr_cursor cursor;
static const Layout stage_layout = {.id = STAGE};
static int failures;

/* Records what the stage commits, as the real resize would set it. */
void resize(Client *c, struct wlr_box geo, ResizeOpts opts) { c->geom = geo; }
/* The size the client itself last committed. */
void client_get_geometry(Client *c, struct wlr_box *geom) {
	*geom =
		c->type == X11 ? x11_geometry[c - clients] : c->surface.xdg->geometry;
}
int32_t client_is_x11(Client *c) { return c->type == X11; }
uint32_t get_mon_curtag(const Monitor *m) { return 0; }
Monitor *monitor_at_point(double x, double y) { return &mon; }
void arrange(Monitor *m, bool want_animation, bool from_view) {}
void client_set_tiled(Client *c, uint32_t edges) {}
void client_pending_minimized_state(Client *c, int32_t isminimized) {}
void client_update_visibility(Client *c) {}
void client_focus(Client *c, int32_t lift) {}
Client *client_focus_top(Monitor *m) { return NULL; }
void card_create(Client *c) {}
void card_destroy(Client *c) {}
void printstatus(uint32_t event_mask) {}
void ipc_notify_client(Client *c) {}

static void check(bool ok, const char *name, const char *detail) {
	printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", name, detail ? " - " : "",
		   detail ? detail : "");
	failures += !ok;
}

static void setup(void) {
	config = (Config){
		.stage_scale_max = 0.8f,
		.stage_scale_min = 0.6f,
		.stage_scale_curve = 1.0f,
		.stage_shrink_zone = 100,
		.stage_dock_tiny = 64,
		.stage_dock_zone = 48,
		.stage_dock_mini_zone = 8,
		.stage_text_zoom = 1.0f,
		.stage_shake_travel = 40,
		.stage_shake_flips = 3,
		.borderpx = 2,
	};
	server.enable_gaps = true;
	server.cursor = &cursor;
	server.grab_client = NULL;
	wl_list_init(&server.clients);
	pertag = (Pertag){.ltidxs = {&stage_layout}};
	mon = (Monitor){
		.m = {0, 0, 2560, 1440},
		.w = {0, 0, 2560, 1440},
		.gappih = 10,
		.gappiv = 10,
		.gappoh = 10,
		.gappov = 10,
		.tagset = {1, 1},
		.pertag = &pertag,
	};
	memset(clients, 0, sizeof(clients));
	memset(xdg, 0, sizeof(xdg));
	memset(x11_geometry, 0, sizeof(x11_geometry));
	for (int32_t i = 0; i < NCLIENTS; i++) {
		wl_list_init(&xdg[i].popups);
		clients[i] = (Client){
			.type = XDGShell,
			.mon = &mon,
			.tags = 1,
			.bw = 2,
			.surface.xdg = &xdg[i],
		};
	}
}

/* Client i tiled or floating at geo, reporting content w x h. */
static Client *add(int32_t i, struct wlr_box geo, int32_t w, int32_t h) {
	Client *c = &clients[i];
	c->geom = geo;
	xdg[i].geometry = (struct wlr_box){0, 0, w, h};
	wl_list_insert(server.clients.prev, &c->link);
	return c;
}

static void set_cursor(double x, double y) {
	cursor.x = x;
	cursor.y = y;
}

static int32_t tiled_count(void) {
	int32_t n = 0;
	for (int32_t i = 0; i < NCLIENTS; i++)
		n += clients[i].mon && clients[i].link.next && ISTILED(&clients[i]);
	return n;
}

enum history { NEVER_STAGED, STALE_STAGED };

struct swap_case {
	const char *name;
	/* Where the grabbed card sat before the drag. */
	int32_t origin_x, origin_y;
	/* Target content as the client last reported it. */
	int32_t tw, th;
	bool x11;
	/* X11 client whose surface has no size yet. */
	bool x11_unsized;
};

struct swap_result {
	struct wlr_box origin, box;
	float scale;
	int32_t lw, lh, tiled;
	bool grabbed_tiled, target_staged;
};

#define TARGET 1
#define GRABBED 4

/* Four tiles, a staged card dragged from its origin onto tile TARGET. */
static struct swap_result run_swap(const struct swap_case *k,
								   enum history history) {
	setup();
	struct wlr_box boxes[STAGE_CENTER_MAX];
	stage_tile_boxes(frame(), STAGE_CENTER_MAX, boxes);
	for (int32_t i = 0; i < STAGE_CENTER_MAX; i++)
		add(i, boxes[i], boxes[i].width - 4, boxes[i].height - 4);
	Client *target = &clients[TARGET];
	if (k->x11) {
		target->type = X11;
		x11_geometry[TARGET] = (struct wlr_box){0, 0, k->tw, k->th};
	}

	if (history == STALE_STAGED) {
		/* Staged once at another size, then tiled and resized again. */
		xdg[TARGET].geometry = (struct wlr_box){0, 0, 1700, 260};
		x11_geometry[TARGET] = xdg[TARGET].geometry;
		stage_stash(target, (struct wlr_box){20, 1200, 140, 25});
		stage_unstash(target);
		target->geom = boxes[TARGET];
	}
	xdg[TARGET].geometry = (struct wlr_box){0, 0, k->tw, k->th};
	x11_geometry[TARGET] = (struct wlr_box){0, 0, k->tw, k->th};
	if (k->x11_unsized)
		x11_geometry[TARGET] = (struct wlr_box){0};

	/* The grabbed card: 800x600 content stashed at 0.4. */
	Client *g = add(GRABBED, (struct wlr_box){0, 0, 804, 604}, 800, 600);
	g->isfloating = 1;
	stage_stash(g, (struct wlr_box){k->origin_x, k->origin_y, 324, 244});

	struct swap_result r = {.origin = g->geom};
	set_cursor(g->geom.x + g->geom.width / 2.0,
			   g->geom.y + g->geom.height / 2.0);
	server.grab_client = g;
	stage_begin_move(g, cursor.x, cursor.y);
	server.grab_offset_x = (int32_t)round(cursor.x) - g->geom.x;
	server.grab_offset_y = (int32_t)round(cursor.y) - g->geom.y;
	/* Over the target, as the pointer code moves the grabbed window. */
	set_cursor(boxes[TARGET].x + boxes[TARGET].width / 2.0,
			   boxes[TARGET].y + boxes[TARGET].height / 2.0);
	stage_drag_motion(g, 0);
	resize(g, g->float_geom, (ResizeOpts){.interact = 0});
	target->drop_direction = LEFT;
	server.grab_client = NULL;
	stage_drop(g);

	r.box = target->geom;
	r.scale = target->stage_scale;
	r.lw = target->stage_lw;
	r.lh = target->stage_lh;
	r.tiled = tiled_count();
	r.grabbed_tiled = ISTILED(g);
	r.target_staged = target->isstaged;
	return r;
}

static void describe(char *out, size_t len, const struct swap_result *r) {
	snprintf(out, len, "box %d,%d %dx%d, scale %.4f, logical %dx%d", r->box.x,
			 r->box.y, r->box.width, r->box.height, r->scale, r->lw, r->lh);
}

/* The swapped target sits on the origin's side, off the center square, keeping
 * its logical aspect. Its scale is the solver's for the anchor the box shows:
 * centred on the origin, or its inner edge one gap off the square. */
static bool swap_valid(const struct swap_result *r) {
	Client *target = &clients[TARGET];
	struct wlr_box s = stage_center_box(frame()), o;
	double ocx = r->origin.x + r->origin.width / 2.0;
	double cx = r->box.x + r->box.width / 2.0;
	bool same_side = (ocx < s.x) == (cx < s.x);
	int32_t right = r->box.x + r->box.width;
	float solved = stage_scale_at(frame(), stage_card_of(target), ocx, 0.5);
	if (right == s.x - mon.gappih)
		solved = stage_scale_at(frame(), stage_card_of(target), right, 1.0);
	else if (r->box.x == s.x + s.width + mon.gappih)
		solved = stage_scale_at(frame(), stage_card_of(target), r->box.x, 0.0);
	return r->target_staged && r->grabbed_tiled && r->tiled == 4 &&
		   isfinite(r->scale) && r->scale > 0.f && r->box.width > 4 &&
		   r->box.height > 4 &&
		   abs(r->box.width - ((int32_t)roundf(r->lw * solved) + 4)) <= 1 &&
		   abs(r->box.width - ((int32_t)roundf(r->lw * r->scale) + 4)) <= 1 &&
		   abs(r->box.height - ((int32_t)roundf(r->lh * r->scale) + 4)) <= 1 &&
		   r->box.x >= mon.w.x && r->box.y >= mon.w.y &&
		   right <= mon.w.x + mon.w.width &&
		   r->box.y + r->box.height <= mon.w.y + mon.w.height && same_side &&
		   !wlr_box_intersection(&o, &r->box, &s);
}

static void swap_case(const struct swap_case *k) {
	char name[128], detail[160];
	struct swap_result fresh = run_swap(k, NEVER_STAGED);
	int32_t want_lw = k->tw, want_lh = k->th;
	if (k->x11_unsized) {
		/* Nothing reported: the tile's content box is the size. */
		struct wlr_box boxes[STAGE_CENTER_MAX];
		stage_tile_boxes(frame(), STAGE_CENTER_MAX, boxes);
		want_lw = boxes[TARGET].width - 4;
		want_lh = boxes[TARGET].height - 4;
	}

	describe(detail, sizeof(detail), &fresh);
	snprintf(name, sizeof(name), "%s, never staged", k->name);
	check(swap_valid(&fresh) && fresh.lw == want_lw && fresh.lh == want_lh,
		  name, detail);

	struct swap_result stale = run_swap(k, STALE_STAGED);
	describe(detail, sizeof(detail), &stale);
	snprintf(name, sizeof(name), "%s, previously staged", k->name);
	check(swap_valid(&stale) && stale.lw == want_lw && stale.lh == want_lh,
		  name, detail);

	snprintf(name, sizeof(name), "%s, history has no effect", k->name);
	snprintf(detail, sizeof(detail), "never %d,%d %dx%d vs stale %d,%d %dx%d",
			 fresh.box.x, fresh.box.y, fresh.box.width, fresh.box.height,
			 stale.box.x, stale.box.y, stale.box.width, stale.box.height);
	check(abs(fresh.box.x - stale.box.x) <= 1 &&
			  abs(fresh.box.y - stale.box.y) <= 1 &&
			  abs(fresh.box.width - stale.box.width) <= 1 &&
			  abs(fresh.box.height - stale.box.height) <= 1,
		  name, detail);
}

int main(void) {
	/* x 70 and 2166: 70px from the edge, inside the shrink zone but past the
	 * dock zone. */
	const struct swap_case cases[] = {
		{"left near-edge wide target", 70, 500, 900, 500},
		{"right near-edge wide target", 2166, 500, 900, 500},
		{"left near-edge tall target", 70, 500, 500, 900},
		{"right near-edge tall target", 2166, 500, 500, 900},
		{"ordinary side location", 200, 500, 400, 300},
		{"wide target that would cover the stage", 250, 500, 900, 500},
		{"xwayland target", 70, 500, 900, 500, true},
		{"xwayland target without reported size", 70, 500, 0, 0, true, true},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		swap_case(&cases[i]);

	/* Ordinary location: the box centres on the origin as before. */
	struct swap_result r = run_swap(&cases[4], NEVER_STAGED);
	char detail[160];
	describe(detail, sizeof(detail), &r);
	check(abs((r.box.x + r.box.width / 2) -
			  (r.origin.x + r.origin.width / 2)) <= 1 &&
			  abs((r.box.y + r.box.height / 2) -
				  (r.origin.y + r.origin.height / 2)) <= 1,
		  "ordinary side location centres on origin", detail);

	/* Too wide to centre on the origin: moved beside the stage, one gap off. */
	r = run_swap(&cases[5], NEVER_STAGED);
	describe(detail, sizeof(detail), &r);
	struct wlr_box center = stage_center_box(frame());
	check(r.box.x + r.box.width == center.x - mon.gappih &&
			  abs((r.box.y + r.box.height / 2) -
				  (r.origin.y + r.origin.height / 2)) <= 1,
		  "covering target moves beside the stage", detail);

	/* Three tiles: the card joins the center and nothing is stashed. */
	setup();
	struct wlr_box boxes[STAGE_CENTER_MAX];
	stage_tile_boxes(frame(), 3, boxes);
	for (int32_t i = 0; i < 3; i++)
		add(i, boxes[i], boxes[i].width - 4, boxes[i].height - 4);
	Client *g = add(GRABBED, (struct wlr_box){0, 0, 804, 604}, 800, 600);
	g->isfloating = 1;
	stage_stash(g, (struct wlr_box){70, 500, 324, 244});
	set_cursor(g->geom.x + 100, g->geom.y + 100);
	stage_begin_move(g, cursor.x, cursor.y);
	set_cursor(boxes[1].x + boxes[1].width / 2.0,
			   boxes[1].y + boxes[1].height / 2.0);
	stage_drag_motion(g, 0);
	resize(g, g->float_geom, (ResizeOpts){.interact = 0});
	clients[1].drop_direction = LEFT;
	stage_drop(g);
	bool none_staged = true;
	for (int32_t i = 0; i < 3; i++)
		none_staged &= !clients[i].isstaged;
	check(ISTILED(g) && tiled_count() == 4 && none_staged,
		  "center not full joins the center", NULL);

	/* Full center, released in the gap between tiles: back to the origin. */
	setup();
	stage_tile_boxes(frame(), STAGE_CENTER_MAX, boxes);
	for (int32_t i = 0; i < STAGE_CENTER_MAX; i++)
		add(i, boxes[i], boxes[i].width - 4, boxes[i].height - 4);
	g = add(GRABBED, (struct wlr_box){0, 0, 804, 604}, 800, 600);
	g->isfloating = 1;
	stage_stash(g, (struct wlr_box){70, 500, 324, 244});
	struct wlr_box origin = g->geom;
	set_cursor(g->geom.x + 100, g->geom.y + 100);
	stage_begin_move(g, cursor.x, cursor.y);
	set_cursor(boxes[0].x + boxes[0].width + 5,
			   boxes[0].y + boxes[0].height / 2.0);
	stage_drag_motion(g, 0);
	resize(g, g->float_geom, (ResizeOpts){.interact = 0});
	stage_drop(g);
	snprintf(detail, sizeof(detail), "box %d,%d %dx%d", g->geom.x, g->geom.y,
			 g->geom.width, g->geom.height);
	check(g->isstaged && wlr_box_equal(&g->geom, &origin) && tiled_count() == 4,
		  "full center gap drop returns to origin", detail);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
