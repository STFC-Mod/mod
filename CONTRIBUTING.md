# Contribute to STFC Community Mod

Send pull requests to **`dev`**.
Use **Zig 0.16.0** and run commands from the repository root.
Zig downloads dependencies and checks the hashes in `build.zig.zon`. No submodules or XMake installation are needed.

## Build for your platform

Start with a clone:

```sh
git clone https://github.com/STFC-Mod/mod.git stfc-mod
cd stfc-mod
```

### macOS

Install Xcode and its command-line tools. Then build the mod, loader, and Swift launcher:

```sh
zig build -Dtarget=aarch64-macos.14.6
zig build launcher -Dtarget=aarch64-macos.14.6
```

For Intel, replace `aarch64` with `x86_64`.
Both targets require macOS 14.6 or later.
The Swift launcher and app assets require a macOS host with Xcode.
The C++ mod and loader can be cross-compiled with a supplied SDK.

To package a universal app and disk image on macOS:

```sh
scripts/package-mac-app.sh release
scripts/create-mac-dmg.sh release
```

To build only the universal mod and loader:

```sh
zig build macos-universal -Doptimize=ReleaseFast
```

The development helper also supports build, run, debug, and crash-log actions:

```sh
scripts/mac-build-test-debug.sh build -m debug
scripts/mac-build-test-debug.sh debug -m releasedbg
```

### Windows

For a pinned MSVC and Windows SDK, supply the archive URL through `STFC_WINDOWS_TOOLCHAIN_URL`:

```sh
zig build fetch-toolchains
zig build -Dtarget=x86_64-windows-msvc -Doptimize=ReleaseFast
python3 scripts/verify-windows-binary.py
```

On Windows, an x64 Visual Studio developer shell can supply the installed SDK when no cached SDK is available.
This fallback uses the local installation rather than the manifest pin.
For cross-compilation, provision the [SDK archive](#provision-sdks) first.

### Build outputs

| Output | Location |
| --- | --- |
| Windows proxy DLL | `zig-out/bin/version.dll` |
| macOS loader | `zig-out/bin/stfc-community-mod-loader` |
| macOS dylib | `zig-out/lib/libstfc-community-mod.dylib` |
| Swift launcher app | `zig-out/macOSLauncher.app` |
| Generated protobuf code and embedded images | `.zig-cache` |

On Windows, copy `version.dll` beside the game's `prime.exe`.
See [installation](docs/INSTALL.md) for the full procedure.
To reset build outputs, remove `.zig-cache` and `zig-out`. Keep `.cache/toolchain` to reuse the SDKs.

## Choose build options

`Debug` is the default. Use `-Doptimize=ReleaseFast` for releases.
Windows release builds enable link-time optimization. Disable it with `-Dlto=false`.
The Swift launcher compiles as one module and links after the native archives are ready.
macOS `ReleaseFast` builds batch feature sources to share header parsing; sync sources compile separately.
Zig 0.16 does not support Mach-O LTO, so macOS builds use ordinary optimized compilation.

| Option | Effect |
| --- | --- |
| `-Dmoddbg=true` | Define `_MODDBG`: enable debug logging by default, disable sync request timeouts, and add diagnostics. |
| `-Dbg_image=<file>` | Replace the embedded loading image. |
| `-Duse_original_bg=true` | Keep the game's loading background. |
| `-Dmacos-sdk=<path>` | Use an explicit macOS SDK. |
| `-Dtest-optimize=Debug` | Keep test fixtures and their assertions in Debug while sharing dependencies built with `-Doptimize=ReleaseFast`. |

`[patches]` switches work in every build.
For library-only checks, use `zig build il2cpp`, `zig build unity`, or `zig build mods` with the required target.
Dependency build steps are `dep-spud`, `dep-protobuf`, and `dep-net`.

## Run checks before submitting

Format changed code and check whitespace:

```sh
zig build fmt
git diff --check
```

`fmt` requires `clang-format` on `PATH`.
It formats C++ under `src` and `tests`, plus the Zig build files. It skips Objective-C++ `.mm` files.

Choose the relevant native tests:

| Command | Coverage |
| --- | --- |
| `zig build test-il2cpp` | Typed bindings, keyboard snapshots, GC handles, and Unity instance tracking |
| `zig build test-mod` | Settings parsing and reload, shortcuts, text helpers, and English example settings |
| `zig build test-sync` | Wire fixtures, retries, removals, target changes, session isolation, and concurrent updates |
| `zig build test-spud` | x64 trampoline relocation, including RIP-relative instructions before a call |
| `zig build test-launcher` | Real rsync deltas, corrupt patches, missing inputs, and output errors on Linux or macOS; 7z extraction fixtures on macOS |

These tests need no game. The boundary and mod tests need no platform SDK on Linux.
Tests must run on a matching operating system and architecture.
To compile Windows tests without running them:

```sh
zig build mods build-tests -Dtarget=x86_64-windows-msvc
```

Before submitting core mod changes, also run the relevant platform build.
For macOS, use:

```sh
zig build mods -Dtarget=aarch64-macos.14.6
```

`test-toolchains` checks SDK provisioning. `test-codegen` checks protobuf message selection.
`test-deps` checks dependency behavior and networking.
For a local HTTPS fixture, pass its URL and CA file:

```sh
zig build test-deps -- https://localhost:port/ /path/to/ca.pem
```

The endpoint must return `zig-network-ok`.
Compilation and fake-runtime tests do not verify native hook fit or behavior in the running game.

## Provision SDKs

`zig/toolchains.zon` records archive versions, URLs, environment overrides, and SHA-256 hashes.
The Windows archive is pinned by hash. Its temporary download URL is not committed.
Instead of a URL, you can supply a local ZIP:

```sh
zig build fetch-toolchains -Dtoolchain-archive=/path/to/windows-toolchain.zip
```

The ZIP must contain:

```text
msvc/include
msvc/lib/x64
winsdk/include       # including cppwinrt
winsdk/lib/ucrt/x64
winsdk/lib/um/x64
```

The fetcher keeps library PDBs and skips compiler executables.
On case-sensitive filesystems, it adds aliases for SDK headers and library names.
It verifies the archive before replacing a completed extraction.
A changed manifest, extractor version, or archive hash invalidates the completion stamp.

For macOS, SDK lookup tries an explicit `-Dmacos-sdk` path, the cache, then native `xcrun`.
To fetch an SDK archive, first fill in its version, URL, and hash in `zig/toolchains.zon`.
`STFC_MACOS_SDK_URL` overrides the URL.

```sh
zig build fetch-toolchains -Dtoolchains=macos_sdk
zig build fetch-toolchains -Dtoolchains=windows_msvc,macos_sdk
```

Use a `.tar.gz` archive with one top-level `MacOSX.sdk` directory. This preserves framework symlinks.

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
| `sync/fleets.cc` | Client fleet records: slot polling, baselines, state changes, snapshots, and late re-reads, on the game thread |

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

### Add a protobuf message

The build generates C++ for the entry-point messages in `zig/deps/protobuf_roots.txt` and their transitive field types.
Add a fully qualified message name there when a new consumer needs it. Nested messages and map entries are included automatically.
If its schema is not already compiled, add its `.pb.cc` file to the source selection in `zig/protobuf.zig`.
The complete source schemas and their enum catalogs remain in `src/mod/game/proto/`.

The selector copies retained message descriptors without changing their fields, options, or wire numbers.
Run `zig build test-codegen test-sync` after changing selection or schemas.

## Update dependencies and build identity

[Third-party dependencies](docs/THIRD_PARTY.md) lists pins, licenses, and source provenance.
To update a fetched dependency, replace `NAME` and `IMMUTABLE_URL` with its package name and archive URL:

```sh
zig fetch --save=NAME IMMUTABLE_URL
```

Review source and configuration changes, then build supported targets.
macOS uses Apple's system libcurl; Windows builds pinned curl with Schannel. Neither builds OpenSSL.
Librsync uses checked-in configuration files. Normal builds need no Perl or CMake.

Windows build-identity options have matching environment fallbacks:

| Zig option | Environment variable |
| --- | --- |
| `stfc_source_state_id` | `STFC_SOURCE_STATE_ID` |
| `stfc_base_commit` | `STFC_BASE_COMMIT` |
| `stfc_build_invocation_id` | `STFC_BUILD_INVOCATION_ID` |
| `stfc_build_channel` | `STFC_BUILD_CHANNEL` |

Prefix options with `-D`, as in `-Dstfc_build_channel=local`.
Values accept 1 to 160 ASCII letters, digits, or `._:+/-`.
The DLL version resource contains the same values.
Keep CI artifact names compatible with the upstream Windows attestation and macOS release jobs.
See [Windows DLL verification](docs/WINDOWS_DLL_PROVENANCE.md) and [launcher version generation](docs/VERSION_SUBSTITUTION.md).
