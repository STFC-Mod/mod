#!/usr/bin/env python3
"""Stamp the current commit hash into version.h for CI builds."""

import argparse
import re
from pathlib import Path


VERSION_HASH_PATTERN = re.compile(
    r'^(#define VERSION_COMMIT_HASH[ \t]+)"[^"\r\n]*"(?=\r?$)',
    re.MULTILINE,
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("commit_hash", help="full Git commit hash")
    parser.add_argument(
        "--header",
        type=Path,
        default=Path("src/mod/version.h"),
        help="version header to update",
    )
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-fA-F]{7,64}", args.commit_hash):
        parser.error("commit_hash must be a hexadecimal Git object ID")

    short_hash = args.commit_hash[:8].lower()
    with args.header.open("r", encoding="utf-8", newline="") as header_file:
        content = header_file.read()

    stamped_content, replacements = VERSION_HASH_PATTERN.subn(
        rf'\g<1>"{short_hash}"',
        content,
    )
    if replacements != 1:
        raise RuntimeError(
            f"expected one VERSION_COMMIT_HASH definition in {args.header}, found {replacements}"
        )

    with args.header.open("w", encoding="utf-8", newline="") as header_file:
        header_file.write(stamped_content)

    print(f"Stamped {args.header} with commit {short_hash}")


if __name__ == "__main__":
    main()
