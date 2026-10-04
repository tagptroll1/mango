/* Center tiles from the layout pass, and drop previews that match where the
 * real drop puts the card. */
#include "stage-fixture.h"

static bool tiles_valid(int32_t n, char *detail, size_t len) {
	struct stage_frame f = stage_frame_of(&mon);
	struct wlr_box area = stage_tile_area(&f), o;
	for (int32_t i = 0; i < n; i++) {
		struct wlr_box *g = &clients[i].geom;
		bool inside = g->x >= area.x && g->y >= area.y &&
					  g->x + g->width <= area.x + area.width &&
					  g->y + g->height <= area.y + area.height;
		if (g->width <= 0 || g->height <= 0 || !inside) {
			snprintf(detail, len, "tile %d: %d,%d %dx%d", i, g->x, g->y,
					 g->width, g->height);
			return false;
		}
		for (int32_t j = 0; j < i; j++)
			if (wlr_box_intersection(&o, g, &clients[j].geom)) {
				snprintf(detail, len, "tiles %d and %d overlap", j, i);
				return false;
			}
	}
	return true;
}

/* Hints the drop of a staged card on target, then drops it there and lays
 * out: the card must land on the hinted box. */
static bool preview_matches(int32_t n, int32_t target, bool after, bool gaps,
							float sx, float sy, bool flip, char *detail,
							size_t len) {
	setup(gaps, sx, sy, flip);
	for (int32_t i = 0; i < n; i++)
		add_tile(i);
	stage(&mon);
	Client *t = &clients[target];
	Client *card = add_card(6, (struct wlr_box){70, 500, 324, 244});
	set_cursor(card->geom.x + 50, card->geom.y + 50);
	stage_begin_move(card, cursor.x, cursor.y);
	server.grab_client = card;
	set_cursor(t->geom.x + (after ? t->geom.width - 2 : 2),
			   t->geom.y + t->geom.height / 2.0);
	t->enable_drop_area_draw = true;
	stage_set_drop_area(t);
	bool hinted = hint.owner == t;
	struct wlr_box want = {0};
	if (hinted)
		want = (struct wlr_box){hint.rect->node.x, hint.rect->node.y,
								hint.rect->width, hint.rect->height};
	resize(card, (struct wlr_box){cursor.x - 50, cursor.y - 50, 100, 100},
		   (ResizeOpts){.interact = 0});
	server.grab_client = NULL;
	stage_drop(card);
	stage(&mon);
	snprintf(detail, len,
			 "n %d target %d %s: hint %d,%d %dx%d, landed %d,%d %dx%d", n,
			 target, after ? "after" : "before", want.x, want.y, want.width,
			 want.height, card->geom.x, card->geom.y, card->geom.width,
			 card->geom.height);
	stage_set_drop_area(t);
	return hinted && !card->isstaged && wlr_box_equal(&want, &card->geom);
}

int main(void) {
	struct wlr_scene *scene = wlr_scene_create();
	server.layers[LyrTile] = wlr_scene_tree_create(&scene->tree);
	const float splits[][2] = {{0, 0}, {0.3f, 0.7f}};
	for (int32_t g = 0; g < 2; g++)
		for (int32_t sp = 0; sp < 2; sp++)
			for (int32_t fl = 0; fl < 2; fl++)
				for (int32_t n = 1; n <= STAGE_CENTER_MAX; n++) {
					char name[96], detail[160] = "";
					snprintf(name, sizeof(name),
							 "gaps %d split %d flip %d, %d tiles", g, sp, fl,
							 n);
					setup(g, splits[sp][0], splits[sp][1], fl);
					for (int32_t i = 0; i < n; i++)
						add_tile(i);
					stage(&mon);
					check(tiles_valid(n, detail, sizeof(detail)), name, detail);

					bool ok = true;
					for (int32_t t = 0; t < n && ok; t++)
						for (int32_t after = 0; after < 2 && ok; after++) {
							ok = preview_matches(n, t, after, g, splits[sp][0],
												 splits[sp][1], fl, detail,
												 sizeof(detail));
						}
					snprintf(
						name, sizeof(name),
						"gaps %d split %d flip %d, %d tiles, drop previews", g,
						sp, fl, n);
					check(ok, name, ok ? NULL : detail);
				}

	/* The arrangement before the split into modules, for one stacked layout
	 * with moved dividers and gaps. */
	setup(true, 0.3f, 0.7f, true);
	for (int32_t i = 0; i < 4; i++)
		add_tile(i);
	stage(&mon);
	const struct wlr_box want[] = {{570, 10, 423, 987},
								   {570, 1007, 423, 423},
								   {1003, 10, 987, 987},
								   {1003, 1007, 987, 423}};
	bool same = true;
	for (int32_t i = 0; i < 4; i++)
		same &= wlr_box_equal(&clients[i].geom, &want[i]);
	check(same, "stacked layout with moved dividers unchanged", NULL);

	wlr_scene_node_destroy(&scene->tree.node);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
