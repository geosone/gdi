# GeosOne Dual Installer

A single dual-mode `INSTALL.EXE` for DOS programs and drivers. Under DOS its
MZ part runs the text installer. Windows 3.1/9x loads the native Win16 NE
frontend from the same file, written directly against USER/GDI without MFC or
another UI framework. Both modes read the same `INSTALL.INI`. The DOS interface
looks like the setup of the Qualitas MAX (386MAX) memory manager: a title bar,
cyan dialog windows and a key bar.

It does the following:
- shows a welcome text and a license that must be accepted,
- asks for the target directory and options,
- copies the files,
- adds or removes lines in `CONFIG.SYS` and `AUTOEXEC.BAT`, and in the `SYSTEM.INI` of Windows 3.x, keeping backups,
- applies checked binary patches by byte signature, without relying on version-specific offsets,
- removes the installation again (`INSTALL /U`),
- takes the files from compressed archives on several disks (GDA, deflate) and asks for the disks,
- finds programs that are already installed (`[Find]`, e.g. MSCDEX or SHSUCDX),
- runs package-supplied detection programs (`[Detect]`) and uses their exit status for option defaults and conditions,
- in DOS mode: partitions and formats the hard disk (FDISK, FORMAT), installs a DOS and then installs further software from other disks (their `INSTALL.INI`) in the same run.

License: GNU General Public License version 3.

## Usage

    INSTALL [/U] [/B:d:] [/O:bits] [/T:plan] [file.INI]

`INSTALL.EXE` reads `INSTALL.INI` from its own directory and takes the files from there.
`/B:d:` names the drive of the system whose `CONFIG.SYS` and `AUTOEXEC.BAT` are changed (default: the boot drive).
`/O:bits` overrides initial option states (`1` on, `0` off, any other character
keeps the INI/detector default). It is primarily the internal interface from
the Windows frontend to the embedded DOS engine. `/T:plan` is the same: the
engine writes the resolved `[Win.ini]` and `[ProgMan]` entries to this file
for the frontend instead of editing `WIN.INI` and the groups itself.

`INSTALL /U` removes an installation:
- Run it from the target directory: the installer copies itself and the INI there (`Uninstall=yes`).
- It removes the `Remove=` lines and the `Extend=` texts from the configuration files, then deletes the files listed in `INSTALL.LOG` and the directory.

**Backups:** the original configuration files are kept as `CONFIG.G01`, `AUTOEXEC.G01`, `SYSTEM.G01`; further runs write `.G02` and so on.
An installed file that replaces an existing one outside `%DIR%` (for example
`REPL.TXT=%WINDIR%\REPL.TXT`) is kept the same way; `INSTALL.LOG` records
`path|backup`, and `INSTALL /U` puts the original back. A second installation
keeps the first backup.

## INSTALL.INI

```ini
[Setup]
Title=AHCIASPI Setup            ; top bar, left
Product=GeosOne AHCIASPI 0.1    ; top bar, right (brand)
License=1,2,3                  ; numbered licenses, in display order
License1=Main product|LICENSE.TXT
License2=DOSLFN|DOSLFN.LIC
License3=WinFile|WINFILE.LIC
Readme=README.TXT               ; offered at the end (optional)
DefaultDir=%BOOT%\AHCIASPI      ; proposed target directory
Done=AHCIASPI is installed. Restart the computer to load the driver.
Uninstall=yes                   ; copy INSTALL.EXE/INI to the target (default)

[Welcome]
Any number of lines of welcome text.

[Options]                       ; check boxes, numbered 1, 2, ... in this order
Option=on|Load AHCIASPI.SYS in CONFIG.SYS
Option=off|Load it into upper memory|HIGH      ; %O2% = "HIGH" if on
Option=on|Also IDE channels
Option=?TAPE|Install the tape driver           ; detected default
Option=!?WIN9X|Install the DOS-only component  ; inverted detected default

[Find]                          ; programs already installed: %NAME% = path
CDEX=SHSUCDX.COM SHSUCDX.EXE MSCDEX.EXE
ASPICD=ASPICD.SYS

[Detect]                        ; arbitrary package programs, not built into GDI
TAPE=ASPISCAN.COM /Q /TAPE     ; exit 0 = true, any other result = false
WIN9X=WINCHECK.COM /Q /9X

[Files]                         ; files to copy (from the INSTALL.EXE directory)
AHCIASPI.SYS
ASPITEST.COM
2|EXTRA.SYS                     ; only if option 2 is on
OLDNAME.TXT=NEWNAME.TXT         ; copy under another name
DOS\*                           ; archive: all files below DOS\ to %DIR%
ROOT\*=%BOOT%\                  ; ... to another directory

[Dirs]                          ; directories to create
%BOOT%\TEMP

[Run]                           ; programs to run before / after copying
Before=SYS.COM %BOOT%

[Config.sys]
Remove=AHCIASPI.SYS             ; lines containing this text are removed first
Add=1|DEVICE%O2%=%DIR%\AHCIASPI.SYS
Position=bottom                 ; top or bottom (default: bottom)

[Autoexec.bat]
Add=...                         ; default position: top (after @ECHO OFF)
; append " /D:ASPICD0" to an MSCDEX or SHSUCDX line, else add a new line
Extend=5,?CDEX|MSCDEX,SHSUCDX| /D:ASPICD0|%CDEX% /D:ASPICD0
Replace=yes                     ; write a new file (the old one is kept as backup)

[System.ini]                    ; Windows 3.x
Remove=EUSBVXD.386
Add=2|386Enh|device=%DIR%\EUSBVXD.386   ; section|line
Set=3|boot|shell=%DIR%\CALMIRA.EXE      ; reversible section|key=value

[HexPatch]                     ; file|old bytes|new bytes|exact matches
Patch=%WINDIR%\SYSTEM\WIN386.EXE|66 C7 46 49 FF FF|6A FF 8F 46 49 90|2
Patch=1|%DIR%\PROGRAM.EXE|7503|EB03|1   ; optional condition
; skip only when the target does not exist; reject an unknown existing file
PatchIfPresent=%WINDIR%\SYSTEM\NDIS.386|...|...|1
; group|language/build label|file|old bytes|new bytes|exact matches
PatchVariant=Win311Fat32|English|%WINDIR%\SYSTEM\WIN386.EXE|...|...|2
PatchVariant=Win311Fat32|German|%WINDIR%\SYSTEM\WIN386.EXE|...|...|2

[Text]                          ; optional: translate the texts of the installer
WelcomeTitle=Willkommen
Accept=Akzeptieren
...
```

For one license, the compatible short form `License=EULA.TXT` is sufficient.
For several, `License=1,2,3` selects `License1=`, `License2=`, and `License3=`
in that display order. Every selected license must be accepted before setup
continues. Use `Name|file` to identify the component in the dialog title, or
only `file` for the generic title. Repeated plain `License=` entries are also
accepted. A package may select at most nine licenses (`License1` through
`License9`). License-file paths are relative to `INSTALL.EXE`.

**Conditions:** `1,!2|text` means option 1 on and option 2 off; `?CDEX`
means `[Find]` found CDEX or the `[Detect]` program named CDEX returned zero.
Text without a `|` always applies.

**Detection programs:** `[Detect] NAME=command` is intentionally generic.
The installer starts the command before showing the option list and remembers
whether its exit status was zero. It does not know what the program tests.
`Option=?NAME|...` uses that result as its initial check-box state; the user
can still change the choice. `Option=!?NAME|...` inverts the initial state.
`?NAME` and `!?NAME` are also available in normal conditions. Relative
program names are loaded next to `INSTALL.EXE`; `%BOOT%`, `%WINDIR%`, `%SRC%`
and `[Find]` variables are expanded in the command line.

**SYSTEM.INI values:** `Set=condition|section|key=value` replaces one named
value instead of adding a duplicate line. Setup records the exact previous line
in `%DIR%\SYSTEM.SET`. Uninstall restores it only while the installed value is
still unchanged; a later administrator change is preserved. Duplicate existing
keys and malformed journal data are rejected. This is suitable for reversible
shell selection, for example `Set=3|boot|shell=%DIR%\CALMIRA.EXE`.

**WIN.INI values:** `[Win.ini]` takes the same `Set=condition|section|key=value`
lines as `[System.ini]` and journals them in `%DIR%\WIN.SET` (same format as
`SYSTEM.SET`). In plain DOS the engine edits `WIN.INI` itself; under Windows,
which keeps `WIN.INI` in memory, the Win16 frontend writes the values with
the profile functions. Both sides restore the same journal on `INSTALL /U`.

**Program Manager:** `[ProgMan]` adds items to Windows 3.x groups:

```ini
[ProgMan]
Item=1|Zubehör|Calmira|%DIR%\CALMIRA.EXE
Item=2|Calmira|Hilfe|WINHELP.EXE %DIR%\CALMIRA.HLP|WINHELP.EXE|0
```

The fields are condition, group, title, command line, icon file (default: the
program of the command line) and icon index. A missing group is created. An
item whose title is already in the group is left alone. In plain DOS the
engine writes the `.GRP` files and `PROGMAN.INI` directly (Program Manager
extracts the icons again on its next start); under Windows the frontend uses
DDE with Program Manager. Both record `%DIR%\PROGMAN.LOG` (`G|group` for a
created group, `I|group|title` for an added item), and `INSTALL /U` removes
those items and the created groups once they are empty.

Texts in `INSTALL.INI` are in the DOS code page (850); `[Win.ini]` and
`[ProgMan]` texts are converted to ANSI for Windows.

**Windows-only work from DOS:** registry entries cannot be written from plain
DOS. The engine then stores the plan as `%DIR%\REGISTRY.PLN`, copies itself to
`%DIR%\GDIPOST.EXE` and adds that to `run=` in `WIN.INI`. On the next start of
Windows, `GDIPOST.EXE` applies the plan and removes itself from `run=`.
`INSTALL /U` from DOS works the other way round: it leaves `UNINST.PND`, and
`GDIPOST.EXE` restores `REGISTRY.LOG` and removes the directory.

**Updates:** an installation that can be removed again (`Uninstall=yes`) is
entered in `%BOOT%\GDI.INI` (`[Installed] Id=directory|options|version`;
`[Setup] Id=` defaults to `Product=`, `[Setup] Version=` is shown). When the
package is installed again, Setup (DOS engine and Win16 frontend) finds that
entry and offers an update of that directory with the options chosen then.
An update only replaces the files: `CONFIG.SYS`, `AUTOEXEC.BAT`, `SYSTEM.INI`,
`WIN.INI`, the Program Manager groups, the registry and binary patches stay as
they are, including manual changes. The settings files of the program are not
overwritten either:

```ini
[Keep]
CALMIRA.INI
1|DATA\*.DAT
```

A pattern without a path is a file name, one with a path is below `%DIR%` (or
a full path). Without `[Keep]`, existing `*.INI` and `*.CFG` files are kept.
`INSTALL.LOG` keeps the first backups, so `INSTALL /U` still restores the
original files; it also removes the entry from `GDI.INI`.

**DOS only:** `[Setup] Windows=no` refuses to run under Windows (DOS engine
and Win16 frontend, text `[Text] NeedDos`), for packages that replace files
Windows has open.

**Application language:** a package can provide one single-choice language
prompt (up to eight codes) during the common DOS installation transaction,
also when launched by the Win16 frontend:

```ini
[Languages]
Default=en
Choice=en|English
Choice=de|Deutsch
Choice=fr|Francais
Config=CALMIRA.INI|Language|Selected
```

`Config` names an installed package-relative INI file and the section/key to
write after copying. Existing other settings are retained. `%LANGUAGE%` is
available to commands and expanded values. The package supplies the actual
catalogue files; this feature does not translate the installer interface.

**Binary patches:** `[HexPatch]` accepts hexadecimal byte pairs separated by spaces, commas, hyphens, or nothing. The old and new signatures must have the same length (up to 32 bytes), and the last field is the exact number of expected matches. Setup refuses an unknown or ambiguous file, writes the changed file through a temporary file, and keeps the original as `.G01`, `.G02`, and so on. Re-running Setup recognizes an already patched file. `INSTALL /U` applies the checked reverse transformation. The WIN386 example is the Windows 3.1/3.11 FAT32 compatibility patch; distributors should enable it only for an appropriate DOS/Windows combination.

`PatchIfPresent=` has the same fields and safety checks as `Patch=`, but skips
the operation when the target file is absent. It is intended for optional
installed components such as WfW 3.11 networking. An existing file with an
unknown or ambiguous signature remains a hard error; “optional” never means
“patch an unverified binary.”

The single WIN386 signature was statically verified against 44 Windows 3.1,
Windows 3.11, and WfW 3.11 language/build variants; see
[`docs/win386-fat32.md`](docs/win386-fat32.md). No language selection is shown.
The WfW 3.11 NDIS CPU-speed patch and its 20-language audit are documented in
[`docs/ndis386-cpu-speed.md`](docs/ndis386-cpu-speed.md).

Use repeated `PatchVariant=` entries for language- or build-specific forms of
one logical patch. The first field groups alternatives and the second is a
human-readable label. Setup probes every alternative and proceeds only when
exactly one before/already-patched signature matches, so selection is based on
the binary itself rather than the configured Windows language. Do not replace
the `...` placeholders above until the signatures have been verified against
the exact vendor binaries.

**Variables:**

| Variable | Meaning |
|---|---|
| `%DIR%` | target directory |
| `%BOOT%` | boot drive (`C:`) |
| `%WINDIR%` | Windows directory |
| `%SRC%` | directory of `INSTALL.EXE` |
| `%On%` | the value of option n when it is on |
| `%NAME%` | the path found by `[Find]` NAME |

**Where lines go:**
- `CONFIG.SYS` with a `[menu]` (multi-configuration): lines go into the `[common]` block, which is created if needed.
- `AUTOEXEC.BAT` with position `bottom`: lines go before a line that starts Windows (`WIN`).

**Windows:** The Windows directory is searched for `WIN.COM` in the `PATH` and in `%BOOT%\WINDOWS`. The user confirms it, or leaves it empty to skip `SYSTEM.INI`.

## Native Win16 frontend

`INSTALL.EXE` is an MZ/NE dual-mode executable. Its NE part targets Windows
3.10 and also runs on Windows 95/98/ME. It uses only `USER`, `GDI`, `KERNEL`
and the standard Win16 C runtime. It reads the package title, welcome text and
repeated options from the normal `INSTALL.INI`, identifies the Windows family
with `GetVersion`, and shows up to sixteen component check boxes.

Options whose defaults come from `[Detect]` are shown as three-state
**Automatic** choices. An unchanged automatic choice is passed as `x`, so the
DOS engine still runs the package-defined detector and decides the default.
Explicit on/off choices are passed through `/O:`. The current frontend extracts
the embedded MZ installer to the Windows temporary directory, starts it for
license display, copying and transactional edits, and removes the temporary
file again. Only the combined `INSTALL.EXE` is distributed or copied for
uninstall. An internal `/W:` completion-status file keeps the Win16 task alive
until the DOS transaction has succeeded, failed or been cancelled. On Windows
3.x the native registry transaction then runs, or `/U` restores its journal
before the Win16 frontend removes the retained uninstaller. The INI format
stays shared.

Registry editing is a package-defined native-engine feature, not OpenASPI
special handling. The DOS engine resolves conditions and variables into one
validated native plan. The Windows 3.x `SHELL.DLL` backend applies `HKCR`
default-string changes with a flushed, lossless rollback/uninstall journal.
The Windows 9x backend is still disabled and rejects active registry entries
before file copying. The format and family capabilities are specified in
[`docs/registry.md`](docs/registry.md).

**[Find]** looks first on the installation disk (next to `INSTALL.EXE`): a file found there is installed to `%DIR%` with the other files (only if an active line uses its `%NAME%`) and removed again by `INSTALL /U`. So a distributor can put e.g. `ASPICD.SYS`, `ASPIDISK.SYS` or `MKELS120.SYS` on the disk. Otherwise it searches the computer: `%BOOT%\DOS`, the `PATH`, the Windows directory and its `COMMAND`, `%BOOT%\` and the paths named in `CONFIG.SYS` and `AUTOEXEC.BAT`.

**Texts:** All texts can be replaced in `[Text]`. The keys are the first arguments of `T()` in `src/install.c`, for example `WelcomeTitle`, `LicenseTitle`, `Accept`, `Decline`, `DirTitle`, `DirText`, `OptionsTitle`, `SumTitle`, `Install`, `Cancel`, `DoneTitle`, `DoneText`, `KeysMsg`.

## Archives and disk sets

Files can come from a GDA archive (`[Setup] Archive=NAME`: `NAME.D01`, `NAME.D02`, ... on one or more disks). The installer asks for each disk ("Please insert disk 2 into drive A:", `[Disks]` `2=label` adds a line) and checks that it is the right one. The format is described in `src/archive.c`: deflate (RFC 1951), CRC-32, file dates; one file can continue on the next disk.

`tools/GdiDisk.ps1` (PowerShell 5.1 or 7, also on Linux with `pwsh`) loads `tools/GdiDisk.cs`:

| Class | Use |
|---|---|
| `Gda` | distribute files over GDA archive parts for given disk capacities |
| `Fat12Image` | write 1.44 MB floppy images; with `BootSectors.Win9xFat12` they boot the `IO.SYS` of MS-DOS 7.x |
| `Fat12Reader` | read the files of a floppy image |
| `MsExpand` | expand SZDD and KWAJ files (`COMPRESS`/`EXPAND`, `*.??_`) |

The boot sector is our own (`tools/boot/bs9x.asm`, `make bootsector`). The MS-DOS 7.1 disk set builder that uses all this is the msdosinst project: https://github.com/geosone/msdosinst

## DOS mode

`[Setup] Mode=DOS` (with `Drive=C:`) installs a DOS from a boot disk:
1. no drive C: yet: `Fdisk=` (default `FDISK.EXE`) runs, then the computer restarts and the setup starts again;
2. drive C: not formatted: `Format=` (e.g. `FORMAT.COM %BOOT% /V:MSDOS71`) runs;
3. the normal steps, with `[Run] Before=SYS.COM %BOOT%` and `Replace=yes` for new configuration files;
4. `Extras=yes`: further software from other disks; the installer loads their `INSTALL.INI` and installs them for the new system in the same run (`ExtrasDir=A:\`);
5. a restart.

## Building

Clone recursively so the pinned UPX fork and its dependencies are present:

    git clone --recurse-submodules https://github.com/geosone/gdi.git

For an existing checkout, run `git submodule update --init --recursive` once.
Building requires Open Watcom 2.0 (`WATCOM=/opt/watcom`), GNU make, CMake,
Ninja, and a host C++ compiler:

    make            -> build/INSTALL.EXE (packed DOS MZ + packed Win16 NE)

The build compiles the pinned UPX fork locally, packs `INSTALL.STB` as
`INSTALL.PST`, links that DOS program as the stub of `INSTALL.UNP`, then packs
the Win16 NE portion into the final `INSTALL.EXE`. Intermediate files remain in
`build/` for diagnosis; distribute only `INSTALL.EXE`.

**Tests:** `make test-host` checks the portable binary-patch engine. `test/run.sh` boots DOS in QEMU and runs a test installation with the keys sent through the QEMU monitor, then shows the changed files.

| Command | Test |
|---|---|
| `make test-host` | binary-patch checks, registry plan/type/journal validation and Program Manager group files |
| `test/run.sh` | installation |
| `WINDOWS=1 HEXPATCH=1 INI=test/TESTHEX.INI test/run.sh` | DOS binary-patch integration (add `TARGET=TESTHEX UNINST=1` for reverse) |
| `KEYS="ret ret ret ret ret ret ret ret ret" UNKEYS="r ret ret ret" UNINST=1 test/run.sh` | installation and `INSTALL /U` |
| `MENU=1 WINDOWS=1 INI=test/TESTWIN.INI test/run.sh` | multi-configuration `CONFIG.SYS` and `SYSTEM.INI` |
| `test/win3x.sh` | `[Win.ini]`, `[ProgMan]`, `[Registry]` and replaced files from plain DOS, then Windows 3.11 (`GDIPOST.EXE`), uninstall and Windows again (`WIN311IMG`) |
| `UPDATE=1 test/win3x.sh` | version 2.0 over 1.0 with a changed settings file and WIN.INI value, then uninstall (add `FRONTEND=1 START=25 STEP=4` for the frontend) |
| `FRONTEND=1 START=25 STEP=4 test/win3x.sh` | the same through the Win16 frontend (profile functions, DDE) |
| `PKG=dir TARGET=NAME test/run.sh` | the distribution in `dir` (its `INSTALL.INI`), target `A:\NAME` |

`test/registry` is the Windows 3.x VM fixture. It creates two temporary HKCR
keys, verifies that a flushed `REGISTRY.LOG` is installed, then exercises the
dual-mode `INSTALL.EXE /U`; successful restoration removes the journal,
payload, uninstaller and destination directory.
