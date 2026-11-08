# Stage patch

Local patch on top of upstream mango: the Stage layout and the shared card renderer that stage and overview both draw through. User docs: `docs/window-management/stage.md`.

Read this before changing patch code or rebasing on upstream.

## Rule: smallest possible upstream surface

Every line in an upstream file is a future rebase conflict. So:

- New logic goes in patch-owned files (list below). Upstream files get one-line hooks: a call, an `if (hook()) return;`, or a struct member.
- Put a hook at the top of a function or next to a stable line, not in the middle of a long `else if` chain that upstream keeps appending to.
- Patch state lives in structs from patch headers: `Client.stage` (`struct stage_client`), `Client.card` (`struct card_state`), `Popup.card` (`struct card_popup`), `Pertag.stage[tag]` (`struct stage_tag`), `Config.stage` (`struct stage_config`). Add fields there, not to upstream structs.
- Patch types reach upstream code through one include: `config/parse.h` includes `layout/stage.h`, which includes `config/stage.h` and `layout/card.h`. Anything that includes `client.h`, `monitor.h` or `parse.h` sees them, so don't add patch includes to upstream files. The exceptions are `layer.h` (`card.h`), `binding_parse.c` (`dispatch/stage.h`) and `ipc.c` (`ipc/stage.h`).
- No patch comments in upstream files. Explain things here or in patch files. When patch code replaces upstream code, keep upstream's comment if it still holds.
- Docs go in `docs/window-management/stage.md`. Upstream pages get only link lines.
- After any change, check the surface with `git diff <upstream> --stat --diff-filter=M`.

## Patch-owned files

| File | Owns |
| :--- | :--- |
| `src/layout/stage-geometry.c` | Pure geometry: center square, tile boxes, scale solver, column/strip/undock boxes. Inputs are a `struct stage_frame` (monitor boxes, gaps, splits, tunables; built by `stage_frame_of()`) and a `struct stage_card` (logical size, border). It never reads clients, config or stage history. |
| `src/layout/stage.c` | Layout pass `stage()` and the state transitions (stash, unstash, dock, undock, forget, adopt size), the domain operations that return `enum stage_result`, and the overview dock strips. |
| `src/layout/stage-drag.c` | Pointer interaction: drag, pickup ease, shake, drop hint, divider resize. |
| `src/layout/stage-internal.h` | Private header shared by those three. `include/mango/layout/stage.h` is the public API and state structs, free of IPC/cJSON types. |
| `src/layout/card.c` | Shared card renderer used by stage cards and overview (moved out of upstream `overview.c`). |
| `src/config/stage.c` | `stage_*` options: parse, defaults, clamps (`include/mango/config/stage.h`). |
| `src/dispatch/stage.c` | Bind/IPC adapters and bind-name parsing (`stage_parse_func`): parse `Arg`, call the domain operation, translate `enum stage_result` into `ipc_dispatch_result`. |
| `src/ipc/stage.c` | `stage_client_json()` (`is_staged`, `stage_dock`, `stage_dock_preview`), the `ipc_dispatch_result` reply (`ipc_dispatch_begin`/`ipc_dispatch_reply`). |
| `tests/` | Focused tests, run by `tests/card-input.sh`. |
| `docs/window-management/stage.md`, `PATCH.md`, `CLAUDE.md` | Docs. |

## Upstream touch points

| Upstream file | What the patch does there |
| :--- | :--- |
| `config/parse.h` | Include `layout/stage.h`; `Config.stage`. |
| `config/option_defs.c` | `stage_parse_option` / `stage_config_defaults` / `stage_config_clamp` at the top of `parse_option`, `set_value_default`, `override_config`. |
| `config/binding_parse.c` | `stage_parse_func` before the name chain in `parse_func_name`. |
| `ipc/ipc.c` | `stage_client_json` in `build_client_json`; `ipc_dispatch_begin`/`ipc_dispatch_reply` around the dispatch in `handle_command`. |
| `layout/layout.h`, `layout/arrange.c` | `STAGE` id and table entry; `stage_resize_tile` in `resize_tile_client`; `Pertag.stage` reset/move in tag gather; `stage_release_all` in `arrange`. |
| `layout/overview.c`, `layout/overview.h` | `overview_scale*` take an explicit area; `overview()` passes the remainder of `stage_overview_docks()`. |
| `overview/overview.c`, `overview/overview.h` | Upstream card code removed (lives in `card.c`); `overview_member` ownership; staged windows keep their card on exit; `overview_change_mon`. |
| `manage/client.h`, `manage/client.c` | `card`, `stage`, `overview_member` members; card lifecycle calls; `stage_content_size` for configure sizes; `stage_adopt_size`, `stage_forget`, `stage_unstash` hooks; docked visibility; overview state handover in `client_replace`; `overview_change_mon` before `c->mon` changes. |
| `animation/client.c` | Card radii/layout/fadeout through `card_*`; staged cards lose the floating shadow and size hints; drop area hook; `stage_drag_morph` in `resize` and `client_draw_frame`. |
| `animation/tag.c` | `stage_arrange_hidden`; overview card visibility keyed on `overview_member`. |
| `input/pointer.c` | `stage_begin_move`, `stage_drag_motion`, `stage_drop`, `stage_tile_at_cursor`; card surface coordinates; X11 pointer scaling moved from `pointer_focus` to its only caller. |
| `manage/misc.c`, `manage/misc.h` | `node_at_point` returns whether coordinates are card-mapped. |
| `manage/layer.c`, `manage/layer.h` | `Popup.card`; `card_popup_create` / `card_popup_destroy`. |
| `manage/monitor.c`, `manage/monitor.h` | `Pertag.stage`; `stage_monitor_close`. |
| `dispatch/bind.c` | Docked windows join overview; picking one undocks it. |
| `main.c` | Unrelated fix: `--job-mode=replace-irreversibly` on the teardown stop of `graphical-session.target`, so portal activation can't cancel it. |
| `mmsg/mmsg.1.in` | `changed`/`reason` in the dispatch reply. |
| `docs/window-management/layouts.md`, `meta.json` | Link and sidebar entry for `stage.md`. |

## Stage invariants

- State changes go only through the transitions. Each one runs in this order: close popups, fields, minimized state, focus, card, arrange, IPC.
- `stage_stash()` validates the box before it changes any field. The first stash sets the logical size (`stage.lw`/`stage.lh`) from the client's committed geometry, so the scale for a box must be solved after that stash (see the full-center swap in `stage_drop()`).
- Stash and unstash also run inside the layout pass, so their callers arrange, and arrange notifies.
- The dock operations notify last, after the final placement. `unminimize()` publishes before the undock box exists, so `stage_undock_client()` notifies again after it.
- There is one pointer grab, so the file-static drag/shake/hint state in `stage-drag.c` is global, not per monitor. `stage_forget()` clears it through `stage_drag_forget()`, and `stage_monitor_close()` drops previews aimed at a removed output.
- Overview gets the dock strips' remainder as an explicit area (`stage_overview_docks()` returns it; `overview_scale(m, area)`). `m->w` is never rewritten.
- Divider resizes are coalesced to one per output frame by upstream's `resize_tile_client` (`m->frame_serial`); `stage_resize_tile` just arranges.

## Card lifecycle (`include/mango/layout/card.h`)

- `Client.card` (`struct card_state`) holds the tree, surface entries, popups and the mapping.
- `card.tree` marks that the card exists:
  - `card_create()` makes the tree, initializes the lists, and then hides the real surface tree through `client_update_visibility()`.
  - `card_destroy()` runs in this order: close card popups, detach surface listeners, destroy the tree, show the real surface.
  - Both are idempotent, and zeroed state means no card.
- A card does not imply overview ownership. `overview_member` and the `overview_*bak` fields belong to overview; stage cards keep their card through overview exit.
- A replacement client builds its own card and never copies another client's listeners.

## Coordinate spaces

- *Card-local*: the card tree sits at `(bw, bw)` inside `c->scene`. Content is scaled by `card.scale_x/y`.
- *Window-geometry-local*: the xdg window geometry. `card.clip_x/y` (from `client_get_clip()`) is its offset into the root surface.
- *Surface-local*: card point / scale + clip - subsurface offset. Card buffers return surface-local hits (`card_owns_buffer()`, `card_surface_coords()`).
- `card_layout()` places each surface from its unrounded origin (`floor` for the node position, exact offset for the source box), so fractional geometry offsets don't shift content. `tests/card-clip.c` checks this.
- Popup geometry is relative to the parent's window geometry, so a card popup sits at `bw + g * scale`.
- X11 sizes are converted to logical once in `client_get_geometry()`. Card code reads the X11 surface's logical size.

## Rebasing on upstream

1. `git rebase <upstream>`. Expect conflicts only at the touch points above.
2. Usual conflicts:
   - **`overview/overview.c` card code**: take ours (the code lives in `card.c`), then port upstream's change into `card_layout()` or its neighbours by hand. Example: upstream b7551beb (unified root/subsurface mapping, `client_surface_get_source_box`).
   - **Renamed upstream helpers** (e.g. `client_apply_*` became `client_set_*`): grep patch files for the old names.
   - **Changed upstream signatures or removed state** (e.g. `resize_tile_client` lost `time`, `server.last_apply_drag_time` went away): adapt the stage hook to the new upstream mechanism. Don't bring the old one back.
   - A patch commit that only adapted to upstream code which has since changed again can be skipped.
3. Build: `nix develop --command ninja -C build` (`meson setup --reconfigure build` after new files).
4. Tests: `nix develop --command sh tests/card-input.sh`, then again with `env 'CC=cc -fsanitize=address -fno-omit-frame-pointer'`. Tests link real sources against stubs, so upstream signature changes show up as stub conflicts (`tests/card-input.c`) and new seat calls in `pointer.c` need a `--wrap` in `tests/card-input.sh`.
5. Config: `./build/mango -p -c <file>` with every `stage_*` key and the stage binds. Unknown `stage_*` keys must still be rejected.
6. IPC: run a headless instance and `mmsg dispatch stage_dock,left` etc. Unset `DBUS_SESSION_BUS_ADDRESS` first: otherwise mango updates the activation environment, and on exit stops `graphical-session.target`, i.e. the live session. Put `XDG_RUNTIME_DIR` on a short absolute path (the socket path must fit in 107 bytes), e.g. `build/e2e-rt`.
7. Check the surface again: `git diff <upstream> --stat --diff-filter=M`.
