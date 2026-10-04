#ifndef __LAYOUT_CARD_H__
#define __LAYOUT_CARD_H__ 1

#include "mango/common/types.h"
#include <scenefx/types/fx/clipped_region.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct wlr_surface;
struct wlr_scene_buffer;

// A card draws a client's surfaces (including subsurfaces) scaled into its own
// scene tree in place of the real surface tree, bound directly to the
// textures and re-laid out on every commit. Stage stashes and overview both
// render through it; neither kind of state lives here.

// Renderer state a client owns while it has a card. tree is the card's
// existence: card_create sets it and initializes the lists, card_destroy
// empties them and clears it, and nothing reads the lists without a tree, so
// zeroed state is a valid "no card".
struct card_state {
	struct wlr_scene_tree *tree;
	struct wl_list surfaces; // card.c's private per-surface entries
	struct wl_list popups;	 // Popup.card_link
	// Surface-local to card coordinates, from the last layout.
	float scale_x, scale_y;
	int32_t clip_x, clip_y;
};

// Hides the real surface tree and builds the card; no-op if one exists or the
// surface is unmapped.
void card_create(Client *c);
// Drops the card and its popups and shows the real surface tree again.
void card_destroy(Client *c);
// Scale and position from the current animated geometry.
void card_layout(Client *c);
void card_set_radii(Client *c, struct fx_corner_radii corners);

// Successful hits on these buffers already return surface-local coordinates.
bool card_owns_buffer(struct wlr_scene_buffer *buffer);
// Maps a layout point to surface-local coordinates, including outside grabs.
// Failure leaves sx/sy unchanged so callers can use their ordinary fallback.
bool card_surface_coords(Client *c, struct wlr_surface *s, double lx, double ly,
						 double *sx, double *sy);

// Direct popups of a card client stay unscaled, anchored to the scaled
// content. Create returns false when the popup takes the normal path.
bool card_popup_create(Popup *popup);
void card_popup_destroy(Popup *popup);

#endif
