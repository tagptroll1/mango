/* Real scene buffers expose SceneFX's native-size fallback for empty clips,
 * and the card lifecycle around them. */
#include "../src/layout/card.c"
#include <assert.h>
#include <stdio.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_subcompositor.h>

struct test_surface {
	struct wlr_surface surface;
	struct wlr_client_buffer buffer;
};

static Client client;
static struct test_surface root, sub;
static struct wlr_subsurface subsurface;
static int failures;

int32_t client_is_x11(Client *c) { return c->type == X11; }
struct wlr_surface *client_surface(Client *c) { return &root.surface; }
int32_t toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc,
								  LayerSurface **pl) {
	*pc = &client;
	return client.type;
}
void client_get_clip(Client *c, struct wlr_box *clip) {
	*clip = c->surface.xdg->geometry;
}
void client_get_geometry(Client *c, struct wlr_box *geom) { *geom = c->geom; }
/* Records what the real visibility update would read: a card hides the real
 * surface tree. */
static int visibility_updates;
static bool visibility_saw_card;
void client_update_visibility(Client *c) {
	visibility_updates++;
	visibility_saw_card = c->card.tree != NULL;
}
void client_send_frame_done(Client *c, const struct timespec *now) {}

static void test_buffer_destroy(struct wlr_buffer *buffer) {}
static const struct wlr_buffer_impl test_buffer_impl = {
	.destroy = test_buffer_destroy,
};

static void test_surface_init(struct test_surface *t, int w, int h,
							  int buffer_scale) {
	*t = (struct test_surface){0};
	struct wlr_surface *s = &t->surface;
	wlr_addon_set_init(&s->addons);
	s->mapped = true;
	s->current.scale = buffer_scale;
	s->current.width = w;
	s->current.height = h;
	s->current.buffer_width = w * buffer_scale;
	s->current.buffer_height = h * buffer_scale;
	wl_list_init(&s->current.subsurfaces_above);
	wl_list_init(&s->current.subsurfaces_below);
	wl_list_init(&s->current.frame_callback_list);
	wl_list_init(&s->current_outputs);
	wl_signal_init(&s->events.commit);
	wl_signal_init(&s->events.destroy);
	wl_signal_init(&s->events.map);
	wl_signal_init(&s->events.unmap);
	wl_signal_init(&s->events.new_subsurface);
	pixman_region32_init(&s->opaque_region);
	pixman_region32_init(&s->buffer_damage);
	pixman_region32_init_rect(&s->input_region, 0, 0, w, h);
	wlr_buffer_init(&t->buffer.base, &test_buffer_impl, w * buffer_scale,
					h * buffer_scale);
	s->buffer = &t->buffer;
}

static void test_surface_fini(struct test_surface *t) {
	pixman_region32_fini(&t->surface.opaque_region);
	pixman_region32_fini(&t->surface.buffer_damage);
	pixman_region32_fini(&t->surface.input_region);
}

static void commit(struct test_surface *t) {
	wl_signal_emit_mutable(&t->surface.events.commit, &t->surface);
}

static void place_card(int x, int y, int w, int h) {
	client.animation.current = (struct wlr_box){x, y, w, h};
	wlr_scene_node_set_position(&client.scene->node, x, y);
	card_layout(&client);
}

static void place_sub(int x, int y) {
	subsurface.current.x = x;
	subsurface.current.y = y;
	card_layout(&client);
}

static void check(bool ok, const char *name, const char *detail) {
	printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", name, detail ? " - " : "",
		   detail ? detail : "");
	failures += !ok;
}

static void check_hidden(const char *name, struct test_surface *t) {
	struct card_surface *entry = card_entry_for_surface(&client, &t->surface);
	char detail[96];
	snprintf(detail, sizeof(detail), "enabled %d, dest %dx%d",
			 entry->buffer->node.enabled, entry->buffer->dst_width,
			 entry->buffer->dst_height);
	check(entry && !entry->buffer->node.enabled, name, detail);
}

static void check_visible(const char *name, struct test_surface *t, int x,
						  int y, int w, int h, struct wlr_fbox src) {
	struct card_surface *entry = card_entry_for_surface(&client, &t->surface);
	struct wlr_scene_buffer *b = entry->buffer;
	char detail[160];
	snprintf(detail, sizeof(detail),
			 "enabled %d, pos %d,%d, dest %dx%d, src %.1f,%.1f %.1fx%.1f",
			 b->node.enabled, b->node.x, b->node.y, b->dst_width, b->dst_height,
			 b->src_box.x, b->src_box.y, b->src_box.width, b->src_box.height);
	check(b->node.enabled && b->node.x == x && b->node.y == y &&
			  b->dst_width == w && b->dst_height == h &&
			  wlr_fbox_equal(&b->src_box, &src) && b->buffer == &t->buffer.base,
		  name, detail);
}

static struct wlr_scene_buffer *buffer_at(struct wlr_scene *scene, double lx,
										  double ly, double *sx, double *sy) {
	struct wlr_scene_node *node =
		wlr_scene_node_at(&scene->tree.node, lx, ly, sx, sy);
	return node && node->type == WLR_SCENE_NODE_BUFFER
			   ? wlr_scene_buffer_from_node(node)
			   : NULL;
}

int main(void) {
	struct wlr_scene *scene = wlr_scene_create();
	Monitor monitor = {.m = {0, 0, 1000, 800}};
	/* Window geometry starts 10,5 into the root; content 400x300 at scale 2. */
	test_surface_init(&root, 420, 310, 2);
	test_surface_init(&sub, 100, 80, 1);
	struct wlr_xdg_surface xdg = {
		.surface = &root.surface,
		.geometry = {10, 5, 400, 300},
	};
	subsurface = (struct wlr_subsurface){.surface = &sub.surface};
	wl_list_insert(&root.surface.current.subsurfaces_above,
				   &subsurface.current.link);

	client = (Client){
		.type = XDGShell,
		.mon = &monitor,
		.bw = 2,
		.scene = wlr_scene_tree_create(&scene->tree),
		.surface.xdg = &xdg,
	};
	mango_scene_node_set(&client.scene->node, client.type, &client);

	/* Card content 200x150 at 102,102: half scale on both axes. */
	subsurface.current.x = 110;
	subsurface.current.y = 45;
	card_create(&client);
	check(client.card.tree && wl_list_length(&client.card.surfaces) == 2 &&
			  visibility_updates == 1 && visibility_saw_card,
		  "create builds entries and hides the real surface", NULL);
	place_card(100, 100, 204, 154);
	/* Commits attach the buffers through the real scene_surface handler. */
	commit(&root);
	commit(&sub);
	struct wlr_fbox root_full = {20, 10, 800, 600};
	struct wlr_fbox sub_full = {0, 0, 100, 80};
	check_visible("fully visible root", &root, 0, 0, 200, 150, root_full);
	check_visible("fully visible subsurface", &sub, 50, 20, 50, 40, sub_full);

	struct card_surface *sub_entry =
		card_entry_for_surface(&client, &sub.surface);
	double sx, sy;
	struct wlr_scene_buffer *hit =
		buffer_at(scene, 102 + 55, 102 + 25, &sx, &sy);
	check(hit == sub_entry->buffer && sx == 10 && sy == 10,
		  "visible subsurface input maps to surface space", NULL);

	place_sub(370, 45);
	check_visible("partially clipped subsurface", &sub, 180, 20, 20, 40,
				  (struct wlr_fbox){0, 0, 40, 80});
	hit = buffer_at(scene, 102 + 185, 102 + 30, &sx, &sy);
	check(hit == sub_entry->buffer && sx == 10 && sy == 20,
		  "partially clipped subsurface input", NULL);

	place_sub(500, 45);
	check_hidden("subsurface wholly outside card clip", &sub);
	place_sub(-90, 45);
	check_hidden("subsurface clipped to zero width", &sub);
	place_sub(110, -75);
	check_hidden("subsurface clipped to zero height", &sub);

	commit(&sub);
	check_hidden("subsurface commit while hidden", &sub);

	place_sub(110, 45);
	check_visible("hidden subsurface restored", &sub, 50, 20, 50, 40, sub_full);

	/* Root partly off the left monitor edge: 48 card pixels are cut. */
	place_card(-50, 100, 204, 154);
	check_visible("partially clipped root", &root, 48, 0, 152, 150,
				  (struct wlr_fbox){212, 10, 608, 600});

	place_card(2000, 100, 204, 154);
	check_hidden("root without monitor intersection", &root);
	check_hidden("subsurface of off-monitor root", &sub);
	commit(&root);
	commit(&sub);
	check_hidden("root commit while hidden", &root);
	check_hidden("subsurface commit while root hidden", &sub);

	place_card(100, 100, 204, 154);
	check_visible("off-monitor root restored", &root, 0, 0, 200, 150,
				  root_full);
	check_visible("off-monitor subsurface restored", &sub, 50, 20, 50, 40,
				  sub_full);

	/* 2px card content: the subsurface scales to 0x0 integer pixels. */
	place_sub(10, 5);
	place_card(100, 100, 6, 6);
	check_hidden("subsurface truncated to zero by tiny scale", &sub);
	hit = buffer_at(scene, 102.1, 102.1, &sx, &sy);
	check(hit != sub_entry->buffer, "truncated subsurface takes no input",
		  NULL);

	for (int i = 0; i < 3; i++) {
		place_card(2000, 100, 204, 154);
		place_sub(500, 45);
		place_card(100, 100, 204, 154);
		place_sub(110, 45);
	}
	check_visible("root after repeated transitions", &root, 0, 0, 200, 150,
				  root_full);
	check_visible("subsurface after repeated transitions", &sub, 50, 20, 50, 40,
				  sub_full);

	int commit_listeners =
		wl_list_length(&root.surface.events.commit.listener_list);
	card_create(&client);
	check(wl_list_length(&client.card.surfaces) == 2 &&
			  wl_list_length(&root.surface.events.commit.listener_list) ==
				  commit_listeners,
		  "repeated create adds nothing", NULL);

	card_destroy(&client);
	check(!client.card.tree && !visibility_saw_card &&
			  wl_list_empty(&root.surface.events.commit.listener_list) &&
			  wl_list_empty(&root.surface.events.destroy.listener_list) &&
			  wl_list_empty(&sub.surface.events.commit.listener_list) &&
			  wl_list_empty(&sub.surface.events.destroy.listener_list),
		  "card teardown leaves no surface listeners", NULL);
	int updates = visibility_updates;
	card_destroy(&client);
	check(!client.card.tree && visibility_updates == updates,
		  "repeated destroy is a no-op", NULL);

	/* Remap: a later create rebuilds from the live surfaces. */
	card_create(&client);
	place_card(100, 100, 204, 154);
	check_visible("recreated root", &root, 0, 0, 200, 150, root_full);
	check_visible("recreated subsurface", &sub, 50, 20, 50, 40, sub_full);

	/* Subsurface destroyed under a live card, then a new one appears. */
	wl_list_remove(&subsurface.current.link);
	wl_signal_emit_mutable(&sub.surface.events.destroy, &sub.surface);
	place_card(100, 100, 204, 154);
	check(wl_list_length(&client.card.surfaces) == 1 &&
			  !card_entry_for_surface(&client, &sub.surface),
		  "destroyed subsurface leaves no entry", NULL);
	check_visible("root after subsurface destroyed", &root, 0, 0, 200, 150,
				  root_full);
	test_surface_fini(&sub);
	test_surface_init(&sub, 100, 80, 1);
	wl_list_insert(&root.surface.current.subsurfaces_above,
				   &subsurface.current.link);
	place_card(100, 100, 204, 154);
	commit(&sub);
	check_visible("new subsurface joins the card", &sub, 50, 20, 50, 40,
				  sub_full);

	card_destroy(&client);
	check(wl_list_empty(&root.surface.events.commit.listener_list) &&
			  wl_list_empty(&sub.surface.events.commit.listener_list) &&
			  wl_list_empty(&sub.surface.events.destroy.listener_list),
		  "teardown after subsurface churn", NULL);

	/* Unmapped: no card, nothing hooked. */
	root.surface.mapped = false;
	updates = visibility_updates;
	card_create(&client);
	check(!client.card.tree && visibility_updates == updates &&
			  wl_list_empty(&root.surface.events.commit.listener_list),
		  "unmapped surface gets no card", NULL);
	root.surface.mapped = true;

	/* A client that never had a card: lists never initialized. */
	Client blank = {.scene = client.scene, .surface.xdg = &xdg};
	card_layout(&blank);
	card_set_radii(&blank, (struct fx_corner_radii){0});
	card_destroy(&blank);
	check(!blank.card.tree, "zeroed card state means no card", NULL);

	wlr_scene_node_destroy(&scene->tree.node);
	test_surface_fini(&root);
	test_surface_fini(&sub);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
