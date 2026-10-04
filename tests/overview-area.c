/* Overview lays out in the area left by the dock strips while the monitor's
 * work area stays its own for every nested call. */
#include "../src/layout/overview.c"
#include "stage-fixture.h"

#define NOV 140
static Client ov[NOV];
static struct wlr_xdg_surface ov_xdg[NOV];
static int32_t nested_calls, nested_bad;

/* Every resize reached from the layout pass sees the real work area. */
static struct wlr_box real_w;
static void nested(Client *c) {
	nested_calls++;
	nested_bad += !wlr_box_equal(&c->mon->w, &real_w);
}

const char default_jump_labels[] = "asdf";
int32_t client_is_x11_popup(Client *c) { return 0; }
int32_t find_keycodes_for_char(char c, MultiKeycode *k) { return 0; }
void mango_jump_label_node_update(MangoJumpLabel *n, const char *text,
								  float scale) {}
void overview_update_jump_label(Client *c) { nested(c); }

static void ov_setup(int32_t w, struct wlr_box usable) {
	setup(true, 0, 0, false);
	mon.m = (struct wlr_box){0, 0, w, 1440};
	mon.w = usable;
	mon.isoverview = 1;
	real_w = usable;
	nested_calls = nested_bad = 0;
	memset(ov, 0, sizeof(ov));
}

static Client *ov_add(int32_t i, int32_t w, int32_t h) {
	ov_xdg[i] = (struct wlr_xdg_surface){.geometry = {0, 0, w, h}};
	wl_list_init(&ov_xdg[i].popups);
	ov[i] = (Client){
		.type = XDGShell,
		.mon = &mon,
		.tags = 1,
		.bw = 2,
		.surface.xdg = &ov_xdg[i],
		.overview_member = true,
		.overview_backup_geom = {0, 0, w, h},
	};
	wl_list_insert(server.clients.prev, &ov[i].link);
	return &ov[i];
}

static void ov_dock(int32_t i, int32_t edge) {
	Client *c = ov_add(i, 900, 600);
	c->stage_docked = true;
	c->stage_dock_edge = edge;
	c->isminimized = 1;
}

/* Lays out normal windows 0..n-1 and docks on the given edges, then prints
 * every box for comparison. */
static void run(const char *name, int32_t n, int32_t left, int32_t right) {
	for (int32_t i = 0; i < n; i++)
		ov_add(i, 600 + 97 * i, 400 + 53 * (i % 3));
	for (int32_t i = 0; i < left; i++)
		ov_dock(n + i, STAGE_DOCK_LEFT);
	for (int32_t i = 0; i < right; i++)
		ov_dock(n + left + i, STAGE_DOCK_RIGHT);
	mon.visible_clients = n;
	overview(&mon);

	bool valid = true;
	for (int32_t i = 0; i < n + left + right; i++) {
		struct wlr_box *g = &ov[i].geom;
		valid &= g->width > 0 && g->height > 0 && g->x >= mon.m.x &&
				 g->x + g->width <= mon.m.x + mon.m.width;
	}
	char detail[96];
	snprintf(detail, sizeof(detail), "%d nested calls, %d saw another area",
			 nested_calls, nested_bad);
	check(valid && nested_calls > 0 && nested_bad == 0 &&
			  wlr_box_equal(&mon.w, &real_w),
		  name, detail);
}

int main(void) {
	on_resize = nested;
	struct wlr_box panel = {0, 40, 2560, 1400};
	ov_setup(2560, panel);
	run("no docks", 5, 0, 0);
	ov_setup(2560, panel);
	run("left docks", 5, 2, 0);
	ov_setup(2560, panel);
	run("right docks", 5, 0, 3);
	ov_setup(2560, panel);
	run("both edges", 5, 2, 3);
	/* Boxes from before the area was passed explicitly. */
	const struct wlr_box both[] = {{775, 972, 600, 400},
								   {1380, 620, 697, 453},
								   {387, 109, 988, 453},
								   {30, 532, 307, 204},
								   {2222, 847, 307, 204}};
	const int32_t both_at[] = {0, 1, 4, 5, 9};
	bool same = true;
	for (int32_t i = 0; i < 5; i++)
		same &= wlr_box_equal(&ov[both_at[i]].geom, &both[i]);
	check(same, "both edges geometry unchanged", NULL);
	ov_setup(1080, (struct wlr_box){0, 40, 1080, 1400});
	run("dense strip on a narrow output", 4, 120, 0);

	ov_setup(2560, panel);
	mon.ov_tab_layout = 1;
	config.overcircle_center_ratio = 0.5f;
	run("tab overview with docks", 5, 2, 3);
	const struct wlr_box tab_focus = {811, 427, 938, 625};
	check(wlr_box_equal(&ov[0].geom, &tab_focus),
		  "tab overview geometry unchanged", NULL);

	ov_setup(2560, panel);
	mon.is_jump_mode = 1;
	for (int32_t i = 0; i < 5; i++)
		ov_add(i, 800, 600);
	ov_dock(5, STAGE_DOCK_LEFT);
	mon.visible_clients = 5;
	overview(&mon);
	char detail[96];
	snprintf(detail, sizeof(detail), "%d nested calls, %d saw another area",
			 nested_calls, nested_bad);
	check(nested_bad == 0 && ov[0].jump_char == 'a', "jump mode with docks",
		  detail);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
