#!/usr/bin/env python3
"""Fails when an example settings file is not valid TOML: users copy them, and toml++ rejects the whole file."""

import sys
from pathlib import Path

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover - fallback for older runners
    import tomli as tomllib  # type: ignore


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    pattern = "example_community_patch_settings*.toml"
    configs = root / "docs" / "configuration"
    files = sorted(configs.glob(pattern))
    if not files:
        print(f"no example config files matched {str(configs / pattern)!r}", file=sys.stderr)
        return 1

    failures = []
    for path in files:
        try:
            with path.open("rb") as handle:
                tomllib.load(handle)
        except tomllib.TOMLDecodeError as exc:
            print(f"  FAIL  {path.name}: {exc}")
            failures.append(path.name)
        else:
            print(f"  PASS  {path.name}")

    if failures:
        print(f"\n{len(failures)} example config(s) are not valid TOML: {', '.join(failures)}", file=sys.stderr)
        return 1

    print(f"\nall {len(files)} example config(s) are valid TOML")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
