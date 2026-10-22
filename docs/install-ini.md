# The package file: INSTALL.INI

`INSTALL.EXE` reads `INSTALL.INI` from its own directory (or the INI file named
on the command line) and takes the package files from there. The DOS engine
and the Windows frontend read the same file.

Texts in `INSTALL.INI` are in the DOS code page (850). `[Win.ini]` and
`[ProgMan]` texts are converted to ANSI for Windows, and so is everything the
Win16 frontend shows.

## Example

```ini
[Setup]
Title=AHCIASPI Setup            ; top bar, left
Product=GeosOne AHCIASPI 0.1    ; top bar, right (brand)
Version=0.1                     ; shown when an update is offered
License=1,2,3                   ; numbered licenses, in display order
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
TAPE=ASPISCAN.COM /Q /TAPE      ; exit 0 = true, any other result = false
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
Windows=%DIR%\SETUP.EXE          ; once under Windows after setup

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

[HexPatch]                      ; file|old bytes|new bytes|exact matches
Patch=%WINDIR%\SYSTEM\WIN386.EXE|66 C7 46 49 FF FF|6A FF 8F 46 49 90|2

[Text]                          ; optional: translate the texts of the installer
WelcomeTitle=Willkommen
Accept=Akzeptieren
```

## Sections

| Section | Purpose | Chapter |
|---|---|---|
| `[Setup]` | title, product, licenses, target directory, behaviour | this chapter |
| `[Welcome]` | welcome text | this chapter |
| `[Options]` | check boxes | this chapter |
| `[Find]`, `[Detect]` | installed programs, detection programs | this chapter |
| `[Files]`, `[Dirs]`, `[Run]` | copying, directories, programs | this chapter |
| `[Languages]` | an application language choice | this chapter |
| `[Text]` | translations of the installer texts | this chapter |
| `[Config.sys]`, `[Autoexec.bat]` | DOS configuration | [System files](system-files.md) |
| `[System.ini]`, `[Win.ini]`, `[ProgMan]` | Windows 3.x configuration | [System files](system-files.md) |
| `[HexPatch]` | checked binary patches | [Binary patches](binary-patches.md) |
| `[Registry]` | Windows registry entries | [Registry](registry.md) |
| `[Keep]` | settings files an update keeps | [Uninstall and updates](uninstall-update.md) |
| `[Installer]` | several packages in one setup | [Combined setup](combined-setup.md) |
| `[Disks]`, `Archive=`, `Mode=DOS` | disk sets, installing DOS | [Disks and DOS mode](disks-dos-mode.md) |

## [Setup]

| Key | Meaning |
|---|---|
| `Title=` | top bar, left |
| `Product=` | top bar, right; the name in lists and messages |
| `Id=` | key of the installation in `GDI.INI` (default: `Product=`) |
| `Version=` | shown when an update is offered |
| `License=` | license file(s), see below |
| `Readme=` | text file offered at the end |
| `DefaultDir=` | proposed target directory (`%DIR%`) |
| `Done=` | text of the last dialog |
| `Uninstall=` | `yes` (default): copy `INSTALL.EXE`/`INSTALL.INI` to the target for `INSTALL /U` |
| `Windows=` | `no`: refuse to run under Windows (text `[Text] NeedDos`), for packages that replace files Windows has open |
| `Archive=` | take the files from a GDA archive |
| `Mode=`, `Drive=`, `Fdisk=`, `Format=`, `Extras=`, `ExtrasDir=` | DOS mode |

## Licenses

For one license, the short form `License=EULA.TXT` is sufficient. For several,
`License=1,2,3` selects `License1=`, `License2=` and `License3=` in that
display order. Every selected license must be accepted before setup
continues. Use `Name|file` to name the component in the dialog title, or only
`file` for the generic title. Repeated plain `License=` entries are also
accepted. A package may select at most nine licenses (`License1` through
`License9`). License-file paths are relative to `INSTALL.EXE`.

A license is shown once per run: it is known by its name, one without a name
by its text. Packages of a [combined setup](combined-setup.md) that share a
license should give it the same name.

## Options and conditions

`[Options]` lists up to sixteen check boxes, numbered in their order.
`Option=on|text` and `Option=off|text` set the initial state; a third field
(`Option=off|text|HIGH`) is the value of `%On%` while the option is on.

**Conditions** stand before a `|` in many entries:

| Condition | True when |
|---|---|
| `1,!2` | option 1 is on and option 2 is off |
| `?CDEX` | `[Find]` found CDEX, or the `[Detect]` program CDEX returned 0 |
| `!?CDEX` | the opposite |
| `?%WINDIR%\NET.EXE` | the file exists |

So `?%WINDIR%\NET.EXE|NET.EXE=%WINDIR%\` replaces a file only where it is
installed. Text without a `|` always applies.

**Detection programs:** `[Detect] NAME=command` is intentionally generic. The
installer starts the command before showing the option list and remembers
whether its exit status was zero; it does not know what the program tests.
`Option=?NAME|...` uses that result as its initial state, the user can still
change it; `Option=!?NAME|...` inverts it. Relative program names are loaded
next to `INSTALL.EXE`; `%BOOT%`, `%WINDIR%`, `%SRC%` and `[Find]` variables
are expanded in the command line.

**[Find]** looks first on the installation disk (next to `INSTALL.EXE`): a
file found there is installed to `%DIR%` with the other files (only if an
active line uses its `%NAME%`) and removed again by `INSTALL /U`. So a
distributor can put e.g. `ASPICD.SYS`, `ASPIDISK.SYS` or `MKELS120.SYS` on
the disk. Otherwise it searches the computer: `%BOOT%\DOS`, the `PATH`, the
Windows directory and its `COMMAND`, `%BOOT%\` and the paths named in
`CONFIG.SYS` and `AUTOEXEC.BAT`.

## Files, directories and programs

`[Files]` lists the files to copy, each optionally with a condition:

- `NAME` copies a file from the directory of `INSTALL.EXE` to `%DIR%`;
- `OLD=NEW` copies it under another name, `NAME=%WINDIR%\` to another
  directory;
- `DIR\*` copies all files below `DIR\` of an archive.

A copied file that replaces an existing one outside `%DIR%` is kept as a
backup (see [Uninstall and updates](uninstall-update.md)). `[Dirs]` creates
directories, `[Run] Before=` and `After=` run DOS programs before and after
copying.

`[Run] Windows=condition|program` starts a Windows program once after setup,
for example the own setup of a package (Internet Explorer). Under Windows the
frontend starts it when setup has finished; from plain DOS the runner starts
it at the next start of Windows (see [Windows](windows.md)).

## Variables

| Variable | Meaning |
|---|---|
| `%DIR%` | target directory |
| `%BOOT%` | boot drive (`C:`) |
| `%WINDIR%` | Windows directory |
| `%SRC%` | directory of `INSTALL.EXE` |
| `%On%` | the value of option n when it is on |
| `%NAME%` | the path found by `[Find]` NAME |
| `%LANGUAGE%` | the code chosen in `[Languages]` |

The Windows directory is searched for `WIN.COM` in the `PATH` and in
`%BOOT%\WINDOWS`. The user confirms it, or leaves it empty to skip the
Windows settings.

## Application language

A package can provide one single-choice language prompt (up to eight codes)
during the DOS installation, also when it is started by the Win16 frontend:

```ini
[Languages]
Default=en
Choice=en|English
Choice=de|Deutsch
Choice=fr|Francais
Config=CALMIRA.INI|Language|Selected
```

`Config` names an installed package-relative INI file and the section/key to
write after copying; its other settings are kept. `%LANGUAGE%` is available
to commands and expanded values. The package supplies the actual catalogue
files; this does not translate the installer itself.

## Installer texts

All texts of the installer can be replaced in `[Text]`. The keys are the
first arguments of `T()` and `gdi_text()` in the installer core (`src/*.c`,
the key bar texts in `src/ui_dos.c`) and of `ini_get( "Text", ...)` in
`src/wininstall.c` (Windows), for example `WelcomeTitle`, `LicenseTitle`,
`Accept`, `Decline`, `DirTitle`, `DirText`, `OptionsTitle`, `SumTitle`,
`Install`, `Cancel`, `DoneTitle`, `DoneText`, `KeysMsg`, `NeedDos`.
