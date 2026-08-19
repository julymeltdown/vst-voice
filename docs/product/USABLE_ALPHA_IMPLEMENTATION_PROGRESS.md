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
- Development voicebank acceptance follows the runtime configuration rather than an unconditional catalog default.

Commits:

```text
c92ecc9 feat: connect standalone editing to production rendering
7bd8923 fix: honor development voicebank trust policy
```

## U1 exit-gate evidence

```text
CLAP and Standalone use seam-authoring-runtime       PASS
CLAP Phase 11/12A/12B regression suite              PASS
Standalone visible Note edit changes production hash PASS
CLAP runtime split files remain under 600 lines      PASS
Standalone sine-wave production helper absent       PASS
Named C++ tests                                      170 / 170 PASS
```

### U2 — Complete project lifecycle

- Validated New Project with bounded tempo, sample rate, channels, routing,
  initial exact voicebank, one vocal track, and one 16-bar region.
- Open, Save, and Save As through the canonical Project codec and durable
  atomic persistence.
- Active project and autosave paths remain outside canonical Project JSON.
- Bounded worker-thread autosave with newest-five pruning and recoverable
  metadata.
- Recovery opens a dirty copy while preserving the original explicit path.
- Recent-project storage is canonicalized, de-duplicated, bounded, and durable.
- Unsaved close uses Save, Discard, or Cancel and remains open after a save or
  autosave-flush failure.
- AppKit and Win32 native file-dialog ports, AppKit application menus, and a
  structured Linux/headless backend are integrated through application ports.
- Native close and keyboard/menu commands route through
  `StandaloneApplicationController`.
- A dedicated U2 sanitizer target detected and closed an autosave-worker
  construction race.

Commit: `49cc235 feat: implement usable standalone project lifecycle`

## U2 exit-gate evidence

```text
New / Open / Save / Save As without CLI                  PASS
Recent Projects                                           PASS
Autosave discovery and recovery                           PASS
Unsaved Save / Discard / Cancel                           PASS
Failed pending autosave blocks close                      PASS
Active path excluded from canonical Project JSON          PASS
Named C++ tests                                           192 / 192 PASS
Dedicated U2 tests                                         22 / 22 PASS
ThreadSanitizer U2 tests                                   22 / 22 PASS
```


### U3 — Voicebank browser, installation, selection, relink, and coverage

- Browser cards are derived from catalog candidates rather than filesystem UI code.
- Trusted installed, development, and untrusted candidates have explicit ordering and selectability.
- Voicebank ID, version, language, style, unit inventory, signer/trust, character availability, and synthesis content hash are visible to the application layer.
- Signed `.seambank` installation requires an explicit trusted Ed25519 public key or configured development trust root.
- Signature, package digest, entry hashes, manifest, synthesis content hash, and installation receipt are validated before a candidate is exposed as trusted.
- Existing ID/version replacement requires an explicit Replace decision and different synthesis content.
- Track selection stores exact ID/version/content hash through an Undoable command.
- Relink adds a search root without rewriting the requested identity and succeeds only for the exact candidate.
- Intentional replacement is separate from relink and remains Undoable.
- Coverage inventory and diagnostics distinguish missing, disabled, wrong-style, and out-of-range units.
- Production project rendering continues unaffected phrases and reports failed phrase diagnostics rather than silently substituting another bank.
- The standalone application controller exposes install, browse, select, relink, replace, and selected-region coverage operations.

Commit: `f05e63e feat: implement usable standalone voicebank workflow`

## U3 exit-gate evidence

```text
Signed install through application service                    PASS
Trusted/untrusted/development browser policy                   PASS
Exact selection and Undo                                      PASS
Exact relink without identity rewrite                          PASS
Explicit replacement and Undo                                 PASS
Coverage diagnostics                                           PASS
Unaffected phrase rendering with failed-phrase diagnostics    PASS
No silent voicebank fallback                                  PASS
Named C++ tests                                               205 / 205 PASS
Dedicated U3 CTest                                              3 / 3 PASS
```

## Current product boundary

U1 and U2 are complete, but Project SEAM is **not yet Usable Alpha**. The
standalone now uses the production sample-concatenative renderer and has a
complete application-level project lifecycle. It now includes the end-user voicebank application services and native macOS menu integration, but still lacks complete production playback/status UI, final export, a genuinely usable rights-cleared demo bank, target Apple Silicon `.app` acceptance, and a real-song end-to-end run.

## Next implementation task

The next priority is **U4.1 — bind standalone document changes to production rendering**, followed by render progress/cancellation UI, complete transport behavior, audio-device settings, backing audio, and realtime-safety instrumentation.
