# Registry transaction design

The native GDI engine will eventually process registry changes from the same
package `INSTALL.INI`. Registry support must remain package-defined: GDI must
not know about OpenASPI, driver names or product-specific keys.

Proposed syntax:

```ini
[Registry]
Set=cond|HKLM|Software\GeosOne\OpenASPI|InstallDir|SZ|%DIR%
Set=cond|HKLM|Software\GeosOne\OpenASPI|Enabled|DWORD|1
DeleteValue=cond|HKLM|Software\GeosOne\OpenASPI|OldValue
DeleteKey=cond|HKLM|Software\GeosOne\OpenASPI\Obsolete
```

Conditions and variables use the existing GDI rules. Supported roots are
planned as `HKCR`, `HKCU`, `HKLM` and `HKU`; value types begin with `SZ`,
`EXPAND_SZ`, `DWORD` and `BINARY`.

This section is currently reserved and is not executed. Activation requires
one transaction implementation that does all of the following:

- records whether every key/value existed and saves its exact old type/data;
- writes that record to the installation log before changing the registry;
- rolls back completed changes if any later setup step fails;
- makes uninstall restore old values instead of blindly deleting them;
- never deletes a non-empty key owned by another package.

Windows 3.1 and Windows 9x need separate backends. Windows 3.1 exposes the old
registration database through `SHELL.DLL`, principally `HKEY_CLASSES_ROOT`
and default string values. Windows 9x provides the full hierarchical registry
and named typed values. A package entry unsupported on the running family must
fail validation before setup changes files.
