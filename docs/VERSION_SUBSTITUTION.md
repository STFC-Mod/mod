# Check the macOS launcher version

After `zig build launcher`, run:

```sh
python3 scripts/verify-version-substitution.py zig-out/macOSLauncher.app/Contents/Info.plist
```

For a custom `--prefix`, pass the plist path under that prefix.

`zig/launcher.zig` reads `VERSION_MAJOR`, `VERSION_MINOR`, `VERSION_REVISION`, and `VERSION_PATCH` from `src/mod/version.h`.
It substitutes the four-part number for `${VERSION}` in `src/launcher/macos/Info.plist.template`,
and `macos_min` from `zig/ctx.zig` for `${MACOS_MIN}`.
Zig generates the plist in its cache and installs it into the app. The source template stays unchanged.
The check fails on a version mismatch or on any placeholder left in the plist.
