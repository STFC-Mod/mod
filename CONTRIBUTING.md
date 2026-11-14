# Contribute to STFC Community Mod

Send pull requests to **`dev`**. Run commands from the repository root.

## Build for your platform

Install xmake and the native C++ toolchain. On macOS, install Xcode and its command-line tools.
Initialize the launcher dependency before building:

```sh
git submodule update --init --recursive
xmake f -c -p macosx -a arm64 -m release -y
xmake build -y
```

For Intel macOS, use `-a x86_64`. On Windows, use an x64 Visual Studio developer shell and configure `-p windows -a x64`.
The default build includes the mod and platform entry points; macOS also builds the Swift launcher.
Outputs are under `build/<platform>/<architecture>/<mode>/`.
Copy `version.dll` beside the game's `prime.exe` on Windows.

`--bg_image=<file>` changes the embedded loading image; `--use_original_bg=y` keeps the game background.
Use `-m releasedbg` for diagnostic builds. `[patches]` switches work in every build.
Dependencies remain in `xmake/dependencies/`, the local package repository in `xmake-packages/`, and `xmake-requires.lock`.

## Run checks before submitting

Run `git diff --check` and the relevant platform build.
Build and run the native fixtures that cover your change:

| Target | Coverage |
| --- | --- |
| `il2cpp-bindings-tests` | Typed runtime bindings and keyboard snapshots |
| `il2cpp-gc-tests` | Managed GC handles |
| `unity-tracker-tests` | Live instance tracking |
| `mod-logic-tests` | Settings reader, shortcut defaults, and text helpers |
| `config-reload-tests` | Configuration replacement and ownership |
| `sync-logic-tests` | Wire fixtures, retries, removals, target changes, and sessions |
| `spud-relocation-tests` | x64 detour relocation |
| `launcher-rsync-tests` | Real rsync deltas and I/O failures |

For example:

```sh
xmake build -y mod-logic-tests
xmake run mod-logic-tests
python3 scripts/validate-example-configs.py
```

Tests need no game. Run them on the matching operating system and architecture.
Compilation and fake-runtime tests do not verify native hook fit or behavior in the running game.

## Change code

Read [AGENTS.md](AGENTS.md) for the source map and project rules.
The [IL2CPP reference](docs/IL2CPP.md) covers bindings, calls, hooks, and managed object lifetimes.

Keep each feature's hooks, shortcuts, and state in its domain file under `src/mod/features/`.
Game bindings belong in `src/mod/game/`; reusable IL2CPP and Unity code belongs in the libraries.
Preserve client enum catalogs, including unused IDs needed for future work.

Settings and defaults live in `config.h` and `Config::Read`.
Update the English example when changing a user-facing setting.
Shortcut definitions live in `src/mod/shortcuts.def`; feature files retain dispatch order and context checks.

### Repair bindings after a game update

Dump the new client with Il2CppDumper, then compare it with the previous dump:

```sh
python3 scripts/check-il2cpp-dump.py check /path/to/new/dump.cs --old /path/to/old/dump.cs
python3 scripts/check-il2cpp-dump.py diff /path/to/old/dump.cs /path/to/new/dump.cs
```

The script finds missing classes, members, and hook targets, plus static and arity changes.
It uses text matching. Runtime type checks and in-game validation remain necessary.

### Data sync ownership

| File | Owns |
| --- | --- |
| `sync/sync.cc` | Game hooks and the raw response queue |
| `sync/records.cc` | Canonical records from game responses |
| `sync/state.cc` | Comparisons, removals, and per-target acknowledgements |
| `sync/upload.cc` | Sessions, the record store, target workers, and HTTP delivery |
| `sync/battlelogs.cc` | Battle-journal fetching |
| `sync/fleets.cc` | Client fleet records: state changes, snapshots, and late re-reads, on the game thread |
| `sync/fleet_watch.cc` | Fleet state polling, baselines, and transitions for fleet sync |

Payload construction and HTTP run outside the store lock.
Session generations prevent old responses and acknowledgements from changing a new login.
Each target advances its cursor only after a 2xx response. Failures retry after one second, with exponential backoff up to one minute.

The first full list establishes `X-PRIME-SYNC: 2`; retries retain it.
A delta before that list cannot claim a full baseline.
Removal records account for uploads that may have committed before a timeout.
After 1,024 unacknowledged new entities, delivery waits for an acknowledgement before introducing more.
Their latest values remain available. Version counters skip unchanged streams, and snapshots share record payloads.

Ordinary categories retain observed state even while disabled, so a new target can receive a baseline.
Battle fetching is opt-in. Each target acknowledges the session's 300 recent journals independently; journals can replay after a restart or login.
Client fleet changes are a journal too: every state change is its own entry, delivered in order and never merged with a later one. A fleet snapshot entry is sent as a first sync.
The raw response queue holds 256 entries and drops the oldest on overflow.
Sync tests use controlled delivery results. They do not contact community services.
