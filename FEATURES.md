# STFC Community Mod Features

The STFC Community Mod adds configurable quality-of-life improvements to the desktop version of Star Trek Fleet Command. This catalog describes the user-facing features represented by the current source code and English example configuration.

Most options are configured in `community_patch_settings.toml`. The mod creates a default settings file when one is missing and also writes a parsed settings file so you can see the values that were applied. Nearly every shortcut can be remapped; see [`KEYMAPPING.md`](KEYMAPPING.md) for valid key names.

## Display and window controls

- Set the overall interface scale and the amount changed by each scale adjustment.
- Scale object viewers independently from the main interface.
- Scale ship models in System View independently from the interface.
- Adjust the interface, viewer, and ship scales while playing with configurable shortcuts.
- Enable freely resizable game windows on Windows.
- Enable borderless fullscreen mode and switch between fullscreen and windowed mode without restarting the game.
- Control whether the game replaces the system cursor.
- Adjust the frame-rate scaling factor used by visual effects.
- Set the duration of screen transitions.

## System View camera and navigation

- Increase the maximum System View zoom distance.
- Set the zoom level used when entering a system.
- Configure five System View zoom presets.
- Zoom in, zoom out, jump to minimum or maximum zoom, and reset zoom by shortcut.
- Save the current zoom or a preset as the new default.
- Configure keyboard zoom speed.
- Configure System View pan momentum and momentum falloff.
- Use keyboard movement controls for System View, or disable those movement keys.
- Configure player and non-player fleet labels to use native, expanded, compact, or zoom-threshold-based detail.
- Configure separate fleet-label zoom thresholds for player and non-player fleets.

## Hotkeys and screen navigation

- Enable or disable all hotkeys.
- Choose between the community mod hotkeys and Scopely's native hotkeys.
- Enable an extended set of mod shortcuts.
- Remap shortcuts in the settings file, including multiple keys or mouse buttons for one action.
- Open or focus commonly used screens directly, including:
  - Alliance, Alliance Armada, and Alliance Help.
  - Artifact Gallery, Away Teams, Battle Reports, Bookmarks, and Chat.
  - Command Center, Daily Missions, Events, ExoComp, Factions, and Fleet Commander.
  - Galaxy View, System View, Station Exterior, and Station Interior.
  - Gifts, Haven, Inventory, Missions, Officers, Q Trials, Refinery, Research, and Scrap Yard.
  - Settings, shield selection, ship construction, ship management, and ship swapping.
- Open Alliance, Galaxy, and Private chat tabs directly.
- Focus the current screen's search field with a shortcut.
- Use configurable primary-action, alternate-action, queue, recall, repair, and cancel shortcuts.
- Select ships 1 through 8 and configure the double-tap timing used to locate a selected ship.
- Recall a selected ship or perform context-sensitive actions against mines, players, hostiles, and other targets.
- Clear focused text or close chat with Escape.

## Combat and ship interaction

- Enable the Kir'Shara attack queue by default when the artifact is owned.
- Add targets to the Kir'Shara queue, clear the queue, or toggle queuing by shortcut.
- Use a configurable primary action for context-sensitive attacks, warps, and cancellations.
- Choose the automatic action for the instant-warp confirmation: show the dialog, use regular warp, or use instant jump.
- Configure ship-name lists that always use regular warp, always use instant jump, or always show the confirmation dialog.
- Cycle the current instant-warp behavior by shortcut.
- Keep the warp action label consistent with the selected automatic action.
- Automatically confirm Forbidden Tech level and tier upgrades when enabled.

## Cargo and target previews

- Show cargo or rewards automatically in the default target preview.
- Configure cargo visibility independently for player ships, stations, hostiles, and armadas.
- Toggle each cargo-preview mode while playing.
- Switch between cargo and the normal target view with a shortcut.
- Close pre-scan and target viewers with Escape.
- Allow or block locate and recall actions while previews are open, with runtime toggles for both behaviors.
- Show cargo totals immediately instead of playing the native count-up animation on supported Windows clients.
- Configure the number of decimal places used for abbreviated cargo values such as `1.25M`.

## Fleet and officer management

- Select the previous or next ship in the fleet-management dock with the Left and Right arrow keys.
- Double-click a ship in the fleet-management dock to assign it.
- Pin configured ship names to the front of the fleet-management dock while preserving the selected sort order.
- Restore the **Below Deck Ability** sorting option to officer roster and assignment screens.
- Show a ship's out-of-dock power while it is docked.

## Rewards donations and Daily Goals

- Skip chest-reveal animations and open rewards immediately.
- Extend the Alliance donation slider to a configurable maximum.
- Extend supported tagged chest-purchase sliders to a configurable maximum on Windows.
- Open the bulk gift-claim flyout automatically when entering Gifts.
- Exit the relevant section after gift collection through the mod's streamlined gift flow.
- Apply the Daily Goals **Claim All** behavior only to selected Federation, Klingon, or Romulan goals.
- Force the Daily Goals **Claim All** toggle on the first time the screen is opened in a session.

## Chat interface and HUD

- Hide Galaxy Chat.
- Hide Veil Chat.
- Open Chat full-screen or docked on either side by shortcut.
- Control the visibility of the Daily Goals, Field Training, Missions, Outposts, and Q Trials HUD buttons.
- Set each supported HUD item to native automatic behavior, always shown, or always hidden.
- Prevent Escape from opening the game-exit prompt.
- Optionally require a configurable double press of Escape before showing the exit prompt.

## Banners audio and popups

- Suppress selected toast-banner types.
- Send desktop notifications for selected banner types.
- Disable standard toast banners through the experimental global toggle.
- Suppress selected audio events by event name.
- Trace audio event names to the log to help build a suppression list.
- Automatically confirm Discovery prompts.

## Loading and transition screens

- Replace the login loading background with the mod's embedded artwork.
- Load a custom loading-screen image from a configured path.
- Scale the loading-screen logo.
- Show rotating community-mod tips on the loading screen.
- Reuse the loading artwork during transitions.
- Use a black transition background instead of artwork.

## Data synchronization

- Sync selected game data to one or more configured community services.
- Configure independent sync targets, including custom endpoints and access tokens.
- Sync any combination of:
  - Battle logs.
  - Buffs.
  - Buildings.
  - Inventory and resources.
  - Jobs and missions.
  - Owned officers, ships, and Forbidden Tech.
  - Research.
  - Slots.
  - Traits.
- Use the built-in target definitions for STFC Data, Spock's Club, and Next Spock's Club.
- Route sync traffic through a proxy.
- Enable or disable SSL certificate verification.
- Configure resolver caching, diagnostic logging, and detailed debug logging.

## Reliability and configuration support

- Apply client-specific crash fixes and compatibility patches.
- Generate a default TOML settings file if no configuration exists.
- Generate a parsed TOML file showing the settings actually applied at runtime.
- Enable or disable patch groups independently in development builds.
- Log mod initialization, patch installation, and optional sync or audio diagnostics for troubleshooting.
- Support both Windows and macOS through platform-specific injection and loading components.

## Platform and availability notes

- Windows and macOS do not necessarily expose every feature in exactly the same way.
- Free window resizing and extended tagged chest-purchase sliders are Windows-specific.
- The instant cargo counter is guarded for a supported Windows client build; unsupported builds retain the native animation.
- Some settings are experimental or diagnostic and may be disabled, client-version-specific, or intended for troubleshooting.
- Patch availability can change when the game client changes because the mod hooks the native IL2CPP runtime.

## Default shortcut overview

The default shortcut map includes the following high-use controls. All of these can be changed in the settings file.

| Action | Default shortcut |
| --- | --- |
| Zoom presets | `F1` to `F5` |
| Zoom in or out | `Q` or `E` |
| Minimum maximum or default zoom | `BACKSPACE` `MINUS` or `=` |
| Main interface scale | `PGUP` or `PGDOWN` |
| Viewer scale | `SHIFT-PGUP` or `SHIFT-PGDOWN` |
| Ship model scale | `CTRL-PGUP` or `CTRL-PGDOWN` |
| Open full-screen chat | `C` |
| Open side chat | `ALT-C` or `` ` `` |
| Select or focus ships | `1` to `8` |
| Primary action or queue action | `SPACE` or `MOUSE1` |
| Recall or alternate action | `R` |
| Toggle cargo view | `V` |
| Toggle Kir'Shara queue | `CTRL-Q` |
| Clear Kir'Shara queue | `CTRL-C` |
| Focus search | `CTRL-F` |
| Toggle borderless fullscreen | `F11` |

For the complete shortcut list and the names accepted by the configuration parser, see [`README.md`](README.md) and [`KEYMAPPING.md`](KEYMAPPING.md).
