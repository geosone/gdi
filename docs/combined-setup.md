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

Under Windows the Win16 frontend starts the DOS engine, which shows the
package list in its DOS window.
