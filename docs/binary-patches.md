# Binary patches

`[HexPatch]` changes bytes in existing files, found by a byte signature
rather than by version-specific offsets:

```ini
[HexPatch]                      ; file|old bytes|new bytes|exact matches
Patch=%WINDIR%\SYSTEM\WIN386.EXE|66 C7 46 49 FF FF|6A FF 8F 46 49 90|2
Patch=1|%DIR%\PROGRAM.EXE|7503|EB03|1   ; optional condition
; skip only when the target does not exist; reject an unknown existing file
PatchIfPresent=%WINDIR%\SYSTEM\NDIS.386|...|...|1
; group|language/build label|file|old bytes|new bytes|exact matches
PatchVariant=Win311Fat32|English|%WINDIR%\SYSTEM\WIN386.EXE|...|...|2
PatchVariant=Win311Fat32|German|%WINDIR%\SYSTEM\WIN386.EXE|...|...|2
```

## Patch=

The bytes are hexadecimal pairs separated by spaces, commas, hyphens or
nothing. The old and new signatures must have the same length (up to 32
bytes), and the last field is the exact number of expected matches.

Setup refuses an unknown or ambiguous file, writes the changed file through a
temporary file, and keeps the original as `.G01`, `.G02`, and so on. Running
Setup again recognizes an already patched file. `INSTALL /U` applies the
checked reverse transformation and removes the backup (it is listed in
`INSTALL.LOG`).

## PatchIfPresent=

`PatchIfPresent=` has the same fields and safety checks as `Patch=`, but
skips the operation when the target file is absent. It is meant for optional
installed components such as WfW 3.11 networking. An existing file with an
unknown or ambiguous signature remains an error: "optional" never means
"patch an unverified binary".

## PatchVariant=

Repeated `PatchVariant=` entries give language- or build-specific forms of
one logical patch. The first field groups the alternatives, the second is a
readable label. Setup probes every alternative and proceeds only when exactly
one before or already-patched signature matches, so the choice is based on
the binary itself rather than on the configured Windows language. Do not
replace the `...` placeholders above until the signatures have been verified
against the exact vendor binaries.

## Verified patches

- The WIN386 example is the Windows 3.1/3.11 FAT32 compatibility patch. Its
  single signature was statically verified against 44 Windows 3.1, Windows
  3.11 and WfW 3.11 language/build variants; see
  [WIN386.EXE and FAT32](win386-fat32.md). Distributors should enable it only
  for an appropriate DOS/Windows combination.
- The WfW 3.11 NDIS CPU-speed patch and its audit of 20 languages are
  described in [NDIS.386 on fast CPUs](ndis386-cpu-speed.md).
