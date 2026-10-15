# Changelog

These are historical release notes. Settings, shortcuts, and build tools below describe those releases.
For current behavior, see [Features](docs/FEATURES.md), [Keyboard shortcuts](docs/KEYMAPPING.md), and [Contributing](CONTRIBUTING.md).

## 1.1.0

### Features

- Standardized naming on "mod" and added `AGENTS.md`. By @a5ehren.
- Added tech-slot sync. By @lightbull-stfc.
- Added splash and transition artwork. By @ChronoXNL.
- Added macOS version-string substitution. By @ttimbul.

### Fixes

- Fixed global and per-target `verify_ssl` parsing, including use without a proxy. By @joelverhagen.
- Disabled faulty features in the example settings. By @Bloody142.
- Temporarily ignored `disable_first_popup`. By @a5ehren.
- Pinned the protobuf Abseil dependency to `20260107.1` to match its inline namespace. By @ChronoXNL.
- Restricted the `setDepth` hook to Windows and corrected macOS signing entitlements. By @a5ehren.

### New contributors

@dependabot[bot], @ttimbul, @joelverhagen, and @wycats.

## 1.0.0

### Features

- Added macOS support and installation guides for macOS and Wine.
- Added maximum Alliance donation sliders and first-message notification suppression.
- Added a Windows title prefix for the `-ccm` profile.
- Improved multi-target sync, error handling, and battle logs. Added missions and officer shard counts.
- Added `Y` for Scrap Yard through `show_scrapyard`.

New settings and controls:

| Setting or control | Purpose |
| --- | --- |
| `ui_scale_viewer` | Scale object viewers |
| `select_current` | Locate the selected ship in dock |
| `UiViewerScaleUp`, `UiViewerScaleDown` | Adjust viewer scale during play |
| `toggle_queue`, `action_queue`, `action_queue_cancel` | Control the Kir'Shara queue |
| `action_quit` | Exit immediately |
| `select_timer` | Set the ship-selection double-tap interval, default 500 ms |
| `disable_veil_chat` | Hide Veil Chat |

### Fixes

- Repaired compatibility after Unity upgrades and restored warp cancellation.
- Made `ui_scale = 0.0` and `ui_scale_viewer = 0.0` disable scaling.
- Stopped `zoom_min` and `zoom_max` from setting entry zoom. Only presets can do that.
- Reported missing hook methods instead of crashing.
- Prevented a crash when a widget has no visibility controller.

### Development

- Switched to XMake.
- Logged the paths for log, settings, vars, and battle-log files.
- Changed the GitHub source to netniV's repository.

## 0.6.0

### Features and fixes

- Added configurable hotkeys and an example settings file.
- Added held UI-scale controls and `ui_scale_adjust` for their step size.
- Added switches to block ship location while a preview is open, Escape-to-exit, and keyboard movement.
- Added session-only cargo toggles and zoom-preset saving.
- Replaced ObjectFinder to improve speed.

Renamed `community_patch_settings_parsed.toml` to `community_patch_runtime.vars`, expanded its header comment, and improved invalid-value reporting.

### Shortcut additions

| Keys | Action |
| --- | --- |
| `N`, `R`, `U` | Manage ship, repair ship, show Research |
| `CTRL-ALT-MINUS`, `CTRL-ALT-EQUAL` | Disable, enable hotkeys |
| `F9`, `F11` | Set log level to debug, info |
| `SHIFT-T` | Show Away Team |
| Forward slash, backslash | Show Gifts, Alliance |
| `CTRL-L`, `CTRL-R` | Toggle locate, recall while a preview is open |
| `ALT-1` to `ALT-5` | Toggle default, player, station, hostile, and armada cargo |
| `SHIFT-F1` to `SHIFT-F5` | Save zoom presets for the session |

Alpha-only additions were Shift-backslash for Alliance Help, Ctrl-backslash for Alliance Armada, and `L` for Lookup.

## 0.5.2

- Repaired free window resizing.
- Limited staying in a bundle after its summary to the officer and event stores.
- Added player names to battle-log sync.
- Fixed trait sync.
- Added active-mission sync.

## 0.5.0

Recorded as planned in the original notes:

- Generate a default TOML file and parsed output showing effective settings.
- Add minimum, default, and maximum zoom shortcuts.
- Add shortcuts for missions, station views, and chat.

## 0.4.0

No details recorded.

## 0.3.8

Added the drydock `H` hotkey.

## 0.3.2

Added Unity 2020.3 LTS compatibility.

## 0.3.1

- Added an option to hide Galaxy Chat.
- Fixed cases where Space did not trigger.

## 0.3.0

- Added `R` to recall when no other selected item uses that action.
- Retired `fix_iss_jellyfish_warp`, `fix_stella`, and `fix_faction` after fixes in the game client.
- Restored free window resizing.
- Extended Alliance donations to the amount needed for the next Alliance level.
- Extended ship-selection hotkeys to seven ships.

## 0.2.0

Initial release.
