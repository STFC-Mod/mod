# Agent guide

STFC Community Mod hooks a Unity IL2CPP game to add desktop features on Windows and macOS.
Read [CONTRIBUTING.md](CONTRIBUTING.md) for builds and tests.
Before changing bindings or hooks, read the [IL2CPP reference](docs/IL2CPP.md).

## Keep changes scoped

- Stage only task-related files. Leave unrelated edits and generated artifacts alone.
- Preserve client enum catalogs in `src/mod/game/`, including unused IDs for future features.
- Run `git diff --check` and the narrowest relevant xmake build before finishing C++ or patch work.
- Review hooks, platform guards, config defaults, and the English settings example in the final diff.
- Put specialized subtree rules in a nearby `AGENTS.md`.

## Build and check

The project uses xmake and C++23. Build on the target platform; the Swift launcher requires macOS and Xcode.

```sh
xmake f -c -p macosx -a arm64 -m release -y
xmake build -y
xmake build -y il2cpp-bindings-tests il2cpp-gc-tests mod-logic-tests sync-logic-tests config-reload-tests unity-tracker-tests launcher-rsync-tests
```

Run each matching test with `xmake run <target>`.
The x64-only `spud-relocation-tests` target checks detour relocation.
See [contributor checks](CONTRIBUTING.md#run-checks-before-submitting) for coverage.

## Source map

| Path | Responsibility |
| --- | --- |
| `src/libs/il2cpp/` | Generic runtime bindings, calls, strings, GC handles, and detours |
| `src/libs/unity/` | Unity core, UI, TextMeshPro, input, and instance tracking |
| `src/mod/mod.h`, `mod.cc` | Startup, feature installation, and the frame tick |
| `src/mod/config.h`, `config.cc` | Typed settings, defaults, parsing, and reload |
| `src/mod/file.h`, `file.cc` | Settings, vars, log paths, and command-line flags |
| `src/mod/shortcuts.h`, `shortcuts.cc`, `shortcuts.def` | Chord parsing, matching, actions, defaults, tiers, and aliases |
| `src/mod/features/` | One file per feature domain, with its hooks, shortcuts, and state |
| `src/mod/game/` | Header-only STFC bindings, grouped by domain |
| `src/mod/game/proto/` | Protobuf definitions for sync |
| `src/mod/sync/` | Response processing, records, acknowledgements, uploads, battle journals, and fleet state |
| `src/mod/text.h`, `features/toast_text.h` | Game-free text helpers used by tests |
| `src/platform/` | Windows proxy DLL, macOS dylib entry point, and macOS loader |
| `src/launcher/macos/` | Swift launcher for updates, entitlements, and game launch |
| `tests/` | Controlled-runtime, mod, sync, detour, and launcher tests |
| `zig/`, `third_party/` | Build logic and vendored dependencies |
| `docs/`, `scripts/` | User guides, example settings, and release or verification tools |

Features own behavior; bindings own game access.
Reusable IL2CPP or Unity helpers belong in their libraries.
Feature files never declare bindings or call raw `il2cpp_*` APIs.
Dependency pins and notices are listed in [THIRD_PARTY.md](docs/THIRD_PARTY.md).

## Add or change a feature

1. Extend the domain file in `src/mod/features/`, or add one named after the feature's purpose.
2. Keep installation private. Export a descriptor such as `const Feature kXxx{"Xxx", &Config::Patches::group, InstallXxx};`.
3. Declare the descriptor in `features/features.h` and add it to `kFeatures` in `mod.cc`.
4. For a new hook group, add a `[patches]` member defaulting to `true` and read it in `Config::Read`.

Shortcut-only helpers need no descriptor.
Frame features supply their frame function as the descriptor's fourth member and return early when disabled.

Only `[patches]` keys decide which hooks install. They are restart-only switches for conflicts and client drift.
Feature settings belong in sections such as `[ui]`, `[graphics]`, or `[audio]`.
Install hooks even when a feature is disabled. Each hook reads the current setting and cheaply calls `original` when off.

## Hooks and frame work

- Use `IL2CPP_HOOK` or `IL2CPP_DETOUR` from `<il2cpp/hook.h>`. Name callbacks `Class_Method_Hook` and take `auto original` first.
- Each macro expansion owns one target. Prefer one detour per game method; chained detours depend on installation order.
- Before hooking a small wrapper or thunk, verify its native extent against SPUD's overwrite size on every supported architecture for the exact client. A build does not prove fit.
- Match game-visible signatures from `dump.cs`. Change hidden `MethodInfo*` handling only with runtime evidence.
- Keep the single `ScreenManager.Update` hook in `mod.cc`. Other hooks that read keys must call `unity::Key::Poll()` first.

The frame tick polls input, checks for settings changes, runs installed feature frame functions, then dispatches shortcuts.
Hooks run in no fixed order within a frame. Reads before polling see the previous frame's input.

## Settings and shortcuts

`Config::Get()` is game-thread-only. Call it where needed; a reload replaces the object, so do not retain its reference.
Background sync workers retain immutable `Config::Sync::Get()` snapshots.
`Config::Parse` reads a TOML table without touching files or live bindings.

When changing settings:

1. Add section members in `config.h`, named after TOML keys and initialized to their defaults.
2. Read them with `r.Read(section, key, member)` in `Config::Read`.
3. Update only the [English example](docs/configuration/example_community_patch_settings_en.toml). Update translations only when requested.
4. Run `zig build test-mod`; it checks agreement between the reader and English example.

The unsuffixed example is a pointer to translations. Do not change it for new settings.
Actions, TOML keys, defaults, optional tiers, and aliases belong in `shortcuts.def`.
Feature files keep dispatch order and context checks.

## Code and writing style

- Use `.clang-format` through `zig build fmt`: two-space indentation, 120 columns, Linux braces, and aligned declarations. It skips `.mm` files.
- Use fixed-width numeric types. Prefer narrow platform guards such as `#if _WIN32`, `#if !_WIN32`, and `#if __APPLE__`.
- Log with `spdlog`. Version components live in `src/mod/version.h` as `VERSION_MAJOR`, `VERSION_MINOR`, `VERSION_REVISION`, and `VERSION_PATCH`.
- Keep docs task-first, concise, and written in plain technical English. Use short paragraphs and varied sentence lengths.

`main` holds stable upstream releases. `dev` is the upstream development and pull-request target.
