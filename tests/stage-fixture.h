/* Stage modules on one 2560x1440 monitor, with compositor effects stubbed:
 * resize records the box, arrange and IPC only count. */
#include "../src/dispatch/stage.c"
#include "../src/ipc/stage.c"
#include "../src/layout/stage-drag.c"
#include "../src/layout/stage-geometry.c"
#include "../src/layout/stage.c"
#include <stdio.h>
#include <wlr/types/wlr_output.h>

struct MangoServer server;
Config config;
struct ipc_dispatch_result ipc_dispatch_result = {-1, NULL};

#define NCLIENTS 8
static Client clients[NCLIENTS];
static struct wlr_xdg_surface xdg[NCLIENTS];
static Monitor mon, other;
static Pertag pertag, other_pertag;
static struct wlr_cursor cursor;
static struct wlr_output output = {.name = "DP-1"};
static const Layout stage_layout = {.id = STAGE};
static const Layout tile_layout = {.id = TILE};
static int failures, arranges, notifies;

/* Tests that watch calls made while laying out set this. */
static void (*on_resize)(Client *c);
void resize(Client *c, struct wlr_box geo, ResizeOpts opts) {
	if (on_resize)
		on_resize(c);
	c->geom = geo;
}
void client_tile_resize(Client *c, struct wlr_box geo, int32_t interact,
						const LayoutContext *ctx) {
	if (on_resize)
		on_resize(c);
	c->geom = geo;
}
void client_get_geometry(Client *c, struct wlr_box *geom) {
	*geom = c->surface.xdg->geometry;
}
int32_t client_is_x11(Client *c) { return 0; }
uint32_t get_mon_curtag(const Monitor *m) { return 0; }
Monitor *monitor_at_point(double x, double y) {
	return x < mon.m.x + mon.m.width ? &mon : &other;
}
void arrange(Monitor *m, bool want_animation, bool from_view) { arranges++; }
void printstatus(uint32_t mask) { notifies++; }
void ipc_notify_client(Client *c) { notifies++; }
void unminimize(Client *c) { c->isminimized = 0; }
void client_pending_minimized_state(Client *c, int32_t v) {
	c->isminimized = v;
}
void client_set_tiled(Client *c, uint32_t edges) {}
void client_update_visibility(Client *c) {}
void client_focus(Client *c, int32_t lift) {}
Client *client_focus_top(Monitor *m) { return NULL; }
void card_create(Client *c) {}
void card_destroy(Client *c) {}
struct wlr_surface *client_surface(Client *c) {
	static struct wlr_surface s = {.mapped = true};
	return &s;
}
double find_animation_curve_at(double t, int32_t type) { return t; }

static void check(bool ok, const char *name, const char *detail) {
	printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", name, detail ? " - " : "",
		   detail ? detail : "");
	failures += !ok;
}

static void setup(bool gaps, float split_x, float split_y, bool flip) {
	config = (Config){
		.stage_scale_max = 0.8f,
		.stage_scale_min = 0.6f,
		.stage_scale_curve = 1.0f,
		.stage_shrink_zone = 100,
		.stage_dock_tiny = 64,
		.stage_dock_zone = 48,
		.stage_dock_mini_zone = 8,
		.stage_overview_dock_ratio = 0.12f,
		.stage_text_zoom = 1.0f,
		.stage_shake_travel = 40,
		.stage_shake_flips = 4,
		.stage_shake_window_ms = 1000,
		.overviewgappi = 5,
		.overviewgappo = 30,
		.borderpx = 2,
	};
	server.enable_gaps = gaps;
	server.cursor = &cursor;
	server.selected_monitor = &mon;
	server.grab_client = NULL;
	server.start_drag_window = false;
	wl_list_init(&server.clients);
	pertag = (Pertag){.ltidxs = {&stage_layout}};
	pertag.stage_split_x[0] = split_x;
	pertag.stage_split_y[0] = split_y;
	pertag.stage_flip[0] = flip;
	other_pertag = (Pertag){.ltidxs = {&tile_layout}};
	mon = (Monitor){
		.m = {0, 0, 2560, 1440},
		.w = {0, 0, 2560, 1440},
		.gappih = 10,
		.gappiv = 10,
		.gappoh = 10,
		.gappov = 10,
		.tagset = {1, 1},
		.pertag = &pertag,
		.wlr_output = &output,
	};
	other = mon;
	other.m.x = other.w.x = 2560;
	other.pertag = &other_pertag;
	memset(clients, 0, sizeof(clients));
	for (int32_t i = 0; i < NCLIENTS; i++) {
		xdg[i] = (struct wlr_xdg_surface){.geometry = {0, 0, 800, 600}};
		wl_list_init(&xdg[i].popups);
		clients[i] = (Client){
			.type = XDGShell,
			.mon = &mon,
			.tags = 1,
			.bw = 2,
			.geom = {0, 0, 804, 604},
			.surface.xdg = &xdg[i],
		};
	}
	mon.sel = &clients[0];
	arranges = notifies = 0;
	ipc_dispatch_result = (struct ipc_dispatch_result){-1, NULL};
}

static Client *add_tile(int32_t i) {
	wl_list_insert(server.clients.prev, &clients[i].link);
	return &clients[i];
}

/* A staged card at box, as the side columns hold them. */
static Client *add_card(int32_t i, struct wlr_box box) {
	Client *c = add_tile(i);
	c->isfloating = 1;
	stage_stash(c, box);
	return c;
}

static void set_cursor(double x, double y) {
	cursor.x = x;
	cursor.y = y;
}
