# Windows

## One file for DOS and Windows

`INSTALL.EXE` is an MZ/NE dual-mode executable. Under DOS its MZ part runs
the text installer. Windows 3.1x and Windows 95/98/ME load the NE part, a
native Win16 frontend written directly against `USER`, `GDI` and `KERNEL`
with the standard Win16 C runtime, without MFC or another UI framework.
Both parts read the same `INSTALL.INI`. Both are packed with UPX.

## The Win16 frontend

The installer core (the same sources as the DOS engine) also runs in the
NE part. Under Windows it shows its steps in windows of its own: the
welcome text, the license, the directories, the options (a list of check
boxes), the language, the summary, a progress window and the end. The
first letter of a button chooses it, as in the text mode. The texts of
`INSTALL.INI` stay in the DOS code page for the files and are shown in the
Windows code page.

The registry, `WIN.INI` and the Program Manager are changed after the
files, as described below, by the frontend itself: the core leaves them in
the same plans as the DOS engine.

### The DOS engine under Windows

What Windows cannot do still goes through the DOS engine: a package (or a
package of `[Installer]`) with `[Setup] Mode=DOS`, `[Detect]` (the exit
status of a program) or `[Run] Before=`/`After=`. For such a package the
frontend reads the package title, welcome text and options from
`INSTALL.INI`, identifies the Windows family with `GetVersion`, and shows up
to sixteen check boxes. Options whose defaults come from `[Detect]` or a
`?` condition are shown as three-state **Automatic** choices; an unchanged
automatic choice is passed as `x`, so the DOS engine still decides the
default. When `GDI.INI` lists an earlier installation, the frontend offers
the update instead (installed and new version).

The frontend then extracts the embedded DOS engine to the Windows temporary
directory and runs it in a DOS window. The engine shows the license, the
target directory, the language choice and, for an `[Installer]` file, the
package list, then copies the files and edits the DOS configuration
transactionally. It hands everything Windows keeps in memory back to the
frontend:

| Switch | Meaning |
|---|---|
| `/O:bits` | the option states of the frontend (`1` on, `0` off, other: the default) |
| `/W:file` | completion status; keeps the Win16 task waiting until the DOS transaction has succeeded, failed or been cancelled |
| `/R:file`, `/F:31` | the resolved registry plan and the Windows family |
| `/T:file` | the resolved `[Win.ini]` and `[ProgMan]` entries and `run=` changes |

When the engine has finished, the frontend applies the registry plan,
writes the `WIN.INI` values with the profile functions and the Program
Manager items through DDE, and deletes the temporary engine. For `/U` it
restores the registry journal first and only then removes the retained
uninstaller. Only the combined `INSTALL.EXE` is distributed or copied for
uninstall.

## Packages for DOS only

`[Setup] Windows=no` refuses to run under Windows (DOS engine and Win16
frontend, text `[Text] NeedDos`). It is meant for packages that replace files
Windows has open, such as drivers in `SYSTEM`.

## Work left for Windows: the runner

Registry entries cannot be written from plain DOS. The engine then stores
the plan as `%DIR%\REGISTRY.PLN`, copies itself to `%DIR%\GDIPxxxx.EXE` (the
runner) and adds that to `run=` in `WIN.INI`. On the next start of Windows,
the runner applies the plan and removes itself from `run=`.

The programs of `[Run] Windows=` go the same way: from plain DOS the engine
writes them to `%DIR%\RUNONCE.PLN`, and the runner starts them once after
the registry work.

`INSTALL /U` from DOS works the other way round: it leaves `UNINST.PND`, and
the runner restores `REGISTRY.LOG` and removes the directory.

Several packages may have a runner in `run=` at the same time. Windows 3.x
starts a program file name only once (a second file of the same name becomes
an instance of the first and gets its path), so `xxxx` is made from the
package directory and every runner has its own name. Runners of older
versions (`GDIPOST.EXE`) are still recognized and removed.
