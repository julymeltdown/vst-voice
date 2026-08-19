# U2 Project Lifecycle Implementation Report

## Status

`U2 — Complete the Project Lifecycle` is complete on the `master` branch.

This milestone provides the standalone application with a production project
lifecycle: New, Open, Save, Save As, bounded autosave and recovery, recent
projects, an unsaved-close policy, and native file-dialog/application-menu
ports. It is not a claim that Project SEAM has reached Usable Alpha. U3 through
U9 still block the end-to-end voicebank, playback, editing, export, Apple
Silicon, and real-song acceptance journey.

## Goal

Convert persistence capabilities that previously existed only as codecs and
core durability utilities into one coherent application workflow:

```text
Native menu / shortcut / close request
        │
        ▼
StandaloneApplicationController
        │
        ├── ProjectLifecycleService
        ├── AutosaveService
        ├── RecentProjectsStore
        ├── IFileDialog
        ├── IUnsavedChangesPrompt
        └── AuthoringSession
                │
                ▼
          AuthoringRuntime
```

Canonical musical state remains independent from active file paths, autosave
locations, recent-project state, native windows, and platform dialogs.

## Implemented components

### U2.1 — Validated New Project

Added `ProjectLifecycleService::createNew()` and the `NewProjectRequest`
contract.

The first-alpha product bounds are explicit:

```text
Project name      non-empty bounded UTF-8 after trimming
Tempo             20.0–400.0 BPM
Sample rate       44,100 / 48,000 / 96,000 Hz
Output channels   1 / 2 / 4 / 8
Initial content   one vocal track and one empty 16-bar region
Character         Minimal
Routing           canonical master bus and device route
Voicebank         exact ID/version/content hash when supplied
```

Invalid requests fail without mutating the current document.

### U2.2 — Open, Save, and Save As

Implemented:

- canonical project loading through `ProjectJsonCodec`;
- schema validation and ID synchronization;
- exact voicebank-resolution diagnostics after Open;
- durable atomic Save and Save As;
- `.bak` preservation where applicable;
- dirty-state clearing only after successful durable publication;
- current-document preservation when Open or Save fails;
- rejection of unsupported future schema;
- explicit `InvalidState` when Save has no assigned path.

`DocumentIdentity` owns the active project and autosave paths. Those paths are
not serialized into canonical Project JSON.

The standalone editor currently requires at least one vocal region because its
editing controller needs an active region. Audio-only document support remains
a later product decision and is not silently synthesized into a fake region.

### U2.3 — Bounded Autosave and Recovery

Added `AutosaveService` with a dedicated worker thread.

Policy:

```text
Time trigger          60 seconds while dirty
Command trigger       25 successful commands
Minimum command delay 15 seconds
Generations           newest 5 per stable document identity
Storage               platform application-support directory
```

Data flow:

```text
UI thread
→ immutable Project snapshot
→ latest-only pending autosave request
→ worker serialization
→ durable atomic Project write
→ durable metadata write
→ bounded generation pruning
```

Recovery metadata records:

- project ID;
- project revision;
- creation time;
- autosave Project filename;
- original explicit Project path, when one existed.

Recovery loads a copy, preserves the original explicit path, records the
source autosave path, and leaves the document dirty. Corrupt or identity-
mismatched candidates remain visible as non-recoverable diagnostics rather
than mutating the current document.

### U2.4 — Recent Projects and Unsaved Close

Added a durable `RecentProjectsStore`:

- at most 10 entries;
- absolute normalized/canonical paths;
- de-duplication by canonical path;
- last-opened UTC milliseconds;
- display name;
- missing-file state retained after parsing;
- removal only after explicit refresh.

The close policy exposes only:

```text
Save
Discard
Cancel
```

A failed Save or failed pending-autosave flush leaves the application open and
dirty. Discard does not delete autosave evidence during the close decision.

### U2.5 — Native File Dialog and Application Menu Ports

Added shared ports:

```text
IFileDialog
IApplicationMenu
IApplicationCommandDispatcher
IUnsavedChangesPrompt
```

Dialog purposes:

```text
OpenProject
SaveProject
ImportAudio
InstallVoicebank
ExportAudio
```

Platform implementations:

- AppKit: `NSOpenPanel`, `NSSavePanel`, native application menus, recent and
  recovery submenus, and an explicit Save/Cancel/Discard alert;
- Win32: `IFileDialog` created through `CLSID_FileOpenDialog` and
  `CLSID_FileSaveDialog`, plus a native unsaved-changes prompt;
- Linux/headless: structured `Unsupported`, with an explicit injected path
  available for deterministic alpha tests.

macOS menu commands:

```text
Command-N           New
Command-O           Open
Command-S           Save
Command-Shift-S     Save As
Command-E           Export entry point (U6 remains intentionally unsupported)
Command-Q           Quit
Command-Z           Undo
Command-Shift-Z     Redo
Space               Play / Pause
```

All commands route through the application dispatcher rather than directly
mutating a platform window.

## Standalone integration

`StandaloneApplicationController` now owns the application-facing lifecycle.
It coordinates native dialogs, unsaved decisions, project replacement,
recent-project persistence, autosave triggers, recovery discovery, menu-state
refresh, and quit requests.

`AuthoringSession` performs project replacement through the same shared
`AuthoringRuntime` used for production rendering. After New, Open, or Recovery
it:

1. selects the first editable vocal track and region;
2. stops shared transport;
3. reconstructs the native editor controller around the new canonical
   document;
4. resolves exact voicebanks;
5. submits a production render;
6. updates dirty-state presentation.

`NativeEditorApp` installs the native application menu, routes keyboard
shortcuts through the dispatcher, ticks autosave outside the audio callback,
and intercepts native close requests.

## Test-first development record

Representative Red → Green cycles:

### Missing lifecycle service

```text
RED    test_project_lifecycle.cpp could not include project_lifecycle.hpp
GREEN  add bounded New/Open/Save/Save As service
```

### Missing autosave service

```text
RED    autosave_service.hpp did not exist
GREEN  add snapshot worker, metadata, discovery, recovery, and pruning
```

### Missing recent-project and close policy

```text
RED    recent_projects.hpp did not exist
GREEN  add durable bounded store and Save/Discard/Cancel behavior
```

### Missing native application adapter

```text
RED    application_controller.hpp did not exist
GREEN  route menu/dialog/close/recovery through the standalone controller
```

### Close after an autosave failure

A regression test created a pending autosave whose destination was an existing
regular file. Before the fix, `requestClose()` ignored `AutosaveService::flush()`
and returned success.

```text
RED    close succeeded after autosave worker reported an I/O failure
GREEN  propagate the flush error; keep the document open and dirty
```

### Autosave worker construction race

ThreadSanitizer reported the worker reading `stopped_` while the constructor
was still initializing fields declared after `worker_`.

Root cause: `std::jthread` starts its callable during member construction, and
C++ initializes members in declaration order.

```text
RED    TSan data race in AutosaveService::workerLoop()
GREEN  declare worker_ last so all inspected state is initialized first
```

A dedicated `seam_u2_tests` target keeps the lifecycle/concurrency surface
small enough for focused sanitizer execution.

## Verification evidence

### Debug

```text
Warnings-as-errors build            PASS
Named C++ tests                     192 / 192 PASS
Dedicated U2 tests                   22 / 22 PASS
CTest registry                       47 tests; all executed PASS
U2 source contract                   PASS
```

The CTest registry was executed in bounded ranges because the execution
harness intermittently stopped streaming after the monolithic `seam_tests`
process. The exact same `seam_tests` executable was run directly and returned
192/192 PASS; all remaining CTest entries were executed by number and passed.

### Release

```text
Warnings-as-errors release build    PASS
Named C++ tests                     192 / 192 PASS
Dedicated U2 tests                   22 / 22 PASS
Focused lifecycle/authoring CTest     4 / 4 PASS
```

### Sanitizers

```text
ASan + UBSan named tests            192 / 192 PASS
ThreadSanitizer dedicated U2 tests   22 / 22 PASS
```

### Contracts and repository integrity

```text
U2 lifecycle source contract        PASS
Standalone production-path contract PASS
Usable Alpha contract verifier      PASS
Master-only branch policy           PASS
Dependency and license audit        PASS
git diff --check                    PASS
git fsck --full                     PASS
```

## Target-platform validation boundary

The Linux environment compiled and executed the common lifecycle, headless
backend, X11 standalone, production renderer, autosave worker, and all shared
tests.

The AppKit and Win32 file-dialog/menu source contracts are checked from Linux,
but actual target runtime validation is still mandatory:

- macOS main-thread AppKit menu and panel behavior;
- Finder launch/open-file behavior;
- Korean and Japanese paths in native panels;
- Win32 COM apartment and dialog behavior;
- native shutdown requests with dirty documents.

Source presence is not target-runtime PASS.

## U2 exit gate

```text
New / Open / Save / Save As through application controller    PASS
Recent projects through application controller                PASS
Autosave discovery and recovery through application controller PASS
Unsaved close Save / Discard / Cancel                          PASS
Failed save or autosave prevents close                         PASS
Fault-injected durable persistence                              PASS
Active project path absent from canonical Project JSON          PASS
```

## Commit

```text
49cc235 feat: implement usable standalone project lifecycle
```

## Deliberately deferred

U2 does not implement:

- end-user voicebank browser, installation, trust display, coverage, or
  exact relink UI;
- backing-audio import;
- final master/stem export;
- complete track/region editing;
- a genuinely usable rights-cleared demo voicebank;
- target Apple Silicon `.app` acceptance;
- real-song end-to-end evidence.

Those remain U3 through U9 and continue to block the Usable Alpha gate.
