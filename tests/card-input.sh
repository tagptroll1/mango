#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d "${TMPDIR:-/tmp}/mango-card-input.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
protocols=$(pkg-config --variable=pkgdatadir wayland-protocols)
for xml in "$protocols/stable/xdg-shell/xdg-shell.xml" \
    "$protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml" \
    "$protocols/staging/cursor-shape/cursor-shape-v1.xml" \
    "$protocols/stable/tablet/tablet-v2.xml" \
    protocols/wlr-layer-shell-unstable-v1.xml; do
    wayland-scanner server-header "$xml" "$build/$(basename "$xml" .xml)-protocol.h"
done
deps='scenefx-0.5 wlroots-0.20 wayland-server libinput xkbcommon pixman-1 libdrm pangocairo libcjson'
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps) \
    tests/card-input.c src/input/pointer.c src/manage/misc.c \
    src/common/scene_node.c \
    -Wl,--gc-sections -Wl,--wrap=wlr_scene_surface_try_from_buffer \
    -Wl,--wrap=wlr_seat_pointer_notify_enter \
    -Wl,--wrap=wlr_seat_pointer_notify_motion \
    -Wl,--wrap=wlr_seat_pointer_notify_clear_focus \
    $(pkg-config --libs $deps) -lm -o "$build/card-input"
"$build/card-input"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps) \
    tests/card-popup.c -Wl,--gc-sections \
    $(pkg-config --libs $deps) -lm -o "$build/card-popup"
"$build/card-popup"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/card-clip.c src/common/util.c src/common/log.c \
    src/common/scene_node.c -Wl,--gc-sections \
    $(pkg-config --libs $deps libpcre2-8) -lm -o "$build/card-clip"
"$build/card-clip"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/stage-overflow.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/stage-overflow"
"$build/stage-overflow"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/stage-swap.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/stage-swap"
"$build/stage-swap"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/stage-undock.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/stage-undock"
"$build/stage-undock"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/stage-tiles.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/stage-tiles"
"$build/stage-tiles"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/stage-adapters.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/stage-adapters"
"$build/stage-adapters"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/stage-interaction.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/stage-interaction"
"$build/stage-interaction"
${CC:-cc} -std=c11 -O1 -g -DWLR_USE_UNSTABLE -DXWAYLAND \
    -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections \
    -Iinclude -I"$build" $(pkg-config --cflags $deps libpcre2-8) \
    tests/overview-area.c src/common/util.c src/common/log.c \
    -Wl,--gc-sections $(pkg-config --libs $deps libpcre2-8) -lm \
    -o "$build/overview-area"
"$build/overview-area"
