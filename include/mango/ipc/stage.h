#ifndef __IPC_STAGE_H__
#define __IPC_STAGE_H__ 1

#include "mango/common/types.h"
#include <cjson/cJSON.h>
#include <stdint.h>

// Adds the stage fields (is_staged, stage_dock, stage_dock_preview) to a
// client's IPC object.
void stage_client_json(Client *c, cJSON *obj);

// Idempotent dispatches report whether they changed anything here, since a
// dispatch's return value already means "stop matching binds".
struct ipc_dispatch_result {
	int32_t changed; // -1 unset, else 0 or 1
	const char *reason;
};
extern struct ipc_dispatch_result ipc_dispatch_result;

// handle_command calls this before each dispatch.
void ipc_dispatch_begin(void);
// The dispatch reply: {"success":true}, plus changed/reason once set.
void ipc_dispatch_reply(int fd);

#endif
