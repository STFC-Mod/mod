#!/usr/bin/env python3
"""Validate that every bundled example config is well-formed TOML.

The mod loads user configs with toml++, which -- per the TOML spec -- rejects
duplicate keys and other malformed input outright (it does not silently ignore
them). The example files exist to be copied into community_patch_settings.toml,
so a malformed example is a copy-paste trap. This check parses each example and
fails if any of them is not valid TOML, guarding against that at PR time.
"""

import glob
import os
import sys

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover - fallback for older runners
    import tomli as tomllib  # type: ignore


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pattern = os.path.join(root, "docs/configuration/example_community_patch_settings*.toml")
    files = sorted(glob.glob(pattern))
    if not files:
        print(f"no example config files matched {pattern!r}", file=sys.stderr)
        return 1

    failures = []
    for path in files:
        name = os.path.basename(path)
        try:
            with open(path, "rb") as handle:
                tomllib.load(handle)
        except tomllib.TOMLDecodeError as exc:
            print(f"  FAIL  {name}: {exc}")
            failures.append(name)
        else:
            print(f"  PASS  {name}")

    if failures:
        print(
            f"\n{len(failures)} example config(s) are not valid TOML: "
            + ", ".join(failures),
            file=sys.stderr,
        )
        return 1

    print(f"\nall {len(files)} example config(s) are valid TOML")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
