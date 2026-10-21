# GeosOne Dual Installer

The GeosOne Dual Installer (GDI) is a single dual-mode `INSTALL.EXE` for DOS
programs, drivers and Windows 3.x software. Under DOS its MZ part runs a text installer in the style of the
Qualitas MAX (386MAX) setup: a title bar, cyan dialog windows and a key bar.
Windows 3.1x and 95/98/ME load the native Win16 frontend from the same file.
Both read the same `INSTALL.INI`.

License: GNU General Public License version 3.

## What it does

- shows a welcome text and licenses that must be accepted, asks for the
  target directory and options, copies the files;
- adds and removes lines in `CONFIG.SYS` and `AUTOEXEC.BAT`, edits
  `SYSTEM.INI`, `WIN.INI`, the Program Manager groups and the Windows 3.x
  registry, everything journalled;
- applies checked binary patches by byte signature, without relying on
  version-specific offsets;
- removes the installation again (`INSTALL /U`) and updates an installed
  version;
- installs several packages in one run (`[Installer]`);
- takes the files from compressed archives on several disks;
- finds installed programs (`[Find]`) and runs package detection programs
  (`[Detect]`) for the defaults of the options;
- in DOS mode: partitions and formats the hard disk, installs a DOS and
  further software in the same run.

## Usage

    INSTALL [/U] [/B:d:] [file.INI]

`INSTALL.EXE` reads `INSTALL.INI` (or the INI file given) from its own
directory and takes the files from there. `/B:d:` names the drive of the
system whose `CONFIG.SYS` and `AUTOEXEC.BAT` are changed (default: the boot
drive). `INSTALL /U`, run from the target directory, removes the
installation.

A minimal package:

```ini
[Setup]
Title=AHCIASPI Setup
Product=GeosOne AHCIASPI 0.1
License=LICENSE.TXT
DefaultDir=%BOOT%\AHCIASPI

[Options]
Option=on|Load AHCIASPI.SYS in CONFIG.SYS

[Files]
AHCIASPI.SYS

[Config.sys]
Remove=AHCIASPI.SYS
Add=1|DEVICE=%DIR%\AHCIASPI.SYS
```

## Documentation

The manual is also available as one PDF (`gdi-manual.pdf` in the release,
`make docs`).

| Chapter | Contents |
|---|---|
| [The package file: INSTALL.INI](docs/install-ini.md) | `[Setup]`, licenses, options and conditions, `[Find]`/`[Detect]`, files, variables, languages, texts |
| [System files](docs/system-files.md) | `CONFIG.SYS`, `AUTOEXEC.BAT`, `SYSTEM.INI`, `WIN.INI`, Program Manager |
| [Registry](docs/registry.md) | `[Registry]`: `Set=`, `DeleteValue=`, `DeleteKey=`, `Import=` of `.REG` files, journal |
| [Binary patches](docs/binary-patches.md) | `[HexPatch]`: `Patch=`, `PatchIfPresent=`, `PatchVariant=` |
| [Uninstall and updates](docs/uninstall-update.md) | `INSTALL /U`, journals, backups, `GDI.INI`, `[Keep]` |
| [Combined setup](docs/combined-setup.md) | `[Installer]`: several packages in one run |
| [Windows](docs/windows.md) | the Win16 frontend, `Windows=no`, the runner |
| [Disks and DOS mode](docs/disks-dos-mode.md) | GDA archives, disk images, installing DOS |
| [Building and testing](docs/building.md) | Open Watcom, UPX, QEMU tests |
| [WIN386.EXE and FAT32](docs/win386-fat32.md) | the verified WIN386 patch |
| [NDIS.386 on fast CPUs](docs/ndis386-cpu-speed.md) | the verified NDIS.386 patch |
