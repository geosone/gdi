# GeosOne DOS Installer

A small installer for DOS programs and drivers (`INSTALL.EXE`, about 41 KB). It is controlled by an `INSTALL.INI` file next to it and looks like the setup of the Qualitas MAX (386MAX) memory manager: a title bar, cyan dialog windows and a key bar.

It does the following:
- shows a welcome text and a license that must be accepted,
- asks for the target directory and options,
- copies the files,
- adds or removes lines in `CONFIG.SYS` and `AUTOEXEC.BAT`, and in the `SYSTEM.INI` of Windows 3.x, keeping backups,
- removes the installation again (`INSTALL /U`),
- takes the files from compressed archives on several disks (GDA, deflate) and asks for the disks,
- finds programs that are already installed (`[Find]`, e.g. MSCDEX or SHSUCDX),
- in DOS mode: partitions and formats the hard disk (FDISK, FORMAT), installs a DOS and then installs further software from other disks (their `INSTALL.INI`) in the same run.

License: GNU General Public License version 3.

## Usage

    INSTALL [/U] [/B:d:] [file.INI]

`INSTALL.EXE` reads `INSTALL.INI` from its own directory and takes the files from there.
`/B:d:` names the drive of the system whose `CONFIG.SYS` and `AUTOEXEC.BAT` are changed (default: the boot drive).

`INSTALL /U` removes an installation:
- Run it from the target directory: the installer copies itself and the INI there (`Uninstall=yes`).
- It removes the `Remove=` lines and the `Extend=` texts from the configuration files, then deletes the files listed in `INSTALL.LOG` and the directory.

**Backups:** the original configuration files are kept as `CONFIG.G01`, `AUTOEXEC.G01`, `SYSTEM.G01`; further runs write `.G02` and so on.

## INSTALL.INI

```ini
[Setup]
Title=AHCIASPI Setup            ; top bar, left
Product=GeosOne AHCIASPI 0.1    ; top bar, right (brand)
License=LICENSE.TXT             ; shown, must be accepted (optional)
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

[Find]                          ; programs already installed: %NAME% = path
CDEX=SHSUCDX.COM SHSUCDX.EXE MSCDEX.EXE
ASPICD=ASPICD.SYS

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

[Text]                          ; optional: translate the texts of the installer
WelcomeTitle=Willkommen
Accept=Akzeptieren
...
```

**Conditions:** `1,!2|text` means option 1 on and option 2 off; `?CDEX` means `[Find]` found CDEX. Text without a `|` always applies.

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

Open Watcom 2.0 (`WATCOM=/opt/watcom`), GNU make:

    make            -> build/INSTALL.EXE

**Tests:** `test/run.sh` boots DOS in QEMU and runs a test installation with the keys sent through the QEMU monitor, then shows the changed files.

| Command | Test |
|---|---|
| `test/run.sh` | installation |
| `UNINST=1 test/run.sh` | installation and `INSTALL /U` |
| `MENU=1 WINDOWS=1 INI=test/TESTWIN.INI test/run.sh` | multi-configuration `CONFIG.SYS` and `SYSTEM.INI` |
| `PKG=dir TARGET=NAME test/run.sh` | the distribution in `dir` (its `INSTALL.INI`), target `A:\NAME` |
