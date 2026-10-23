# Agent guide

STFC Community Mod hooks a Unity IL2CPP game to add desktop features on Windows and macOS.
Read [CONTRIBUTING.md](CONTRIBUTING.md) for builds and tests.
Before changing bindings or hooks, read the [IL2CPP reference](docs/IL2CPP.md).

## Keep changes scoped

- Stage only task-related files. Leave unrelated edits and generated artifacts alone.
- Preserve client enum catalogs in `src/mod/game/`, including unused IDs for future features.
- Run `git diff --check` and the narrowest relevant Zig build before finishing C++ or patch work.
- Review hooks, platform guards, config defaults, and the English settings example in the final diff.
- Put specialized subtree rules in a nearby `AGENTS.md`.

## Build and check

The project uses **Zig 0.16 or 0.17** and **C++23**. Keep build changes working on both; `zig/compat.zig` holds the differences.
Build definitions are in `build.zig` and `zig/`. Dependency URLs and hashes are pinned in `build.zig.zon`.
Windows uses the MSVC ABI and static CRT.

For core mod changes, use the relevant platform build:

```sh
zig build mods -Dtarget=aarch64-macos.14.6
zig build mods build-tests -Dtarget=x86_64-windows-msvc
```

Native logic tests need no game:

```sh
zig build test-il2cpp test-mod test-sync test-spud test-launcher
```

`test-spud` is x64-only; `test-launcher` requires Linux or macOS.
See [contributor checks](CONTRIBUTING.md#run-checks-before-submitting) for coverage and SDK requirements.
Outputs use `zig-out`, generated sources use `.zig-cache`, and SDKs use `.cache/toolchain`.

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

1. Extend the domain file in `src/mod/features/`, or add one named after the feature's purpose. Merge micro-files.
2. Keep installation private. Export a descriptor such as `const Feature kXxx{"Xxx", &Config::Patches::group, InstallXxx};`.
   `InstallXxx` returns whether the feature went in, usually `return hooks.Commit();`. A feature that declines for a
   reason of its own logs that reason with its tag first; the library reports missing methods and failed detours.
3. Declare the descriptor in `features/features.h` and add it to `kFeatures` in `mod.cc`, both grouped by file.
4. For a new hook group, add a `[patches]` member defaulting to `true` and read it in `Config::Read`.

Shortcut-only helpers need no descriptor.
Frame features supply their frame function as the descriptor's fourth member and return early when disabled.
`mod.cc` logs each feature as installed, skipped or not installed, and runs frame work only for installed features.
A feature that only one platform can install is left out of `features.h` and `kFeatures` elsewhere with `#if _WIN32`.

Only `[patches]` keys decide which hooks install. They are restart-only switches for conflicts and client drift.
Feature settings belong in sections such as `[ui]`, `[graphics]`, or `[audio]`.
Install hooks even when a feature is disabled. Each hook reads the current setting and cheaply calls `original` when off.

Every feature file has the same shape:

- A top comment naming the settings it owns, then includes: project headers, `game/`, libraries, then std.
- One anonymous namespace holding state, helpers, hooks and, last, `Install<Descriptor>()`. Sections use `// --- X ---`.
- Exported calls from `features.h`, then the descriptors at the end of the file.

macOS release builds compile feature files four to a translation unit. Give file-scope names a feature-specific
name (`recovery_installed`, `InstallPinnedShips`), never a generic one like `installed`, `Enabled` or `Install`.

## Hooks and frame work

- Every installer stages its hooks in one `il2cpp::HookSet` named after its descriptor, then commits it. Use
  `IL2CPP_STAGE` on a `Class::Method` lookup; `IL2CPP_STAGE_AT` is for native exports.
- Name callbacks `Class_Method_Hook`, take `auto original` first and the receiver as `_this`, typed with its binding.
- `_this` and `unity::Instances` items are never null; do not check them. Check other pointers from the game before
  calling through them, since receivers must not be null. Do not cap counts or sizes the game reports.
- Use `Method<R(P...)>` or `StaticMethod<R(P...)>` when the hook reads value-type or enum parameters.
- A hook shared by two features goes in its own set, committed once from a function-local static that both call.
- SPUD measures each target from the image's unwind or function-start tables and refuses one too small for its patch;
  the set reports it. Installers do not repeat a check that SPUD, the library or the dump checker already makes.
- Match game-visible signatures from `dump.cs`. Change hidden `MethodInfo*` handling only with runtime evidence.
- Keep the single `ScreenManager.Update` hook in `mod.cc`. Other hooks that read keys must call `unity::Key::Poll()` first.

The library reports missing members, signature drift and failed detours. Features do not log hook failures or
success. A set is one SPUD transaction: every hook or none, so a feature installs whole or not at all, and a hook
that runs can rely on the rest of its set. Do not pre-check fields or properties by default: a missing one is
reported and reads as zero, and the dump checker catches drift before a release. `HasField` and `HasProperty` are for
the rare hook that would take an in-game action on a value read as zero.

Bindings contain managed exceptions, so hooks need no catch-all. Catch only around `il2cpp::InvokeBound`, which
lets them through so the caller can fall back to the game's own path.

Feature log lines start with the descriptor name: `spdlog::info("[PinnedShips] ...")`. Log what the mod changed or
could not do, at most once for anything that repeats.

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
