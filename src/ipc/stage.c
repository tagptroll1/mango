#include "mango/ipc/stage.h"
#include "mango/ipc/ipc.h"
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
	cJSON *dock = c->stage.docked
					  ? stage_dock_json(c->stage.dock_edge, c->stage.dock_tier,
										c->stage.dock_y, NULL)
					  : cJSON_CreateNull();
	cJSON *preview =
		c->stage.previewing && !c->stage.docked
			? stage_dock_json(c->stage.preview_edge, c->stage.preview_tier,
							  c->stage.preview_y, c->stage.preview_mon)
			: cJSON_CreateNull();
	cJSON_AddBoolToObject(obj, "is_staged", c->stage.staged);
	cJSON_AddItemToObject(obj, "stage_dock", dock);
	cJSON_AddItemToObject(obj, "stage_dock_preview", preview);
}

struct ipc_dispatch_result ipc_dispatch_result = {-1, NULL};

void ipc_dispatch_begin(void) {
	ipc_dispatch_result = (struct ipc_dispatch_result){-1, NULL};
}

void ipc_dispatch_reply(int fd) {
	if (ipc_dispatch_result.changed < 0) {
		send_static_json(fd, "{\"success\":true}\n");
		return;
	}
	cJSON *r = cJSON_CreateObject();
	cJSON_AddBoolToObject(r, "success", true);
	cJSON_AddBoolToObject(r, "changed", ipc_dispatch_result.changed);
	if (ipc_dispatch_result.reason)
		cJSON_AddStringToObject(r, "reason", ipc_dispatch_result.reason);
	ipc_notify_json_to_fd(fd, r);
	cJSON_Delete(r);
}
