# U1 Shared Authoring Runtime Implementation Report

## Status

`U1 — Shared Authoring Runtime` is complete on the `master` branch.

This is an architectural and runtime milestone, not a claim that Project SEAM
has reached Usable Alpha. U2 through U9 remain required for the end-user
project lifecycle, voicebank workflow, export, usable demo bank, target macOS
application, and real-song acceptance.

## Goal

Remove the split where the CLAP editor owned the real project/voicebank/render
path while the standalone executable owned a hard-coded project and a sine-wave
audio timeline.

The completed ownership model is:

```text
Standalone adapter ──────┐
                         ├── AuthoringRuntime
CLAP adapter ─────────────┘

AuthoringRuntime
├── ProjectDocument
├── VoicebankSession
├── TechnicalEditController
├── AuthoringRenderCoordinator
└── TransportController
```

## Implemented components

### U1.1 — `ProjectDocument`

- Shared ownership of `EditorSession` and `ProjectFactory`.
- Revision-derived dirty state.
- Explicit save and recovery identity.
- Command execution, Undo, Redo, project replacement, and ID synchronization.
- File paths remain outside canonical musical state.

Commit: `4af04e6 refactor: add shared project document state`

### U1.2 — `VoicebankSession`

- Canonicalized, deduplicated search roots.
- Catalog refresh and exact candidate inventory.
- Exact ID/version/content-hash resolution.
- Explicit missing, version mismatch, hash missing, hash mismatch, untrusted,
  and invalid-reference states.
- Undoable track binding without silent fallback.
- Runtime-level development-fixture trust policy is now enforced rather than
  merely stored in configuration.

Commits:

```text
9faed3a refactor: extract shared voicebank session
7bd8923 fix: honor development voicebank trust policy
```

### U1.3 — `AuthoringRenderCoordinator`

- Immutable production project render requests.
- Existing `ProductionProjectRenderer` and content-addressed `PcmCache` reuse.
- Revision cancellation and stale-result rejection.
- Preview/Final quality separation.
- Explicit voicebank failure publication as silence plus diagnostics.
- Reader-counted, three-slot realtime publication.
- Safe worker shutdown and callback lifetime management.

Commit: `4d38442 refactor: extract shared production render coordinator`

### U1.4 — `TechnicalEditController`

Shared, undoable technical editing for:

- phoneme timing boundaries;
- unit variants;
- unit renderer selection;
- pitch point add, move, delete, and interpolation;
- seam controls;
- renderer and fallback diagnostics.

A successful edit produces exactly one canonical project revision and one
production render request. Invalid targets do not mutate state or render.

Commit: `247cf23 refactor: extract shared technical edit controller`

### U1.5 — `TransportController`

- Shared multichannel feeder and interleaved SPSC ring buffer.
- Play, pause, stop, seek, and loop.
- Consumer-owned reset epochs.
- Old buffered frames are discarded after seek, loop, or play-state changes.
- Stale project revisions are rejected.
- New render publications replace old audio through a bounded crossfade.
- Paused transport yields silence without queueing a long zero-valued clip.

Commit: `7017aa8 refactor: add shared authoring transport controller`

### U1.6 — `AuthoringRuntime`

- One application-level facade over document, voicebank, render, technical
  edit, and transport state.
- One canonical edit maps to one project revision and one render submission.
- Selection changes do not mutate canonical project state.
- Unresolved tracks remain explicit in the document and are muted only in the
  immutable render copy.
- Completion callbacks publish the newest PCM to the shared transport.
- Character presentation remains outside render and cache identity.

Commit: `c57e1a9 refactor: compose shared authoring runtime`

### U1.7 — CLAP thin adapter

- Replaced duplicate project/voicebank/render ownership with one
  `AuthoringRuntime` instance.
- Retained only host lifecycle, host timeline mapping, event conversion,
  child-window behavior, state streaming, and CLAP presentation concerns.
- Split the former 1,889-line runtime into focused files, all below 600 lines.
- Preserved Phase 11, 12A, and 12B adapter behavior and state contracts.
- Added explicit shutdown so asynchronous render callbacks cannot outlive
  adapter state.

Commit: `6e3be9e refactor: convert CLAP editor to shared authoring adapter`

### U1.8 — Standalone production path

Removed from the production standalone executable:

```text
makeDemoProject()
makeDemoTimeline()
sine-wave playback
fake official.voice.01 identity
contentHash = "demo"
```

Added:

- `seam-standalone` composition layer around the same `AuthoringRuntime` used
  by CLAP;
- empty `Untitled` project bootstrap with one vocal track and one region;
- exact voicebank binding by ID, version, and content hash;
- native edit notifications into the shared runtime;
- production multichannel PCM publication to the shared transport;
- physical system audio with explicit threaded fallback;
- integration proof that moving a visible Note changes the production phrase
  hash and that the audio callback receives non-zero voicebank PCM;
- a source contract that rejects reintroduction of the sine-wave demo path.

Commit: `c92ecc9 feat: connect standalone editing to production rendering`

## Test-first development record

The implementation followed repeated Red → Green → Refactor cycles.
Representative failures observed before implementation included:

- missing shared controller and runtime headers;
- CLAP adapter still owning duplicated render state;
- standalone source-contract failure caused by the sine-wave timeline;
- a revision-zero preview being treated as absent;
- normal-allocator heap corruption caused by worker destruction order;
- development voicebanks resolving even when the runtime policy disabled
  them.

Each failure received a focused regression test before the minimal production
change. The worker-lifetime issue was traced to C++ reverse member destruction
order and fixed through explicit shutdown/join plus safe member ordering.

## Fresh verification evidence

### Debug

```text
Warnings-as-errors build                  PASS
Named C++ tests                           170 / 170 PASS
Full CTest                                45 / 45 PASS
```

### Release

```text
Warnings-as-errors release build          PASS
Named C++ tests                           170 / 170 PASS
Full CTest                                45 / 45 PASS
```

### Sanitizers

```text
ASan + UBSan named tests                  170 / 170 PASS
ASan + UBSan focused authoring/X11 CTest   4 / 4 PASS
ThreadSanitizer named tests               170 / 170 PASS
ThreadSanitizer characterization tests      8 / 8 PASS
ThreadSanitizer render coordinator tests    7 / 7 PASS
```

### Contracts and repository integrity

```text
Usable Alpha contract tests               4 / 4 PASS
Usable Alpha contract verifier            PASS
CLAP shared-authoring adapter contract     PASS
Standalone production-path contract       PASS
Master-only branch policy                  PASS
Dependency and license audit               PASS
git diff --check                           PASS
git fsck --full                            PASS
```

### Runtime smoke

The real X11 standalone window was executed with the development voicebank,
production renderer, shared transport, and threaded audio fallback. It
reported exact voicebank resolution and produced a non-empty native screenshot.

## U1 exit gate

```text
Shared runtime used by Standalone and CLAP              PASS
Technical edits shared                                  PASS
Transport shared                                        PASS
CLAP duplicate business-state ownership removed         PASS
CLAP split source files below 600 lines                  PASS
Standalone sine-wave path removed                       PASS
Visible Note edit changes production render identity    PASS
Production PCM reaches callback path                     PASS
Development voicebank policy enforced                   PASS
```

## Deliberately deferred

U1 does not implement:

- New/Open/Save/Save As and recovery UI;
- recent projects and unsaved-close handling;
- end-user voicebank browser, installation, coverage diagnostics, and relink;
- complete track/region management;
- backing-audio workflow;
- master and stem export;
- a genuinely usable rights-cleared demo voicebank;
- Apple Silicon `.app` acceptance;
- a real-song end-to-end acceptance run.

These remain U2 through U9 and continue to block the Usable Alpha gate.
