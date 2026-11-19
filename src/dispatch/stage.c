#include "mango/dispatch/stage.h"
#include "mango/common/server.h"
#include "mango/dispatch/bind.h"
#include "mango/ipc/stage.h"
#include "mango/layout/stage.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"
#include <stdlib.h>
#include <string.h>

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
		c && c->stage.docked ? stage_undock_window(c)
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

FuncType stage_parse_func(const char *name, Arg *arg, const char *value) {
	if (strcmp(name, "stage_dock") == 0) {
		arg->i = strcmp(value, "left") == 0		 ? LEFT
				 : strcmp(value, "right") == 0	 ? RIGHT
				 : strcmp(value, "nearest") == 0 ? UNDIR
												 : -1;
		return stage_dock;
	} else if (strcmp(name, "stage_undock") == 0) {
		return stage_undock;
	} else if (strcmp(name, "stage_dock_toggle") == 0) {
		return stage_dock_toggle;
	} else if (strcmp(name, "stage_dock_move") == 0) {
		arg->i = atoi(value);
		return stage_dock_move;
	} else if (strcmp(name, "stage_flip") == 0) {
		return stage_flip_toggle;
	}
	return NULL;
}
