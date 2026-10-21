# Disks and DOS mode

## Archives and disk sets

Files can come from a GDA archive (`[Setup] Archive=NAME`: `NAME.D01`,
`NAME.D02`, ... on one or more disks). The installer asks for each disk
("Please insert disk 2 into drive A:"; `[Disks]` `2=label` adds a line) and
checks that it is the right one. The format is described in
`src/archive.c`: deflate (RFC 1951), CRC-32, file dates; one file can
continue on the next disk.

`tools/GdiDisk.ps1` (PowerShell 5.1 or 7, also on Linux with `pwsh`) loads
`tools/GdiDisk.cs`:

| Class | Use |
|---|---|
| `Gda` | distribute files over GDA archive parts for given disk capacities |
| `Fat12Image` | write 1.44 MB floppy images; with `BootSectors.Win9xFat12` they boot the `IO.SYS` of MS-DOS 7.x |
| `Fat12Reader` | read the files of a floppy image |
| `MsExpand` | expand SZDD and KWAJ files (`COMPRESS`/`EXPAND`, `*.??_`) |

The boot sector is our own (`tools/boot/bs9x.asm`, `make bootsector`). The
MS-DOS 7.1 disk set builder that uses all this is the msdosinst project:
<https://github.com/geosone/msdosinst>

## DOS mode

`[Setup] Mode=DOS` (with `Drive=C:`) installs a DOS from a boot disk:

1. no drive C: yet: `Fdisk=` (default `FDISK.EXE`) runs, then the computer
   restarts and the setup starts again;
2. drive C: not formatted: `Format=` (e.g. `FORMAT.COM %BOOT% /V:MSDOS71`)
   runs;
3. the normal steps, with `[Run] Before=SYS.COM %BOOT%` and `Replace=yes`
   for new configuration files;
4. `Extras=yes`: further software from other disks; the installer loads
   their `INSTALL.INI` and installs them for the new system in the same run
   (`ExtrasDir=A:\`);
5. a restart.

Packages installed through `Extras=yes` cannot have `[Registry]` entries
yet (see [Registry](registry.md)).
