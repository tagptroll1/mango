#include "mango/dispatch/stage.h"
#include "mango/common/server.h"
#include "mango/dispatch/bind.h"
#include "mango/ipc/ipc.h"
#include "mango/layout/stage.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"

static struct ipc_dispatch_result stage_ipc_result(enum stage_result r) {
	switch (r) {
	case STAGE_CHANGED:
		return (struct ipc_dispatch_result){1, NULL};
	case STAGE_UNCHANGED:
		return (struct ipc_dispatch_result){0, NULL};
	case STAGE_NO_CLIENT:
		return (struct ipc_dispatch_result){0, "no-client"};
	case STAGE_NOT_STAGED:
		return (struct ipc_dispatch_result){0, "not-staged"};
	case STAGE_NOT_DOCKED:
		return (struct ipc_dispatch_result){0, "not-docked"};
	}
	return (struct ipc_dispatch_result){0, NULL};
}

// The target client, else the focused one.
static Client *stage_dispatch_client(const Arg *arg) {
	if (arg && arg->tc)
		return arg->tc;
	return server.selected_monitor ? server.selected_monitor->sel : NULL;
}

int32_t stage_dock(const Arg *arg) {
	int32_t edge;
	if (arg->i == LEFT) {
		edge = STAGE_DOCK_LEFT;
	} else if (arg->i == RIGHT) {
		edge = STAGE_DOCK_RIGHT;
	} else if (arg->i == UNDIR) {
		edge = STAGE_DOCK_NEAREST;
	} else {
		ipc_dispatch_result = (struct ipc_dispatch_result){0, "bad-edge"};
		return 0;
	}
	ipc_dispatch_result =
		stage_ipc_result(stage_dock_window(stage_dispatch_client(arg), edge));
	return 0;
}

int32_t stage_undock(const Arg *arg) {
	ipc_dispatch_result =
		stage_ipc_result(stage_undock_window(stage_dispatch_client(arg)));
	return 0;
}

int32_t stage_dock_toggle(const Arg *arg) {
	Client *c = stage_dispatch_client(arg);
	ipc_dispatch_result = stage_ipc_result(
		c && c->stage_docked ? stage_undock_window(c)
							 : stage_dock_window(c, STAGE_DOCK_NEAREST));
	return 0;
}

int32_t stage_dock_move(const Arg *arg) {
	Client *c = arg ? arg->tc : NULL;
	ipc_dispatch_result =
		stage_ipc_result(stage_move_dock(c, arg ? arg->i : 0));
	return 0;
}

int32_t stage_flip_toggle(const Arg *arg) {
	stage_flip(server.selected_monitor);
	return 0;
}
