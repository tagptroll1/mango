#ifndef __CONFIG_STAGE_H__
#define __CONFIG_STAGE_H__ 1

#include <stdbool.h>
#include <stdint.h>

// Config.stage: the stage_* options.
struct stage_config {
	float aspect;
	float scale_max;
	float scale_min;
	float scale_curve;
	int32_t shrink_zone;
	int32_t dock_tiny;
	int32_t dock_zone;
	int32_t dock_mini_zone;
	float overview_dock_ratio;
	int32_t shake_flips;
	int32_t shake_travel;
	int32_t shake_window_ms;
	float text_zoom;
};

// False when key is not a stage option.
bool stage_parse_option(struct stage_config *s, const char *key,
						const char *value);
void stage_config_defaults(struct stage_config *s);
void stage_config_clamp(struct stage_config *s);

#endif
