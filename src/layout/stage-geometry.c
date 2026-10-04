#include "mango/common/util.h"
#include "stage-internal.h"
#include <math.h>

// Side room a dock_tiny card needs, gaps and borders included.
static int32_t stage_side_room(const struct stage_frame *f) {
	return f->dock_tiny + 2 * f->borderpx + f->gap_in +
		   MANGO_MAX(f->gap_in, f->gap_out_h);
}

// Full height and square where the output allows, narrowed so each side keeps
// room for at least one floor-sized card.
struct wlr_box stage_center_box(const struct stage_frame *f) {
	const struct wlr_box *u = &f->usable;
	int32_t room = stage_side_room(f);
	int32_t side = MANGO_MAX(1, MANGO_MIN(f->mon.height, u->width - 2 * room));
	int32_t x = f->mon.x + (f->mon.width - side) / 2;
	x = MANGO_MIN(x, u->x + u->width - room - side);
	x = MANGO_MAX(x, u->x + MANGO_MIN(room, (u->width - side) / 2));
	return (struct wlr_box){
		.x = x, .y = u->y, .width = side, .height = u->height};
}

// Smallest scale a stashed card takes: its longest side at dock_tiny.
float stage_floor_scale(const struct stage_frame *f, int32_t lw, int32_t lh) {
	int32_t longest = MANGO_MAX(lw, lh);
	return MANGO_MIN(f->scale_max, (float)f->dock_tiny / longest);
}

bool stage_in_center(const struct stage_frame *f, double x, double y) {
	struct wlr_box s = stage_center_box(f);
	return wlr_box_contains_point(&s, x, y);
}

// Keeps a stashed box on the usable area, so nothing is dragged out of reach.
void stage_clamp(const struct stage_frame *f, struct wlr_box *b) {
	const struct wlr_box *u = &f->usable;
	b->x = MANGO_MIN(b->x, u->x + u->width - b->width);
	b->y = MANGO_MIN(b->y, u->y + u->height - b->height);
	b->x = MANGO_MAX(b->x, u->x);
	b->y = MANGO_MAX(b->y, u->y);
}

// Scale for a box at left with this width: the side falloff follows its
// center and the edge shrink its nearer side, so the grab point never matters.
float stage_scale_of(const struct stage_frame *f, struct stage_card k,
					 double left, double width) {
	struct wlr_box s = stage_center_box(f);
	const struct wlr_box *m = &f->mon;
	double x = left + width / 2.0;
	double t = 0;
	if (x < s.x && s.x > m->x)
		t = (s.x - x) / (double)(s.x - m->x);
	else if (x > s.x + s.width && m->x + m->width > s.x + s.width)
		t = (x - (s.x + s.width)) / (double)(m->x + m->width - (s.x + s.width));
	t = MANGO_MAX(0.0, MANGO_MIN(1.0, t));
	// Under 1 shrinks sooner near the center, over 1 holds size longer. A
	// power curve would jump at the square's edge, so warp with a finite slope
	// and smoothstep so the shrink eases in and out.
	double c = f->scale_curve;
	t = t / (t + c * (1.0 - t));
	t = t * t * (3.0 - 2.0 * t);
	// Interpolating in log space shrinks by the same share per pixel, which
	// reads as linear; plain linear scale speeds up visibly as boxes get small.
	double lbase =
		log(f->scale_max) + (log(f->scale_min) - log(f->scale_max)) * t;

	double d = MANGO_MIN(left - m->x, m->x + m->width - (left + width));
	int32_t longest = MANGO_MAX(k.lw, k.lh);
	if (d >= f->shrink_zone || longest <= 0)
		return (float)exp(lbase);
	double ltiny = MANGO_MIN(lbase, log((double)f->dock_tiny / longest));
	double u =
		(f->shrink_zone - d) / (double)(f->shrink_zone - f->dock_mini_zone);
	u = MANGO_MIN(1.0, u);
	// Smoothstep so the rate changes gradually entering and leaving the zone.
	u = u * u * (3.0 - 2.0 * u);
	return (float)exp(lbase + (ltiny - lbase) * u);
}

// Scale for a box holding x at share rx of its width. Box and scale depend on
// each other; scale minus stage_scale_of rises with scale, so bisect for where
// they agree.
float stage_scale_at(const struct stage_frame *f, struct stage_card k, double x,
					 double rx) {
	double bw2 = 2.0 * k.bw;
	double lo = 0.0, hi = f->scale_max;
	for (int32_t i = 0; i < 24; i++) {
		double s = (lo + hi) / 2.0;
		double w = k.lw * s + bw2;
		if (s < stage_scale_of(f, k, x - rx * w, w))
			lo = s;
		else
			hi = s;
	}
	return (float)hi;
}

struct wlr_box stage_box_at(struct stage_card k, double cx, double cy,
							float s) {
	int32_t w = (int32_t)roundf(k.lw * s) + 2 * k.bw;
	int32_t h = (int32_t)roundf(k.lh * s) + 2 * k.bw;
	return (struct wlr_box){
		.x = (int32_t)round(cx - w / 2.0),
		.y = (int32_t)round(cy - h / 2.0),
		.width = w,
		.height = h,
	};
}

// Box centered on cx, cy, or moved beside the center square when that would
// cover it. A side too narrow for the box leaves it to stage_clamp.
struct wlr_box stage_box_beside(const struct stage_frame *f,
								struct stage_card k, double cx, double cy) {
	struct wlr_box s = stage_center_box(f), o;
	struct wlr_box b = stage_box_at(k, cx, cy, stage_scale_at(f, k, cx, 0.5));
	if (!wlr_box_intersection(&o, &b, &s))
		return b;
	// Solved with the inner edge pinned, since the move changes the scale.
	bool left = cx < s.x + s.width / 2.0;
	double x = left ? s.x - f->gap_in : s.x + s.width + f->gap_in;
	b = stage_box_at(k, 0, 0, stage_scale_at(f, k, x, left ? 1.0 : 0.0));
	b.x = left ? (int32_t)x - b.width : (int32_t)x;
	b.y = (int32_t)round(cy - b.height / 2.0);
	return b;
}

// Boxes for a column of k stashed cards against the square's edge, one slot
// each, top to bottom.
void stage_column_boxes(const struct stage_frame *f,
						const struct stage_card *cards, int32_t k, bool left,
						struct wlr_box *boxes) {
	struct wlr_box s = stage_center_box(f);
	const struct wlr_box *u = &f->usable;
	int32_t gap = f->gap_in;
	int32_t col_w =
		left ? s.x - u->x - gap - f->gap_out_h
			 : u->x + u->width - (s.x + s.width) - gap - f->gap_out_h;
	int32_t top = u->y + f->gap_out_v;
	int32_t avail_h = u->height - 2 * f->gap_out_v;
	if (k <= 0)
		return;
	int32_t slot_h = (avail_h - (k - 1) * gap) / k;

	// Cards shrink to fit their slot down to the floor scale. A column too
	// crowded for floor cards fans them over its height instead.
	int32_t max_h = 0;
	bool fan = false;
	for (int32_t pass = 0; pass < 2; pass++) {
		int32_t step = k > 1 ? MANGO_MAX(0, (avail_h - max_h) / (k - 1)) : 0;
		for (int32_t i = 0; i < k; i++) {
			struct stage_card c = cards[i];
			int32_t bw2 = 2 * c.bw;
			float sw = (float)(col_w - bw2) / c.lw;
			float sh = (float)(slot_h - bw2) / c.lh;
			float sc = MANGO_MAX(stage_floor_scale(f, c.lw, c.lh),
								 MANGO_MIN(f->scale_max, MANGO_MIN(sw, sh)));
			int32_t w = (int32_t)roundf(c.lw * sc) + bw2;
			int32_t h = (int32_t)roundf(c.lh * sc) + bw2;
			if (pass == 0) {
				max_h = MANGO_MAX(max_h, h);
				fan |= h > slot_h;
				continue;
			}
			boxes[i] = (struct wlr_box){
				.x = left ? s.x - gap - w : s.x + s.width + gap,
				.y = fan ? top + i * step
						 : top + i * (slot_h + gap) + (slot_h - h) / 2,
				.width = w,
				.height = h,
			};
		}
	}
}

// Box against the square's edge on one side, at y or the side's top when y
// is above it or the card would run off the bottom.
struct wlr_box stage_free_box(const struct stage_frame *f, struct stage_card k,
							  bool left, int32_t y) {
	struct wlr_box s = stage_center_box(f);
	const struct wlr_box *u = &f->usable;
	int32_t gap = f->gap_in;
	int32_t top = u->y + f->gap_out_v;
	int32_t side_w = left ? s.x - u->x : u->x + u->width - (s.x + s.width);
	int32_t bw2 = 2 * k.bw;
	float sc = MANGO_MAX(
		stage_floor_scale(f, k.lw, k.lh),
		MANGO_MIN(f->scale_max, (float)(side_w - 2 * gap - bw2) / k.lw));
	int32_t w = (int32_t)roundf(k.lw * sc) + bw2;
	int32_t h = (int32_t)roundf(k.lh * sc) + bw2;
	y = MANGO_MAX(y, top);
	if (y + h > u->y + u->height)
		y = top;
	return (struct wlr_box){
		.x = left ? s.x - gap - w : s.x + s.width + gap,
		.y = y,
		.width = w,
		.height = h,
	};
}

// Back beside its edge at full stash size, just outside the shrink zone so it
// neither redocks nor returns as the tiny drag preview; y is from the output
// top.
struct wlr_box stage_undock_box(const struct stage_frame *f,
								struct stage_card k, bool left, int32_t y) {
	const struct wlr_box *m = &f->mon;
	double ex = left ? m->x + f->shrink_zone : m->x + m->width - f->shrink_zone;
	float s = stage_scale_at(f, k, ex, left ? 0.0 : 1.0);
	struct wlr_box box = stage_box_at(k, 0, 0, s);
	double cx = left ? ex + box.width / 2.0 : ex - box.width / 2.0;
	return stage_box_at(k, cx, m->y + y, s);
}

// A dragged window becomes a square card a quarter of the center, whatever its
// shape, so every drag looks the same; off-stage scales it down from there.
// Returns that card's logical side.
int32_t stage_drag_logical(const struct stage_frame *f, int32_t bw) {
	struct wlr_box a = stage_tile_area(f);
	int32_t side = (MANGO_MIN(a.width, a.height) - f->gap_in) / 2;
	int32_t l = (int32_t)roundf((side - 2 * bw) / f->scale_max);
	return MANGO_MAX(1, l);
}

// Side edge a window released at b is against, or -1 when it is not close.
int32_t stage_dock_edge_at(const struct stage_frame *f, struct wlr_box b,
						   int32_t *tier) {
	int32_t dl = b.x - f->mon.x;
	int32_t dr = f->mon.x + f->mon.width - (b.x + b.width);
	int32_t d = MANGO_MIN(dl, dr);
	if (d > f->dock_zone)
		return -1;
	*tier = d <= f->dock_mini_zone ? STAGE_DOCK_MINI : STAGE_DOCK_FULL;
	return dl <= dr ? STAGE_DOCK_LEFT : STAGE_DOCK_RIGHT;
}

struct wlr_box stage_tile_area(const struct stage_frame *f) {
	struct wlr_box s = stage_center_box(f);
	return (struct wlr_box){
		.x = s.x + f->gap_out_h,
		.y = s.y + f->gap_out_v,
		.width = s.width - 2 * f->gap_out_h,
		.height = s.height - 2 * f->gap_out_v,
	};
}

float stage_split(float f) { return f > 0.f ? f : 0.5f; }

static struct wlr_box stage_cell(struct wlr_box a, struct wlr_box b) {
	struct wlr_box r;
	wlr_box_intersection(&r, &a, &b);
	return r;
}

void stage_tile_boxes(const struct stage_frame *f, int32_t n,
					  struct wlr_box *boxes) {
	struct wlr_box in = stage_tile_area(f);
	int32_t gi = f->gap_in;
	int32_t lw = (int32_t)roundf((in.width - gi) * stage_split(f->split_x));
	int32_t th = (int32_t)roundf((in.height - gi) * stage_split(f->split_y));
	// Either side of the vertical and the horizontal divider.
	struct wlr_box l = {in.x, in.y, lw, in.height};
	struct wlr_box r = {in.x + lw + gi, in.y, in.width - lw - gi, in.height};
	struct wlr_box t = {in.x, in.y, in.width, th};
	struct wlr_box b = {in.x, in.y + th + gi, in.width, in.height - th - gi};

	// Flipped is the same arrangement transposed.
	bool flip = f->flip;
	switch (n) {
	case 1:
		boxes[0] = in;
		break;
	case 2:
		boxes[0] = flip ? t : l;
		boxes[1] = flip ? b : r;
		break;
	case 3:
		boxes[0] = flip ? t : l;
		boxes[1] = flip ? stage_cell(b, l) : stage_cell(r, t);
		boxes[2] = stage_cell(r, b);
		break;
	default:
		boxes[0] = stage_cell(l, t);
		boxes[1] = flip ? stage_cell(l, b) : stage_cell(r, t);
		boxes[2] = flip ? stage_cell(r, t) : stage_cell(l, b);
		boxes[3] = stage_cell(r, b);
		break;
	}
}

// Boxes for n docked cards of these height/width aspects stacked in one
// edge's strip of area; returns the strip width taken, or 0 for none.
int32_t stage_strip_boxes(const struct stage_frame *f, struct wlr_box area,
						  const float *aspects, int32_t n, bool left,
						  struct wlr_box *boxes) {
	if (n == 0)
		return 0;
	int32_t gap = f->overview_gap_in;
	int32_t pad = f->overview_gap_out;
	float strip_w = area.width * f->overview_dock_ratio;
	float total_h = 0.0f;
	for (int32_t i = 0; i < n; i++)
		total_h += strip_w * aspects[i];

	float inner_h = area.height - 2.0f * pad;
	float avail_h = inner_h - gap * (n - 1);
	float s = total_h > avail_h ? avail_h / total_h : 1.0f;
	// Cards shrink to fit down to a dock_tiny width; past that they fan over
	// the strip's height.
	float w = MANGO_MAX(strip_w * s, MANGO_MIN(strip_w, f->dock_tiny));
	float stack_h = total_h * w / strip_w + gap * (n - 1);
	bool fan = stack_h > inner_h;
	float max_h = 0.0f;
	for (int32_t i = 0; i < n; i++)
		max_h = MANGO_MAX(max_h, w * aspects[i]);
	float step = n > 1 ? MANGO_MAX(0.0f, (inner_h - max_h) / (n - 1)) : 0.0f;
	float y = fan ? area.y + pad : area.y + (area.height - stack_h) / 2.0f;
	float x = left ? area.x + pad : area.x + area.width - pad - strip_w;

	for (int32_t i = 0; i < n; i++) {
		float h = w * aspects[i];
		boxes[i] = (struct wlr_box){(int32_t)(x + (strip_w - w) / 2.0f),
									(int32_t)y, (int32_t)w, (int32_t)h};
		y += fan ? step : h + gap;
	}
	return (int32_t)strip_w + pad;
}
