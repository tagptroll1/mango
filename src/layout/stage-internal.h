#ifndef __LAYOUT_STAGE_INTERNAL_H__
#define __LAYOUT_STAGE_INTERNAL_H__ 1

// Shared between the stage's own files: geometry (stage-geometry.c), layout
// and transitions (stage.c), and pointer interaction (stage-drag.c).

#include "mango/layout/stage.h"
#include <stdbool.h>
#include <stdint.h>
#include <wlr/util/box.h>

#define STAGE_CENTER_MAX 4

// Geometry. Everything a calculation reads is in the frame or its arguments:
// no client list, no stage history, no scene or IPC effects.

// Inputs taken from a monitor and the config once (stage_frame_of).
struct stage_frame {
	struct wlr_box mon;	   // Whole output: side falloff and edge shrink.
	struct wlr_box usable; // Work area: center height, clamps, columns.
	// Gaps, 0 without gaps: between tiles and cards, and the outer ones.
	int32_t gap_in, gap_out_h, gap_out_v;
	// Divider shares (0 for half) and stacked instead of side by side.
	float split_x, split_y;
	bool flip;
	int32_t borderpx;
	// Tunables, documented under Stage Layout in
	// docs/window-management/layouts.md.
	float scale_max, scale_min, scale_curve;
	int32_t shrink_zone, dock_tiny, dock_zone, dock_mini_zone;
	float overview_dock_ratio;
	int32_t overview_gap_in, overview_gap_out;
};

// A card's content size and border.
struct stage_card {
	int32_t lw, lh, bw;
};

float stage_split(float f);
struct wlr_box stage_center_box(const struct stage_frame *f);
bool stage_in_center(const struct stage_frame *f, double x, double y);
struct wlr_box stage_tile_area(const struct stage_frame *f);
// Boxes for n center tiles, in client list order.
void stage_tile_boxes(const struct stage_frame *f, int32_t n,
					  struct wlr_box *boxes);
void stage_clamp(const struct stage_frame *f, struct wlr_box *b);
float stage_floor_scale(const struct stage_frame *f, int32_t lw, int32_t lh);
float stage_scale_of(const struct stage_frame *f, struct stage_card k,
					 double left, double width);
float stage_scale_at(const struct stage_frame *f, struct stage_card k, double x,
					 double rx);
struct wlr_box stage_box_at(struct stage_card k, double cx, double cy, float s);
struct wlr_box stage_box_beside(const struct stage_frame *f,
								struct stage_card k, double cx, double cy);
void stage_column_boxes(const struct stage_frame *f,
						const struct stage_card *cards, int32_t k, bool left,
						struct wlr_box *boxes);
struct wlr_box stage_free_box(const struct stage_frame *f, struct stage_card k,
							  bool left, int32_t y);
struct wlr_box stage_undock_box(const struct stage_frame *f,
								struct stage_card k, bool left, int32_t y);
int32_t stage_drag_logical(const struct stage_frame *f, int32_t bw);
int32_t stage_dock_edge_at(const struct stage_frame *f, struct wlr_box b,
						   int32_t *tier);
int32_t stage_strip_boxes(const struct stage_frame *f, struct wlr_box area,
						  const float *aspects, int32_t n, bool left,
						  struct wlr_box *boxes);

// Layout and transitions (stage.c).

struct stage_frame stage_frame_of(Monitor *m);
struct stage_card stage_card_of(Client *c);
void stage_notify(Client *c);
void stage_stash(Client *c, struct wlr_box box);
void stage_stash_free(Monitor *m, Client *c);
void stage_align_sides(Monitor *m, Client *skip);
void stage_dock_client(Client *c, int32_t edge, int32_t tier);
int32_t stage_center_count(Monitor *m, Client *skip);

// Interaction (stage-drag.c).

// Drops the drag, shake and hint references to c.
void stage_drag_forget(Client *c);

#endif
