# Install the mod

Download a build from the [release list](https://github.com/STFC-Mod/mod/releases/).
Expand **Assets** to see the files. Versioned and unversioned ZIPs contain the same files.

If a game update breaks the release, check **#info** in the
[STFC Community Mod Discord](https://discord.gg/PrpHgs7Vjs) for a hotfix.

Choose your platform: [Windows](#install-on-windows), [macOS](#install-on-macos), or [Wine](#install-under-wine).

## Install on Windows

Close the game before you replace an existing mod.

1. Download `stfc-community-mod.zip` and extract `version.dll`.
2. Open the game folder that contains `prime.exe`.
3. Put `version.dll` beside `prime.exe`.
4. Start the game. A loaded mod creates `community_patch.log` and a settings file if one is missing.
5. To change the defaults, follow [Change settings](#change-settings).

The usual game folder is:

```text
C:\Games\Star Trek Fleet Command\Star Trek Fleet Command\default\game
```

If your game is elsewhere, use [Find the Windows game folder](#find-the-windows-game-folder).
Keep `version.dll` out of the official launcher's folder. A DLL there can stop the launcher and game updates from working.

To verify an attested download, see [Windows DLL verification](WINDOWS_DLL_PROVENANCE.md).

## Install on macOS

Current source builds require **macOS 14.6 or later**.
Check the requirements of the release you download.

1. Download `stfc-community-mod-installer.dmg`.
2. Open the DMG and drag **STFC Community Mod** to **Applications**.
3. Open **STFC Community Mod**. If macOS blocks the app, use **System Settings > Privacy & Security > Open Anyway**.
4. Start the game from this launcher. If prompted, let it re-sign the game to add the entitlements needed by the mod.
5. To change the defaults, follow [Change settings](#change-settings).

Use the Community Mod launcher each time. Starting the game from the official launcher does not load the mod.
A game update can remove the required entitlements, so the Community Mod launcher may ask to re-sign it again.

## Install under Wine

Use the Windows mod and follow the [Windows steps](#install-on-windows).
The game folder is inside your Wine prefix, usually at:

```text
drive_c/Games/Star Trek Fleet Command/Star Trek Fleet Command/default/game
```

Set the `version` DLL override to **native, then builtin** in `winecfg` or your launcher's runner settings.
You can also set this environment variable before launch:

```sh
export WINEDLLOVERRIDES="version.dll=n,b"
```

## Change settings

Edit **`community_patch_settings.toml`**. The other files are outputs.

| File | Purpose |
| --- | --- |
| `community_patch_settings.toml` | Your settings. Missing keys use defaults. |
| `community_patch_runtime.vars` | Parsed values, including defaults. Edits here have no effect. |
| `community_patch.log` | Startup, settings errors, and diagnostics. |

By default, these files are beside `prime.exe` on Windows and Wine.
On macOS, they are in:

```text
~/Library/Preferences/com.stfcmod.startrekpatch
```

For commented settings, copy an example to that folder and name it `community_patch_settings.toml`.
Back up an existing settings file before replacing it.

| Language | Example |
| --- | --- |
| English | [Current settings reference](configuration/example_community_patch_settings_en.toml) |
| German | [German example](configuration/example_community_patch_settings_de.toml) |
| French | [French example](configuration/example_community_patch_settings_fr.toml) |
| Dutch | [Dutch example](configuration/example_community_patch_settings_nl.toml) |

The English example tracks the current code. Translations can lag behind.

Settings reload after you save the file. To reload manually, press `CTRL-SHIFT-F6`.
Changes under `[patches]` require a game restart.
Shortcuts that adjust settings during play do not save those changes to your TOML file.

## Fix installation problems

Start with the log and runtime values.

| Problem | Check |
| --- | --- |
| No mod log on Windows | Put `version.dll` beside the running game's `prime.exe`. Check **Properties > General > Unblock** on the DLL. |
| Mod does not load on macOS | Start the game through **STFC Community Mod** and accept its entitlement repair prompt. |
| Mod does not load under Wine | Set the `version` DLL override to `n,b`. |
| A setting has no effect | Check its section, spelling, and value type. Compare `community_patch_runtime.vars` and read the log for parse errors. |

### Find the Windows game folder

1. Start the game and open **Task Manager**.
2. Find the game process. Expand its group if needed.
3. Right-click `prime.exe` and choose **Open file location**.
4. Check that the folder contains `prime.exe`, then close the game before installing the DLL.

### Open the macOS settings folder

In Finder, press `Command-Shift-G` and enter:

```text
~/Library/Preferences/com.stfcmod.startrekpatch
```

This works even when Finder hides `Library`.
If the launcher shows game version `-1`, reinstall the game, then open the Community Mod launcher again.
You do not need to reinstall the mod launcher.

For further help, use the [Discord server](https://discord.gg/PrpHgs7Vjs).
