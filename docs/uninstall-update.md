# Uninstall and updates

## INSTALL /U

`INSTALL /U` removes an installation. With `[Setup] Uninstall=yes` (the
default) Setup copies `INSTALL.EXE` and `INSTALL.INI` to the target
directory; `INSTALL /U` is run from there. It undoes everything in reverse
order:

1. the programs of `[Run] Uninstall=`,
2. the binary patches (checked reverse transformation),
3. the `Remove=` lines and `Extend=` texts of `CONFIG.SYS` and
   `AUTOEXEC.BAT`, the lines and values of `SYSTEM.INI`, `WIN.INI` and the
   other INI files,
4. the Program Manager items and the groups Setup created,
5. the registry (from its journal, see [Registry](registry.md)),
6. the files listed in `INSTALL.LOG`, the target directory and the empty
   parent directories Setup created for it (`DIRS.LOG`), and the entry in
   `GDI.INI`.

## The journals

All of them are in the target directory (`%DIR%`):

| File | Contents |
|---|---|
| `INSTALL.LOG` | the installed files; `path|backup` for a replaced file |
| `DIRS.LOG` | the first parent directory Setup created for `%DIR%` |
| `SYSTEM.ADD` | the lines added to `SYSTEM.INI` |
| `SYSTEM.CHG` | the lines of `SYSTEM.INI` replaced (`Change=`) |
| `SYSTEM.SET`, `WIN.SET` | the previous lines of the values set in `SYSTEM.INI`, `WIN.INI` |
| `INI.SET` | the file, section, key and previous line of the values set by `[Ini]` |
| `PROGMAN.LOG` | the created groups (`G|group`) and added items (`I|group|title`) |
| `REGISTRY.LOG` | the previous state of every registry key and value changed |
| `REGISTRY.PLN`, `UNINST.PND` | registry work left for the next Windows start |
| `RUNONCE.PLN` | the `[Run] Windows=` programs left for the next Windows start |

A value or line changed by the user after the installation is kept: the
uninstaller restores only what is still as Setup left it. A registry key that
one package created and a later package also uses stays when the first
package is removed before the later one. A parent directory stays as well,
but the packages still in it (a subdirectory with `INSTALL.INI` and no
`DIRS.LOG` of its own) get the `DIRS.LOG`: the last of them removes the
parent with its own directory, also when that happens only at the next
Windows start (a package whose registry is restored by its runner).
Packages removed in reverse order leave nothing behind. Setup and
`INSTALL /U` work in plain DOS and under Windows; a package installed in one
can be removed in the other.

## Backups

The original configuration files are kept as `CONFIG.G01`, `AUTOEXEC.G01`,
`SYSTEM.G01`; further runs write `.G02` and so on. An installed file that
replaces an existing one outside `%DIR%` (for example
`REPL.TXT=%WINDIR%\REPL.TXT`) is kept the same way; `INSTALL.LOG` records
`path|backup`, and `INSTALL /U` puts the original back. A second installation
keeps the first backup.

When two packages replace the same file, the later one keeps the earlier
one's file as its backup. If the earlier package is removed first, the file
stays as the later package left it, and the earlier package hands its backup
(the original) over to the later one; when it had created the file, the later
package removes it at its own removal. Whatever the order of removal, the
original comes back in the end.

## Several packages in one directory: SetupDir=

Normally `INSTALL.EXE`, `INSTALL.INI` and the journals are in `%DIR%`, so
each package needs a directory of its own. With `[Setup] SetupDir=` they
go to a subdirectory of `%DIR%` instead, and several packages can put their
files into the same directory:

```ini
[Setup]
Id=AHCIASPI
DefaultDir=%BOOT%\OPENASPI     ; AHCIASPI.SYS etc. in C:\OPENASPI
SetupDir=AHCIASPI              ; INSTALL.EXE, INSTALL.INI, INSTALL.LOG in C:\OPENASPI\AHCIASPI
```

`GDI.INI` names the setup directory; `C:\OPENASPI\AHCIASPI\INSTALL /U`
removes the files of this package only and its setup directory, and
`C:\OPENASPI` stays as long as another package uses it (its setup directory
takes it over). A file that two packages install (a shared driver) is
handed over as described above.

## All installed packages: INSTALL /M

`INSTALL /M` lists the packages in `GDI.INI` `[Installed]` with name,
version and directory; those checked are removed one after the other, the
last installed first, each as with `INSTALL /U` from its directory. It works
in plain DOS and under Windows (a package with `Windows=no` only in DOS; the
list says so). Any `INSTALL.EXE` that knows `/M` can be used, for
instance the one of a package's directory.

## Shared DLLs: [Newer]

A file that several programs use (`CTL3D.DLL`, `VBRUN300.DLL`, the MFC
DLLs) should not replace a newer version that is there already:

```ini
[Newer]
*.DLL
1|%WINDIR%\SYSTEM\CTL3D*.DLL
```

The lines of `[Newer]` are patterns as in `[Keep]` (a file name, a path
below `%DIR%`, or a full path; optionally with a condition). A file they
name that exists already is replaced only when the new one has a higher file
version (the `VS_FIXEDFILEINFO` of its version resource); otherwise the
installed file stays and is not part of the installation (`INSTALL /U`
leaves it). When one of the two files has no version resource, the file is
replaced as usual. Files from an archive are unpacked to a temporary file in
`%DIR%` first and compared afterwards.

## Updates

An installation that can be removed again (`Uninstall=yes`) is entered in
`%BOOT%\GDI.INI`:

```ini
[Installed]
GeosOne AHCIASPI=C:\AHCIASPI|110|0.1
```

The key is `[Setup] Id=` (default: `Product=`), the value the directory, the
options chosen and `[Setup] Version=`. When the package is installed again,
Setup (DOS engine and Win16 frontend) finds that entry and offers an update
of that directory with the options chosen then, showing the installed and the
new version.

An update replaces the files and adds only what is new in this version:
`Add=`, `Extend=` and `Set=` entries of `[Config.sys]`, `[Autoexec.bat]`,
`[System.ini]` and `[Win.ini]`, and `[ProgMan]` items (group and title), that
are not in the installed `INSTALL.INI`. Existing entries stay as they are,
including manual changes; `Remove=` lines, binary patches and the registry
are not applied again, and an old Program Manager item the user deleted does
not come back. New `Set=` values are added to `SYSTEM.SET`/`WIN.SET`, new
items to `PROGMAN.LOG`, so `INSTALL /U` removes them as well.

The settings files of the program are not overwritten either:

```ini
[Keep]
CALMIRA.INI
1|DATA\*.DAT
```

A pattern without a path is a file name, one with a path is below `%DIR%`
(or a full path). Without `[Keep]`, existing `*.INI` and `*.CFG` files are
kept. `INSTALL.LOG` keeps the first backups, so `INSTALL /U` still restores
the original files; it also removes the entry from `GDI.INI`.
