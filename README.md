# Star Trek Fleet Command - Community Mod

## IMPORTANT NOTE:

The latest full release will always be available on the GitHub site. However, for interim patches, please see the #info channel
on the [STFC Community Mod](https://discord.gg/PrpHgs7Vjs) discord server. This channel will always contain any hotfixes that
enable the game to work even if the full release does not.

## BACK TO THE REGULAR STUFF:

<p align="center">
    <img src="https://repository-images.githubusercontent.com/693298224/6a87716f-3dbb-48a5-80e0-709e0a1ad133" alt="STFC Community Mod">
    <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3">
    <img src="https://img.shields.io/github/sponsors/netniv" alt="Sponsorship"><br>
    A community mod (patch) that adds a couple of tweaks to the mobile game <b>Star Trek Fleet Command&#8482;
</p>

## Contributing / Development

If you wish to contribute to the project, or simply compile the DLL yourself, please see [CONTRIBUTING.md](CONTRIBUTING.md)

There is a discord server with friendly, helpful people who will assist if you have issues (see the support section below).

This project is maintained solely at my own cost of time, energy and money. Any contributions and help are greatly welcomed.

## Features

- Set system UI scale + adjustment factor
- Set viewer UI scale
- Set system zoom
  - default
  - maximum
  - keyboard speed
  - presets (1-5)
- Set transition time
- Disable various toast banners
- Disable galaxy chat
- Enable/Disable hotkeys (community mod or scopely)
- Enable extended donation slider (alliance)
- Extend tagged chest-purchase sliders up to 160 on Windows
- Show alternative cargo screens for:
  - default
  - player
  - station
  - hostile
  - armada
- Press ESCAPE to remove pre-scan viewers
- Skip reveal sections when opening chests
- Exit section when collecting gifts
- Create default toml file settings file if none exists
- Create parsed toml file to show what settings have been applied
- Customise your keyboard shortcuts

For a full list of features see our [FEATURES.md](FEATURES.md)

## Installing

Please see the [INSTALL.md](INSTALL.md) instructions which has steps on how to use this mod with Star Trek Fleet Command.

For the Windows DLL identity marker, artifact-attestation trust boundary, and verification command, see
[Windows DLL provenance](docs/WINDOWS_DLL_PROVENANCE.md).

Please note, that whilst Mac support was added in this version, it's supported on an as-is basis due to lack of Mac development environments.

## Keyboard shortcuts

Most keyboard shortcuts can be modified by updated your TOML file. If your
file is empty, see the VARS file which has all the runtime settings that have
been applied. Valid values for any short can be found in [KEYMAPPING.md](KEYMAPPING.md)

### UI shortcuts

|          Key | Shortcut                         |
| -----------: | -------------------------------- |
|          F10 | Bug fixer (exits game)           |
|        F1-F5 | Zoom presets                     |
|            Q | Zoom In                          |
|            E | Zoom Out                         |
|        MINUS | Zoom (min)                       |
|            = | Zoom (default)                   |
|    BACKSPACE | Zoom (max)                       |
|            C | Open/Focus Chat - Full Screen    |
|        Alt-C | Open/Focus Chat - Side of Screen |
|            ` | Open/Focus Chat - Side of Screen |
|         PGUP | UI Scale Up                      |
|       PGDOWN | UI Scale Down                    |
|   SHIFT-PGUP | UI Viewer Scale Up               |
| SHIFT-PGDOWN | UI Viewer Scale Down             |

### Combat/Navigation shorcuts

|             Key | Shortcut                                                                   |
| --------------: | -------------------------------------------------------------------------- |
| SPACE or MOUSE1 | Perform default action (MOUSE1 = right mouse click)                        |
| SPACE or MOUSE1 | Add to Kir'Shara queue (if owned) and already attacking                    |
|             1-8 | Ship select/focus                                                          |
|               R | When ship selected, recall ship                                            |
|               R | When clicking on mine/player/enemy, perform non-default action (eg, scan)  |
|               V | When clicking on mine/player/enemy, toggle view of cargo or default screen |
|          CTRL-Q | Enable/Disable Kir'Shara queue (if owned)                                  |
|          CTRL-C | Clear Kir'Shara queue (if owned)                                           |

NOTE: There are some common changes made to allow both mouse and keyboard to
action items such as:

- set action_queue, action_primary and action_recall_cancel to `SPACE|MOUSE1`
  allowing both right mouse click and spacebar to action attacks on (or
  queuing of) hostiles or cancel a warp.

- set action_recall to `R|MOUSE3` to allow recalling using
  both spacebar and the side mouse button

### Section shortcuts

|     Key | Shortcut         |
| ------: | ---------------- |
|       T | Away Teams       |
|       G | Galaxy           |
|  Ctrl-G | Galaxy (native)  |
|       H | System           |
| Shift-E | Events           |
|  Ctrl-E | Events (native)  |
| Shift-G | Exterior View    |
| Shift-H | Interior View    |
|       B | Bookmarks        |
|       F | Factions         |
| Shift-F | Refinery         |
| Shift-I | Artifact Gallery |
|       U | Research         |
|       Y | Scrap Yard       |
|       I | Inventory        |
|       M | Active Missions  |
|       O | Command Center   |
| Shift-O | Officers         |
| Shift-Q | Q-Trials         |
|       X | ExoComp          |
|       Z | Daily Missions   |

## Support

For STFC Community Mod items, please visit the [STFC Community Mod](https://discord.gg/PrpHgs7Vjs) discord server.

Tashcan has now retired all things STFC from [Ripper's Corner](https://discord.gg/gPuQ5sPYM9) but still swing by to say hello to the wonderful man.

## Disclaimer

This is intended to give people insight and possibility to add new things for QoL improvements.

There is no guarantee or promise that using this for features outside of what is officially offered via this repository will not result actions against your account.

All features and additions provided here via this repository are sanctioned by Scopely and thus aren't subject to account actions.

## License

- GPLv3
