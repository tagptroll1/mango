/* Undocking ends with IPC watchers holding the restored geometry. */
#include "../src/dispatch/stage.c"
#include "../src/layout/stage-geometry.c"
#include "../src/layout/stage.c"
#include <stdio.h>
#include <wlr/types/wlr_cursor.h>

struct MangoServer server;
Config config;
struct ipc_dispatch_result ipc_dispatch_result = {-1, NULL};

static Client client;
static struct wlr_xdg_surface xdg;
static Monitor mon;
static Pertag pertag;
static struct wlr_cursor cursor;
static const Layout stage_layout = {.id = STAGE};
static const Layout tile_layout = {.id = TILE};
static int failures;

/* What a watcher received, copied when it was sent. */
enum stream { ALL_CLIENTS, ONE_CLIENT };
struct event {
	enum stream stream;
	struct wlr_box geom;
	bool docked, minimized;
};
static struct event events[64];
static int nevents;

static void record(enum stream stream) {
	if (nevents < 64)
		events[nevents++] = (struct event){
			stream, client.geom, client.stage_docked, client.isminimized};
}

/* The streams handle_print_status feeds for these masks. */
void printstatus(uint32_t mask) {
	if (mask & IPC_WATCH_ALL_CLIENTS)
		record(ALL_CLIENTS);
	if (mask & IPC_WATCH_CLIENT)
		record(ONE_CLIENT);
}
void ipc_notify_client(Client *c) {
	if (c == &client)
		record(ONE_CLIENT);
}
/* Arrange ends by publishing; the stage layout leaves staged boxes alone. */
void arrange(Monitor *m, bool want_animation, bool from_view) {
	printstatus(IPC_WATCH_ARRANGGE);
}
/* The minimized branch: state, arrange (which publishes), focus. */
void unminimize(Client *c) {
	c->isminimized = 0;
	arrange(c->mon, false, false);
}
void client_pending_minimized_state(Client *c, int32_t isminimized) {
	c->isminimized = isminimized;
}
void resize(Client *c, struct wlr_box geo, ResizeOpts opts) { c->geom = geo; }
void client_get_geometry(Client *c, struct wlr_box *geom) {
	*geom = c->surface.xdg->geometry;
}
int32_t client_is_x11(Client *c) { return 0; }
uint32_t get_mon_curtag(const Monitor *m) { return 0; }
void client_set_tiled(Client *c, uint32_t edges) {}
void client_update_visibility(Client *c) {}
void client_focus(Client *c, int32_t lift) {}
Client *client_focus_top(Monitor *m) { return NULL; }
void card_create(Client *c) {}
void card_destroy(Client *c) {}

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
		.borderpx = 2,
	};
	server.enable_gaps = true;
	server.cursor = &cursor;
	server.selected_monitor = &mon;
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
	xdg = (struct wlr_xdg_surface){.geometry = {0, 0, 800, 600}};
	wl_list_init(&xdg.popups);
	client = (Client){
		.type = XDGShell,
		.mon = &mon,
		.tags = 1,
		.bw = 2,
		.isfloating = 1,
		.geom = {0, 0, 804, 604},
		.surface.xdg = &xdg,
	};
	mon.sel = &client;
	wl_list_insert(&server.clients, &client.link);
}

/* Staged at box, docked on edge, watchers' history cleared. */
static void docked_at(struct wlr_box box, int32_t edge) {
	setup();
	stage_stash(&client, box);
	stage_dock_client(&client, edge, STAGE_DOCK_FULL);
	nevents = 0;
	ipc_dispatch_result = (struct ipc_dispatch_result){-1, NULL};
}

static const struct event *last(enum stream stream) {
	for (int i = nevents - 1; i >= 0; i--)
		if (events[i].stream == stream)
			return &events[i];
	return NULL;
}

/* Each stream's last message matches what a query now returns. */
static void check_streams(const char *name) {
	const char *streams[] = {"all-clients", "client"};
	for (enum stream s = ALL_CLIENTS; s <= ONE_CLIENT; s++) {
		const struct event *e = last(s);
		char label[128], detail[160];
		snprintf(label, sizeof(label), "%s, %s watcher", name, streams[s]);
		snprintf(detail, sizeof(detail),
				 "sent %d,%d %dx%d docked %d min %d, query %d,%d %dx%d",
				 e ? e->geom.x : 0, e ? e->geom.y : 0, e ? e->geom.width : 0,
				 e ? e->geom.height : 0, e ? e->docked : -1,
				 e ? e->minimized : -1, client.geom.x, client.geom.y,
				 client.geom.width, client.geom.height);
		check(e && wlr_box_equal(&e->geom, &client.geom) &&
				  e->docked == client.stage_docked &&
				  e->minimized == client.isminimized,
			  label, detail);
	}
}

/* Restored beside its edge, just outside the shrink zone. */
static void check_restored(const char *name, int32_t edge) {
	char detail[96];
	snprintf(detail, sizeof(detail), "box %d,%d %dx%d, result %d %s",
			 client.geom.x, client.geom.y, client.geom.width,
			 client.geom.height, ipc_dispatch_result.changed,
			 ipc_dispatch_result.reason ? ipc_dispatch_result.reason : "-");
	bool at_edge = edge == STAGE_DOCK_LEFT
					   ? abs(client.geom.x - config.stage_shrink_zone) <= 1
					   : abs(client.geom.x + client.geom.width -
							 (mon.m.width - config.stage_shrink_zone)) <= 1;
	check(at_edge && !client.stage_docked && !client.isminimized &&
			  client.isstaged && ipc_dispatch_result.changed == 1,
		  name, detail);
	check_streams(name);
}

int main(void) {
	docked_at((struct wlr_box){20, 500, 324, 244}, STAGE_DOCK_LEFT);
	stage_undock(&(Arg){.tc = &client});
	check_restored("left undock dispatch", STAGE_DOCK_LEFT);

	docked_at((struct wlr_box){2216, 500, 324, 244}, STAGE_DOCK_RIGHT);
	stage_dock_toggle(&(Arg){.tc = &client});
	check_restored("right dock toggle", STAGE_DOCK_RIGHT);

	/* The tiny preview a drag released on the edge leaves behind. */
	docked_at((struct wlr_box){0, 700, 68, 52}, STAGE_DOCK_LEFT);
	stage_undock(&(Arg){0});
	check_restored("small preview, selected client", STAGE_DOCK_LEFT);

	/* What an overview pick calls. */
	docked_at((struct wlr_box){2216, 500, 324, 244}, STAGE_DOCK_RIGHT);
	ipc_dispatch_result = (struct ipc_dispatch_result){
		stage_undock_window(&client) == STAGE_CHANGED, NULL};
	check_restored("overview pick", STAGE_DOCK_RIGHT);

	/* The layout changed under the dock: restored without a stage box. */
	docked_at((struct wlr_box){20, 500, 324, 244}, STAGE_DOCK_LEFT);
	pertag.ltidxs[0] = &tile_layout;
	stage_undock(&(Arg){.tc = &client});
	check(!client.stage_docked && !client.isminimized &&
			  ipc_dispatch_result.changed == 1,
		  "layout changed undock", NULL);
	check_streams("layout changed undock");

	docked_at((struct wlr_box){20, 500, 324, 244}, STAGE_DOCK_LEFT);
	stage_undock(&(Arg){.tc = &client});
	nevents = 0;
	struct wlr_box before = client.geom;
	stage_undock(&(Arg){.tc = &client});
	check(ipc_dispatch_result.changed == 0 && ipc_dispatch_result.reason &&
			  !strcmp(ipc_dispatch_result.reason, "not-docked") &&
			  nevents == 0 && wlr_box_equal(&before, &client.geom),
		  "already undocked", NULL);

	server.selected_monitor = NULL;
	stage_undock(&(Arg){0});
	check(ipc_dispatch_result.changed == 0 && ipc_dispatch_result.reason &&
			  !strcmp(ipc_dispatch_result.reason, "no-client") && nevents == 0,
		  "no target", NULL);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
