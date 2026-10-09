# GeosOne DOS Installer

A small installer for DOS programs and drivers (`INSTALL.EXE`, about 27 KB). It is controlled by an `INSTALL.INI` file next to it and looks like the setup of the Qualitas MAX (386MAX) memory manager: a title bar, cyan dialog windows and a key bar.

It does the following:
- shows a welcome text and a license that must be accepted,
- asks for the target directory and options,
- copies the files,
- adds or removes lines in `CONFIG.SYS` and `AUTOEXEC.BAT`, and in the `SYSTEM.INI` of Windows 3.x, keeping backups,
- removes the installation again (`INSTALL /U`).

License: GNU General Public License version 3.

## Usage

    INSTALL [/U] [file.INI]

`INSTALL.EXE` reads `INSTALL.INI` from its own directory and takes the files from there.

`INSTALL /U` removes an installation:
- Run it from the target directory: the installer copies itself and the INI there (`Uninstall=yes`).
- It removes the `Remove=` lines from the configuration files, then deletes the files and the directory.

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

[Files]                         ; files to copy (from the INSTALL.EXE directory)
AHCIASPI.SYS
ASPITEST.COM
2|EXTRA.SYS                     ; only if option 2 is on
OLDNAME.TXT=NEWNAME.TXT         ; copy under another name

[Config.sys]
Remove=AHCIASPI.SYS             ; lines containing this text are removed first
Add=1|DEVICE%O2%=%DIR%\AHCIASPI.SYS
Position=bottom                 ; top or bottom (default: bottom)

[Autoexec.bat]
Add=...                         ; default position: top (after @ECHO OFF)

[System.ini]                    ; Windows 3.x
Remove=EUSBVXD.386
Add=2|386Enh|device=%DIR%\EUSBVXD.386   ; section|line

[Text]                          ; optional: translate the texts of the installer
WelcomeTitle=Willkommen
Accept=Akzeptieren
...
```

**Conditions:** `1,!2|text` means option 1 on and option 2 off. Text without a `|` always applies.

**Variables:**

| Variable | Meaning |
|---|---|
| `%DIR%` | target directory |
| `%BOOT%` | boot drive (`C:`) |
| `%WINDIR%` | Windows directory |
| `%SRC%` | directory of `INSTALL.EXE` |
| `%On%` | the value of option n when it is on |

**Where lines go:**
- `CONFIG.SYS` with a `[menu]` (multi-configuration): lines go into the `[common]` block, which is created if needed.
- `AUTOEXEC.BAT` with position `bottom`: lines go before a line that starts Windows (`WIN`).

**Windows:** The Windows directory is searched for `WIN.COM` in the `PATH` and in `%BOOT%\WINDOWS`. The user confirms it, or leaves it empty to skip `SYSTEM.INI`.

**Texts:** All texts can be replaced in `[Text]`. The keys are the first arguments of `T()` in `src/install.c`, for example `WelcomeTitle`, `LicenseTitle`, `Accept`, `Decline`, `DirTitle`, `DirText`, `OptionsTitle`, `SumTitle`, `Install`, `Cancel`, `DoneTitle`, `DoneText`, `KeysMsg`.

## Building

Open Watcom 2.0 (`WATCOM=/opt/watcom`), GNU make:

    make            -> build/INSTALL.EXE

**Tests:** `test/run.sh` boots DOS in QEMU and runs a test installation with the keys sent through the QEMU monitor, then shows the changed files.

| Command | Test |
|---|---|
| `test/run.sh` | installation |
| `UNINST=1 test/run.sh` | installation and `INSTALL /U` |
| `MENU=1 WINDOWS=1 INI=test/TESTWIN.INI test/run.sh` | multi-configuration `CONFIG.SYS` and `SYSTEM.INI` |
