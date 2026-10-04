/* Stage stash geometry on outputs and columns that run out of room. */
#include "../src/layout/stage-geometry.c"
#include "../src/layout/stage.c"
#include <stdio.h>

struct MangoServer server;
Config config;

#define NCLIENTS 160
static Client clients[NCLIENTS];
static Monitor mon;
static Pertag pertag;
/* Geometry inputs as the layout reads them from mon. */
static const struct stage_frame *frame(void) {
	static struct stage_frame f;
	f = stage_frame_of(&mon);
	return &f;
}
static int failures;

/* Records what the stage commits; the real resize clamps to one content pixel
 * and would hide an invalid box. */
void resize(Client *c, struct wlr_box geo, ResizeOpts opts) { c->geom = geo; }
void client_get_geometry(Client *c, struct wlr_box *geom) {
	*geom = (struct wlr_box){0, 0, c->geom.width - 2 * (int32_t)c->bw,
							 c->geom.height - 2 * (int32_t)c->bw};
}
int32_t client_is_x11(Client *c) { return 1; }
void client_set_tiled(Client *c, uint32_t edges) {}
void card_create(Client *c) {}
uint32_t get_mon_curtag(const Monitor *m) { return 0; }

static void check(bool ok, const char *name, const char *detail) {
	printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", name, detail ? " - " : "",
		   detail ? detail : "");
	failures += !ok;
}

static void setup(int32_t w, int32_t h, bool gaps) {
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
		.overviewgappi = 5,
		.overviewgappo = 30,
		.borderpx = 2,
	};
	server.enable_gaps = gaps;
	wl_list_init(&server.clients);
	mon = (Monitor){
		.m = {0, 0, w, h},
		.w = {0, 0, w, h},
		.gappih = 10,
		.gappiv = 10,
		.gappoh = 10,
		.gappov = 10,
		.tagset = {1, 1},
		.pertag = &pertag,
	};
	memset(clients, 0, sizeof(clients));
}

static Client *add_client(int32_t w, int32_t h, uint32_t bw) {
	for (int32_t i = 0; i < NCLIENTS; i++) {
		Client *c = &clients[i];
		if (c->mon)
			continue;
		c->mon = &mon;
		c->tags = 1;
		c->bw = bw;
		c->geom =
			(struct wlr_box){0, 0, w + 2 * (int32_t)bw, h + 2 * (int32_t)bw};
		wl_list_insert(server.clients.prev, &c->link);
		return c;
	}
	abort();
}

/* A committed stash is valid when it can be drawn and grabbed: positive finite
 * scale matching the box, a box on the usable area, off the center square,
 * and no smaller than a stage_dock_tiny card. */
static bool stash_valid(Client *c, char *detail, size_t len) {
	struct wlr_box s = stage_center_box(frame());
	struct wlr_box *u = &mon.w;
	struct wlr_box *g = &c->geom;
	int32_t bw2 = 2 * (int32_t)c->bw;
	float sc = c->stage_scale;
	int32_t longest = MANGO_MAX(c->stage_lw, c->stage_lh);
	float floor_px = MANGO_MIN((float)config.stage_dock_tiny,
							   longest * config.stage_scale_max);
	snprintf(detail, len,
			 "scale %.4f, box %d,%d %dx%d, logical %dx%d, center x %d-%d", sc,
			 g->x, g->y, g->width, g->height, c->stage_lw, c->stage_lh, s.x,
			 s.x + s.width);
	struct wlr_box overlap;
	return c->isstaged && isfinite(sc) && sc > 0.f && g->width > bw2 &&
		   g->height > bw2 &&
		   abs(g->width - ((int32_t)roundf(c->stage_lw * sc) + bw2)) <= 1 &&
		   abs(g->height - ((int32_t)roundf(c->stage_lh * sc) + bw2)) <= 1 &&
		   longest * sc >= floor_px - 1.f && g->x >= u->x && g->y >= u->y &&
		   g->x + g->width <= u->x + u->width &&
		   g->y + g->height <= u->y + u->height &&
		   !wlr_box_intersection(&overlap, g, &s);
}

static void check_stash(const char *name, Client *c) {
	char detail[160];
	check(stash_valid(c, detail, sizeof(detail)), name, detail);
}

static void check_all_staged(const char *name) {
	char detail[200] = "";
	int32_t bad = 0, n = 0;
	for (int32_t i = 0; i < NCLIENTS; i++) {
		Client *c = &clients[i];
		if (!c->mon || !c->isstaged)
			continue;
		n++;
		char d[160];
		if (!stash_valid(c, d, sizeof(d)) && bad++ == 0)
			snprintf(detail, sizeof(detail), "client %d: %s", i, d);
	}
	char summary[400];
	snprintf(summary, sizeof(summary), "%d/%d valid%s%s", n - bad, n,
			 bad ? "; first bad " : "", detail);
	check(n > 0 && bad == 0, name, summary);
}

/* The fifth tiled window on an output: stage() hands it to stage_stash_free. */
static void fifth_tile(const char *name, int32_t w, int32_t h, bool gaps) {
	setup(w, h, gaps);
	Client *c = add_client(1200, 900, 2);
	stage_stash_free(&mon, c);
	check_stash(name, c);
}

static void stash_column(int32_t w, int32_t h, bool gaps, int32_t n,
						 uint32_t bw) {
	setup(w, h, gaps);
	for (int32_t i = 0; i < n; i++) {
		Client *c = add_client(1200, 900, bw);
		c->isfloating = 1;
		c->geom.y = i;
		stage_stash(c, c->geom);
	}
	/* What a shake does once the grabbed window's neighbours are stashed. */
	stage_align_sides(&mon, NULL);
}

int main(void) {
	fifth_tile("portrait fifth tile, gaps on", 1080, 1920, true);
	fifth_tile("portrait fifth tile, gaps off", 1080, 1920, false);
	fifth_tile("square fifth tile, gaps on", 1920, 1920, true);
	fifth_tile("square fifth tile, gaps off", 1920, 1920, false);
	fifth_tile("side narrower than gaps and borders", 1040, 1024, true);
	fifth_tile("narrow landscape, gaps on", 1100, 1024, true);
	fifth_tile("narrow landscape, gaps off", 1100, 1024, false);

	/* Ordinary wide output keeps its existing placement. */
	setup(2560, 1440, true);
	Client *wide = add_client(1200, 900, 2);
	stage_stash_free(&mon, wide);
	check_stash("wide output fifth tile", wide);
	char detail[96];
	snprintf(detail, sizeof(detail), "box %d,%d %dx%d", wide->geom.x,
			 wide->geom.y, wide->geom.width, wide->geom.height);
	check(wide->geom.x == 10 && wide->geom.y == 10 && wide->geom.width == 540 &&
			  wide->geom.height == 406,
		  "wide output geometry unchanged", detail);
	Client *next = add_client(800, 600, 2);
	stage_stash_free(&mon, next);
	check_stash("wide output sixth tile", next);

	stash_column(2560, 1440, true, 6, 2);
	check_all_staged("wide output, roomy columns");
	snprintf(detail, sizeof(detail), "first box %d,%d %dx%d", clients[0].geom.x,
			 clients[0].geom.y, clients[0].geom.width, clients[0].geom.height);
	check(clients[0].geom.x == 10 && clients[0].geom.y == 40 &&
			  clients[0].geom.width == 540 && clients[0].geom.height == 406,
		  "wide output column geometry unchanged", detail);

	/* 40 slots of 36px with 18px borders leave no content height. */
	stash_column(2560, 1440, false, 80, 18);
	check_all_staged("slot height exactly exhausted by borders");
	/* 150 cards and their gaps need more than the column height. */
	stash_column(2560, 1440, true, 150, 2);
	check_all_staged("gaps exceed column height");
	stash_column(1080, 1920, true, 12, 2);
	check_all_staged("portrait shake alignment");

	/* Alignment is stable: repeating it changes nothing. */
	stash_column(2560, 1440, true, 150, 2);
	struct wlr_box before[NCLIENTS];
	int32_t lw_before[NCLIENTS];
	for (int32_t i = 0; i < NCLIENTS; i++) {
		before[i] = clients[i].geom;
		lw_before[i] = clients[i].stage_lw;
	}
	for (int32_t r = 0; r < 3; r++) {
		stage_align_sides(&mon, NULL);
		for (int32_t i = 0; i < NCLIENTS; i++)
			if (clients[i].mon)
				stage_sync_logical(&clients[i]);
	}
	bool same = true;
	for (int32_t i = 0; i < NCLIENTS; i++)
		same &= wlr_box_equal(&before[i], &clients[i].geom) &&
				lw_before[i] == clients[i].stage_lw;
	check(same, "repeated alignment keeps geometry and logical size", NULL);

	/* Overview dock strip whose gaps alone exceed its height. */
	setup(2560, 1440, true);
	config.overviewgappi = 20;
	for (int32_t i = 0; i < 120; i++) {
		Client *c = add_client(1200, 900, 2);
		c->stage_docked = true;
		c->stage_dock_edge = STAGE_DOCK_LEFT;
		c->overview_member = true;
		c->overview_backup_geom = (struct wlr_box){0, 0, 1200, 900};
	}
	struct wlr_box area = {0, 0, 2560, 1440};
	int32_t taken = stage_overview_strip(&mon, area, STAGE_DOCK_LEFT);
	int32_t bad = 0;
	char first[96] = "";
	for (int32_t i = 0; i < 120; i++) {
		struct wlr_box *g = &clients[i].geom;
		bool ok = g->width > 0 && g->height > 0 && g->y >= area.y &&
				  g->y + g->height <= area.y + area.height && g->x >= area.x &&
				  g->x + g->width <= area.x + taken;
		if (!ok && bad++ == 0)
			snprintf(first, sizeof(first), "card %d: %d,%d %dx%d", i, g->x,
					 g->y, g->width, g->height);
	}
	check(bad == 0, "crowded overview dock strip", first);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
