#!/usr/bin/env python3
"""Audit uncompressed WfW 3.11 NDIS.386 files for the CPU-speed patch."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

BEFORE = bytes.fromhex(
    "B9 00 00 10 00 90 E2 FE CD 20 3F 00 01 00 "
    "2B C2 B9 E8 03 00 00 F7 E1 A3"
)
AFTER = bytes.fromhex(
    "B9 00 00 40 06 E2 FE CD 20 3F 00 01 00 29 "
    "D0 75 01 40 6A 0A 59 F7 E1 A3"
)


def candidates(paths: list[Path]) -> list[Path]:
    found: set[Path] = set()
    for path in paths:
        if path.is_dir():
            found.update(p for p in path.rglob("*") if p.name.upper() == "NDIS.386")
        elif path.name.upper() == "NDIS.386":
            found.add(path)
    return sorted(found)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="+", type=Path)
    args = parser.parse_args()
    files = candidates(args.path)
    if not files:
        parser.error("no uncompressed NDIS.386 files found")

    failed = False
    for path in files:
        data = path.read_bytes()
        before = data.count(BEFORE)
        after = data.count(AFTER)
        if before == 1 and after == 0:
            state = "original"
            offset = data.find(BEFORE)
        elif before == 0 and after == 1:
            state = "patched"
            offset = data.find(AFTER)
        else:
            state = "UNKNOWN"
            offset = -1
            failed = True
        digest = hashlib.sha256(data).hexdigest()
        where = f"0x{offset:x}" if offset >= 0 else "-"
        print(
            f"{state:8} size={len(data):5} offset={where:>6} "
            f"before={before} after={after} sha256={digest} {path}"
        )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
