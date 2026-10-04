#ifndef __OVERVIEW_OVERVIEW_H__
#define __OVERVIEW_OVERVIEW_H__ 1

#include "mango/common/types.h"
#include "mango/dispatch/bind.h"
#include <stdint.h>

// Overview preview: each client is shown through its card (layout/card.h).
// Overview owns membership (overview_member) and the backed-up state.

// Returns 0 when the target window shares its tag with other windows.
uint32_t want_restore_fullscreen(Client *target_client);

void overview_update_jump_label(Client *c);

// Entering overview: shows the client through its card.
void overview_backup_surface(Client *c);

// Saves the window old state when switching from the normal view to overview.
void overview_backup(Client *c);

// Restores window state when switching back from overview to the normal view.
void overview_restore(Client *c, const Arg *arg);

// Call before c->mon changes to m.
void overview_change_mon(Client *c, Monitor *m);

#endif
