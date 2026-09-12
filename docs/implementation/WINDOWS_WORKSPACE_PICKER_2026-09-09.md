# Windows workspace picker and shared input validation

Added the Windows implementation of `chooseProductionWorkspace`. It uses a
filesystem-only existing-folder picker with explicit inventory-digest and
operator-ID edit controls. Cancellation returns no selection. Failures to create,
configure, show or read the dialog return structured errors. COM interfaces and
returned text buffers have scoped cleanup, with the apartment released last.

The native control API was checked against Microsoft's
[IFileDialogCustomize documentation](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifiledialogcustomize).
Its returned edit-box strings are released using CoTaskMemFree as specified by
[GetEditBoxText](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialogcustomize-geteditboxtext).
This documentation check is not a Windows compilation or runtime test.

The Windows and macOS adapters now share input validation: nonempty path,
exactly 64 lowercase hexadecimal digest characters, and a nonempty operator ID
of at most 128 UTF-8 bytes with no control characters. Studio validates the
returned fields again before opening. These are shape checks only; the existing
controller still matches the actual inventory and registered PRODUCER identity.
No identity, authority, source rights or approval is inferred by the dialog.

## Verification boundary

Full macOS Release build passed. File-dialog/U2, aggregate-core and Studio
manifest-draft suites passed in 23.52 seconds. Added tests cover missing fields,
wrong digest lengths/characters/case, ID size limits, newline and embedded NUL.
This was not a new full-suite run.

No Windows runtime or Windows cross-compiler was available in this environment.
The `_WIN32` implementation was therefore not compiled or exercised here; neither
its layout, keyboard behavior nor native cancellation is qualified. Those checks
remain mandatory before claiming Windows workspace-opening parity. Linux remains
explicitly unavailable for this dialog. Full Beta GO remains open.

Logs are retained under `evidence/workspace-dialog-parity-2026-09-09/`. No captured
files were missing relative to `session-preservation-uHwtkM`. Existing dirty work
was preserved with no staging, commit or push; no whole roadmap unit is accepted.
