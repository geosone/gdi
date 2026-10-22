# Combined setup

An `INSTALL.INI` with an `[Installer]` section installs several packages,
each with its own `INSTALL.INI`:

```ini
[Installer]
Inst1=VSHARE            ; a directory with INSTALL.INI ...
Inst2=EXTRA\NET.INI     ; ... or an INI file, relative to INSTALL.EXE
Inst3=off|WINFILE       ; not selected at first
Select=no               ; install all, without the list (default: yes)
```

Up to 99 entries (`Inst1` to `Inst99`). Setup shows the packages in a list
(their `Product=`, marked when `GDI.INI` lists them as installed), all
selected unless marked `off|`. Then it asks the licenses and options of
every selected package, and afterwards installs them one after the other
without further questions, each into its `DefaultDir` and with its own
`INSTALL.LOG`, journals and `GDI.INI` entry. The summary at the end names the
packages that failed or were cancelled.

- `INSTALL /U` removes each package separately, from its own directory.
- A license is shown once per run: packages that share a license should give
  it the same name (`License=Name|file`).
- A package with `[Setup] Windows=no` is skipped while Windows runs.
- The registry entries of each package get their own plan and journal; from
  plain DOS, the runner in each package directory applies them at the
  next Windows start (see [Windows](windows.md)).

## Requires= and Conflicts=

A package names the packages it needs and the ones it cannot be used with
in its `[Setup]`, by their Ids (`Id=`, else `Product=`, the keys of
`GDI.INI`), separated by commas:

```ini
[Setup]
Product=Notepad from Windows 95
Requires=W3U-OLE2           ; needs OLE 2
Conflicts=W3U-USERME        ; both replace USER.EXE
```

In a combined setup, after the package list:

- a required package of the list that is not installed is selected as
  well, and installed before the packages that need it; Setup names the
  packages it added (Back returns to the list);
- a package whose required package is neither in the list nor installed is
  left out, with a message;
- packages that conflict with each other or with an installed package send
  Setup back to the list (with `Select=no`: Setup stops).

A single package whose required package is not installed, or that conflicts
with an installed one, asks whether to continue anyway (default: Cancel).

Under Windows the Win16 frontend shows the package list and the messages in
its own windows; when a package needs the DOS engine (`Mode=DOS`,
`[Detect]`, `[Run] Before=`/`After=`), the engine shows them in its DOS
window.
