/* Real scene nodes keep wlroots popup geometry in the anchoring regression. */
#include "../src/layout/card.c"
#include <assert.h>
#include <stdio.h>

static Client client;
static struct wlr_surface parent_surface;
static int failures;

int32_t client_is_x11(Client *c) { return c->type == X11; }
struct wlr_surface *client_surface(Client *c) { return &parent_surface; }
int32_t toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc,
								  LayerSurface **pl) {
	*pc = &client;
	return client.type;
}

static void check_position(const char *name, struct wlr_scene_tree *tree,
						   struct wlr_xdg_surface *xdg, int x, int y) {
	int actual_x, actual_y, surface_x, surface_y;
	wlr_scene_node_coords(&tree->node, &actual_x, &actual_y);
	struct wlr_scene_node *surface_node =
		wl_container_of(tree->children.next, surface_node, link);
	wlr_scene_node_coords(surface_node, &surface_x, &surface_y);
	bool ok = actual_x == x && actual_y == y &&
			  surface_x + xdg->geometry.x == x &&
			  surface_y + xdg->geometry.y == y;
	printf("%s: %s (geometry %d,%d; surface %d,%d; expected %d,%d)\n",
		   ok ? "PASS" : "FAIL", name, actual_x, actual_y, surface_x, surface_y,
		   x, y);
	failures += !ok;
}

int main(void) {
	const struct {
		const char *name;
		int clip_x, clip_y;
		float scale_x, scale_y;
		int popup_x, popup_y;
		int own_x, own_y;
		int expected_x, expected_y;
	} cases[] = {
		{"nonzero parent offsets", 20, 12, .5f, .25f, 101, 83, 0, 0, 254, 324},
		{"parent x offset only", 20, 0, .5f, .25f, 101, 83, 0, 0, 254, 324},
		{"parent y offset only", 0, 12, .5f, .25f, 101, 83, 0, 0, 254, 324},
		{"zero parent offsets", 0, 0, .5f, .25f, 101, 83, 0, 0, 254, 324},
		{"unequal scales", 20, 12, .25f, .75f, 101, 83, 0, 0, 228, 365},
		{"unit scale", 20, 12, 1.f, 1.f, 101, 83, 0, 0, 304, 386},
		{"popup own geometry offsets", 20, 12, .5f, .25f, 101, 83, 7, 11, 254,
		 324},
		{"left output edge", 20, 12, .5f, .25f, -300, 83, 7, 11, 100, 324},
		{"right output edge", 20, 12, .5f, .25f, 1500, 83, 7, 11, 820, 324},
		{"top output edge", 20, 12, .5f, .25f, 101, -600, 7, 11, 254, 200},
		{"bottom output edge", 20, 12, .5f, .25f, 101, 2200, 7, 11, 254, 740},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct wlr_scene *scene = wlr_scene_create();
		Monitor monitor = {.w = {100, 200, 800, 600}};
		client = (Client){
			.type = XDGShell,
			.mon = &monitor,
			.bw = 3,
			.scene = wlr_scene_tree_create(&scene->tree),
			.card.clip_x = cases[i].clip_x,
			.card.clip_y = cases[i].clip_y,
			.card.scale_x = cases[i].scale_x,
			.card.scale_y = cases[i].scale_y,
		};
		client.animation.current = (struct wlr_box){200, 300, 400, 400};
		wlr_scene_node_set_position(&client.scene->node, 200, 300);
		client.card.tree = wlr_scene_tree_create(client.scene);
		wl_list_init(&client.card.popups);

		/* XDG scene placement needs no rendered buffer. */
		struct wlr_surface surface = {0};
		wlr_addon_set_init(&surface.addons);
		surface.current.scale = 1;
		wl_list_init(&surface.current.subsurfaces_above);
		wl_list_init(&surface.current.subsurfaces_below);
		wl_list_init(&surface.current.frame_callback_list);
		wl_list_init(&surface.current_outputs);
		wl_signal_init(&surface.events.commit);
		wl_signal_init(&surface.events.destroy);
		wl_signal_init(&surface.events.map);
		wl_signal_init(&surface.events.unmap);
		wl_signal_init(&surface.events.new_subsurface);
		pixman_region32_init(&surface.opaque_region);
		struct wlr_xdg_surface xdg = {
			.role = WLR_XDG_SURFACE_ROLE_POPUP,
			.surface = &surface,
			.geometry = {cases[i].own_x, cases[i].own_y, 80, 60},
		};
		wl_signal_init(&xdg.events.destroy);
		struct wlr_xdg_popup wlr_popup = {
			.base = &xdg,
			.parent = &parent_surface,
			.current.geometry = {cases[i].popup_x, cases[i].popup_y, 80, 60},
		};
		xdg.popup = &wlr_popup;
		Popup popup = {.wlr_popup = &wlr_popup};
		assert(card_popup_create(&popup));
		struct wlr_scene_tree *tree = surface.data;
		check_position(cases[i].name, tree, &xdg, cases[i].expected_x,
					   cases[i].expected_y);
		assert(wlr_popup.current.geometry.width == 80 &&
			   wlr_popup.current.geometry.height == 60);

		if (i == 6) {
			client.animation.current.x = 350;
			client.animation.current.y = 400;
			wlr_scene_node_set_position(&client.scene->node, 350, 400);
			client.card.scale_x = .75f;
			client.card.scale_y = .5f;
			wlr_popup.current.geometry.x = 61;
			wlr_popup.current.geometry.y = 43;
			wl_signal_emit_mutable(&surface.events.commit, &surface);
			check_position("move and reposition commit", tree, &xdg, 399, 425);
		}

		card_popup_destroy(&popup);
		assert(!popup.card_client && wl_list_empty(&client.card.popups));
		assert(wl_list_empty(&surface.events.commit.listener_list));
		assert(wl_list_empty(&xdg.events.destroy.listener_list));
		card_popup_destroy(&popup);

		wlr_popup.parent = &surface;
		assert(!card_popup_create(&popup));
		wlr_popup.parent = &parent_surface;
		client.card.tree = NULL;
		assert(!card_popup_create(&popup));
		wlr_scene_node_destroy(&scene->tree.node);
		pixman_region32_fini(&surface.opaque_region);
	}
	puts("PASS: popup teardown and nested/non-card routing guards");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
