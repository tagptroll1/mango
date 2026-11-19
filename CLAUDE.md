# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Stage patch (read first)

This tree carries a local patch (Stage layout, shared card renderer) on top of upstream. **Before changing patch code, touching an upstream file for the patch, or rebasing, read `PATCH.md` in full and follow it.** It holds the minimal-upstream-surface rules, the patch-owned files, every upstream touch point, the invariants, and the rebase and verification steps. Update `PATCH.md` when the patch surface changes.

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

**Tags and layouts.** Tags (bitmask, `TAGMASK`), not workspaces. Each monitor has a `Pertag` holding per-tag state, including `ltidxs[tag]` (layout pointer). `arrange(m, ...)` in `src/layout/arrange.c` is the central re-layout entry: `pre_calculate_before_arrange`, then the current tag's `Layout.arrange(m)` (or `overviewlayout` when `m->isoverview`), then tag gather, pointer refresh, and an IPC status broadcast. Layouts register in the `layouts[]` table in `arrange.c` with an enum id in `include/mango/layout/layout.h`; `predict` callbacks compute a client's future box via `LayoutContext.probe`. Layout implementations live in `src/layout/{horizontal,vertical,scroll,dwindle,overview}.c` (stage: see `PATCH.md`).

**Geometry vs. rendering.** Layouts set target geometry; `src/animation/` interpolates it (baked bezier curves in `server.baked_points_*`) and applies it to scene nodes. Client visibility, borders, and SceneFX effects are applied in `src/animation/client.c`; tag-switch animation in `src/animation/tag.c`. Closing windows/layers move to `fadeout_clients`/`fadeout_layers`.

**Dispatch functions.** User actions are `int32_t fn(const Arg *arg)` functions (mostly `src/dispatch/bind.c`). `parse_func_name()` in `src/config/parse_config.c` is the single name-to-function map, a long `strcmp` chain that also parses arguments. It is used by keybinds, mouse/axis/gesture binds, and IPC `dispatch` commands. Adding a dispatch means: implement the function, declare it, add a branch in `parse_func_name()`, document it in `docs/bindings/`.

**Config.** `parse_config()` parses the text config (`assets/config.conf` is the shipped default) into `config`; `src/config/config_watcher.c` uses inotify for hot reload; parse errors go to `config_error_store` and are shown via `mangonag` (`config_error_nag.c`). New options need: field in `Config`, default, parsing branch, validation, docs in `docs/configuration/` or relevant page, and Nix module options in `nix/hm-modules.nix` / `nix/nixos-modules.nix` if exposed there.

**IPC.** `src/ipc/ipc.c` serves a Unix socket (`MANGO_INSTANCE_SIGNATURE`) with cJSON. `handle_command()` handles `get ...` queries, `watch ...` subscriptions, and `dispatch <func>,<args...>` (routed through `parse_func_name()`; result reported via `ipc_dispatch_result`). State changes call `printstatus(IPC_WATCH_*)` / `ipc_notify_*` to push updates to watchers. CLI contract documented in `docs/ipc.md` and `mmsg/mmsg.1.in`.

**Protocols.** Extra Wayland protocol glue (ext-workspace, foreign-toplevel, tearing, text-input/IME, xdg-activation, HDR, xdg-output) lives in `src/ext-protocol/`.

## Docs

User docs in `docs/` (Markdown/MDX, synced to the website by `.github/workflows/sync-website.yml`). Layouts: `docs/window-management/layouts.md`; IPC: `docs/ipc.md`; binds: `docs/bindings/`. `docs/nix-options.md` is generated from the Nix modules by CI.
