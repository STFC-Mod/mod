# Changelog for STFC Community Mod

<p align="center">
  <img src="https://repository-images.githubusercontent.com/693298224/6a87716f-3dbb-48a5-80e0-709e0a1ad133" alt="STFC Community Mod">
</p>

These are historical release notes. Settings, shortcuts, and build tools below describe those releases.
For current behavior, see [Features](docs/FEATURES.md), [Keyboard shortcuts](docs/KEYMAPPING.md), and [Contributing](CONTRIBUTING.md).

## Unreleased

Changes on `dev` since the `v1.1.10.0` tag. Pending working-tree changes are not included.

### Features and fixes

- Added dock switching in the ship management screen.
- Added opt-in live fleet-state sync through `fleets` and enabled Kir'Shara queue protections on macOS.
- Fixed wave joining and chat auto-opening, Alliance Games Store research and purchase refresh,
  galaxy-map drag momentum, and station-exterior zoom restoration after visiting the interior.
- Fixed fleet-sync number conversion on supported macOS versions and the first-sync flag for Haven.
- Improved Galactic Anomaly timer recovery across HUD canvases.
- Fixed initial system zoom and added guards during restart and macOS runtime shutdown.
- Made sync delivery track acknowledgements independently for each target, retry failed uploads,
  and reject work from previous login sessions. Sync workers retain settings safely across reloads.
- Improved pooled inventory-widget tracking, mission HUD refreshes, ship pinning, shortcut sequencing,
  and notification text substitution. Store-popup suppression now keeps the first popup per session.

### Development

- Replaced XMake with Zig builds and packaging, supporting Zig 0.16 and 0.17.
- Added typed IL2CPP bindings, shared Unity helpers, and transactional feature hook installation.
- Updated SPUD, reorganized sources under `src`, and expanded native tests.
- Hardened the macOS loader and launcher and rewrote installation and contributor guides.
- Restricted release tags to their appropriate branches.

## 1.1.10

Includes changes from the 1.1.9 beta series.

### Features

- Added cross-platform notification audio cues and an `All` wildcard for named audio suppression.
- Added an opt-in Galactic Anomaly countdown, configurable instant cargo totals, and automatic
  Warp/Jump labels on the system course button.
- Added station-interior zoom-out, expanded Haven zoom with road-surface detail, and applied backdrop
  scaling on system entry. Added an option to disable target camera lock.
- Added Shift+click ship pinning, preferred ships with applied Forbidden Tech when pinned levels tie,
  and expanded ship-management, ship-swap, officer-swap, and officer-preset shortcuts.
- Added options to hide chat player titles and the artifact Exchange All button.
- Added confirmation skipping for the Excelsior's Decloaking Deflector and the Alliance max-level
  contribution warning, plus Forbidden Tech names in selection lists.
- Added sync for Away Team assignments and trait demand, Haven building pieces and map data,
  and opt-in local-player profiles and faction-store offers.
- Added log history and newest-first Haven help history.

### Fixes

- Protected and recovered Kir'Shara queues after target or address changes.
- Restored battle-log and ship sync for client build 267 and re-emitted job events when timing changes.
- Fixed artifact-gallery navigation, mission HUD visibility, player titles on Windows, and ship shortcut
  hints when dock indices have gaps.
- Preserved 64-bit ship locale IDs and fixed x64 trampoline offsets when multiple instructions expand.
- Made empty shortcut strings disable bindings, coordinated WASD movement with configured hotkeys,
  and corrected feature and patch enablement.
- Made daily faction bulk-claim defaults explicit and primary-action target visibility protection opt-in.

## 1.1.8

Includes changes from the 1.1.7 beta series.

### Features

- Added faction filtering for daily bulk claims, configurable fleet-label detail, and native navigation actions.
- Added construction, battle-log, and shield shortcuts, and displayed mod bindings in native shortcut hints.
- Improved ship management and added arrow-key rotation of the station exterior.
- Added named audio suppression, per-ship instant-jump configuration, and tagged-chest purchase sliders.
- Restored configurable double-tap Escape exit and added a clear-cache-and-restart shortcut.
- Added macOS `-ccm` settings-path overrides, Haven support, and Windows DLL build provenance and attestation.

### Fixes

- Fixed text-input focus detection, modified directional hotkeys, and armada notifications.
- Fixed bulk-claim flyouts reopening after reward presentation and limited daily faction filtering to
  Federation, Klingon, and Romulan dailies.
- Extended donation sliders on macOS, corrected base-raid defender names in battle logs, and fixed
  the garbage-collector finalizer callback.

## 1.1.6

Includes changes from the 1.1.5 beta series.

### Features

- Added mission HUD visibility settings, cargo-format precision, BDA officer sorting, and ship-level scaling.
- Added automatic confirmation for Forbidden Tech upgrades and instant warp.
- Added left/right arrow navigation for artifacts and translated example settings.

### Fixes

- Restored sync compatibility with M93.3.0 and corrected RTC slot parsing and Alliance resource sync names.
- Fixed macOS ARM64 loading-transition crashes, stale zoom scene pointers, and Right Alt detection.
- Restored unlimited donation sliders and stopped converting empty legacy sync targets.
- Removed the obsolete resolution-list patch and improved local universal macOS packaging.

## 1.1.4

### Features

- Added Ctrl+F coordinate search and automatic opening of the Gifts bulk-claim flyout.
- Added a build option to preserve the game's original loading background.

### Fixes

- Restored slot sync and updated bindings for client 1.000.50296.
- Hardened shortcut parsing and allowed recall and repair while a viewer popup is open, preserving scan priority.
- Honored patch switches in release builds and made the GameServer initialization sync hook optional.
- Fixed macOS zoom-range limits and loading-screen assets across scene transitions.

## 1.1.2

### Features

- Added zoom scaling and camera controls, custom embedded loading artwork, and loading-logo fade-in.
- Added the notification foundation and battle text, plus `ALL` logging selection.
- Added compressed update artifacts for the macOS launcher and improved updater diagnostics and packaging.

### Fixes

- Fixed sync parser crashes and the changed macOS BuffService parser signature.
- Fixed chained Xsolla patch updates and loading-texture resets.
- Removed the remaining system-view border and required the same fleet key for double-tap locate.

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
