#!/bin/bash
# Fails when a macOS binary links a library users may not have: anything from Homebrew, or OpenSSL. The system
# libcurl is the one allowed exception; every other libcurl, libssl or libcrypto is forbidden.
#
# Usage: scripts/check-mac-linkage.sh <binary>...
set -euo pipefail

status=0
for binary in "$@"; do
  echo "==> $binary"
  libraries="$(otool -L "$binary")"
  printf '%s\n' "$libraries"
  if printf '%s\n' "$libraries" |
    grep -vE '^[[:space:]]+/usr/lib/libcurl(\.[0-9]+)*\.dylib[[:space:]]' |
    grep -E '/opt/homebrew/|/usr/local/(opt|Cellar)/|(^|/)lib(ssl|crypto|curl)(\.[0-9]+)*\.dylib'; then
    echo "::error file=$binary::Forbidden dynamic dependency in $binary"
    status=1
  fi
done
exit "$status"
