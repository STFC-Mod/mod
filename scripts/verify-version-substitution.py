#!/usr/bin/env python3
"""Check a built launcher's plist against the mod's version header."""
import argparse
import plistlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def header_version() -> str:
    """The dotted MAJOR.MINOR.REVISION.PATCH version from src/mod/version.h."""
    header = (ROOT / "src/mod/version.h").read_text()
    parts = []
    for component in ("MAJOR", "MINOR", "REVISION", "PATCH"):
        match = re.search(rf"^#define\s+VERSION_{component}\s+(\d+)\s*$", header, re.MULTILINE)
        if not match:
            raise SystemExit(f"Missing VERSION_{component}")
        parts.append(match.group(1))
    return ".".join(parts)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plist", nargs="?", default="zig-out/macOSLauncher.app/Contents/Info.plist")
    args = parser.parse_args()

    version = header_version()
    with Path(args.plist).open("rb") as file:
        plist = plistlib.load(file)
    for key in ("CFBundleShortVersionString", "CFBundleVersion"):
        if plist.get(key) != version:
            raise SystemExit(f"{key}: expected {version}, found {plist.get(key)!r}")
    # The build also substitutes the minimum macOS version; nothing may be left as a template placeholder.
    leftover = sorted(key for key, value in plist.items() if isinstance(value, str) and "${" in value)
    if leftover:
        raise SystemExit(f"Unsubstituted placeholders in {', '.join(leftover)}")
    print(f"Verified launcher version {version}")


if __name__ == "__main__":
    main()
