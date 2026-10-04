#ifndef __IPC_STAGE_H__
#define __IPC_STAGE_H__ 1

#include "mango/common/types.h"
#include <cjson/cJSON.h>

// Adds the stage fields (is_staged, stage_dock, stage_dock_preview) to a
// client's IPC object.
void stage_client_json(Client *c, cJSON *obj);

#endif
