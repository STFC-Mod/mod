# Features

The mod adds controls and display options to the desktop game.
Change them in `community_patch_settings.toml`; the
[English example](configuration/example_community_patch_settings_en.toml) lists the settings and defaults.
See [installation](INSTALL.md#change-settings) for file locations and [keyboard shortcuts](KEYMAPPING.md) for controls.

Features depend on the game client. A client update can break a hook until the mod is updated.

## Display and camera

| Area | Controls |
| --- | --- |
| UI scale | Adjust the main interface, object viewers, and system-view ship models separately. |
| System zoom | Set the maximum range, entry zoom, keyboard speed, and five presets. Save a preset or the current zoom as the default. |
| Camera movement | Pan with configurable keys and set momentum falloff. Keyboard panning can be disabled. The camera can also stop following a selected target, so you can pan away from it. |
| Station interior | Zoom further out inside the station. `graphics.interior_zoom_scale` multiplies the native distance; `1.0` keeps it. |
| Haven | Extend camera range while keeping road surfaces above the ground. `graphics.haven_zoom = 0.0` uses the native range. |
| Fleet labels | Choose native, expanded, compact, or zoom-threshold detail separately for player and non-player fleets. |
| Window and cursor | Resize the game window freely on Windows and control whether the game replaces the system cursor. |
| Anomaly timer | Show the remaining time in a Galactic Anomaly system. Disabled by default. |

`fr_scale` scales system-view backdrop renderables.
The code reads `transition_time`, `system_pan_momentum`, and `borderless_fullscreen`, but does not currently apply them.
F11 does not toggle borderless mode in this build.

## Keyboard controls

- Choose mod hotkeys or the game's native hotkeys. Remap actions, including alternatives for the same action.
- Open screens, select chat tabs, and focus a search field by shortcut.
- Select ships 1 through 8. Configure the double-tap interval that locates a selected ship.
- Use context-sensitive attack, warp, recall, repair, cancel, and cargo actions. The primary action also joins a wave
  defense from its course or viewer card.
- Clear text focus with Escape and control whether Escape opens the exit prompt, including an optional double press.

Extended shortcuts add controls such as cargo toggles and zoom-preset saving.
Experimental shortcuts require both extended and experimental controls to be enabled.
The [shortcut reference](KEYMAPPING.md) lists the defaults.

## Combat, cargo, and fleets

| Area | Behavior |
| --- | --- |
| Kir'Shara queue | Start with queuing enabled, add targets, clear the queue, or toggle it. Requires ownership of the artifact. |
| Kir'Shara fixes | Keep a queue moving when its target vanishes mid-course or the watchdog prunes it, and keep it from being cleared when moving another ship briefly misreports the fleet's position. `control.kirshara_fixes = false` turns all three off. |
| Instant warp | Show the confirmation, use regular warp, or jump automatically. Ship-name filters can override the choice. |
| Target previews | Show cargo by target type, toggle cargo view, and close viewers with Escape. Locate and recall can remain enabled while previews are open. |
| Cargo numbers | Choose decimal places for abbreviated totals. Supported Windows clients can skip the count-up animation. |
| Ship assignment | Select adjacent ships with arrow keys, double-click to assign, and pin named ships while preserving sort order. Shift+click a ship tile to pin or unpin it; only the `pinned_ships` line of the settings file changes. |
| Officer sorting | Restore **Below Deck Ability** in roster and assignment screens. |
| Ship power | Show out-of-dock power while a ship is docked. |
| Forbidden Tech | The selected item shows the ship it is equipped on, including ships without a dock. |
| Confirmations | Skip the confirmation for Forbidden Tech upgrades, the Alliance contribution max-level warning, and the U.S.S. Excelsior's Decloaking Deflector. Each is off by default. |

## Rewards and interface

- Skip chest reveals, open bulk gift claims, and leave the section after the supported gift-collection flow.
- Extend Alliance donations and supported tagged chest-purchase sliders.
- Limit Daily Goals **Claim All** to selected Federation, Klingon, or Romulan goals. Its toggle can start enabled each session.
  `ui.daily_bulk_claim_factions` lists the factions; blank means all three, and `none` by itself excludes all of them. Other dailies stay eligible.
- Hide Galaxy or Veil Chat, and player titles under chat names. Set supported HUD buttons to automatic, always shown, or always hidden.
- Open alliance side chat once per login, once the station and chat have finished loading, and set side chat's
  starting width. Both are off by default.
- Hide **Exchange All** in the artifact-shards popup while retaining single exchanges.
- Restore the native **Research** actions on Alliance Games Store items, and refresh the store's currencies and
  visible offers after it reloads them.

The supported HUD buttons are Field Training, Missions, Outposts, and Q Trials.
Other options show the newest Haven help-history entries first, limit ads to the first popup each session, and confirm Discovery prompts.

## Banners and audio

- Hide selected banner types or use the experimental global banner switch.
- Send desktop notifications for selected banners on Windows.
- Play optional victory, defeat, and Armada sounds on Windows and macOS.
- Suppress named audio events, or use `All` to block named events that start or resume audio.
- Log audio event names to help build a suppression list.

If a notification's text binding is unavailable, unresolved placeholders remain visible and the mod logs a warning.
The log retains five previous sessions as `.log.1` through `.log.5`.

## Loading screens

Replace the login background with the embedded artwork or a custom image.
Adjust logo scale and show rotating mod tips.
Transitions can reuse the artwork or use a black background with logos.

## Data sync

Sync selected data to STFC Data, Spock's Club, Next Spock's Club, or custom endpoints.
Each target has its own options and access token.

| Category | Data |
| --- | --- |
| Progress | Research, buffs, traits, jobs, and missions |
| Assets | Buildings, inventory, resources, officers, ships, Forbidden Tech, and slots |
| Haven | Owned building pieces and map buildings, with status and position. Maps identified as another player's are ignored. |
| Battles | Up to 300 recent battle journals per session |
| Fleets | Live fleet state: each state change with cargo, mining and timers, and the server's view of deployed fleets. Every change is sent, not just the latest; up to 300 per session. Off unless a target sets `fleets`. |
| Store | Faction store offers: favor level prices, requirements and remaining purchases. Off unless a target sets `store_offers`. |
| Profile | Your own level, XP, Power and Haven unlock. Other players' records are never sent. Off unless a target sets `profile`. |

Failed uploads retry with the latest observed values.
Target changes apply on reload and trigger a fresh snapshot.
Recent battle journals can replay after a restart or login.
Proxy, certificate verification, name-cache, and diagnostic options are in the settings example.

## Compatibility switches

During an in-process game restart, including one initiated by the server, the mod pauses its frame work and
shortcuts while the game continues updating. Pending UI actions and session-owned UI references are cleared.
Mod frame work resumes after the game has left login and section changes have settled for three seconds.
The log records these steps under `[Reload]`, warns once if readiness takes over a minute, and records the first
system or galaxy view reached afterward. These guards reduce interference during reload; they do not repair a
failed game connection or asset download.

`[patches]` keys disable hook groups in every build. Changes require a restart.
Use them to isolate a broken hook or a conflict with another mod.
Normal feature settings remain separate and can change during play.
