/* Divider resizes, shake, pickup ease, cross-monitor drops, and drag state
 * released when its client or monitor goes away. */
#include "stage-fixture.h"

static Client *grab(Client *c, double x, double y) {
	set_cursor(x, y);
	stage_begin_move(c, x, y);
	server.grab_client = c;
	server.grab_offset_x = (int32_t)round(x) - c->geom.x;
	server.grab_offset_y = (int32_t)round(y) - c->geom.y;
	return c;
}

static void move_to(Client *c, double x, double y, uint32_t time) {
	set_cursor(x, y);
	stage_drag_motion(c, time);
	resize(c, c->float_geom, (ResizeOpts){.interact = 0});
}

int main(void) {
	char detail[160];

	/* Keyboard resize moves the divider by the offset's share of the area. */
	setup(true, 0, 0, false);
	for (int32_t i = 0; i < 4; i++)
		add_tile(i);
	stage(&mon);
	struct stage_frame f = stage_frame_of(&mon);
	struct wlr_box in = stage_tile_area(&f);
	stage_resize_tile(&clients[0], false, 100, -50, 0);
	float want_x = 0.5f + 100.f / (in.width - f.gap_in);
	float want_y = 0.5f - 50.f / (in.height - f.gap_in);
	snprintf(detail, sizeof(detail), "split %.4f,%.4f want %.4f,%.4f",
			 pertag.stage_split_x[0], pertag.stage_split_y[0], want_x, want_y);
	check(fabsf(pertag.stage_split_x[0] - want_x) < 1e-5f &&
			  fabsf(pertag.stage_split_y[0] - want_y) < 1e-5f && arranges == 1,
		  "keyboard resize", detail);

	/* Pointer resize follows the cursor from where the drag began. */
	setup(true, 0, 0, false);
	for (int32_t i = 0; i < 4; i++)
		add_tile(i);
	stage(&mon);
	set_cursor(1280, 720);
	stage_resize_tile(&clients[0], true, 0, 0, 1);
	set_cursor(1280 + 200, 720);
	stage_resize_tile(&clients[0], true, 0, 0, 100);
	want_x = 0.5f + 200.f / (in.width - f.gap_in);
	snprintf(detail, sizeof(detail), "split %.4f want %.4f",
			 pertag.stage_split_x[0], want_x);
	check(fabsf(pertag.stage_split_x[0] - want_x) < 1e-5f &&
			  pertag.stage_split_y[0] == 0.5f,
		  "pointer resize", detail);

	/* Four quick flips of a dragged card stash every center tile. */
	setup(true, 0, 0, false);
	for (int32_t i = 0; i < 4; i++)
		add_tile(i);
	stage(&mon);
	Client *card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	grab(card, 200, 600);
	double xs[] = {300, 200, 300, 200, 300};
	for (int32_t i = 0; i < 5; i++)
		move_to(card, xs[i], 600, 100 + 50 * i);
	int32_t staged = 0;
	for (int32_t i = 0; i < 4; i++)
		staged += clients[i].isstaged;
	check(staged == 4, "shake stashes the center", NULL);

	/* The same moves spread over too long a time do nothing. */
	setup(true, 0, 0, false);
	for (int32_t i = 0; i < 4; i++)
		add_tile(i);
	stage(&mon);
	card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	grab(card, 200, 600);
	for (int32_t i = 0; i < 5; i++)
		move_to(card, xs[i], 600, 100 + 600 * i);
	staged = 0;
	for (int32_t i = 0; i < 4; i++)
		staged += clients[i].isstaged;
	check(staged == 0, "slow moves are not a shake", NULL);

	/* Pickup eases from the old box only with move animations on. */
	setup(true, 0, 0, false);
	config.animations = 1;
	config.animation_duration_move = 1000000;
	struct wlr_scene *scene = wlr_scene_create();
	card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	card->scene = wlr_scene_tree_create(&scene->tree);
	struct wlr_box origin = card->geom;
	grab(card, 200, 600);
	check(stage_drag_morph(card) &&
			  card->animation.current.width <= origin.width &&
			  card->animation.current.width >= 1,
		  "pickup eases with animations", NULL);
	setup(true, 0, 0, false);
	card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	card->scene = wlr_scene_tree_create(&scene->tree);
	grab(card, 200, 600);
	check(!stage_drag_morph(card), "no pickup ease without animations", NULL);

	/* A dragged card unmapping drops every reference: a later drop of another
	 * window is not taken for its drag. */
	setup(true, 0, 0, false);
	card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	grab(card, 200, 600);
	move_to(card, 300, 600, 0);
	stage_forget(card);
	check(!drag.c && !shake.c && !card->isstaged, "unmap during drag", NULL);
	Client *plain = add_tile(5);
	plain->isfloating = 1;
	check(!stage_drop(plain) && !plain->isstaged,
		  "floating window dropped after unmap stays out", NULL);

	/* A preview aimed at a monitor going away is dropped and reported. */
	setup(true, 0, 0, false);
	card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	grab(card, 200, 600);
	move_to(card, 30, 600, 0);
	bool previewing = card->stage_previewing && card->stage_preview_mon == &mon;
	int32_t before = notifies;
	stage_monitor_close(&mon);
	check(previewing && !card->stage_previewing && !card->stage_preview_mon &&
			  notifies > before,
		  "monitor removal clears the preview", NULL);

	/* Dropped on a monitor without the stage: joins its tiling. */
	setup(true, 0, 0, false);
	card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	grab(card, 200, 600);
	card->mon = &other;
	check(!stage_drop(card) && !card->isstaged && !card->isfloating,
		  "drop into an ordinary layout", NULL);

	/* A tile dragged in from another monitor stashes where it lands. */
	setup(true, 0, 0, false);
	Client *tile = add_tile(0);
	tile->mon = &other;
	tile->geom = (struct wlr_box){2700, 100, 804, 604};
	check(!stage_begin_move(tile, 2800, 200), "tile layout move is not ours",
		  NULL);
	/* What the pointer code does with a tile it drags. */
	tile->drag_to_tile = true;
	tile->isfloating = 1;
	tile->mon = &mon;
	set_cursor(300, 600);
	resize(tile, (struct wlr_box){250, 550, 100, 100},
		   (ResizeOpts){.interact = 0});
	server.grab_client = NULL;
	bool kept = stage_drop(tile);
	snprintf(detail, sizeof(detail), "box %d,%d %dx%d", tile->geom.x,
			 tile->geom.y, tile->geom.width, tile->geom.height);
	check(kept && tile->isstaged && tile->geom.width > 4 &&
			  wlr_box_contains_point(&tile->geom, 300, 600),
		  "tile from another monitor stashes at the cursor", detail);

	wlr_scene_node_destroy(&scene->tree.node);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
