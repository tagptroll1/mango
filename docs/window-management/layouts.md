---
title: Layouts
description: Configure and switch between different window layouts.
---

## Supported Layouts

mangowm supports a variety of layouts that can be assigned per tag.

- `tile`
- `scroller`
- `monocle`
- `grid`
- `deck`
- `center_tile`
- `vertical_tile`
- `right_tile`
- `vertical_scroller`
- `vertical_grid`
- `vertical_deck`
- `dwindle`
- `fair`
- `vertical_fair`
- `stage`

---

## Scroller Layout

The Scroller layout positions windows in a scrollable strip, similar to PaperWM.

### Configuration

| Setting | Default | Description |
| :--- | :--- | :--- |
| `scroller_structs` | `20` | Width reserved on sides when window ratio is 1. |
| `scroller_default_proportion` | `0.9` | Default width proportion for new windows. |
| `scroller_focus_center` | `0` | Always center the focused window (1 = enable). |
| `scroller_prefer_center` | `0` | Center focused window only if it was outside the view. |
| `scroller_prefer_overspread` | `1` | Allow windows to overspread when there's extra space. |
| `edge_scroller_pointer_focus` | `1` | Focus windows even if partially off-screen. |
| `edge_scroller_focus_allow_speed` | `0.0` | Allow pointer focus to happen if the pointer moves at a speed greater than this value. |
| `scroller_proportion_preset` | `0.5,0.8,1.0` | Presets for cycling window widths. |
| `scroller_ignore_proportion_single` | `1` | Ignore proportion adjustments for single windows. |
| `scroller_default_proportion_single` | `1.0` | Default proportion for single windows in scroller. **Requires `scroller_ignore_proportion_single=0` to take effect.** |

> **Warning:** `scroller_prefer_overspread`, `scroller_focus_center`, and `scroller_prefer_center` interact with each other. Their priority order is:
>
> **scroller_prefer_overspread > scroller_focus_center > scroller_prefer_center**
>
> To ensure a lower-priority setting takes effect, you must set all higher-priority options to `0`.

```ini
# Example scroller configuration
scroller_structs=20
scroller_default_proportion=0.9
scroller_focus_center=0
scroller_prefer_center=0
scroller_prefer_overspread=1
edge_scroller_pointer_focus=1
edge_scroller_focus_allow_speed=0.0
scroller_default_proportion_single=1.0
scroller_proportion_preset=0.5,0.8,1.0
```

---

## Master-Stack Layouts

These settings apply to layouts like `tile` and `center_tile`.

| Setting | Default | Description |
| :--- | :--- | :--- |
| `new_is_master` | `1` | New windows become the master window. |
| `default_master_factor` | `0.55` | The split ratio between master and stack areas. |
| `default_master_count` | `1` | Number of allowed master windows. |
| `center_master_overspread` | `0` | (Center Tile) Master spreads across screen if no stack exists. |
| `center_when_single_stack` | `1` | (Center Tile) Center master when only one stack window exists. |

```ini
# Example master-stack configuration
new_is_master=1
smart_gaps=0
default_master_factor=0.55
default_master_count=1
tag_num=9
tag_gather=0
```

---

## Dwindle Layout

The Dwindle layout arranges windows as a binary tree of recursive splits. Each new window splits the focused window's container, producing a spiral-like tiling.

### Configuration

| Setting | Default | Description |
| :--- | :--- | :--- |
| `dwindle_split_ratio` | `0.5` | Ratio used for new splits (`0.05`–`0.95`). |
| `dwindle_smart_split` | `0` | Pick the split axis from the cursor's position inside the focused window. The new window appears on the cursor's side. |
| `dwindle_horizontal_split` | `1` | Side-by-side splits: where the new window goes. `0` = follow cursor, `1` = right, `2` = left. |
| `dwindle_vertical_split` | `1` | Top/bottom splits: where the new window goes. `0` = follow cursor, `1` = below, `2` = above. |
| `dwindle_preserve_split` | `0` | Keep the sibling's split orientation when a window is closed. |
| `dwindle_smart_resize` | `0` | When dragging to resize, move the split toward the cursor regardless of which side was grabbed. |
| `dwindle_drop_simple_split` | `1` | Drag-to-tile drop preview. `1` = 2-zone preview matching `dwindle_split_ratio`, `0` = 4-quadrant preview. |
| `dwindle_manual_split` | `0` | Manually split windows mode. |

```ini
# Example dwindle configuration
dwindle_split_ratio=0.5
dwindle_smart_split=0
dwindle_horizontal_split=0
dwindle_vertical_split=0
dwindle_preserve_split=0
dwindle_smart_resize=0
dwindle_drop_simple_split=1
```

---

## Monocle Layout

The Monocle layout shows a single window at a time, with the other windows on the tag stacked behind it.

### Configuration

| Setting | Default | Description |
| :--- | :--- | :--- |
| `monocle_tab_mode` | `1` | Auto merge stacked windows into a tab in monocle layout. |
| `monocle_no_border` | `0` | Remove window borders from tiled windows (including maximized ones) while the monocle layout is active. |
| `monocle_no_gap` | `0` | Remove gaps around tiled windows (including maximized ones) while the monocle layout is active. |

```ini
# Example monocle configuration
monocle_tab_mode=1
monocle_no_border=0
monocle_no_gap=0
```

---

## Deck Layout

The Deck layout keeps the master area visible and stacks the remaining windows as a deck of cards.

### Configuration

| Setting | Default | Description |
| :--- | :--- | :--- |
| `deck_tab_mode` | `1` | Auto merge stack area windows into a tab in deck layout. |

```ini
# Example deck configuration
deck_tab_mode=1
```

---

## Stage Layout

The Stage layout tiles up to four windows in a square in the middle of the monitor, the *stage*. Every other window becomes a *card* on the sides: the live window drawn scaled down, shrinking the further it sits from the stage.

### The Stage

- The stage is always the full height of the work area. It is square where the output allows. On portrait, square, or narrow outputs it is made narrower so each side keeps room for at least one card of `stage_dock_tiny` plus gaps and borders. Cards never go above or below the stage.
- The first four tiled windows, in tiling order, share the stage. The 5th and later tiled windows become cards on the emptier side, below the last card there.
- `stage_flip` switches between side-by-side and stacked tiles. A mouse resize (`moveresize,curresize`) or `resizewin` on a stage tile moves the dividers, which stay between 10% and 90% of the stage.

### Cards

- A card keeps the window's *logical size*, the size the client is configured to. Moving a card changes only how large it is drawn, so the client does not redraw at every step.
- `stage_text_zoom` configures the client that many times smaller than its logical size. The card then draws it that many times larger, so text stays readable at card scale.
- Sizes are in layout (logical) pixels, the same units as gaps and window geometry.

### Moving Windows

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

### Docks

A docked window is hidden and reported as minimized. It keeps its edge, tier and position along the edge.

- **mango does not draw docks.** A shell has to draw them from the `stage_dock` and `stage_dock_preview` fields of the client IPC (see [IPC](/docs/ipc#stage-layout)). Without such a shell, a docked window is reachable only by undocking it with `stage_undock` or `stage_dock_toggle`, by restoring it as a minimized window, or from overview.
- Undocking puts the window back beside its edge, just outside `stage_shrink_zone`, at its dock position.
- Restoring it as a minimized window (taskbar or `restore_minimized`) also ends the dock.
- Docks persist across tag switches. When a tag leaves the Stage layout, its cards and docks return to normal tiling.
- In overview, docked windows of the shown tags are listed in a strip on their own edge, `stage_overview_dock_ratio` of the overview width. Picking one undocks it. A crowded strip shrinks its cards to `stage_dock_tiny` wide, then overlaps them.

### Configuration

All values are clamped when the config is loaded. Some limits depend on other keys.

| Setting | Default | Range | Description |
| :--- | :--- | :--- | :--- |
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
tagrule=id:1,layout_name:stage
bind=SUPER,f,stage_flip
bind=SUPER,d,stage_dock_toggle
mousebind=SUPER,btn_left,moveresize,curmove
```

The stage dispatches are listed under [Layouts](/docs/bindings/keys#layouts) in the dispatcher list.

### Limitations

- The layout needs a usable width of more than twice the side room (`stage_dock_tiny` plus borders and gaps on each side). Narrower outputs leave no usable stage and are not supported.
- A tile swapped out of a full stage can still cover part of the stage when its side is too narrow for it.

---

## Switching Layouts
| Setting | Default | Description |
| :--- | :--- | :--- |
| `circle_layout` | - | A comma-separated list of layouts `switch_layout` cycles through,the value sample:`tile,scroller`. |

You can switch layouts dynamically or set a default for specific tags using [Tag Rules](/docs/window-management/rules#tag-rules).

**Keybinding Examples:**

```ini
# Cycle through layouts
circle_layout=grid,scroller,tile
bind=SUPER,n,switch_layout

# Set specific layout
bind=SUPER,t,setlayout,tile
bind=SUPER,s,setlayout,scroller
```
