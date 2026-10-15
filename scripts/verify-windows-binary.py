#!/usr/bin/env python3
"""Verify the Windows proxy's architecture, exports, static CRT and provenance marker."""
import argparse
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", nargs="?", default="zig-out/bin/version.dll")
parser.add_argument("--identity", help="Expected complete stfc-identity-v1 version-resource marker")
args = parser.parse_args()
data = Path(args.binary).read_bytes()
pe = struct.unpack_from("<I", data, 60)[0]
if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
    raise SystemExit("Expected a PE executable")
if struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
    raise SystemExit("Expected x86_64 machine architecture")
section_count = struct.unpack_from("<H", data, pe + 6)[0]
optional_size = struct.unpack_from("<H", data, pe + 20)[0]
optional = pe + 24
if struct.unpack_from("<H", data, optional)[0] != 0x20B:
    raise SystemExit("Expected a PE32+ optional header")
section_table = optional + optional_size
sections = [struct.unpack_from("<IIII", data, section_table + i * 40 + 8) for i in range(section_count)]


def offset(rva):
    for virtual_size, virtual_address, raw_size, raw_address in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_address + rva - virtual_address
    raise ValueError(f"Unmapped RVA {rva:#x}")


def string(rva):
    start = offset(rva)
    return data[start:data.index(b"\0", start)].decode("ascii")


imports = []
position = offset(struct.unpack_from("<I", data, optional + 120)[0])
while any(data[position:position + 20]):
    imports.append(string(struct.unpack_from("<I", data, position + 12)[0]))
    position += 20
crt_prefixes = ("msvcp", "vcruntime", "ucrtbase", "api-ms-win-crt-")
if any(name.lower().startswith(crt_prefixes) for name in imports):
    raise SystemExit(f"Dynamic CRT dependency: {imports}")
export_directory = offset(struct.unpack_from("<I", data, optional + 112)[0])
name_count = struct.unpack_from("<I", data, export_directory + 24)[0]
name_table = offset(struct.unpack_from("<I", data, export_directory + 32)[0])
exports = {string(struct.unpack_from("<I", data, name_table + i * 4)[0]) for i in range(name_count)}
root = Path(__file__).resolve().parent.parent
definition = (root / "src/platform/windows/version.def").read_text()
expected = set(definition.split("EXPORTS", 1)[1].split())
if exports != expected:
    raise SystemExit(f"Export mismatch: missing={expected - exports}, extra={exports - expected}")
marker = args.identity or "stfc-identity-v1;distribution=netniv.stfc-community-mod;"
if marker.encode("utf-16le") not in data:
    raise SystemExit("Missing or incorrect UTF-16 version-resource provenance marker")
print(f"Verified x86_64 proxy: {len(exports)} exports, static CRT, provenance marker")
