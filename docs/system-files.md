# System files

The installer changes `CONFIG.SYS` and `AUTOEXEC.BAT` of the system on
`/B:d:` (default: the boot drive), and `SYSTEM.INI`, `WIN.INI` and the
Program Manager groups of Windows 3.x. Every change is recorded so that
`INSTALL /U` can undo it (see [Uninstall and updates](uninstall-update.md)).

## CONFIG.SYS and AUTOEXEC.BAT

```ini
[Config.sys]
Remove=AHCIASPI.SYS             ; lines containing this text are removed first
Add=1|DEVICE%O2%=%DIR%\AHCIASPI.SYS
Position=bottom                 ; top or bottom (default: bottom)

[Autoexec.bat]
Add=...                         ; default position: top (after @ECHO OFF)
Extend=5,?CDEX|MSCDEX,SHSUCDX| /D:ASPICD0|%CDEX% /D:ASPICD0
Replace=yes                     ; write a new file (the old one is kept as backup)
```

- `Remove=text` removes the lines that contain the text, before lines are
  added.
- `Add=condition|line` adds a line.
- `Extend=condition|programs|text|new line` appends the text to the first
  line that starts one of the programs, or adds the new line when there is
  none (above: `/D:ASPICD0` for MSCDEX or SHSUCDX).
- `Replace=yes` writes a new file; the old one is kept as backup.

**Where lines go:**

- `CONFIG.SYS` with a `[menu]` (multi-configuration): lines go into the
  `[common]` block, which is created if needed.
- `AUTOEXEC.BAT` with position `bottom`: lines go before a line that starts
  Windows (`WIN`).

**Backups:** the original files are kept as `CONFIG.G01`, `AUTOEXEC.G01`;
further runs write `.G02` and so on.

## SYSTEM.INI

```ini
[System.ini]
Remove=EUSBVXD.386
Add=2|386Enh|device=%DIR%\EUSBVXD.386   ; section|line
Set=3|boot|shell=%DIR%\CALMIRA.EXE      ; section|key=value
```

**Lines:** `Add=condition|section|line` adds a line (for example
`device=vshare.386`) only when the section does not have it yet. The lines
actually added are recorded in `%DIR%\SYSTEM.ADD`, and `INSTALL /U` removes
exactly these, so a line that was there before stays.

**Values:** `Set=condition|section|key=value` replaces one named value
instead of adding a duplicate line. Setup records the exact previous line in
`%DIR%\SYSTEM.SET`. Uninstall restores it only while the installed value is
still unchanged; a later change by the administrator is kept. Duplicate
existing keys and malformed journal data are rejected. This is suitable for a
reversible shell selection, for example `Set=3|boot|shell=%DIR%\CALMIRA.EXE`.

The original file is kept as `SYSTEM.G01` (`.G02`, ...).

## WIN.INI

`[Win.ini]` takes the same `Set=condition|section|key=value` lines as
`[System.ini]` and journals them in `%DIR%\WIN.SET` (same format as
`SYSTEM.SET`). In plain DOS the engine edits `WIN.INI` itself; under Windows,
which keeps `WIN.INI` in memory, the Win16 frontend writes the values with
the profile functions. Both sides restore the same journal on `INSTALL /U`.

## Program Manager

`[ProgMan]` adds items to Windows 3.x groups:

```ini
[ProgMan]
Item=1|Zubehör|Calmira|%DIR%\CALMIRA.EXE
Item=2|Calmira|Hilfe|WINHELP.EXE %DIR%\CALMIRA.HLP|WINHELP.EXE|0
```

The fields are condition, group, title, command line, icon file (default:
the program of the command line) and icon index. A missing group is created.
An item whose title is already in the group is left alone.

In plain DOS the engine writes the `.GRP` files and `PROGMAN.INI` directly
(Program Manager extracts the icons again on its next start); under Windows
the frontend uses DDE with Program Manager. Both record `%DIR%\PROGMAN.LOG`
(`G|group` for a created group, `I|group|title` for an added item), and
`INSTALL /U` removes those items and the created groups once they are empty.
A package installed from DOS can be removed under Windows and the other way
round: both sides read the same journals.
