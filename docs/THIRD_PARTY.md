# Third-party dependencies

Preserve upstream licenses and notices when distributing source or binaries.

The xmake dependency declarations live in `xmake/dependencies/`; local package definitions are in `xmake-packages/` and resolved versions are in `xmake-requires.lock`.
The launcher uses the PLzmaSDK submodule at `macos-launcher/deps/PLzmaSDK`.

SPUD is vendored under `xmake-packages/packages/s/spud/spud-src` at `e4d0c78be89ef2587af33dd634ae5e8071935be7`.
Its upstream source and MIT license are unchanged.
`third_party/libil2cpp` contains the Unity SDK headers; the bundled sparsehash headers retain their BSD notices.

Review dependency changes with their source provenance, platform configuration, and native build results.
