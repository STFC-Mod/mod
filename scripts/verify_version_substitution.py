#!/usr/bin/env python3
"""Check a built launcher's plist against the mod's version header."""
import argparse
import pathlib
import plistlib
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("plist", nargs="?", default="zig-out/macOSLauncher.app/Contents/Info.plist")
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parent.parent
header = (root / "src/mod/version.h").read_text()
parts = []
for component in ("MAJOR", "MINOR", "REVISION", "PATCH"):
    match = re.search(rf"^#define\s+VERSION_{component}\s+(\d+)\s*$", header, re.MULTILINE)
    if not match:
        raise SystemExit(f"Missing VERSION_{component}")
    parts.append(match.group(1))
version = ".".join(parts)
with pathlib.Path(args.plist).open("rb") as file:
    plist = plistlib.load(file)
for key in ("CFBundleShortVersionString", "CFBundleVersion"):
    if plist.get(key) != version:
        raise SystemExit(f"{key}: expected {version}, found {plist.get(key)!r}")
print(f"Verified launcher version {version}")
