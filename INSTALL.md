# Star Trek Fleet Command - Community Mod

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3">
  <img src="https://img.shields.io/github/sponsors/netniv" alt="Sponsorship">
</p>

<p align="center">
   A community mod (patch) for PC and macOS that adds a couple of tweaks to the <b>Star Trek Fleet Command&#8482;</b> game
</p>

## Downloads / Releases

The STFC Community Mod is available on GitHub.

You can download either the [latest official release](https://github.com/STFC-Mod/mod/releases/latest/) or any other
release, including [alpha/beta](https://github.com/STFC-Mod/mod/releases/) builds.

In the event that a game update breaks the latest release, please visit the [STFC Community Mod](https://discord.gg/PrpHgs7Vjs)
discord server, and check the #INFO channel for the latest hotfixes.

**Notes:**

- GitHub may require you to log in to see the downloads for
  pre-releases, and you will need to expand the
  assets section to see the downloads.

- There is no difference between the versioned and unversioned zip
  files. They are simply named this way to allow people to store
  multiple versions rather than overwriting or having a random
  number appended by windows.

## Configuration

**IMPORTANT**: Any desired configuration changes should **ONLY** be made in the
`community_patch_settings.toml` file.

When the game is launched:

- If the configuration file does not exist, it will be created with default values. It
  will not have any comments describing these settings.

- The configuration file is read with any missing settings defaulting to predefined values.

- The finalized configuration – including any assumed default values – is written to
  `community_patch_runtime.vars`.

  **NOTE:** You should NOT make any changes to this file. This file is
  constantly rewritten when the game starts, so any modifications to it are never used and will be
  lost.

If you have any problems with a setting, check for that setting in the _.vars_ file to verify
that the parse value of that setting was correctly applied. You may also need to check the
`community_patch.log` file to see if any errors were encountered while parsing the _.toml_ file.

### Configuration example files

Commented example configurations are maintained separately for each supported language, using the same flags as
[ModConfig](https://modconfig.pages.dev):

- <img src="docs/flags/gb.svg" alt="United Kingdom flag" width="24"> [English](example_community_patch_settings_en.toml)
- <img src="docs/flags/de.svg" alt="German flag" width="24"> [German](example_community_patch_settings_de.toml)
- <img src="docs/flags/fr.svg" alt="French flag" width="24"> [French](example_community_patch_settings_fr.toml)
- <img src="docs/flags/nl.svg" alt="Netherlands flag" width="24"> [Dutch](example_community_patch_settings_nl.toml)

Choose the example for your preferred language and save a copy in the appropriate settings folder as
`community_patch_settings.toml`.

## Installation on Windows

**NOTE:** The `Star Trek Fleet Command` game itself is located by default at:

- `C:\Games\Star Trek Fleet Command\Star Trek Fleet Command\default\game`

Installation of the Community Mod is a manual process for Windows (or Wine).

1. Download the `stfc-community-mod.zip` file from your chosen [GitHub release](https://github.com/STFC-Mod/mod/releases/) and extract the `version.dll` file.

2. Open the game folder in Explorer. The default folder for the game also holds the settings file.

   **NOTE:** If this folder isn't present, or no log files are created when running the game,
   see the [Problems Under Windows](#problems-under-windows) section below.

3. Move the extracted `version.dll` file into this folder.

4. Run the game! If all is well, and one does not already exist, the mod will
   create `community_patch_settings.toml` and populate this with the default
   values.

5. For first-time users of the Community Mod, it is recommended to choose one of the
   [localized configuration examples](#configuration-example-files) and save a copy to the game folder as
   `community_patch_settings.toml`. These examples contain additional comments that explain the available settings.

## Installation on macOS - macOS 13.5 or later required

**NOTE:** The `Star Trek Fleet Command` game itself is located at:

- `~/Library/Application Support/Star Trek Fleet Command/Games/Star Trek Fleet Command/Star Trek Fleet Command/default/game`

You should only need to access this folder if you need to view the `community_patch.log` file while troubleshooting a problem.

1. Download the `stfc-community-mod-installer.dmg` file from your chosen [GitHub release](https://github.com/STFC-Mod/mod/releases/).

2. Open the DMG, then drag and drop the `STFC Community Mod` to your `Applications` folder. The
   STFC Community Mod launcher must be used to start the game to have the mod loaded. If
   you start the game using the games Launcher, you will NOT have the mod loaded.

3. Running the launcher for the first time will prompt a warning from macOS because the Community Mod is not signed with an Apple Developer ID certificate.

   > "STFC Community Mod" Not Opened
   >
   > Apple could not verify “STFC Community Mod” is free of malware that may harm your Mac or compromise your privacy

   Dismiss the warning by clicking `Done`, then go to **System Settings**, **Privacy & Security** and scroll down to the **Security** section
   where you can override this behaviour by clicking `Open Anyway` next to `"STFC Community Mod" was blocked to protect your Mac.`

4. Run the game from the launcher!

   Since M82.1, the **Star Trek Fleet Command** app ships with heightened security and lacks the entitlements to correctly load
   the Community Mod. As a workaround, the Community Mod launcher will prompt you to re-sign the game if it detects
   missing entitlements at startup. This process needs to be repeated with every game update.

   If all is well, the mod will create `community_patch_settings.toml`
   in the settings folder `~/Library/Preferences/com.stfcmod.startrekpatch`.

   Note: By default, macOS hides the `~/Library` folder in Finder, so if it isn't visible,
   see the [Problems under macOS](#problems-under-macos) section below for tips on opening it.

5. For first-time users of the Community Mod, it is recommended to choose one of the
   [localized configuration examples](#configuration-example-files) and save a copy to the settings folder as
   `community_patch_settings.toml`. These examples contain additional comments that explain the available settings.

## Installation on Wine/Linux

**NOTE:** The `Star Trek Fleet Command` game itself is located at:

- `~/Games/star-trek-fleet-command/drive_c/Games/Star Trek Fleet Command/Star Trek Fleet Command/default/game`.

The Windows version of the mod, and the game itself, run well under Linux using the Wine compatability layer.

If the game is installed using the [Lutris STFC installer](https://lutris.net/games/star-trek-fleet-command/),
the mod can be installed by following the [Windows](#installation-on-windows) directions
above.

If the game is installed through other launchers or directly in an unmanaged `WINEPREFIX`,
an additional adjustment may be needed for it to load the mod library correctly; see the
[Problems Under Wine/Linux](#problems-under-winelinux) notes below.

## Common Problems

The most common problems getting the DLL to work are:

1. Not installed in the correct location. This must be `game` folder where `prime.exe` also exists.

2. Windows is blocking the DLL. Right-click the file and select Properties. On the `General` tab
   there will be additional text at the bottom:

   ```console
   This file can from another
   computer and might be blocked to
   help protect this computer
   ```

   To the right of this, there will be a tick box called `Unblock`. Tick the box and then click OK
   to unblock the file.

3. The configuration file has the wrong name (see above)

4. The configuration file is not being parsed as you expect, which is normal because:
   - Your configuration isn't being parsed
   - The configuration option name is spelt wrong
   - The configuration option name is in the wrong section
   - The configuration option value is not a true or false

   You can verify your configuration by looking at `community_patch_runtime.vars` and/or the
   log file `community_patch.log`.

## Problems under Windows

On Windows, sometimes the game is installed in a user's profile folder instead of the
default location. This happens when the STFC installer finds more than one user
attempted to install it, or if the current user doesn't have admin rights.

If this is the case, to find the correct game location for the `version.dll` file, do the
following:

1. Open _Task Manager_ while the game is running (press Ctrl-Alt-Delete)
2. Right-click `prime.exe` and select 'Expand'
3. Right-click `Star Trek Fleet Command` and select 'Open file location'

An _Explorer_ window will open, and you can check for `prime.exe` to verify this is the
correct folder.

**IMPORTANT**: The `version.dll` file should NOT be placed within the official STFC launcher's
folder as this will prevent the launcher from working and the ability to update the game.

## Problems under macOS

macOS hides the `Library` folder in Finder by default. There are several ways to open it:

- Hold the `⌥ Option` key (or `Alt` on a PC keyboard) in **Finder** and click the **Go to** menu item
  and then click the **Library** folder item

  or

- Press `⌘ Command`+`⇧ Shift`+`G` in the **Finder** for the Goto box and type in `~/Library`

  or

- Use **Terminal** and type: `open ~/Library`

The Community Launcher shows the version of `Star Trek Fleet Command` that was detected
under the current user. If this shows as `-1`, then the recommended course of action is
to uninstall and reinstall `Star Trek Fleet Command` whcih can be done without needing
to reinstall the Community Mod launcher.

### Problems under Wine/Linux

To use the Windows version of the mod under Wine, the wine DLL override setting for
`version.dll` _must_ be set to `n,b` or it will not be loaded.

When using the [Lutris STFC installer](https://lutris.net/games/star-trek-fleet-command/)
(which is recommended), this override will have already been set by the installer within
the runner configuration.

Otherwise, it can be set in the `winecfg.exe` Libraries tab or by setting the `WINEDLLOVERRIDES` enviroment variable to `version.dll=n,b` before launching the game.

If the game folder does not exist in the default location mentioned under [Installation on Wine/Linux](#installation-on-winelinux), it may exist under:

- `drive_c/Games/Star Trek Fleet Command/Star Trek Fleet Command/default/game`

**NOTE:** This is a relative path that starts at the wine environment (WINEPREFIX).
