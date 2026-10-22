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

### Further software named in advance: [Extras]

`[Extras]` names packages that are offered with the options (a list to
check) and installed after the main setup, each from its own disk:

```ini
[Extras]
Extra=on|AHCIASPI|AHCIASPI - CD/DVD drives on SATA/AHCI
Extra=off|EUSBASPI|EUSBASPI - USB drives
Extra=off|OPENASPI|OpenASPI - SCSI adapters|R:\OPENASPI
```

The fields are the default (`on`/`off`), the `Id=` of the package (or its
`Product=` without `Id=`), the text in the list and, optionally, its
directory. For each package checked Setup asks for its disk (the directory
proposed is `ExtrasDir=`, `A:\` by default; Cancel skips the package) and
checks the `Id=` of the `INSTALL.INI` there: for a disk with another
package or none Setup offers Retry (after the right disk is in) or Skip,
so a package checked by mistake can always be left out. A package with a directory of its own (on a setup CD) is
installed from there without a question; when that directory is not there
(Setup was started from disks), Setup asks for its disk. With `Extras=yes` Setup asks afterwards for
further software as before.
