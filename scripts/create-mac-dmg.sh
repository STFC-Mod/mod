#!/bin/bash

set -xe

CONFIG=${1:-release}
ARCH=arm64

xmake clean
# Build the arm64 version
xmake f -y -p macosx -a "arm64" -m $CONFIG --target_minver=14.6
xmake

# Build the x86_64 version
xmake f -y -p macosx -a "x86_64" -m $CONFIG --target_minver=14.6
xmake

# Rebuild the package app bundle after switching architectures so xcode.xcassets
# regenerates Assets.car even when xmake clean leaves stale asset dependencies.
xmake f -y -p macosx -a "$ARCH" -m "$CONFIG" --target_minver=14.6
xmake -r -y macOSLauncher
test -f "build/macosx/$ARCH/$CONFIG/macOSLauncher.app/Contents/Resources/Assets.car"

rm build/macosx/$ARCH/$CONFIG/libmods.a || true

lipo -create build/macosx/$ARCH/$CONFIG/macOSLauncher build/macosx/x86_64/$CONFIG/macOSLauncher -output build/macosx/$ARCH/$CONFIG/macOSLauncher.app/Contents/MacOS/macOSLauncher
lipo -create build/macosx/$ARCH/$CONFIG/stfc-community-mod-loader build/macosx/x86_64/$CONFIG/stfc-community-mod-loader -output build/macosx/$ARCH/$CONFIG/macOSLauncher.app/Contents/stfc-community-mod-loader
lipo -create build/macosx/$ARCH/$CONFIG/libstfc-community-mod.dylib build/macosx/x86_64/$CONFIG/libstfc-community-mod.dylib -output build/macosx/$ARCH/$CONFIG/macOSLauncher.app/Contents/libstfc-community-mod.dylib
cp assets/launcher.icns build/macosx/$ARCH/$CONFIG/macOSLauncher.app/Contents/Resources/
cp macos-launcher/src/Info.plist build/macosx/$ARCH/$CONFIG/macOSLauncher.app/Contents/

rm -rf build/macosx/$ARCH/$CONFIG/*.dSYM || true
rm -rf build/macosx/$ARCH/$CONFIG/STFC\ Community\ Mod.app || true
rm stfc-community-mod-installer.dmg || true

mv build/macosx/$ARCH/$CONFIG/macOSLauncher.app build/macosx/$ARCH/$CONFIG/STFC\ Community\ Mod.app
test -f "build/macosx/$ARCH/$CONFIG/STFC Community Mod.app/Contents/Resources/Assets.car"

codesign --force --verify --verbose --deep --sign "-" build/macosx/$ARCH/$CONFIG/STFC\ Community\ Mod.app

DMG_SOURCE="build/macosx/$ARCH/$CONFIG/dmg-source"
rm -rf "$DMG_SOURCE"
mkdir "$DMG_SOURCE"
mv "build/macosx/$ARCH/$CONFIG/STFC Community Mod.app" "$DMG_SOURCE/"

create-dmg --filesystem APFS --format ULFO --volname "STFC Community Mod Installer" \
--volicon assets/launcher.icns --no-internet-enable \
--background assets/mac_installer_background.png \
--window-pos 200 120 --window-size 800 400 --icon-size 100 \
--icon "STFC Community Mod.app" 200 190 \
--app-drop-link 600 185 --applescript-sleep-duration 1 \
stfc-community-mod-installer.dmg "$DMG_SOURCE/"
