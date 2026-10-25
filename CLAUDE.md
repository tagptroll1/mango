# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Mango is a Wayland compositor in C, forked from dwl. Built on wlroots 0.20 and SceneFX 0.5 (blur, shadow, corner radius). Meson build produces three binaries:

- `mango`: compositor (`src/**/*.c`, `src/main.c` holds `main()`, `setup()`, `run()`, `cleanup()`)
- `mmsg`: IPC CLI client (`mmsg/mmsg.c`, links a few `src/common` + `src/config` files)
- `mangonag`: layer-shell nag/notification client (`mangonag/mangonag.c`)

## Commands

```bash
# Meson (dev shell: `nix develop`, adds clang-tools)
meson setup build            # re-run (or `meson setup --reconfigure build`) after adding .c files
ninja -C build
meson setup build -Dasan=true        # AddressSanitizer
meson setup build -Dxwayland=disabled

# Nix build without install/result symlink. Use `path:` so untracked files are included
nix build --no-link --no-write-lock-file 'path:/mnt/bigboi/dev/C/mango#mango'

# Validate a config file without starting the compositor
./build/mango -p -c <file>

# Formatting (repo .clang-format: LLVM base, tabs, width 4, sorted includes)
clang-format --dry-run --Werror <files>
git diff --check
```

`format.sh` reformats every header/source in the repo; prefer formatting only touched files. CONTRIBUTING asks for formatted code and squashed commits.

Focused tests (no Meson target) build the real sources against stubbed compositor effects and run in the dev shell:

```bash
nix develop --no-write-lock-file 'path:/mnt/bigboi/dev/C/mango' --command sh tests/card-input.sh
# AddressSanitizer
nix develop --no-write-lock-file 'path:/mnt/bigboi/dev/C/mango' --command env 'CC=cc -fsanitize=address -fno-omit-frame-pointer' sh tests/card-input.sh
```

Each test prints `PASS`/`FAIL` lines; the script stops at the first failing binary.

`mango` flags: `-c <file>` config, `-s <cmd>` startup command, `-d` debug log, `-p` parse config and exit, `-r <fd>` readiness fd. Requires `XDG_RUNTIME_DIR`.

## Build notes

- `meson.build` collects sources with `find src -name '*.c'` at configure time. New `.c` files under `src/` need no meson edit, only a reconfigure.
- `-DWLR_USE_UNSTABLE` is required; `.clangd` adds it and reads `build/compile_commands.json`.
- `-DXWAYLAND` is defined when xcb + xcb-icccm are found; Xwayland code is wrapped in `#ifdef XWAYLAND`.
- Protocol headers are generated from `protocols/` by wayland-scanner into the build dir.

## Architecture

**Global state.** Two globals drive everything: `struct MangoServer server` (`include/mango/common/server.h`: wlroots objects, scene layer trees `server.layers[Lyr*]`, client/monitor lists, seat, drag state) and `Config config` (`include/mango/config/parse_config.h`). Core types (`Client`, `Monitor`, `Arg`, ...) are in `include/mango/common/types.h` and `include/mango/manage/*.h`. `server.clients` is tiling order; `server.focus_stack` is focus order.

**Headers mirror sources.** `src/<area>/<file>.c` pairs with `include/mango/<area>/<file>.h`, included as `"mango/<area>/<file>.h"`. Areas: `animation`, `common`, `config`, `dispatch`, `draw`, `ext-protocol`, `input`, `ipc`, `layout`, `manage`, `overview`, `switcher`.

**Tags and layouts.** Tags (bitmask, `TAGMASK`), not workspaces. Each monitor has a `Pertag` holding per-tag state, including `ltidxs[tag]` (layout pointer). `arrange(m, ...)` in `src/layout/arrange.c` is the central re-layout entry: `pre_calculate_before_arrange`, then the current tag's `Layout.arrange(m)` (or `overviewlayout` when `m->isoverview`), then tag gather, pointer refresh, and an IPC status broadcast. Layouts register in the `layouts[]` table in `arrange.c` with an enum id in `include/mango/layout/layout.h`; `predict` callbacks compute a client's future box via `LayoutContext.probe`. Layout implementations live in `src/layout/{horizontal,vertical,scroll,dwindle,overview,stage}.c`.

**Geometry vs. rendering.** Layouts set target geometry; `src/animation/` interpolates it (baked bezier curves in `server.baked_points_*`) and applies it to scene nodes. Client visibility, borders, and SceneFX effects are applied in `src/animation/client.c`; tag-switch animation in `src/animation/tag.c`. Closing windows/layers move to `fadeout_clients`/`fadeout_layers`.

**Dispatch functions.** User actions are `int32_t fn(const Arg *arg)` functions (mostly `src/dispatch/bind.c`). `parse_func_name()` in `src/config/parse_config.c` is the single name-to-function map, a long `strcmp` chain that also parses arguments. It is used by keybinds, mouse/axis/gesture binds, and IPC `dispatch` commands. Adding a dispatch means: implement the function, declare it, add a branch in `parse_func_name()`, document it in `docs/bindings/`.

**Config.** `parse_config()` parses the text config (`assets/config.conf` is the shipped default) into `config`; `src/config/config_watcher.c` uses inotify for hot reload; parse errors go to `config_error_store` and are shown via `mangonag` (`config_error_nag.c`). New options need: field in `Config`, default, parsing branch, validation, docs in `docs/configuration/` or relevant page, and Nix module options in `nix/hm-modules.nix` / `nix/nixos-modules.nix` if exposed there.

**IPC.** `src/ipc/ipc.c` serves a Unix socket (`MANGO_INSTANCE_SIGNATURE`) with cJSON. `handle_command()` handles `get ...` queries, `watch ...` subscriptions, and `dispatch <func>,<args...>` (routed through `parse_func_name()`; result reported via `ipc_dispatch_result`). State changes call `printstatus(IPC_WATCH_*)` / `ipc_notify_*` to push updates to watchers. CLI contract documented in `docs/ipc.md` and `mmsg/mmsg.1.in`.

**Protocols.** Extra Wayland protocol glue (ext-workspace, foreign-toplevel, tearing, text-input/IME, xdg-activation, HDR, xdg-output) lives in `src/ext-protocol/`.

## Stage layout and cards

User-facing behaviour, config keys and IPC are documented in `docs/window-management/layouts.md#stage-layout` and `docs/ipc.md#stage-layout`.

**Module map.**

| File | Owns |
| :--- | :--- |
| `src/layout/stage-geometry.c` | Pure geometry: center square, tile boxes, scale solver, column/strip/undock boxes. Inputs are a `struct stage_frame` (monitor boxes, gaps, splits, tunables; built by `stage_frame_of()`) and a `struct stage_card` (logical size, border). It never reads clients, config or stage history. |
| `src/layout/stage.c` | Layout pass `stage()` and the state transitions (stash, unstash, dock, undock, forget, adopt size), the domain operations that return `enum stage_result`, and the overview dock strips. |
| `src/layout/stage-drag.c` | Pointer interaction: drag, pickup ease, shake, drop hint, divider resize. |
| `src/layout/stage-internal.h` | Private header shared by those three. `include/mango/layout/stage.h` is the public API, free of IPC/cJSON types. |
| `src/dispatch/stage.c` | Bind/IPC adapters: parse `Arg`, call the domain operation, translate `enum stage_result` into `ipc_dispatch_result`. |
| `src/ipc/stage.c` | `stage_client_json()`: the `is_staged`, `stage_dock`, `stage_dock_preview` client fields. |
| `src/layout/card.c` | Shared card renderer used by stage cards and overview. |

**Stage invariants.**
- State changes go only through the transitions. Each one runs in this order: close popups, fields, minimized state, focus, card, arrange, IPC.
- `stage_stash()` validates the box before it changes any field. The first stash sets the logical size (`stage_lw`/`stage_lh`) from the client's committed geometry, so the scale for a box must be solved after that stash (see the full-center swap in `stage_drop()`).
- Stash and unstash also run inside the layout pass, so their callers arrange, and arrange notifies.
- The dock operations notify last, after the final placement. `unminimize()` publishes before the undock box exists, so `stage_undock_client()` notifies again after it.
- There is one pointer grab, so the file-static drag/shake/hint state in `stage-drag.c` is global, not per monitor. `stage_forget()` clears it through `stage_drag_forget()`, and `stage_monitor_close()` drops previews aimed at a removed output.
- Overview gets the dock strips' remainder as an explicit area (`stage_overview_docks()` returns it; `overview_scale(m, area)`). `m->w` is never rewritten.

**Card lifecycle** (`include/mango/layout/card.h`).
- `Client.card` (`struct card_state`) holds the tree, surface entries, popups and the mapping.
- `card.tree` marks that the card exists:
  - `card_create()` makes the tree, initializes the lists, and then hides the real surface tree through `client_update_visibility()`.
  - `card_destroy()` runs in this order: close card popups, detach surface listeners, destroy the tree, show the real surface.
  - Both are idempotent, and zeroed state means no card.
- A card does not imply overview ownership. `overview_member` and the `overview_*bak` fields belong to overview; stage cards keep their card through overview exit.
- A replacement client builds its own card and never copies another client's listeners.

**Coordinate spaces.**
- *Card-local*: the card tree sits at `(bw, bw)` inside `c->scene`. Content is scaled by `card.scale_x/y`.
- *Window-geometry-local*: the xdg window geometry. `card.clip_x/y` (from `client_get_clip()`) is its offset into the root surface.
- *Surface-local*: card point / scale + clip - subsurface offset. Card buffers return surface-local hits (`card_owns_buffer()`, `card_surface_coords()`).
- Popup geometry is relative to the parent's window geometry, so a card popup sits at `bw + g * scale`.
- X11 sizes are converted to logical once in `client_get_geometry()`. Card code reads the X11 surface's logical size.

## Docs

User docs in `docs/` (Markdown/MDX, synced to the website by `.github/workflows/sync-website.yml`). Layouts: `docs/window-management/layouts.md`; IPC: `docs/ipc.md`; binds: `docs/bindings/`. `docs/nix-options.md` is generated from the Nix modules by CI.
