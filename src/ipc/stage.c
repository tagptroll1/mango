#include "mango/ipc/stage.h"
#include "mango/layout/stage.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"
#include <wlr/types/wlr_output.h>

// A preview names its monitor, since the drop can land off the client's own.
static cJSON *stage_dock_json(int32_t edge, int32_t tier, int32_t y,
							  Monitor *m) {
	cJSON *dock = cJSON_CreateObject();
	cJSON_AddStringToObject(dock, "edge",
							edge == STAGE_DOCK_LEFT ? "left" : "right");
	cJSON_AddStringToObject(dock, "tier",
							tier == STAGE_DOCK_MINI ? "mini" : "full");
	cJSON_AddNumberToObject(dock, "y", y);
	if (m)
		cJSON_AddStringToObject(dock, "monitor", m->wlr_output->name);
	return dock;
}

void stage_client_json(Client *c, cJSON *obj) {
	cJSON *dock = c->stage_docked
					  ? stage_dock_json(c->stage_dock_edge, c->stage_dock_tier,
										c->stage_dock_y, NULL)
					  : cJSON_CreateNull();
	cJSON *preview =
		c->stage_previewing && !c->stage_docked
			? stage_dock_json(c->stage_preview_edge, c->stage_preview_tier,
							  c->stage_preview_y, c->stage_preview_mon)
			: cJSON_CreateNull();
	cJSON_AddBoolToObject(obj, "is_staged", c->isstaged);
	cJSON_AddItemToObject(obj, "stage_dock", dock);
	cJSON_AddItemToObject(obj, "stage_dock_preview", preview);
}
