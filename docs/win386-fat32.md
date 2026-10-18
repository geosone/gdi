# WIN386 FAT32 Patch Coverage

The installer applies the R. Loew Windows 3.11 FAT32 compatibility change by
exact byte signature, not by language, file size, fixed offset, or a user
choice:

```ini
[HexPatch]
Patch=%WINDIR%\SYSTEM\WIN386.EXE|66 C7 46 49 FF FF|6A FF 8F 46 49 90|2
```

The before/after bytes and the two-match requirement were recovered directly
from `311FAT32.COM` (558 bytes). Its original implementation additionally
requires a single file size and twelve fixed offsets. GDI intentionally uses
the surrounding instruction signature instead, while refusing missing,
already mixed, or ambiguous matches and retaining the original as `.G01`.

## Verified source matrix

Static analysis of the repository owner's `16-BIT.iso` covered 44 unmodified
`WIN386.EXE` files, representing 32 unique SHA-256 hashes. No proprietary
binaries are stored in this repository.

| Family/layout | Files | Signature offsets | Languages/variants |
|---|---:|---|---|
| Windows 3.1/3.11, 544789 bytes | 18 | `5EA26`, `5EC38` | Arabic, Central European, Czech, Danish, Dutch, Finnish, French/Arabic, French, Greek, Hebrew, Italian, Norwegian, Polish, Russian, Swedish, Thai, Turkish, US |
| Windows 3.1/3.11, 546837 bytes | 5 | `5F226`, `5F438` | Catalan, Hungarian, German, Portuguese, Spanish |
| WfW 3.11, 577557 bytes | 17 | `65A26`, `65C38` | Arabic, Danish, English/Arabic, Finnish, French, French/Arabic, Hebrew, Hungarian, Italian, Norwegian, Polish, Russian, Spanish, Swedish, Thai, US |
| WfW 3.11, 579605 bytes | 3 | `66226`, `66438` | Dutch, German, Portuguese |
| Windows 3.1 Korean, 579673 bytes | 1 | `63226`, `63438` | Korean |

Every file contains the before signature exactly twice and the after signature
zero times. This proves deterministic applicability; boot/runtime testing is
still required before declaring each distribution operational on FAT32.

Run `python3 tools/audit_win386.py PATH` against legally obtained, expanded
Windows files. Exit status 1 means at least one binary is unknown or ambiguous.

`osr2fix.zip` is a separate DOS 7.10 compatibility patch: its `W3XSTART.EXE`
modifies `IO.SYS`, not `WIN386.EXE`, and must use a separate patch group.
