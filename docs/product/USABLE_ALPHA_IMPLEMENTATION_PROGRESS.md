# Usable Alpha Implementation Progress

This document records implementation progress against
`docs/superpowers/plans/2026-08-18-usable-standalone-product.md`.
It is not a Usable Alpha completion claim.

## Completed milestones

### U0 — Baseline and product contract

- Canonical English and Korean Usable Alpha acceptance contracts.
- Stable requirement IDs `UA-001` through `UA-020`.
- Fail-closed evidence-path and SHA-256 validation.
- Characterization tests for the pre-refactor CLAP authoring behavior.

Commits:

```text
1af7090 docs: define the usable standalone alpha gate
9e7a9a0 test: characterize authoring runtime behavior
```

### U1 — Shared authoring runtime and production Standalone path

#### U1.1 — `ProjectDocument`

- Shared `EditorSession` and `ProjectFactory` ownership.
- Revision-based dirty state.
- Save and recovery identity.
- Undo, Redo, replacement, and ID synchronization.

Commit: `4af04e6 refactor: add shared project document state`

#### U1.2 — `VoicebankSession`

- Exact ID/version/content-hash resolution.
- Canonical search roots and catalog refresh.
- Explicit missing, mismatch, untrusted, and invalid states.
- Undoable track binding without silent fallback.

Commit: `9faed3a refactor: extract shared voicebank session`

#### U1.3 — `AuthoringRenderCoordinator`

- Immutable production render requests.
- Revision cancellation and stale-result rejection.
- Existing content-addressed PCM cache reuse.
- Reader-counted realtime publication.
- Progress, diagnostics, Preview/Final separation, and safe shutdown.

Commit: `4d38442 refactor: extract shared production render coordinator`

#### U1.4 — `TechnicalEditController`

- Shared phoneme, unit, renderer, pitch, and seam edits.
- Undo/Redo and one successful edit to one render request.
- Unit fallback diagnostics kept outside CLAP-specific business logic.

Commit: `247cf23 refactor: extract shared technical edit controller`

#### U1.5 — `TransportController`

- Shared multichannel feeder, ring buffer, play/pause/stop/seek/loop.
- Consumer-owned reset epoch and stale revision rejection.
- Bounded render-replacement crossfade.

Commit: `7017aa8 refactor: add shared authoring transport controller`

#### U1.6 — `AuthoringRuntime`

- One shared facade over document, voicebank, render, technical-edit, and transport state.
- One canonical edit to one project revision and one production render request.
- Unresolved tracks remain explicit and silent in the render copy.

Commit: `c57e1a9 refactor: compose shared authoring runtime`

#### U1.7 — CLAP thin adapter

- The 1,889-line CLAP runtime was split into focused adapter files under 600 lines.
- CLAP keeps host lifecycle, timeline, GUI, event conversion, and state streaming.
- Project, voicebank, render, and technical-edit ownership delegates to `AuthoringRuntime`.
- Explicit shutdown prevents render callbacks from outliving adapter state.

Commit: `6e3be9e refactor: convert CLAP editor to shared authoring adapter`

#### U1.8 — Production Standalone path

- Removed `makeDemoTimeline()`, sine-wave playback, fake `official.voice.01`, and `contentHash = "demo"` from the production standalone target.
- Added `seam-standalone` composition code around the same shared `AuthoringRuntime` used by CLAP.
- Production startup creates an empty `Untitled` project with one vocal track and region.
- The exact discovered voicebank is bound by ID/version/content hash.
- Native Note and lyric edits notify the shared runtime and trigger production rendering.
- Production multichannel PCM feeds `TransportController`, the interleaved ring buffer, and the physical or explicit threaded audio adapter.
- Integration tests prove that moving a visible Note changes the phrase content hash and that transport emits non-zero voicebank PCM.
- A source contract rejects reintroduction of the sine demo and fake voicebank identity.

Commit: `c92ecc9 feat: connect standalone editing to production rendering`

## U1 exit-gate evidence

```text
CLAP and Standalone use seam-authoring-runtime       PASS
CLAP Phase 11/12A/12B regression suite              PASS
Standalone visible Note edit changes production hash PASS
CLAP runtime split files remain under 600 lines      PASS
Standalone sine-wave production helper absent       PASS
Named C++ tests                                      169 / 169 PASS
```

## Current product boundary

U1 is complete, but Project SEAM is **not yet Usable Alpha**. The standalone
now plays the canonical project through the production sample-concatenative
pipeline, but it still lacks the complete New/Open/Save/Recovery lifecycle,
voicebank browser and install UI, final export workflow, a genuinely usable
rights-cleared demo voicebank, and target Apple Silicon acceptance evidence.

## Next implementation task

The next priority is **U2.1 — New Project**, followed by Open, Save, Save As,
autosave recovery, recent projects, and unsaved-close handling.
