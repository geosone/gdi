# Uninstall and updates

## INSTALL /U

`INSTALL /U` removes an installation. With `[Setup] Uninstall=yes` (the
default) Setup copies `INSTALL.EXE` and `INSTALL.INI` to the target
directory; `INSTALL /U` is run from there. It undoes everything in reverse
order:

1. the binary patches (checked reverse transformation),
2. the `Remove=` lines and `Extend=` texts of `CONFIG.SYS` and
   `AUTOEXEC.BAT`, the lines and values of `SYSTEM.INI` and `WIN.INI`,
3. the Program Manager items and the groups Setup created,
4. the registry (from its journal, see [Registry](registry.md)),
5. the files listed in `INSTALL.LOG`, the target directory and the empty
   parent directories Setup created for it (`DIRS.LOG`), and the entry in
   `GDI.INI`.

## The journals

All of them are in the target directory (`%DIR%`):

| File | Contents |
|---|---|
| `INSTALL.LOG` | the installed files; `path|backup` for a replaced file |
| `DIRS.LOG` | the first parent directory Setup created for `%DIR%` |
| `SYSTEM.ADD` | the lines added to `SYSTEM.INI` |
| `SYSTEM.SET`, `WIN.SET` | the previous lines of the values set in `SYSTEM.INI`, `WIN.INI` |
| `PROGMAN.LOG` | the created groups (`G|group`) and added items (`I|group|title`) |
| `REGISTRY.LOG` | the previous state of every registry key and value changed |
| `REGISTRY.PLN`, `UNINST.PND` | registry work left for the next Windows start |
| `RUNONCE.PLN` | the `[Run] Windows=` programs left for the next Windows start |

A value or line changed by the user after the installation is kept: the
uninstaller restores only what is still as Setup left it. A registry key or
parent directory that one package created and a later package also uses stays
when the first package is removed before the later one: it is not empty yet.
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
