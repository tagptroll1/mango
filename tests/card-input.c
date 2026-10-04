/* Include the renderer so fixtures can use its private surface entries and
 * hit callback without exposing test-only compositor APIs. */
#include "../src/layout/card.c"
#include "mango/animation/client.h"
#include "mango/input/pointer.h"
#include "mango/manage/misc.h"
#include <assert.h>
#include <stdio.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_seat.h>

struct MangoServer server;
Config config;

static Client client;
static struct wlr_surface surface;
static struct wlr_surface other_surface;
static struct wlr_surface *expected_surface;
static struct wlr_scene_surface scene_surface;
static struct wlr_seat seat;
static struct wlr_cursor cursor;
static struct card_surface entry;
static struct wlr_pointer_constraints_v1 constraints;
static double enter_x, enter_y, motion_x, motion_y;
static int enters, motions, failures;

struct wlr_scene_surface *
__wrap_wlr_scene_surface_try_from_buffer(struct wlr_scene_buffer *buffer) {
	return buffer == scene_surface.buffer ? &scene_surface : NULL;
}

void __wrap_wlr_seat_pointer_notify_enter(struct wlr_seat *s,
										  struct wlr_surface *target, double sx,
										  double sy) {
	assert(s == &seat && target == expected_surface);
	enter_x = sx;
	enter_y = sy;
	enters++;
	s->pointer_state.focused_surface = target;
}

void __wrap_wlr_seat_pointer_notify_motion(struct wlr_seat *s, uint32_t time,
										   double sx, double sy) {
	assert(s == &seat);
	motion_x = sx;
	motion_y = sy;
	motions++;
}

void __wrap_wlr_seat_pointer_notify_clear_focus(struct wlr_seat *s) {
	s->pointer_state.focused_surface = NULL;
}

/* Client lookup is independent of the input mapping under test. */
int32_t client_is_x11(Client *c) { return c->type == X11; }
struct wlr_surface *client_surface(Client *c) { return &surface; }
int32_t client_is_unmanaged(Client *c) { return 0; }
int32_t toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc,
								  LayerSurface **pl) {
	if (!s)
		return -1;
	if (pc)
		*pc = &client;
	if (pl)
		*pl = NULL;
	return client.type;
}
Monitor *monitor_at_point(double x, double y) { return NULL; }

/* These branches must stay inactive in the pointer-delivery fixtures. */
void client_focus(Client *c, int32_t lift) { abort(); }
void set_selected_monitor(Monitor *m) { abort(); }
void printstatus(uint32_t mask) { abort(); }
void stage_drag_motion(Client *c, uint32_t time) { abort(); }
Client *stage_tile_at_cursor(Monitor *m, Client *skip) { abort(); }
bool is_stage_layout(Monitor *m) { return false; }
bool is_scroller_layout(Monitor *m) { return false; }
void client_set_drop_area(Client *c) { abort(); }
void resize(Client *c, struct wlr_box geo, ResizeOpts opts) { abort(); }
void resize_tile_client(Client *c, bool isdrag, int32_t x, int32_t y,
						uint32_t time) {
	abort();
}
bool stage_begin_move(Client *c, double x, double y) { abort(); }
void client_set_floating(Client *c, int32_t floating) { abort(); }
void exit_scroller_stack(Client *c) { abort(); }
void set_size_per(Monitor *m, Client *c) { abort(); }
Client *client_group_active(Client *c) { abort(); }
bool client_group_leave(Client *tc) { abort(); }
bool client_is_group_member(const Client *c) { abort(); }
void mango_bar_decoration_set_close_hover(MangoBarDecoration *node,
										  bool hover) {
	abort();
}
bool mango_bar_decoration_close_contains(MangoBarDecoration *node, double lx,
										 double ly) {
	abort();
}

static void setup(int type, float xscale, bool ignore_scale) {
	client = (Client){.type = type, .xwayland_scale = xscale};
	config = (Config){.xwayland_ignore_scale = ignore_scale};
	server = (struct MangoServer){0};
	seat = (struct wlr_seat){0};
	cursor = (struct wlr_cursor){0};
	constraints = (struct wlr_pointer_constraints_v1){0};
	wl_list_init(&constraints.constraints);
	wl_list_init(&client.card.surfaces);
	wl_list_init(&server.clients);
	server.scene = wlr_scene_create();
	for (int i = 0; i < NUM_LAYERS; i++)
		server.layers_wrap[i] = wlr_scene_tree_create(&server.scene->tree);
	server.seat = &seat;
	server.cursor = &cursor;
	server.pointer_constraints = &constraints;
	server.drag_icon = wlr_scene_tree_create(&server.scene->tree);
	server.cursor_hidden = true;
	client.geom = (struct wlr_box){.x = 20, .y = 30};
	client.scene = wlr_scene_tree_create(server.layers_wrap[LyrTile]);
	mango_scene_node_set(&client.scene->node, type, &client);
	wlr_scene_node_set_position(&client.scene->node, 20, 30);
	client.card.tree = wlr_scene_tree_create(client.scene);
	client.card.scale_x = 0.25f;
	client.card.scale_y = 0.5f;
	surface = (struct wlr_surface){0};
	expected_surface = &surface;
	surface.current.width = 1600;
	surface.current.height = 800;
	pixman_region32_init_rect(&surface.input_region, 0, 0, 1600, 800);
	scene_surface = (struct wlr_scene_surface){
		.surface = &surface,
		.buffer = wlr_scene_buffer_create(client.card.tree, NULL),
	};
	scene_surface.buffer->point_accepts_input = card_buffer_point_accepts_input;
	wlr_scene_buffer_set_dest_size(scene_surface.buffer, 400, 400);
	entry = (struct card_surface){
		.c = &client,
		.surface = &surface,
		.scene_surface = &scene_surface,
		.buffer = scene_surface.buffer,
	};
	wl_list_insert(&client.card.surfaces, &entry.link);
	enters = motions = 0;
}

static void finish(void) {
	wlr_scene_node_destroy(&server.scene->tree.node);
	pixman_region32_fini(&surface.input_region);
}

static void expect_motion(const char *name, double x, double y,
						  double expected_x, double expected_y) {
	cursor.x = x;
	cursor.y = y;
	pointer_process_motion(0, NULL, 0, 0, 0, 0);
	bool ok = enters == 1 && motions == 1 &&
			  fabs(enter_x - expected_x) < 0.00001 &&
			  fabs(enter_y - expected_y) < 0.00001 &&
			  fabs(motion_x - expected_x) < 0.00001 &&
			  fabs(motion_y - expected_y) < 0.00001;
	printf("%s: %s (enter %.3f,%.3f; motion %.3f,%.3f; expected %.3f,%.3f)\n",
		   ok ? "PASS" : "FAIL", name, enter_x, enter_y, motion_x, motion_y,
		   expected_x, expected_y);
	failures += !ok;
}

static void expect_rejected(const char *name, double x, double y) {
	cursor.x = x;
	cursor.y = y;
	pointer_process_motion(0, NULL, 0, 0, 0, 0);
	struct wlr_surface *target = &surface;
	node_at_point(x, y, &target, NULL, NULL, NULL, NULL, NULL);
	bool ok = enters == 0 && motions == 0 && target == NULL;
	printf("%s: %s (enter count %d; motion count %d)\n", ok ? "PASS" : "FAIL",
		   name, enters, motions);
	failures += !ok;
}

int main(void) {
	const struct {
		const char *name;
		int type;
		float scale;
		bool ignore_scale;
	} cases[] = {
		{"X11 card, scale 2", X11, 2.f, true},
		{"X11 card, fractional scale", X11, 1.5f, true},
		{"X11 card, scale 1", X11, 1.f, true},
		{"X11 card, ignore-scale disabled", X11, 2.f, false},
		{"Wayland card", XDGShell, 2.f, true},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		setup(cases[i].type, cases[i].scale, cases[i].ignore_scale);
		expect_motion(cases[i].name, 120.25, 80.125, 401, 100.25);
		finish();
	}

	setup(X11, 2.f, true);
	server.cursor_mode = CurPressed;
	seat.pointer_state.focused_surface = &surface;
	expect_motion("pressed drag outside card", 520, -20, 2000, -100);
	finish();

	setup(X11, 2.f, true);
	server.cursor_mode = CurPressed;
	seat.pointer_state.focused_surface = &other_surface;
	expected_surface = &other_surface;
	expect_motion("pressed fallback to non-card surface", 120, 80, 200, 100);
	finish();

	setup(X11, 2.f, true);
	wlr_scene_node_set_enabled(&scene_surface.buffer->node, false);
	const float color[4] = {0, 0, 0, 1};
	wlr_scene_rect_create(client.scene, 400, 400, color);
	expect_motion("rectangle fallback", 120, 80, 400, 100);
	finish();

	setup(X11, 2.f, true);
	wlr_scene_node_set_enabled(&scene_surface.buffer->node, false);
	wlr_scene_rect_create(client.scene, 400, 400, color);
	wl_list_remove(&entry.link);
	expect_motion("rectangle with missing card entry", 120, 80, 200, 100);
	finish();

	setup(X11, 2.f, true);
	client.card.scale_x = 0;
	server.cursor_mode = CurPressed;
	seat.pointer_state.focused_surface = &surface;
	expect_motion("pressed fallback with invalid card scale", 120, 80, 200,
				  100);
	finish();

	setup(X11, 2.f, true);
	client.card.clip_x = 12;
	client.card.clip_y = 18;
	entry.sx = 32;
	entry.sy = 48;
	wlr_scene_node_set_position(&scene_surface.buffer->node, 10, 15);
	expect_motion("clipped subsurface", 120, 80, 380, 70);
	finish();

	setup(X11, 2.f, true);
	pixman_region32_clear(&surface.input_region);
	expect_rejected("rejected input region", 120, 80);
	finish();

	setup(X11, 2.f, true);
	pixman_region32_fini(&surface.input_region);
	pixman_region32_init_rect(&surface.input_region, 390, 90, 30, 30);
	expect_motion("input region uses surface space", 120, 80, 400, 100);
	finish();

	setup(X11, 2.f, true);
	pixman_region32_clear(&surface.input_region);
	struct wlr_scene_rect *background =
		wlr_scene_rect_create(client.scene, 400, 400, color);
	wlr_scene_node_lower_to_bottom(&background->node);
	expect_rejected("rejected input above card rectangle", 120, 80);
	finish();

	const int types[] = {XDGShell, X11};
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		int type = types[i];
		setup(type, 2.f, true);
		scene_surface.buffer->point_accepts_input = NULL;
		client.card.tree = NULL;
		double scale = type == X11 ? 2 : 1;
		expect_motion(type == X11 ? "ordinary X11" : "ordinary Wayland", 120,
					  80, 100 * scale, 50 * scale);
		finish();
	}

	setup(X11, 2.f, true);
	scene_surface.buffer->point_accepts_input = NULL;
	expect_motion("ordinary buffer owned by card client", 120, 80, 200, 100);
	finish();

	setup(X11, 2.f, true);
	Monitor monitor = {.isoverview = true, .tagset = {1, 1}};
	client.mon = &monitor;
	client.tags = 1;
	client.animation.current = (struct wlr_box){20, 30, 400, 400};
	wl_list_insert(&server.clients, &client.link);
	server.selected_monitor = &monitor;
	scene_surface.buffer->point_accepts_input = NULL;
	double ox = 0, oy = 0;
	struct wlr_surface *target = NULL;
	assert(node_at_point(120, 80, &target, NULL, NULL, NULL, &ox, &oy));
	assert(target == &surface && ox == 400 && oy == 100);
	puts("PASS: overview selection supplies matching surface coordinates");
	finish();

	Client no_card = {0};
	double sx = 12, sy = 34;
	assert(!card_surface_coords(NULL, &surface, 0, 0, &sx, &sy));
	assert(!card_surface_coords(&no_card, &surface, 0, 0, &sx, &sy));
	assert(sx == 12 && sy == 34);
	puts("PASS: unavailable card mapping preserves fallback coordinates");

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
