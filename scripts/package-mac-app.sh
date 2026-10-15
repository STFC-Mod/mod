#!/bin/bash
# Build both architectures and assemble a signed universal launcher bundle.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
MODE="${1:-release}"
case "$MODE" in
  debug|check) OPTIMIZE=Debug; MODDBG=false ;;
  release) OPTIMIZE=ReleaseFast; MODDBG=false ;;
  releasedbg) OPTIMIZE=ReleaseFast; MODDBG=true ;;
  *) echo "Unknown build mode: $MODE" >&2; exit 1 ;;
esac
OUT="zig-out/universal/$MODE"
zig build macos-universal -Doptimize="$OPTIMIZE" -Dmoddbg="$MODDBG" --prefix "$OUT"
for ARCH in arm64 x86_64; do
  TARGET="$ARCH"
  [[ "$ARCH" != arm64 ]] || TARGET=aarch64
  zig build launcher -Dtarget="$TARGET-macos.14.6" -Doptimize="$OPTIMIZE" -Dmoddbg="$MODDBG" --prefix "zig-out/$ARCH/$MODE"
done
APP="$OUT/STFC Community Mod.app"
rm -rf "$APP"
ditto "zig-out/arm64/$MODE/macOSLauncher.app" "$APP"
lipo -create \
  "zig-out/arm64/$MODE/macOSLauncher.app/Contents/MacOS/macOSLauncher" \
  "zig-out/x86_64/$MODE/macOSLauncher.app/Contents/MacOS/macOSLauncher" \
  -output "$APP/Contents/MacOS/macOSLauncher"
cp "$OUT/bin/stfc-community-mod-loader" "$APP/Contents/"
cp "$OUT/lib/libstfc-community-mod.dylib" "$APP/Contents/"
codesign --force --verify --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "Built $APP"
