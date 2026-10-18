# Registry transaction format

The native GDI engine processes registry changes from the same package
`INSTALL.INI`. Registry support remains package-defined: GDI does
not know about OpenASPI, driver names or product-specific keys.

Package syntax:

```ini
[Registry]
Set=|HKLM|Software\GeosOne\OpenASPI|InstallDir|SZ|%DIR%
Set=cond|HKLM|Software\GeosOne\OpenASPI|Enabled|DWORD|1
DeleteValue=cond|HKLM|Software\GeosOne\OpenASPI|OldValue
DeleteKey=cond|HKLM|Software\GeosOne\OpenASPI\Obsolete
```

The condition is always the first field and may be empty. `@` denotes the
unnamed/default value. Conditions use the existing GDI rules (`1,!2,?NAME`),
and data and paths use the existing variables. Supported roots are `HKCR`,
`HKCU`, `HKLM` and `HKU`. Supported value types are `SZ`, `EXPAND_SZ`, `DWORD`
and `BINARY`; DWORD data may be decimal or `0x` hexadecimal, and binary bytes
may be contiguous or separated by spaces, commas or hyphens. A literal `|`
cannot occur in a field because it is the field separator.

`src/regtxn.c` implements the shared strict parser, family-capability
validation, typed-data conversion and the versioned lossless journal record.
It is portable C89 and is compiled into the Win16 frontend. Host tests cover
valid and invalid plans, Windows-family restrictions, typed values and binary
journal round trips.

Journal records are ASCII with all variable strings and binary data hex
encoded:

```text
V1|V|HKLM|<hex-key>|<hex-name>|1|BINARY|<hex-old-data>
V1|K|HKLM|<hex-key>||0|NONE|
```

`V` preserves a value's exact previous existence, type and bytes. `K`
preserves whether a key existed. Backends must append and flush the record
before each mutation, then replay records in reverse for rollback or
uninstall. `DeleteKey` is permitted only for a key that has no subkeys and
whose values the backend can preserve completely; GDI never recursively
deletes a foreign or shared subtree.

The DOS engine now resolves package conditions and variables and writes a
temporary `GDI-REGISTRY-PLAN 1` file for the native frontend. Unsupported
active entries fail before file copying or configuration edits. Inactive
entries for another Windows family remain valid in a shared package.

The Windows 3.x backend is active. It writes and flushes `REGISTRY.LOG` before
each `SHELL.DLL` mutation and replays only the current additions in reverse if
an operation fails. Missing path components are journalled separately, so
rollback removes child keys before parents. Because the old API cannot delete
one default value independently, `DeleteValue` is accepted only when its key
has no subkeys; the backend then preserves the value and key and recreates
both during rollback.

For `/U`, the DOS engine removes package files and configuration edits but
keeps `INSTALL.EXE`, `INSTALL.INI` and `REGISTRY.LOG`. The native frontend
replays the journal first. Only after successful restoration does it delete
those recovery files and the destination directory, so a failed registry
restore can be retried.

The Windows 9x mutation backend is not active yet. An active `[Registry]`
entry on 9x is rejected by the DOS engine before file copying. Its backend
must use the same plan and journal and do all of the following:

- records whether every key/value existed and saves its exact old type/data;
- writes that record to the installation log before changing the registry;
- rolls back completed changes if any later setup step fails;
- makes uninstall restore old values instead of blindly deleting them;
- never deletes a non-empty key owned by another package.

Registry sections in packages launched through DOS-mode `[Setup] Extras=yes`
are rejected for now. Each independently uninstallable Extras package needs
its own journal transaction rather than sharing or overwriting the parent
package plan.

Windows 3.1 and Windows 9x use separate backends over this common format.
Windows 3.1 exposes the old registration database through `SHELL.DLL`, so its
validator accepts only `HKCR`, the default value, and `SZ` (plus empty-key
deletion). Windows 9x provides the full roots, named values and types through
the Win32 registry API. A package entry unsupported on the running family must
fail validation before setup changes files.
