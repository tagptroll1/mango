#include "mango/config/stage.h"
#include "mango/config/parse.h"
#include "mango/layout/stage.h"
#include <stdlib.h>
#include <string.h>

bool stage_parse_option(struct stage_config *s, const char *key,
						const char *value) {
	if (strncmp(key, "stage_", 6) != 0)
		return false;
	key += 6;
	if (strcmp(key, "aspect") == 0) {
		s->aspect = atof(value);
	} else if (strcmp(key, "scale_max") == 0) {
		s->scale_max = atof(value);
	} else if (strcmp(key, "scale_min") == 0) {
		s->scale_min = atof(value);
	} else if (strcmp(key, "scale_curve") == 0) {
		s->scale_curve = atof(value);
	} else if (strcmp(key, "shrink_zone") == 0) {
		s->shrink_zone = atoi(value);
	} else if (strcmp(key, "dock_tiny") == 0) {
		s->dock_tiny = atoi(value);
	} else if (strcmp(key, "dock_zone") == 0) {
		s->dock_zone = atoi(value);
	} else if (strcmp(key, "dock_mini_zone") == 0) {
		s->dock_mini_zone = atoi(value);
	} else if (strcmp(key, "overview_dock_ratio") == 0) {
		s->overview_dock_ratio = atof(value);
	} else if (strcmp(key, "shake_flips") == 0) {
		s->shake_flips = atoi(value);
	} else if (strcmp(key, "shake_travel") == 0) {
		s->shake_travel = atoi(value);
	} else if (strcmp(key, "shake_window_ms") == 0) {
		s->shake_window_ms = atoi(value);
	} else if (strcmp(key, "text_zoom") == 0) {
		s->text_zoom = atof(value);
	} else {
		return false;
	}
	return true;
}

void stage_config_defaults(struct stage_config *s) {
	*s = (struct stage_config){
		.aspect = 1.0f,
		.scale_max = 0.8f,
		.scale_min = 0.6f,
		.scale_curve = 1.0f,
		.shrink_zone = 100,
		.dock_tiny = 64,
		.dock_zone = 48,
		.dock_mini_zone = 8,
		.overview_dock_ratio = 0.12f,
		.shake_flips = 4,
		.shake_travel = 40,
		.shake_window_ms = 800,
		.text_zoom = 1.0f,
	};
}

// Stage falloff and shrink math divide by these gaps, so keep them ordered.
void stage_config_clamp(struct stage_config *s) {
	s->aspect = CLAMP_FLOAT(s->aspect, 0.5f, 4.0f);
	s->scale_max = CLAMP_FLOAT(s->scale_max, 0.05f, 1.0f);
	s->scale_min = CLAMP_FLOAT(s->scale_min, 0.05f, s->scale_max);
	s->scale_curve = CLAMP_FLOAT(s->scale_curve, 0.1f, 10.0f);
	s->dock_mini_zone = CLAMP_INT(s->dock_mini_zone, 0, 1000);
	s->dock_zone = CLAMP_INT(s->dock_zone, s->dock_mini_zone, 1000);
	s->shrink_zone = CLAMP_INT(s->shrink_zone, s->dock_zone + 1, 2000);
	s->dock_tiny = CLAMP_INT(s->dock_tiny, 8, 1000);
	s->overview_dock_ratio = CLAMP_FLOAT(s->overview_dock_ratio, 0.02f, 0.5f);
	s->shake_flips = CLAMP_INT(s->shake_flips, 2, STAGE_SHAKE_FLIPS_MAX);
	s->shake_travel = CLAMP_INT(s->shake_travel, 1, 1000);
	s->shake_window_ms = CLAMP_INT(s->shake_window_ms, 50, 10000);
	s->text_zoom = CLAMP_FLOAT(s->text_zoom, 0.5f, 4.0f);
}
