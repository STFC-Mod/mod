#!/bin/bash
# Builds both macOS architectures and assembles the signed universal launcher bundle at
# zig-out/universal/<mode>/STFC Community Mod.app.
#
# Usage: scripts/package-mac-app.sh [debug|release|releasedbg]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE="${1:-release}"
case "$MODE" in
  debug) OPTIMIZE=Debug MODDBG=false ;;
  release) OPTIMIZE=ReleaseFast MODDBG=false ;;
  releasedbg) OPTIMIZE=ReleaseFast MODDBG=true ;;
  *)
    echo "Unknown build mode: $MODE" >&2
    exit 1
    ;;
esac

# Keep in step with macos_min in zig/ctx.zig.
MACOS_MIN=14.6
PREFIXES=()
for ARCH in arm64 x86_64; do
  TARGET="$ARCH"
  if [[ "$ARCH" == arm64 ]]; then
    TARGET=aarch64
  fi
  PREFIX="zig-out/$ARCH/$MODE"
  zig build install launcher -Dtarget="$TARGET-macos.$MACOS_MIN" -Doptimize="$OPTIMIZE" -Dmoddbg="$MODDBG" \
    --prefix "$PREFIX"
  PREFIXES+=("$PREFIX")
done
scripts/assemble-mac-app.sh "zig-out/universal/$MODE/STFC Community Mod.app" "${PREFIXES[@]}"
