# In-game Mod Settings

Open the game's Settings and choose **Mod Settings** to change Community Mod preferences. Pages group controls by task: camera, interface scale, travel, cargo, claims, officers, chat, notifications, HUD buttons, and fleet labels. Search accepts labels and canonical TOML names such as `ui.show_player_title`.

The editor uses the same configuration values and defaults as the mod's TOML configuration. Opening a page or reading a value does not save, normalize, or reset it. TOML remains available for every setting; **Advanced & Configuration** shows the active file's location.

## Applying and saving

Ordinary controls apply to the current session and save automatically. Each control reports its own save state. A failed save or an external edit conflict remains visible; another successful save does not clear it. Resolve a conflicting external edit before retrying. Unrelated TOML values and comments are preserved.

Save feedback describes the current value. A later session-only change cannot retain an older Saved badge. Explicit save-preset/default shortcuts persist their zoom values; automatically using the last preset as the session default remains session-only. After a save attempt in the current session, Defaults & Help offers Save current value for a later session-only change.

Slider and scale-shortcut changes apply immediately and save after 250 ms without another change to that value. Orderly exit flushes pending changes. If the game vetoes a quit request, saving resumes for the continuing session. A failure to write the generated runtime-vars diagnostic does not prevent startup with a readable configuration.

Some controls take effect when their screen is reopened. Restart controls retain the requested preference separately from the active value, so a saved restart change is not presented as already active. Examples include native-game shortcut mode, window setup, loading assets, cargo formatting, officer sort initialization, and chat channel suppression.

Open a page's **Defaults & Help** to read setting details, restore one default, retry a failed save, or review a page reset. Reset uses the default declared by the setting. A page reset applies each available setting once, including a number shown both as an exact entry and a convenience slider. The confirmation reports unavailable settings it will skip. If an included setting changes while confirmation is open, confirm again from a fresh dialog. Changes save individually; the page reset is not a transactional file batch. It leaves unrelated settings and other pages alone.

## Numbers and lists

Use **Edit** for an exact numeric value. Convenience sliders have useful ranges; an existing custom value outside that range remains visible in the exact editor and is preserved until explicitly changed. Numeric entries reject invalid syntax, non-finite numbers, and values outside a setting's valid domain. Integer controls require whole numbers. Units and setting-specific limits appear with the control.

Ship filter lists accept comma-separated names using the same matching rules as TOML. Instant warp filters accept `*`; pinned ships require names. Faction and banner lists validate names against the startup parser rather than silently discarding unknown entries. Clear a list explicitly to remove its entries.

The loading image control accepts a local file path. Empty selects the embedded fallback. Selecting a path does not copy, move, or modify the image file, and the loading asset changes require restart.

## Shortcuts

The shortcut editor covers the mod's registered actions, including actions currently bound to `NONE`. Each action supports multiple alternatives, recording a replacement or additional binding, removing a binding, restoring defaults, and undoing the most recent removal/default restoration.

Recording waits for keys to be released. Confirm the proposed binding to apply it, or cancel with Escape. Losing application focus cancels recording. Gameplay shortcuts remain suppressed while an editor owns keyboard input and until held keys have been released.

Overlap warnings help identify shared keys. Some actions intentionally share bindings in different contexts, so warnings do not remove another binding automatically. Unsupported native adapters leave the relevant action unavailable for that session. Changing extended shortcut availability or the game's own shortcut mode requires restart. Experimental actions remain visible when disabled and require their TOML option and restart.

## Advanced settings kept in TOML

These categories are explained in the Advanced page rather than exposed as ordinary live controls:

| Category | Reason |
| --- | --- |
| `[patches]` | Emergency hook-install compatibility switches require restart. They are separate from feature preferences. |
| `[sync]` and `[sync.targets.<name>]` | Dynamic service targets, credentials, connection options, and worker configuration require a separate secret-safe editor. Token values are never shown in the settings UI. Restart after editing. |
| `audio.trace_events`, `audio.disabled_events` | Diagnostic event tracing and arbitrary internal event-name filters remain advanced configuration. |
| `[config]` endpoint overrides | Configuration/distribution infrastructure is configured before the in-game editor starts. Restart after editing. |
| `control.enable_experimental` | Enables experimental shortcuts at startup. Configure it in TOML and restart. Disabled actions remain visible with a reason. |
| `graphics.system_pan_momentum`, `graphics.transition_time`, `ui.disable_toast_banners` | Legacy parser inputs with no current gameplay consumer. Pan glide uses `graphics.system_pan_momentum_falloff`; banner suppression uses `ui.disabled_banner_types`. |

`graphics.free_resize`, `graphics.borderless_fullscreen`, `graphics.allow_cursor`, `ui.instant_cargo_counter` and `ui.notify_banner_types` have Windows-only consumers. The macOS editor explains their platform scope instead of offering controls that cannot affect that client.

See the English example configuration for accepted advanced values. Deprecated aliases remain startup compatibility inputs; the editor writes canonical keys. Old queue feature switches and constants that the parser no longer reads are not extra controls.

## Coverage and maintenance

[settings-coverage.json](settings-coverage.json) inventories all 154 canonical configuration keys and 105 canonical shortcut actions. The feature includes 87 player-setting controls on Windows and 82 on macOS. The remaining 28 named non-hook options and 39 hook-install switches have explicit TOML-only treatment. Dynamic sync target fields follow the same sync policy. The emergency switches `patches.nativesettingshooks` and `patches.runtimeconfighooks` independently control native settings and the shared persistence runtime.

`settings/upstream_settings.cc` defines controls with their canonical key, default, help, unit, application timing, availability, validation, and persistence serializer together. Shared instant-warp and Forbidden Tech models retain their own definitions so native settings and the mod page use the same state. Live shortcut and pin actions reuse the same field serializer through `PersistCurrentSetting`; they do not maintain a second save whitelist. A new option should receive either an in-game definition or an explicit inventory exclusion. A new shortcut should appear through the registered action catalog.

Both Windows and macOS use the shared models, persistence, and page definitions. Platform-specific code handles native UI and input. A successful build does not establish native hook compatibility or player smoke coverage for another client build or architecture.

## Local validation and player checks

Run `pwsh -NoProfile -File tests/run-settings.ps1` for the standalone models, save/reload, conflicts, shutdown and file-permission fixtures on Windows. Run `python -m unittest discover -s tests -p test_settings_coverage.py` to check the catalog inventory. These are local development checks and are not added to CI.

For each candidate artifact, record its source commit, client build, platform and architecture. macOS ARM64 and x86_64 require separate build and native-fit evidence; results on one architecture do not qualify the other. The native adapter checks loaded Mac method boundaries before installing detours and leaves unsupported controls unavailable.

Use reversible preferences for these player checks; no claim or purchase is required:

1. Open Mod Settings, visit each category, search a label and a TOML key, follow a result, and use Back. Type a query, Alt-Tab away and back, then edit and clear the query on the same page. Close and reopen Settings; verify the game's own pages still work.
2. Toggle a live option, reopen its page, then restart the client and check the saved value. Confirm native Fleet Commander preferences still use their existing game storage.
3. Enter an exact number, an invalid number, and a valid value outside its convenience slider range. Cancel an edit and verify no change or save occurred. Check interface scale at the smallest supported window size.
4. Change a restart preference and verify it shows pending restart while the active behavior stays unchanged. Restore it afterward.
5. Review a single default and a page reset. Cancel each once, then confirm with reversible preferences. Check a conditional slider and its exact editor reset only once.
6. Edit a ship list, faction list, banner list, and optional loading-image path. Check empty values, invalid entries, and a path containing spaces or non-ASCII characters.
7. Record, cancel, replace and remove a shortcut; restore its default and undo. Include an initially `NONE` action and a modifier combination. Verify no gameplay hotkey fires while typing, recording, or releasing held keys. Use Command combinations on Mac.
8. Switch HUD buttons through Auto, Always and Never and restore Auto. Switch fleet labels through Native and overrides, change thresholds, then change system/scene and reopen Settings.
9. With a copy of the config, exercise a read-only file and an external edit to the same key. Check the failure stays visible after saving another key. Resolve the conflict, retry that key, and verify unrelated comments and values survive. Quit immediately after an edit and check it survives the next launch.
