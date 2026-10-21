# NDIS.386 on fast CPUs

The optional `NDIS.386` patch prevents the Windows for Workgroups 3.11 network
driver's startup calibration loop from failing on fast processors. GDI applies
it only to an installed `%WINDIR%\SYSTEM\NDIS.386`; a missing driver is skipped,
while an unknown or ambiguous existing binary stops installation.

The signature and replacement originate from JHRobotics' MIT-licensed
[`patcher9x`](https://github.com/JHRobotics/patcher9x), specifically its
`PATCH_CPU_SPEED_NDIS_V4` implementation. Copyright (c) 2022 Jaroslav Hensl;
used under the repository's MIT license. GDI retains its normal numbered backup
and supports checked reversal during uninstall.

## Validation

`tools/audit_ndis386.py PATH...` checks extracted, uncompressed `NDIS.386`
files. The audit performed for this integration covered all 20 WfW 3.11
language directories in the local 16-bit archive. Every file contained exactly
one original signature at offset `0x5e0a` and no patched signature. Binary sizes
were 27,802 or 27,846 bytes; duplicate localized binaries were counted as
separate installation variants.

The installed file is patched directly. Compressed setup-media files named
`NDIS.38_` must first be expanded by the Windows setup toolchain; GDI does not
rewrite or recompress KWAJ installation media.

This is static signature validation, not a substitute for boot and networking
tests on real hardware and representative virtual machines.
