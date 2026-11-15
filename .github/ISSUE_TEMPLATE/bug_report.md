---
name: Bug report
about: Report a problem with the mod, installation, or settings.
title: ''
labels: ''
assignees: ''
---

## Checks before reporting

<!-- Tick the checks you completed. Leave checks that do not apply unchecked. -->

- [ ] I checked existing issues for the same problem.
- [ ] I read the [installation and troubleshooting guide](https://github.com/STFC-Mod/mod/blob/dev/docs/INSTALL.md#fix-installation-problems).
- [ ] I checked the latest release and Discord **#info** for a hotfix, especially after a game update.
- [ ] I checked `community_patch.log` for startup, settings, or hook errors.
- [ ] For settings problems, I checked the section, spelling, and value type against the example and compared `community_patch_runtime.vars`.
- [ ] After changing a `[patches]` setting, I fully restarted the game.

## Platform checks

<!-- Tick the checks relevant to your platform. -->

- [ ] Windows: `version.dll` is beside the running game's `prime.exe`, not in the official launcher's folder; I checked **Properties > General > Unblock** on the DLL.
- [ ] macOS: I started the game through **STFC Community Mod** and accepted any entitlement repair prompt.
- [ ] Wine: the `version` DLL override is set to **native, then builtin** (`n,b`).

## Platform and versions

- Platform: <!-- Windows / macOS / Wine -->
- Operating system and version: <!-- Include the host OS if using Wine. -->
- Processor architecture: <!-- Apple Silicon / Intel / AMD -->
- Game version/build:
- Mod version: <!-- Include the commit hash for a development build. -->
- Mod download/build source: <!-- Release link, beta, or local build. -->
- Community Mod launcher version, if using macOS:
- Wine/CrossOver/Proton version and runner, if applicable:

## What happened

<!-- Describe the problem and what you expected to happen instead. -->

## Steps to reproduce

1.
2.
3.

<!-- Does it happen every time? Did it start after a game or mod update? -->

## Logs and settings

<!-- Attach community_patch.log from an affected session, or paste the relevant lines below.
Include relevant settings and runtime values; do not edit community_patch_runtime.vars to change settings.

IMPORTANT: Remove sync tokens, passwords, and other private information before posting.

For crashes, please try to have a crash report if available, it should be sent by discord upon request. Screenshots or video can also help and should be provided here. -->

```text
Paste relevant log lines here.
```

```toml
# Paste relevant settings here.
```
