#!/bin/bash
# Builds the mod, loader and launcher for local testing on a Mac, then runs or debugs them.
set -euo pipefail

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS] [ACTION]

Actions:
  build       Build and package the app bundle (default)
  run         Build, then run the loader (or the launcher with -l). Crashes leave system crash reports.
  debug       Build, then run under lldb. lldb catches crashes, so no crash report is written.
  crashlogs   List recent crash reports for the launcher, loader and game
  clean       Remove .zig-cache and zig-out

Options:
  -m, --mode MODE   debug (default), release, or releasedbg (release with the mod's -Dmoddbg diagnostics)
  -a, --arch ARCH   arm64, x86_64 or universal (default: this Mac's)
  -l, --launcher    Run the launcher bundle instead of the loader
  -c, --clean       Clean before building
  -h, --help        Show this help
EOF
}

MODE=debug
ARCH="$(uname -m)"
ACTION=build
CLEAN=false
USE_LAUNCHER=false
while (($#)); do
  case "$1" in
    -m | --mode) MODE="$2" && shift 2 ;;
    -a | --arch) ARCH="$2" && shift 2 ;;
    -l | --launcher) USE_LAUNCHER=true && shift ;;
    -c | --clean) CLEAN=true && shift ;;
    -h | --help) usage && exit 0 ;;
    build | run | debug | crashlogs | clean) ACTION="$1" && shift ;;
    *) echo "Unknown option: $1" >&2 && usage && exit 1 ;;
  esac
done

case "$MODE" in
  debug) OPTIMIZE=Debug MODDBG=false ;;
  release) OPTIMIZE=ReleaseFast MODDBG=false ;;
  releasedbg) OPTIMIZE=ReleaseFast MODDBG=true ;;
  *) echo "Invalid mode: $MODE (debug, release, releasedbg)" >&2 && exit 1 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PREFIX="zig-out/$ARCH/$MODE"
APP="$PREFIX/STFC Community Mod.app"
if [[ "$ARCH" == universal ]]; then
  APP="zig-out/universal/$MODE/STFC Community Mod.app"
fi
# The loader injects the dylib that sits next to it, so it runs from the bundle, not from bin/.
LOADER="$APP/Contents/stfc-community-mod-loader"

build() {
  if [[ "$(zig version)" != 0.16.0 ]]; then
    echo "Zig 0.16.0 is required" >&2
    exit 1
  fi
  if [[ "$ARCH" == universal ]]; then
    scripts/package-mac-app.sh "$MODE"
    return
  fi
  local target="$ARCH"
  if [[ "$ARCH" == arm64 ]]; then
    target=aarch64
  fi
  # Keep in step with macos_min in zig/ctx.zig.
  zig build install launcher -Dtarget="$target-macos.14.6" -Doptimize="$OPTIMIZE" -Dmoddbg="$MODDBG" \
    --prefix "$PREFIX"
  scripts/assemble-mac-app.sh "$APP" "$PREFIX"
}

executable() {
  if [[ "$USE_LAUNCHER" == true ]]; then
    echo "$APP/Contents/MacOS/macOSLauncher"
  else
    echo "$LOADER"
  fi
}

run() {
  echo "Crash reports go to ~/Library/Logs/DiagnosticReports; list them with: $(basename "$0") crashlogs"
  echo "Game log stream: log stream --predicate 'process == \"Star Trek Fleet Command\"' --level debug"
  if [[ "$USE_LAUNCHER" == true ]]; then
    open "$APP"
  else
    "$LOADER"
  fi
}

debug() {
  # The loader execs the game; lldb follows the exec, so the game itself runs under the debugger.
  lldb -o "settings set target.process.stop-on-exec false" -o "settings set stop-line-count-before 3" \
    -o "settings set stop-line-count-after 3" -- "$(executable)"
}

crashlogs() {
  local reports="$HOME/Library/Logs/DiagnosticReports"
  local found
  found="$(ls -t "$reports"/{macOSLauncher,stfc-community-mod-loader,*"Star Trek Fleet Command"}*.{crash,ips} \
    2>/dev/null | head -10 || true)"
  if [[ -z "$found" ]]; then
    echo "No crash reports in $reports"
    return
  fi
  echo "$found"
  local newest
  newest="$(head -1 <<<"$found")"
  read -p "View $newest? [y/N] " -n 1 -r
  echo
  if [[ $REPLY =~ ^[Yy]$ ]]; then
    less "$newest"
  fi
}

clean() {
  rm -rf "$ROOT/.zig-cache" "$ROOT/zig-out"
}

if [[ "$CLEAN" == true && "$ACTION" != clean ]]; then
  clean
fi
# Separate commands, not `build && run`: set -e does not apply inside a function called on the left of &&, so a
# failed build would go on to run a stale bundle.
case "$ACTION" in
  build) build ;;
  run)
    build
    run
    ;;
  debug)
    build
    debug
    ;;
  crashlogs) crashlogs ;;
  clean) clean ;;
esac
