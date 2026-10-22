# Building and testing

## Building

Clone recursively so the pinned UPX fork and its dependencies are present:

    git clone --recurse-submodules https://github.com/geosone/gdi.git

For an existing checkout, run `git submodule update --init --recursive`
once. Building requires Open Watcom 2.0 (`WATCOM=/opt/watcom`), GNU make,
CMake, Ninja, a host C++ compiler and Python 3 (`tools/mklang.py` makes
the texts of `src/language.ini` into `build/language.h`):

    make            -> build/INSTALL.EXE (packed DOS MZ + packed Win16 NE)
    make docs       -> build/gdi-manual.pdf (pandoc and LuaLaTeX)

The build compiles the pinned UPX fork locally, packs `INSTALL.STB` as
`INSTALL.PST`, links that DOS program as the stub of `INSTALL.UNP`, then
packs the Win16 NE portion into the final `INSTALL.EXE`. Intermediate files
remain in `build/` for diagnosis; distribute only `INSTALL.EXE`.

## Tests

`test/run.sh` boots DOS in QEMU and runs a test installation with the keys
sent through the QEMU monitor, then shows the changed files. The Windows
tests need a hard disk image with Windows 3.1x (`WIN311IMG`); Windows is
ended cleanly by `test/exitwin/EXITWIN.EXE`, because WfW 3.11 writes its
disk cache and `WIN.INI` only then, and `test/regdat.py` compares the
registry (`REG.DAT`) logically.

| Command | Test |
|---|---|
| `make test-host` | binary-patch checks, registry plan/type/journal validation and Program Manager group files |
| `test/run.sh` | installation |
| `WINDOWS=1 HEXPATCH=1 INI=test/TESTHEX.INI test/run.sh` | DOS binary-patch integration (add `TARGET=TESTHEX UNINST=1` for reverse) |
| `KEYS="ret ret ret ret ret ret ret ret ret" UNKEYS="r ret ret ret" UNINST=1 test/run.sh` | installation and `INSTALL /U` |
| `MENU=1 WINDOWS=1 INI=test/TESTWIN.INI test/run.sh` | multi-configuration `CONFIG.SYS` and `SYSTEM.INI` |
| `test/win3x.sh` | `[Win.ini]`, `[ProgMan]`, `[Registry]` (`Set=` and `Import=`) and replaced files from plain DOS, then Windows 3.11 (the runner), uninstall and Windows again; registry and `WIN.INI` must be as before |
| `FRONTEND=1 START=25 STEP=4 test/win3x.sh` | the same through the Win16 frontend (profile functions, DDE) |
| `UPDATE=1 test/win3x.sh` | version 2.0 (new Program Manager item, WIN.INI value and SYSTEM.INI line) over 1.0 with a changed settings file and WIN.INI value, then uninstall (add `FRONTEND=1 START=25 STEP=4` for the frontend) |
| `PKG=test/combined TARGET=PKGA KEYS="ret ret ret ret ret ret ret ret ret ret" test/run.sh` | `[Installer]` with two packages that share a license |
| `PKG=dir TARGET=NAME test/run.sh` | the distribution in `dir` (its `INSTALL.INI`), target `A:\NAME` |

`test/registry` is a Windows 3.x VM fixture. It creates two temporary HKCR
keys, verifies that a flushed `REGISTRY.LOG` is installed, then exercises the
dual-mode `INSTALL.EXE /U`; successful restoration removes the journal,
payload, uninstaller and destination directory.
