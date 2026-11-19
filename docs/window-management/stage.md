---
title: Stage
description: Stage layout with scaled side cards and edge docks.
---

The Stage layout tiles up to four windows in a full-height box in the middle of the monitor, the *stage*. Every other window becomes a *card* on the sides: the live window drawn scaled down, shrinking the further it sits from the stage.

## The Stage

- The stage is always the full height of the work area. It is `stage_aspect` times the output height wide where the output allows, square at the default. On portrait, square, or narrow outputs it is made narrower so each side keeps room for at least one card of `stage_dock_tiny` plus gaps and borders. Cards never go above or below the stage.
- The first four tiled windows, in tiling order, share the stage. The 5th and later tiled windows become cards on the emptier side, below the last card there.
- `stage_flip` switches between side-by-side and stacked tiles. A mouse resize (`moveresize,curresize`) or `resizewin` on a stage tile moves the dividers, which stay between 10% and 90% of the stage.

## Cards

- A card keeps the window's *logical size*, the size the client is configured to. Moving a card changes only how large it is drawn, so the client does not redraw at every step.
- `stage_text_zoom` configures the client that many times smaller than its logical size. The card then draws it that many times larger, so text stays readable at card scale.
- Sizes are in layout (logical) pixels, the same units as gaps and window geometry.

## Moving Windows

Moving a stage window with the pointer (for example `mousebind=SUPER,btn_left,moveresize,curmove`) turns it into a square card a quarter of the stage. Its size follows its position:

- Next to the stage it is drawn at `stage_scale_max`. Its size falls to `stage_scale_min` at the output edge, with `stage_scale_curve` setting how early it shrinks.
- Inside `stage_shrink_zone` of the left or right output edge it shrinks further. At `stage_dock_mini_zone` its longest side is `stage_dock_tiny`.

Where it is released decides what happens:

| Released | Result |
| :--- | :--- |
| Card within `stage_dock_mini_zone` of the left or right output edge | Docked on that edge, `mini` tier. |
| Card within `stage_dock_zone` of the left or right output edge | Docked on that edge, `full` tier. |
| Pointer beside the stage | Stays a card where it was released. |
| Pointer on the stage, stage not full | Joins the stage; on a tile, before or after it by the tile edge nearest the pointer. |
| Pointer on a tile of a full stage | Swaps: the card takes the tile's place, and the tile becomes a card at the card's old spot. If it would cover the stage there, it is moved beside the stage when that side has room. |
| Pointer in a gap of a full stage | Goes back to where it was picked up. |
| On a monitor without the Stage layout | Joins that monitor's tiling. |

A tiled window dragged in from another monitor becomes a card where it is released beside the stage, or docks at an edge. With `drag_tile_to_tile=1`, a drop hint shows the box the window will take on the stage.

**Shake:** while moving a card, reverse direction horizontally `stage_shake_flips` times, each time over at least `stage_shake_travel` pixels, all within `stage_shake_window_ms`. Every other stage window then becomes a card, and all cards line up in one column per side. A column that runs out of height shrinks its cards to `stage_dock_tiny`, then overlaps them.

## Docks

A docked window is hidden and reported as minimized. It keeps its edge, tier and position along the edge.

- **mango does not draw docks.** A shell has to draw them from the `stage_dock` and `stage_dock_preview` fields of the client IPC (see [IPC](#ipc)). Without such a shell, a docked window is reachable only by undocking it with `stage_undock` or `stage_dock_toggle`, by restoring it as a minimized window, or from overview.
- Undocking puts the window back beside its edge, just outside `stage_shrink_zone`, at its dock position.
- Restoring it as a minimized window (taskbar or `restore_minimized`) also ends the dock.
- Docks persist across tag switches. When a tag leaves the Stage layout, its cards and docks return to normal tiling.
- In overview, docked windows of the shown tags are listed in a strip on their own edge, `stage_overview_dock_ratio` of the overview width. Picking one undocks it. A crowded strip shrinks its cards to `stage_dock_tiny` wide, then overlaps them.

## Configuration

All values are clamped when the config is loaded. Some limits depend on other keys.

| Setting | Default | Range | Description |
| :--- | :--- | :--- | :--- |
| `stage_aspect` | `1.0` | `0.5`–`4.0` | Stage width as a share of the output height; `1.5` is 3:2. |
| `stage_scale_max` | `0.8` | `0.05`–`1.0` | Card scale next to the stage. |
| `stage_scale_min` | `0.6` | `0.05`–`stage_scale_max` | Card scale at the output edge, before the edge shrink. |
| `stage_scale_curve` | `1.0` | `0.1`–`10.0` | Under `1` cards shrink sooner after leaving the stage, over `1` they keep their size longer. |
| `stage_text_zoom` | `1.0` | `0.5`–`4.0` | Cards configure the client this many times smaller and draw it this many times larger. |
| `stage_dock_mini_zone` | `8` | `0`–`1000` | Release distance from the output edge (pixels) that docks in the `mini` tier. |
| `stage_dock_zone` | `48` | `stage_dock_mini_zone`–`1000` | Release distance from the output edge (pixels) that docks. |
| `stage_shrink_zone` | `100` | `stage_dock_zone + 1`–`2000` | Distance from the output edge (pixels) where cards shrink toward `stage_dock_tiny`. |
| `stage_dock_tiny` | `64` | `8`–`1000` | Longest side of the smallest card (pixels). Also sets the minimum side room next to the stage. |
| `stage_overview_dock_ratio` | `0.12` | `0.02`–`0.5` | Width of each overview dock strip as a share of the overview area. |
| `stage_shake_flips` | `4` | `2`–`8` | Direction reversals that make a shake. |
| `stage_shake_travel` | `40` | `1`–`1000` | Minimum horizontal travel (pixels) of each reversal. |
| `stage_shake_window_ms` | `800` | `50`–`10000` | Time the reversals must fit in (milliseconds). |

```ini
# Use the stage on tag 1 and bind its dispatches
tag_rule=id:1,layout_name:stage
bind=SUPER,f,stage_flip
bind=SUPER,d,stage_dock_toggle
mousebind=SUPER,btn_left,moveresize,curmove
```

## Dispatches

| Command | Param | Description |
| :--- | :--- | :--- |
| `stage_flip` | - | Switch the focused monitor's stage tiles between side by side and stacked. |
| `stage_dock` | `left/right/nearest` | Dock the focused stage card on that edge (`nearest`: the closer one), or move its dock there. Any other value is rejected (`bad-edge`). |
| `stage_undock` | - | Undock the focused window back to a card beside its edge. |
| `stage_dock_toggle` | - | Undock the focused window if docked, else dock it on the nearest edge. |
| `stage_dock_move` | `y` | Move a dock along its edge to `y` pixels from the monitor top (clamped to the monitor). Needs a target window: `mmsg dispatch stage_dock_move,<y> client,<id>`. |

The stage dock dispatches act on the window given by `client,<id>` over IPC, else on the focused window (except `stage_dock_move`, which needs `client,<id>`).

## IPC

The reply is `{"success":true}`, or `{"error":"unknown function"}` / `{"error":"no client found"}`. Dispatches that report their effect, such as the stage dock dispatches, add `changed` and, when they refuse, `reason`:

```json
{"success":true,"changed":false,"reason":"not-docked"}
```

The Stage layout adds three fields to every client object (`get client`, `get all-clients`, `watch client`, `watch all-clients`):

| Field | Value |
| :--- | :--- |
| `is_staged` | `true` while the window is a stage card or dock. |
| `stage_dock` | `null`, or `{"edge":"left\|right","tier":"mini\|full","y":<int>}` while docked. `y` is the dock's center in pixels from the top of the client's monitor. |
| `stage_dock_preview` | `null`, or the dock a release would make right now while a card is dragged near an edge: the same fields plus `"monitor":"<output name>"`, since the drop can land on another monitor. Always `null` while docked. |

mango does not draw docks; a shell draws them from these fields. Changes to them push an update to `watch client <id>` and `watch all-clients`. After an undock, both streams receive the window's restored box with no further input needed.

Dock dispatch replies:

| Dispatch | `changed` | `reason` when refused |
| :--- | :--- | :--- |
| `stage_dock,left\|right\|nearest` | `true` when it docked or changed edge; `false` without a reason when already docked there, or docked and `nearest` was asked | `bad-edge` (other or missing edge), `no-client`, `not-staged` (not a card on a Stage layout monitor) |
| `stage_undock` | `true` when it undocked | `no-client`, `not-docked` |
| `stage_dock_toggle` | as `stage_undock` when docked, else as `stage_dock,nearest` | as those |
| `stage_dock_move,<y>` | `true` when `y` (clamped to the monitor height) differs; `false` without a reason when unchanged | `no-client` (no `client,<id>` given), `not-docked` |

`stage_flip` replies only `{"success":true}`.

```bash
# Dock client 375 on the left edge, then move it 300 pixels down from the top
mmsg dispatch stage_dock,left client,375
mmsg dispatch stage_dock_move,300 client,375
mmsg dispatch stage_undock client,375
```

## Limitations

- The layout needs a usable width of more than twice the side room (`stage_dock_tiny` plus borders and gaps on each side). Narrower outputs leave no usable stage and are not supported.
- A tile swapped out of a full stage can still cover part of the stage when its side is too narrow for it.

