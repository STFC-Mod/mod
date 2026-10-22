#!/bin/bash
# Wraps an assembled launcher bundle in the installer disk image. Needs create-dmg (brew install create-dmg).
#
# Usage: scripts/create-mac-dmg.sh <STFC Community Mod.app> [output.dmg]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if (($# < 1)); then
  echo "usage: $0 <STFC Community Mod.app> [output.dmg]" >&2
  exit 1
fi
APP="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
DMG="${2:-stfc-community-mod-installer.dmg}"

# create-dmg packs a whole folder, so the bundle gets one of its own.
SOURCE="$(mktemp -d)"
trap 'rm -rf "$SOURCE"' EXIT
ditto "$APP" "$SOURCE/STFC Community Mod.app"

rm -f "$DMG"
create-dmg --filesystem APFS --format ULFO --volname "STFC Community Mod Installer" \
  --volicon "$ROOT/assets/launcher.icns" --no-internet-enable \
  --background "$ROOT/assets/mac_installer_background.png" \
  --window-pos 200 120 --window-size 800 400 --icon-size 100 \
  --icon "STFC Community Mod.app" 200 190 \
  --app-drop-link 600 185 --applescript-sleep-duration 1 \
  "$DMG" "$SOURCE/"
