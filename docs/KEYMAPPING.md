# Keyboard shortcuts

Set bindings under `[shortcuts]` in `community_patch_settings.toml`.
Names are case-insensitive. The mod writes the parsed bindings and their source to
`[shortcuts]` and `[shortcuts_source]` in `community_patch_runtime.vars`.

## Binding format

| Value | Meaning |
| --- | --- |
| `"Q"` | Q with no modifier held |
| `"SHIFT-Q"` | Either Shift key and Q |
| `"ALT-LSHIFT-T"` | Either Alt key, left Shift, and T |
| `"SPACE\|MOUSE1"` | Space or the right mouse button |
| `"NONE"` or `""` | Disable this binding |

Use `-` between modifiers and one key. Use `|` between alternative bindings.
A modified binding requires its listed modifiers. A plain binding does not fire while a modifier is held.
After a modified binding fires, its plain-key action waits until you release the key.

For example:

```toml
[shortcuts]
action_primary = "SPACE|MOUSE1"
action_recall = "R|MOUSE3"
move_left = "LEFT"
```

An invalid alternative is ignored. If no alternative is valid, the action uses its default and logs a warning.
Escape's close and focus behavior is fixed, even though `ESCAPE` is also a valid key name for bindings.

## Default controls

These are mod hotkeys. `[control].use_scopely_hotkeys = true` selects the game's controls instead.
Most actions wait while you type in a text field.

### Camera and scale

| Action | Default | Setting |
| --- | --- | --- |
| Move up, down, left, right | `W\|UP`, `S\|DOWN`, `A\|LEFT`, `D\|RIGHT` | `move_up`, `move_down`, `move_left`, `move_right` |
| Zoom in, out | `Q`, `E` | `zoom_in`, `zoom_out` |
| Maximum, minimum, default zoom | `MINUS`, `BACKSPACE`, `=` | `zoom_max`, `zoom_min`, `zoom_reset` |
| Zoom presets 1 to 5 | `F1` to `F5` | `zoom_preset1` to `zoom_preset5` |
| Main UI scale up, down | `PGUP`, `PGDOWN` | `ui_scaleup`, `ui_scaledown` |
| Viewer scale up, down | `SHIFT-PGUP`, `SHIFT-PGDOWN` | `ui_scaleviewerup`, `ui_scaleviewerdown` |
| Ship scale up, down | `CTRL-PGUP`, `CTRL-PGDOWN` | `ui_scaleshipup`, `ui_scaleshipdown` |

`[ui].disable_move_keys = true` disables keyboard panning.
Existing bindings take precedence, so `move_left = "LEFT"` keeps movement on the left arrow only.

### Ships and combat

Actions depend on the selected ship and target.

| Action | Default | Setting |
| --- | --- | --- |
| Select or locate ship 1 to 8 | `1` to `8` | `select_ship1` to `select_ship8` |
| Locate the selected ship | `CTRL-SPACE` | `select_current` |
| Primary action or add to queue | `SPACE\|MOUSE1` | `action_primary`, `action_queue` |
| Alternate action | `TAB\|MOUSE4` | `action_secondary` |
| Recall or repair | `R\|MOUSE3` | `action_recall`, `action_repair` |
| Cancel warp | `SPACE\|MOUSE1` | `action_recall_cancel` |
| Toggle cargo view | `V\|MOUSE2` | `action_view` |
| Toggle Kir'Shara queue | `CTRL-Q` | `toggle_queue` |
| Clear the queue | `CTRL-C` | `action_queue_clear` |
| Cycle instant-warp confirmation | `ALT-I` | `toggle_instant_warp` |

The Kir'Shara queue requires the artifact.
`[control].select_timer` sets the ship-selection double-tap interval in milliseconds.

### Screens and chat

| Screen or action | Default | Setting |
| --- | --- | --- |
| Full-screen chat | `C` | `show_chat` |
| Side chat | `ALT-C`, backtick | `show_chatside1`, `show_chatside2` |
| Galaxy, Alliance, Private chat tabs | `CTRL-1`, `CTRL-2`, `CTRL-3` | `select_chatglobal`, `select_chatalliance`, `select_chatprivate` |
| Galaxy, System | `G`, `H` | `show_galaxy`, `show_system` |
| Native Galaxy, Events | `CTRL-G`, `CTRL-E` | `show_galaxy_native`, `show_events_native` |
| Station exterior, interior | `SHIFT-G`, `SHIFT-H` | `show_stationexterior`, `show_stationinterior` |
| Haven | `ALT-H` | `show_haven` |
| Away Teams, Gifts | `T`, `/` | `show_awayteam`, `show_gifts` |
| Artifact Gallery, Command Center | `SHIFT-I`, `O` | `show_artifacts`, `show_commander` |
| Daily Missions, active Missions | `Z`, `M` | `show_daily`, `show_missions` |
| Events, ExoComp | `SHIFT-E`, `X` | `show_events`, `show_exocomp` |
| Factions, Inventory | `F`, `I` | `show_factions`, `show_inventory` |
| Research, Scrap Yard | `U`, `Y` | `show_research`, `show_scrapyard` |
| Refinery, Q Trials | `SHIFT-F`, `SHIFT-Q` | `show_refinery`, `show_qtrials` |
| Settings, Officers | `SHIFT-S`, `SHIFT-O` | `show_settings`, `show_officers` |
| Officer Presets, Officer Swap | `CTRL-O`, `ALT-O` | `show_officerpresets`, `show_officerswap` |
| Manage ship, build ship, swap ship | `N`, `SHIFT-N`, `ALT-N` | `show_ships`, `show_shipconstruction`, `show_shipswap` |
| Peace Shield selection | `CTRL-S` | `show_shields` |
| Battle Reports | `SHIFT-B` | `show_battlelogs` |
| Focus search | `CTRL-F` | `focus_search` |
| Toggle shortcut hints | `HOME` | `toggle_shortcut_hints` |

The shield shortcut opens the selection popup. You still choose the shield.
Ship Swap, Officer Swap, and Officer Presets wait for their destination button while a screen opens.
A new screen shortcut replaces the pending request. Disabling mod hotkeys or selecting native hotkeys cancels it.

### Extended controls

These require `[control].hotkeys_extended = true`, which is the default.

| Action | Default | Setting |
| --- | --- | --- |
| Alliance, Bookmarks | `ALT-'`, `B` | `show_alliance`, `show_bookmarks` |
| Save zoom preset 1 to 5 | `SHIFT-F1` to `SHIFT-F5` | `set_zoom_preset1` to `set_zoom_preset5` |
| Save current zoom as default | `CTRL-=` | `set_zoom_default` |
| Toggle locate, recall while previews are open | `CTRL-R`, `CTRL-T` | `toggle_preview_locate`, `toggle_preview_recall` |
| Toggle default, player, station, hostile, armada cargo | `ALT-1` to `ALT-5` | `toggle_cargo_default`, `toggle_cargo_player`, `toggle_cargo_station`, `toggle_cargo_hostile`, `toggle_cargo_armada` |

Alliance Help, Alliance Armada, and Lookup also require `[control].enable_experimental = true`.
Their defaults are `SHIFT-'`, `CTRL-'`, and `L`, with settings `show_alliance_help`, `show_alliance_armada`, and `show_lookup`.

### Session and diagnostics

| Action | Default | Setting |
| --- | --- | --- |
| Disable, enable mod hotkeys | `CTRL-ALT-MINUS`, `CTRL-ALT-=` | `set_hotkeys_disabled`, `set_hotkeys_enabled` |
| Reload settings | `CTRL-SHIFT-F6` | `reload_settings` |
| Restart the game | `F9` | `restart` |
| Quit the game | `F10` | `quit` |
| Log trace, debug, info | `CTRL-SHIFT-F7`, `CTRL-SHIFT-F9`, `CTRL-SHIFT-F8` | `log_trace`, `log_debug`, `log_info` |
| Log warnings, errors, off | `CTRL-SHIFT-F10`, `CTRL-SHIFT-F11`, `CTRL-SHIFT-F12` | `log_warn`, `log_error`, `log_off` |

Hotkey enable and disable bindings are checked before the hotkey-mode switch.
The complete definitions are in [`shortcuts.def`](../src/mod/shortcuts.def).

## Accepted key names

### Modifiers

| Modifier | Either side | Left only | Right only |
| --- | --- | --- | --- |
| Shift | `SHIFT` | `LSHIFT` | `RSHIFT` |
| Control | `CTRL` | `LCTRL` | `RCTRL` |
| Alt | `ALT` | `LALT` | `RALT` |
| Windows | `WIN` | `LWIN` | `RWIN` |
| Command | `CMD` or `APPLE` | `LCOM` or `LAPPLE` | `RCOM` or `RAPPLE` |

`ALTGR` selects AltGr. On macOS, `CTRL` means Control; use `CMD` for Command.

### Letters, numbers, and function keys

| Group | Names |
| --- | --- |
| Letters | `A` to `Z` |
| Number row | `0` to `9` |
| Function keys | `F1` to `F15` |
| Number pad | `KEY0` to `KEY9` |
| Number-pad operations | `KEYDIVIDE`, `KEYENTER`, `KEYEQUAL`, `KEYMINUS`, `KEYMULTI`, `KEYPERIOD`, `KEYPLUS`, `NUMLOCK` |

### Navigation and mouse

| Group | Names |
| --- | --- |
| Arrows | `LEFT`, `RIGHT`, `UP`, `DOWN` |
| Navigation | `HOME`, `END`, `PGUP`, `PGDOWN`, `INSERT`, `DELETE` |
| Common keys | `BACKSPACE`, `ESCAPE`, `RETURN`, `SPACE`, `TAB` |
| Other keys | `BREAK`, `CAPS`, `CLEAR`, `HELP`, `MENU`, `PAUSE`, `PRINT`, `SCROLL`, `SYSREQ` |
| Mouse buttons | `MOUSE0` to `MOUSE6`. `MOUSE0` is left, `MOUSE1` is right, and `MOUSE2` is middle. |

### Punctuation

Use `MINUS` for the minus key because `-` separates modifiers.
The pipe character `|` separates alternatives and cannot be bound as a key.

Other accepted symbols are:

```text
_ , ; : ! ? . ' ( ) [ ] { } @ * / " \ & # % ` ^ + < = > ~ $
```

TOML double-quoted strings need escapes for a backslash or double quote.
Literal strings avoid those escapes:

```toml
[shortcuts]
show_gifts = '\'
show_chat = '"'
```
