/* Stage dispatch results and client JSON, as captured before the stage was
 * split into modules. */
#include "stage-fixture.h"

static void json_is(const char *name, Client *c, const char *want) {
	cJSON *obj = cJSON_CreateObject();
	stage_client_json(c, obj);
	char *got = cJSON_PrintUnformatted(obj);
	check(got && !strcmp(got, want), name, got);
	free(got);
	cJSON_Delete(obj);
}

static void result_is(const char *name, int32_t changed, const char *reason) {
	struct ipc_dispatch_result r = ipc_dispatch_result;
	char detail[64];
	snprintf(detail, sizeof(detail), "%d %s", r.changed,
			 r.reason ? r.reason : "-");
	check(r.changed == changed &&
			  (reason ? r.reason && !strcmp(r.reason, reason) : !r.reason),
		  name, detail);
	ipc_dispatch_result = (struct ipc_dispatch_result){-1, NULL};
}

int main(void) {
	setup(true, 0, 0, false);
	Client *c = add_tile(0);
	json_is("json plain", c,
			"{\"is_staged\":false,\"stage_dock\":null,"
			"\"stage_dock_preview\":null}");
	c->isfloating = 1;
	stage_stash(c, (struct wlr_box){200, 300, 324, 244});
	json_is("json staged", c,
			"{\"is_staged\":true,\"stage_dock\":null,"
			"\"stage_dock_preview\":null}");
	c->stage_previewing = true;
	c->stage_preview_edge = STAGE_DOCK_RIGHT;
	c->stage_preview_tier = STAGE_DOCK_MINI;
	c->stage_preview_y = 333;
	c->stage_preview_mon = &mon;
	json_is("json preview", c,
			"{\"is_staged\":true,\"stage_dock\":null,\"stage_dock_preview\":"
			"{\"edge\":\"right\",\"tier\":\"mini\",\"y\":333,"
			"\"monitor\":\"DP-1\"}}");
	c->stage_previewing = false;
	c->stage_preview_mon = NULL;

	stage_dock(&(Arg){.i = 99, .tc = c});
	result_is("dock bad edge", 0, "bad-edge");
	stage_dock(&(Arg){.i = UNDIR, .tc = c});
	result_is("dock nearest", 1, NULL);
	json_is("json docked", c,
			"{\"is_staged\":true,\"stage_dock\":{\"edge\":\"left\","
			"\"tier\":\"full\",\"y\":422},\"stage_dock_preview\":null}");
	stage_dock(&(Arg){.i = UNDIR, .tc = c});
	result_is("dock nearest again", 0, NULL);
	stage_dock(&(Arg){.i = LEFT, .tc = c});
	result_is("dock same edge", 0, NULL);
	stage_dock(&(Arg){.i = RIGHT, .tc = c});
	result_is("dock other edge", 1, NULL);
	stage_dock_move(&(Arg){.i = 5000, .tc = c});
	result_is("move clamped", 1, NULL);
	json_is("json moved", c,
			"{\"is_staged\":true,\"stage_dock\":{\"edge\":\"right\","
			"\"tier\":\"full\",\"y\":1440},\"stage_dock_preview\":null}");
	int32_t before = notifies;
	stage_dock_move(&(Arg){.i = 1440, .tc = c});
	result_is("move same", 0, NULL);
	check(notifies > before, "move same still notifies", NULL);
	stage_dock_move(&(Arg){.i = -3, .tc = c});
	result_is("move negative", 1, NULL);
	stage_undock(&(Arg){.tc = c});
	result_is("undock", 1, NULL);
	json_is("json undocked", c,
			"{\"is_staged\":true,\"stage_dock\":null,"
			"\"stage_dock_preview\":null}");
	stage_undock(&(Arg){.tc = c});
	result_is("undock again", 0, "not-docked");
	stage_dock_move(&(Arg){.i = 5, .tc = c});
	result_is("move not docked", 0, "not-docked");
	stage_dock_toggle(&(Arg){.tc = c});
	result_is("toggle dock", 1, NULL);
	stage_dock_toggle(&(Arg){.tc = c});
	result_is("toggle undock", 1, NULL);
	stage_dock(&(Arg){.i = LEFT});
	result_is("dock selected", 1, NULL);
	Client *t = add_tile(1);
	stage_dock(&(Arg){.i = LEFT, .tc = t});
	result_is("dock tiled", 0, "not-staged");
	stage_dock_move(&(Arg){.i = 5});
	result_is("move no client", 0, "no-client");
	server.selected_monitor = NULL;
	stage_dock(&(Arg){.i = LEFT});
	result_is("dock no client", 0, "no-client");
	stage_undock(&(Arg){0});
	result_is("undock no client", 0, "no-client");
	stage_dock_toggle(&(Arg){0});
	result_is("toggle no client", 0, "no-client");
	t->mon = NULL;
	stage_undock(&(Arg){.tc = t});
	result_is("undock no monitor", 0, "no-client");

	server.selected_monitor = &mon;
	pertag.stage_split_x[0] = 0.3f;
	pertag.stage_split_y[0] = 0.6f;
	stage_flip_toggle(&(Arg){0});
	check(pertag.stage_flip[0] && pertag.stage_split_x[0] == 0.6f &&
			  pertag.stage_split_y[0] == 0.3f,
		  "flip swaps the dividers", NULL);
	pertag.ltidxs[0] = &tile_layout;
	stage_flip_toggle(&(Arg){0});
	check(pertag.stage_flip[0], "flip outside the stage does nothing", NULL);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
