#!/usr/bin/env python3
"""Audit extracted Windows 3.x WIN386.EXE files for the FAT32 patch."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


BEFORE = bytes.fromhex("66 C7 46 49 FF FF")
AFTER = bytes.fromhex("6A FF 8F 46 49 90")


def offsets(data: bytes, pattern: bytes) -> list[int]:
    found: list[int] = []
    start = 0
    while True:
        start = data.find(pattern, start)
        if start < 0:
            return found
        found.append(start)
        start += 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="verify exact WIN386.EXE FAT32 patch signatures"
    )
    parser.add_argument("root", type=Path, help="tree containing extracted files")
    args = parser.parse_args()
    files = sorted(
        path for path in args.root.rglob("*")
        if path.is_file() and path.name.upper() == "WIN386.EXE"
    )
    if not files:
        parser.error("no WIN386.EXE files found")

    failed = 0
    hashes: set[str] = set()
    for path in files:
        data = path.read_bytes()
        before = offsets(data, BEFORE)
        after = offsets(data, AFTER)
        digest = hashlib.sha256(data).hexdigest()
        hashes.add(digest)
        state = "original" if len(before) == 2 and not after else (
            "patched" if len(after) == 2 and not before else "UNKNOWN"
        )
        if state == "UNKNOWN":
            failed += 1
        relative = path.relative_to(args.root)
        positions = before if before else after
        print(
            f"{state:8} {len(data):7} {digest} "
            f"{','.join(hex(position) for position in positions):17} {relative}"
        )
    print(f"{len(files)} file(s), {len(hashes)} unique SHA-256 hashes, "
          f"{failed} unknown")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
