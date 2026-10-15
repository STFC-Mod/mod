#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
MODE="${1:-release}"
scripts/package-mac-app.sh "$MODE"
OUT="zig-out/universal/$MODE"
DMG_SOURCE="$OUT/dmg-source"
rm -rf "$DMG_SOURCE"
mkdir -p "$DMG_SOURCE"
ditto "$OUT/STFC Community Mod.app" "$DMG_SOURCE/STFC Community Mod.app"
rm -f stfc-community-mod-installer.dmg
create-dmg --filesystem APFS --format ULFO --volname "STFC Community Mod Installer" \
  --volicon assets/launcher.icns --no-internet-enable \
  --background assets/mac_installer_background.png \
  --window-pos 200 120 --window-size 800 400 --icon-size 100 \
  --icon "STFC Community Mod.app" 200 190 \
  --app-drop-link 600 185 --applescript-sleep-duration 1 \
  stfc-community-mod-installer.dmg "$DMG_SOURCE/"
