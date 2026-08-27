# Usable Alpha Implementation Progress

This document records implementation progress against
`docs/superpowers/plans/2026-08-18-usable-standalone-product.md`.
It is not a Usable Alpha completion claim.

## 2026-08-27 native-editor design remediation

Implementation candidate `af5a1d8f95fad33f03b5ae56ccf8158c7574c6dc`
closes the native-editor design rubric at 100%. It adds bounded Unicode text,
inspectable overlap stacks with stable cycling, persistent adaptive technical
lanes, exact voice/character gating, deterministic reduced-motion transitions,
responsive toolbar containment, human recovery controls, deterministic design
captures, and shared native/CLAP geometry.

The same candidate passed Release 66/66, ASan/UBSan 63/63, TSan 63/63, the
10,000-note 16.7 ms paint budget at 2.19 ms p95, a fresh 64-image visual packet,
an actual isolated AppKit edit/focus journey, and two independent fresh visual
reviews with no blockers. Reproducible details are recorded in
`docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md` and
`.omo/evidence/native-editor-design-completion.md`.

This is a native-editor design-completion claim only. The separate Usable Alpha,
External Beta, RC, and GA acceptance gates remain fail-closed.

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

The rebuilt Apple Silicon standalone bundle was also exercised through the
native File menu: Save As created `seam-live-acceptance.seam`, the Open panel
listed the saved file, and reopening it returned to a `READY` production
editor with the exact Japanese note sequence and development voicebank
identity intact. The screenshot and saved-project hash are retained in
`.omo/evidence/standalone-save-reopen.md`.

After a normal `Cmd-Q` shutdown, a fresh bundle launch reopened the same
project through **Open Recent**. A second native master export had the same
SHA-256 as the pre-shutdown export and passed the same finite/non-silent WAV
inspection. The parity capture and hashes are retained in
`.omo/evidence/native-reopen-parity.md`.

The native recovery surface has also been exercised after a forced `SIGKILL`:
the autosave worker persisted revision 28, **File → Recover Autosave** listed
it, and reopening the candidate returned to `READY / PRODUCTION` with the
exact Japanese notes and bank identity. Hash-bound metadata, autosave content,
and the capture are retained in `.omo/evidence/native-autosave-recovery.md`.

The AppKit restoration path now has one explicit flush for both accepted close
routes. Window-delegate close saves the stable `ProjectSEAM.Editor` frame and
the current `.seam` path, while `NativeEditorApp::requestClose()` performs the
same `INativeWindow::saveRestorationState()` call after an application-level
Cmd-Q request is accepted. The CJK-path AppKit runtime test exercises the
flush and verifies `ProjectSEAM.LastDocumentPath`; the source contract locks
the application quit call site. Release CTest remains **61/61**, with the
release executable at **367/367**; sanitized executable lanes remain
**365/365** under ASan/UBSan and TSan. One sanitized Phase 13A CTest import is
host-Python-specific (`pyexpat`/system-libexpat) and is not a code failure.

### 2026-08-25 progress snapshot

These percentages distinguish repository implementation from the canonical
user-acceptance gate:

| Surface | Progress | Basis |
| --- | ---: | --- |
| Named implementation milestones U0–U7 | **≈88%** | 7 of 8 named U0–U7 milestones are locally implemented; U7 production tooling is partial and its usable bank is external. |
| Local automated regression | **100%** | Release CTest 61/61; release `seam_tests` 389/389; ASan/UBSan and TSan executable lanes 387/387. |
| Canonical Usable Alpha acceptance | **0%** | 0/20 UA rows have repository-relative evidence hashes; the canonical gate remains `BLOCKED`. |
| Practical release readiness | **≈65–70%** | Engineering is substantially complete, but target Apple Silicon/Finder, physical listening, rights-cleared bank, notarization, external-player, cross-platform host, and 30-minute stability evidence remain. |

The last row is a planning estimate, not a replacement for the fail-closed
acceptance JSON.

The shared sample-microscope primitive has now completed another code-owned
design-system pass. Panel, header, close-control, plot-label, waveform,
spectrogram-grid, marker, and pitch-mark geometry are driven by
`EditorSceneLayout`; the semantic close target uses the same bounded helper as
the painter, and the compact-width regression proves the control remains inside
the modal panel. Release CTest remains **61/61**, the release executable is
**389/389**, and the ASan/UBSan and TSan executable lanes are **387/387**.
This improves implementation consistency only; it does not change the
canonical Usable Alpha gate or its external evidence requirements.

The full character dock now uses the same shared layout contract. Its minimum
timeline width, portrait bounds, metadata offsets, typography, scale, divider,
and portrait border are named `EditorSceneLayout` tokens; paint, semantics, and
controller panel hit-testing share the minimum timeline-width token, while
`characterDockPortraitBounds` and `characterDockMetadataTop` centralize the
portrait/metadata geometry. A focused regression covers the 1280×720 dock
frame, and a fresh capture was visually inspected. This closes another
code-owned design-system inconsistency without changing the external gate.

Technical-lane geometry is now tokenized across the complete interaction
surface. Shared content insets, unit-card minimums, divider/item strokes,
compact reserves, automation center/range/vertical scale, curve strokes, and
point size are consumed by the native painter, native controller hit testing,
embedded CLAP model rebuild, CLAP painter, and CLAP pointer mapping. The
regression covers normal and compact lane frames, while the fresh full-dock
capture shows the resulting lane hierarchy without clipping. Release CTest
remains **61/61**, release tests are **389/389**, and ASan/UBSan plus TSan are
**387/387**.

Piano-roll grid, keyboard, and note-label metrics now also use the shared
layout contract. Bar cadence, strong/weak grid strokes, ruler and keyboard
label offsets/type, note-label threshold/insets/width, and note outline width
are named tokens; `noteLabelBounds` is shared by the painter and its regression
coverage. A fresh 1280×720 full-dock capture was visually inspected after the
change. This removes another scene-level literal cluster without changing the
external acceptance gate.

Arrangement and dock-panel controls now share the same layout metrics across
painting, semantics, and pointer hit-testing. Action-button bounds are derived
by `arrangementActionBoundsForWidth`; panel instruction baselines, secondary
instruction type, action-label type, and audio-row text metrics are named
tokens. The fresh CJK arrangement/inspector capture remains contained and
readable at 1280×720. Current release CTest is **61/61**, release tests are
**389/389**, and ASan/UBSan plus TSan are **387/387**.

Voicebank-browser and audio-settings panel text now use shared secondary-text,
row, diagnostic-reserve, and card-height metrics. The semantic audio-diagnostic
node uses the same stats bounds as the painter, and a focused regression covers
the bounded panel metrics. The fresh CJK arrangement/inspector capture remains
contained after the change.

Diagnostic, export, status, lyric-editor, selection, playhead, and focus-ring
metrics now have named layout ownership as well. The compact-strip regression
and a fresh CJK arrangement/inspector capture remain clean, with no new
clipping or overlap. Current release CTest is **61/61**, release tests are
**389/389**, and ASan/UBSan plus TSan are **387/387**.

Voicebank Studio marker-label sizing now uses UTF-8 display columns rather than
raw byte length, so Japanese/CJK labels receive the correct horizontal reserve
without artificial over-width. The new Unicode regression passes, and a fresh
1200x800 Voicebank Studio raster capture was visually inspected. This is a
code-owned U7 tooling improvement; performer consent, a production-ready
rights-cleared bank, and cross-machine audio evidence remain external gates.

Render completion now projects production-audio status during the UI paint pass;
the renderer worker only requests repaint and no longer mutates the
UI-owned controller. The regression covers a completed production render and
the release, ASan/UBSan, and TSan lanes pass at **389/389**, **387/387**, and
**387/387** respectively. This removes a teardown/paint race without changing
the external acceptance gate.

The embedded CLAP sample microscope now retains and reports its focused
semantic element while the modal surface is open. `SetFocus` dispatch validates
the modal node and `accessibilityFocusedNode()` returns the focused close,
waveform, or panel element instead of dropping focus on the modal boundary.
The Phase 11 runtime regression covers the native close-control focus path;
this closes a code-owned accessibility parity gap while leaving physical
VoiceOver/Narrator certification as an external gate.

CLAP project replacement now also closes and clears any active microscope,
including its decoded audio, focused modal target, selected unit, and in-flight
drag keys. The Phase 11 replacement journey verifies that a new project cannot
inherit inspection state from the previous document. Closing the microscope
itself now releases its decoded audio buffer as well, preventing inspection
memory from persisting across a long authoring session.

Autosave discovery and recovery now reject symlinked metadata and payload
files, and recovery rechecks the payload at use time to prevent a
discovery-to-recovery path swap from escaping the autosave root. The regression
covers a symlinked payload and preserves fail-closed recovery behavior.

Voicebank manifest load and save now reject symlinked and non-regular manifest
targets, including a symlinked `.bak` path, before reading or staging bytes.
The regression preserves the outside target and keeps the manifest boundary
consistent with autosave and export fail-closed behavior.

The WAV reader now rejects non-finite IEEE Float32 payloads instead of silently
turning corrupt samples into zero. A byte-level regression confirms malformed
Float32 input cannot enter voicebank inspection or production data paths.

Direct WAV path reads and streaming-writer destinations now also reject
symlinked and non-regular targets before following or truncating them. The
regression preserves the outside WAV and keeps media I/O aligned with the
manifest, autosave, and export path boundaries.

The shared SHA-256 file primitive now rejects symbolic links before hashing,
so package, export, support, and evidence callers cannot accidentally bind an
outside target through a redirected path. A regression covers symlink and
directory targets while retaining normal regular-file hashing.

Project JSON load/save now applies the same symlink and regular-file boundary,
including the durable `.bak` path used by Save and Save As. The lifecycle
regression preserves the outside project and keeps open, save, and recovery
from following redirected document paths.

The shared durable-atomic-write primitive now rejects symlink and non-regular
targets before backup reads or replacement. Settings, recent-project, update,
crash-marker, cache, and codec persistence therefore inherit one fail-closed
write boundary instead of relying on caller-specific checks.

The bounded file-read primitive now also rejects symlink and non-regular input
targets before allocation or decoding. This keeps persistence, support,
update, and attachment readers from following redirected paths when a caller
has not added a narrower domain check.

The voicebank layer now exposes deterministic dry-take inspection for the U7
production workflow. It records source format, finite/clipping/silence/DC
quality, analyzed root MIDI, and an explicit accepted verdict; invalid format
or acoustic quality remains reviewable rather than being silently repaired.

Voicebank Studio now retains that inspection result on its controller, and its
recording path exports PCM24 source takes before inspection. The existing
interactive marker/pitch workflow can therefore surface an accepted or review
state without mutating the raw capture or hiding a format mismatch.

Studio capture destinations are now numbered immutable take files rather than
a fixed per-unit filename. A pre-existing destination is rejected before
export, preserving prior raw takes and their inspection provenance across
retake attempts; a timed native run produced the first `take-0001.wav` file
with a 24-bit header and no input-read failures.

Each Studio take now writes a non-overwritable inspection sidecar containing
the take filename, SHA-256, source format, expected/analyzed pitch, quality
flags, and `ACCEPTED` or `REVIEW` status. A timed synthetic run emitted both
the PCM24 take and a hash-bound `REVIEW` record, preserving the silence finding
instead of claiming a usable source recording. Inspection hashes the take
before and after analysis, and sidecar persistence rejects a subsequent byte
change, so sidecar quality measurements cannot be attached to replacement
audio.

Export-set replacement now rejects symbolic-link destinations and preserved
entries before moving the prior set. This prevents a user-owned symlink from
being copied through the replacement path or silently dropped; the export
regression preserves the original set and outside target. Recovery also rejects
symlinked export directories and `master.wav` entries before hashing a
recovered set, so publication and recovery both fail closed at the export
boundary.


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

The rebuilt standalone also exercised the missing-bank recovery surface on
AppKit: an exact development bank was temporarily moved, `BANK_MISSING` and
`RELINK_VOICEBANK` appeared, and selecting the moved root cleared the
diagnostic and resubmitted production rendering to `READY / PRODUCTION`. The
runtime fix and capture are recorded in `.omo/evidence/native-voicebank-relink.md`.

## Current product boundary

U1 through U6 are locally implemented and regression-tested. U7
repository-owned recording and inventory tooling is implemented, but a
genuinely usable rights-cleared singing bank remains an external content gate.
Project SEAM is therefore still **not yet Usable Alpha**: the remaining
mandatory proof requires a target Apple Silicon `.app`, physical audio,
exact-bank reopen/recovery/export listening, and a real-song stability session
performed through the UI.

### U4 — production rendering, playback, settings, and backing audio

- Canonical edits invalidate the shared production renderer with debounce,
  stale-result rejection, failed-render preservation, and explicit stale-audio
  status/retry/cancel controls. Render admission is monotonic as well: a
  lower revision cannot cancel or replace a newer request.
- Changing Preview versus Final quality submits a fresh render at the current
  document revision, so the published audio and cache identity match the
  selected quality without creating a canonical document edit.
- Every accepted render request also carries a monotonic generation token;
  same-revision replacements and explicit cancellation cannot publish an older
  result after a newer request is admitted.
- Render status carries requested versus audible quality as well as revisions,
  so audible Preview audio is marked stale while a same-revision Final render
  is in flight.
- Shared transport covers play, pause, stop, seek, loop, timeline replacement,
  sample-rate remapping, and controlled audio-device restart.
- Transport availability is derived from a non-empty published timeline rather
  than revision numbering, so the initial revision-zero render is playable and
  can become stale when a newer revision is queued.
- Audio settings stay outside project JSON; backing media supports reference and
  project-copy identity, trim, resampling, routing, and missing-media diagnostics.
- A ready vocal render remains playable when backing media is unavailable, and
  the render status carries the renderer's actionable media diagnostic.
- Backing media can be exact-relinked from the native File menu or the
  `MEDIA_MISSING` recovery action; the content hash must match before the
  undoable path replacement is committed.
- The native `I` audio-settings dock lists the live device catalog and exposes
  sample rate, block size, channel count, underflow, and xrun state. Changes
  route through the existing transactional audio restart/persistence path, and
  `AUDIO_UNAVAILABLE` opens the same surface instead of failing closed.
- Render preflight failures now become thread-safe actionable diagnostics;
  missing-bank startup retains a recoverable track selection and offers Install,
  Relink, Choose, and support actions. The macOS release target emits a 13.0+
  arm64 `Project SEAM.app` and keeps audio/export work active through App Nap.
- Development-mode command-line fixture roots are classified as development
  candidates and bind automatically only outside release mode. A successful
  provisional fixture bind clears the startup missing-bank diagnostic; release
  mode retains exact-selection/no-fallback behavior.
- The release app now receives the executable-resolved `Resources/Manual` root,
  so bundled EULA validation and the documentation menu work from the actual
  `.app` rather than a test-style user-data path.
- The realtime probe covers 100,000 callback blocks across the declared block and
  channel matrix with zero allocation/deallocation, lock, file-I/O, logging,
  non-finite, or unexpected-underflow events.
- Standalone smoke output now records render state, requested/published
  revisions, transport availability, and render diagnostics for target-runtime
  evidence and later physical soak records.
- Native audio startup now opens the negotiated device without running its
  callback, waits for a rendered timeline to reach the transport watermark, and
  only then starts physical playback. Paused launches therefore report zero
  callbacks and zero underflow frames; the exact packaged AppKit saved-project
  playback and a five-second natural-completion soak both reported zero
  underflow, zero xruns, and zero write failures.
- The mono and multichannel callback paths now report reset-epoch zero-fill
  separately from genuine ring starvation. Seek, loop, and timeline
  replacement can therefore preserve intentional silence without inflating the
  user-visible underflow counter; the standalone probe exposes both counters.
- A fresh AppKit physical Play/Pause probe reported 158 CoreAudio callbacks,
  80,384 delivered frames, zero underflow, zero xruns, and zero write failures;
  512 intentional reset frames were accounted separately. The clean-shutdown
  record is retained in `.omo/evidence/native-transport-physical.md`.

### U5 — complete native editing surface

The native controller now covers track/region arrangement, note and lyric
editing, batch lyric distribution, phoneme/unit/pitch/seam controls, microscope inspection and playback,
IME rename, keyboard and pointer actions, accessibility dispatch, transient A/B
seam preview, responsive minimum-width transport controls, live character-dock
semantic bounds, and a card-based exact voicebank browser with pointer and
accessibility selection, live audio settings with transactional restart, plus
an explicit AppKit exact-relink-folder command. Current native verification is
369 passing tests. Loop transport is now exposed as an optional transactional
native control when the host provides loop ownership. AppKit startup open-file delivery now replaces only the
provisional Untitled document without presenting an unsaved modal before the
window event loop begins.

The AppKit accessibility bridge now exposes this same semantic tree through
stable native elements with screen-space bounds, correct parent links, lazy
virtual note pages, and action forwarding that matches the shared dispatcher.
Transport semantics are split into a toolbar container and independently
bounded Play/Pause, Stop, and tempo elements using the same layout tokens as
painting and hit testing. Source, native-suite, full-CTest, and deterministic
AppKit smoke checks are green; physical VoiceOver/Accessibility Inspector
observation remains an external gate.

The standalone path now also has a compiled runtime contract test that opens a
real AppKit window, traverses Japanese identifiers and lazy note pages, checks
screen-space frames and parent links, and executes the modern AppKit
`accessibilityPerformPress` and `accessibilityPerformConfirm` methods through
the shared dispatcher. The evidence is recorded in
`.omo/evidence/appkit-accessibility-runtime.md`; it does not claim physical
assistive-technology certification.

The embedded CLAP AppKit view now exposes that same semantic tree instead of
being an accessibility-silent custom view. Runtime snapshots are lock-safe,
virtual note pages remain lazy and stable, and Press/focus actions dispatch
through `NativeEditorController`; the new evidence is recorded in
`.omo/evidence/embedded-clap-accessibility.md`.
Editable note values now also support the modern AppKit writable-value setter
and deprecated compatibility selector, forwarding through the lock-safe
`EditorRuntime` setter to the same bounded UTF-8 lyric command as Standalone.
Embedded repaint callbacks now invalidate the Objective-C accessibility
snapshot through an atomic dirty flag, keeping render-thread updates from
racing AX traversal or leaving stale lazy-note pages visible.

The Windows embedded CLAP child now routes `WM_GETOBJECT` and UIA invalidation
through the shared `Win32AccessibilityBridge` and the same runtime dispatcher;
bounded note pages now expand lazily during UIA navigation, matching AppKit;
logical client bounds are converted to physical screen coordinates for UIA
rectangles and hit testing; Windows compilation and Narrator/UIA traversal
remain target-runner gates.

Windows UIA runtime IDs are now derived from the stable semantic element ID
instead of the mutable flattened snapshot index, so refreshes and lazy note-page
expansion do not present an existing control as a new automation element. UIA
focus discovery also expands the relevant lazy note page before returning a
focused offscreen note. The actual Windows/Narrator traversal gate remains
target-runner evidence.

The Win32 bridge now refreshes the semantic snapshot after a successful UIA
SetFocus and raises `UIA_AutomationFocusChangedEventId` for the focused control,
including a focused note materialized from its lazy page. Layout invalidation
also publishes the same focus transition, so keyboard-driven Tab changes are
observable by UIA clients instead of requiring polling.

Win32 UIA now also exposes Toggle and SelectionItem providers for the same
semantic controls that AppKit exposes: transport and track-mix toggles report
their current state, while notes, arrangement rows, voicebank cards, and audio
devices can be selected through the shared activation dispatcher. Unsupported
additive and removal operations remain explicit UIA invalid operations rather
than silently mutating the wrong single-selection state.

SelectionItem parents now advertise UIA SelectionPattern as well. Arrangement,
voicebank, audio-device, note-page, and root containers return their currently
selected direct children, including lazily expanded offscreen note pages, and
truthfully report a single-selection/non-required contract.

Win32 invalidation now compares stable semantic IDs against the last announced
snapshot and raises one UIA property-change event for each changed selection,
editable/value, toggle-state, or enabled property. Refreshes caused by provider
queries do not consume the announcement baseline, so a later repaint still
reports the real transition exactly once.

Editable note semantics now carry the current lyric separately from the MIDI
value. The standalone and CLAP clients expose a bounded UTF-8 value setter, and
the Win32 bridge maps UIA ValuePattern `SetValue` through the same lyric
composition/undo path used by native IME input. Invalid UTF-16 and oversized
payloads fail before reaching the project model.

The standalone and embedded AppKit adapters now report that same editable lyric
as the note accessibility value while retaining numeric MIDI values for
non-editable controls. VoiceOver clients therefore see the text they can edit,
not an unrelated pitch number.

The AppKit bridge also implements the modern writable `accessibilityValue`
property setter, in addition to the deprecated compatibility selector. A live
`AXUIElementSetAttributeValue` probe changed a Japanese fixture note from `こ`
to `あ`, and a fresh semantic snapshot reported the committed value after
repaint. The bridge, controller, full CTest, ASan/UBSan, and TSan regressions
are green; the live capture and exact AX probe output are retained in
`.omo/evidence/native-appkit-ax-lyric-edit.md`. This remains runtime
accessibility evidence, not VoiceOver certification.

The standalone AppKit runtime regression also now drives the native
`NSTextInputClient` sequence for Japanese composition: marked `き`, full
committed `きゃ` insertion over the marked range, and Return commit all reach
the shared text client. This closes the code-owned IME forwarding gap while
leaving candidate-window and external VoiceOver certification as target gates.
The same test opens at configured 2× logical scale and verifies a nonzero
screen-space `firstRectForCharacterRange:` candidate rectangle with the expected
UTF-16 selection range, covering the code-owned Retina geometry path.

AppKit windows now use the stable `ProjectSEAM.Editor` frame-autosave name and
restore the saved frame before presentation, preserving geometry without
serializing project content. Cross-process document-path restoration remains a
separate lifecycle gate. The native runtime test now also restores a temporary
canonical `.seam` path from `ProjectSEAM.LastDocumentPath`; explicit startup
projects disable restoration so user-supplied paths retain precedence.

Technical lane semantics now expose phoneme token/warning counts, unit and seam
override counts, selected-boundary state, and pitch automation point counts with
descriptions, so assistive-technology clients can understand current technical
state without relying on color.

The scene and embedded CLAP runtime now share one scaled
`EditorSceneLayout::TechnicalLaneGeometry` for painter placement, semantic
bounds, phoneme/unit model rebuilds, and pitch/phoneme/unit hit testing. The
compact 480×320 Phase 11 regression drags a pitch point through the scaled
geometry and verifies the edit, while lane and overlay tests assert that the
derived frames remain inside their declared surfaces. Runtime and Phase 12B
overlay tops are anchored below the toolbar and within the ruler band instead
of relying on independent absolute positions. This closes the code-owned
compact-layout divergence; target packet regeneration and external host review
remain separate gates.

The same overlay layout now clamps runtime and Phase 12B panels to the logical
host width, clips the runtime meter to its panel, and scales Phase 12B hit
regions with the bounded panel. Regression coverage exercises 320, 480, 800,
and 1100 logical widths; release CTest remains **61/61**, with release
`seam_tests` at **369/369** and sanitizer executable lanes at **367/367**.

Technical-lane typography is now height-aware. At compact heights, helper
instructions are omitted, unit cards reduce their bottom reserve and only draw
renderer sublabels when they fit, seam bars remain inside their rails, and
empty-pitch guidance is clamped to the lane. Normal-height layout retains the
existing detail density; compact reserve transitions are covered by native UI
regressions and the same release/sanitizer lanes.

The CLAP sample microscope now uses the shared `EditorScenePainter` primitive
and shared layout-derived waveform/spectrogram bounds instead of maintaining a
second renderer. A compact Phase 11 runtime test paints the open microscope
and verifies exact bound parity; the Phase 12B source contract now protects the
shared path. Runtime packet provenance and independent host review remain
external evidence gates.

The embedded CLAP accessibility snapshot now follows that microscope state as
well: opening the microscope exposes only its modal panel and descendants,
removes background note pages, and routes `microscope.close` through the same
runtime lifecycle. The compact Phase 11 regression verifies open, semantic
surface, close, and return-to-editor behavior.

While the CLAP microscope is open, `EditorRuntime::paint` now stops after the
shared modal scene, preventing runtime overlays from being composited over the
microscope. The Phase 11 regression compares the open and post-close header
pixels to verify the modal paint boundary, not just the semantic tree.

The native piano-roll surface now exposes the full pitch-automation edit loop:
click adds a point, dragging moves it with snap and region bounds, double-click
cycles Step/Linear/Smooth interpolation, and Shift-click removes it. Unit
renderer cycling, batch lyric distribution through the native IME field, and
track output-bus cycling are also available through the same controller,
pointer, keyboard, and semantic-dispatch paths.

New notes created by the native Japanese authoring path now default to Hiragana
`あ` instead of Latin `a`. This matters for a fresh project: the bundled
Japanese voicebank can render the first note immediately, so the editor reaches
`READY / PRODUCTION` rather than failing on an unsupported phoneme sequence.
The live regression capture and hash are recorded in
`.omo/evidence/native-default-hiragana-render.md`.

A live AppKit fixture journey also created a pitch point, cycled a unit variant,
edited a seam parameter, and dragged a phoneme boundary. Each edit advanced the
document and returned to production-ready audio; the final revision and
capture are recorded in `.omo/evidence/native-technical-editing.md`. That live
journey also caught a hidden error when a unit lane click landed on the
non-start phoneme of a multi-phone unit; unit dispatch now canonicalizes the
covered token to its unit start, and a clean post-fix AppKit run exits without
`last_editor_error`. A full post-fix composite run completed at revision `r6`
with pitch, unit, seam, and phoneme edits and the same clean shutdown result.

Native Play/Pause state is now transactional with the host transport callback:
when the audio device or transport rejects a request, the controller preserves
the previous playing state and returns the actionable failure instead of
displaying a stale playing indicator.

Loop transport now follows the same contract. Shift-click ruler bounds, the
pointer LOOP button, and the L/semantic LOOP control propagate host failures,
preserve the previous loop state, and expose the current enabled state to the
native scene and semantic tree when a loop host is connected.

Ruler seeking now also returns host transport failures instead of silently
advancing the seek interaction after a rejected position update.

Diagnostic recovery now also exposes Save As, recovery-folder opening, and
privacy-safe copy-to-clipboard actions. The Standalone AppKit/Win32 adapters
route these actions through native path and clipboard services, while the
headless platform returns explicit Unsupported results.

Each diagnostic action is also materialized as its own semantic button, so
assistive-technology clients can activate Copy, Retry, Save As, or recovery
actions individually instead of being forced through only the first action.

Editable notes also advertise text-field/edit-control roles on AppKit and UIA;
non-editable notes retain their generic musical-note role. This keeps role,
value, and edit action semantics aligned instead of exposing a text editor as a
generic group.

Semantic descriptions are now carried by the shared tree: notes explain MIDI,
duration, and the lyric-edit command, while the root explains keyboard focus
navigation. AppKit exposes these as accessibility help text and Win32 exposes
the corresponding UIA Description property.

AppKit accessibility announcements are now coalesced with repaint requests:
standalone and embedded views emit one value-change notification for a pending
state update and do not emit focused-element notifications from every draw.
Focus notifications remain tied to actual semantic SetFocus dispatches.

The AppKit presentation boundary now mirrors the bottom-left `CGImage` once
into the flipped native view. The real window therefore preserves the same
top-left toolbar, piano-roll, technical-lane, and status ordering as the
software-raster and accessibility coordinate systems; a live rendered-project
capture caught and verifies the previous vertical inversion.

The AppKit accessibility bridge now implements the protocol's
`isAccessibilityEnabled` getter on the native view, semantic elements, and
lazy note pages. Ready transport controls and editable notes therefore remain
enabled to VoiceOver/UI clients, and a live Play activation transitions to
Pause/Playing through the same dispatcher. The embedded CLAP AppKit bridge
uses the same protocol getter for its parent view and virtual note pages.

Semantic focus now also reaches the raster scene: the controller publishes the
focused element bounds and the painter draws a high-contrast rectangular ring
around the focused control. This preserves visible keyboard focus without
depending on selection color or platform accessibility overlays.

When the Sample Microscope is open, the shared semantic tree is now modal: it
exposes a panel, waveform, spectrogram, acoustic-marker and pitch-mark
landmarks, plus close and (when connected) test-play actions, while removing
background editor controls from the accessibility surface. This keeps the
read-only inspection workflow navigable without allowing assistive-technology
clients to activate controls hidden behind the overlay.

### U6 — production export

Final-quality master and stem export writes atomic, deterministic WAV outputs
and receipts binding project, build, render ABI, exact voicebank identity,
format, frame count, and output hashes. The required PCM16/PCM24/Float32 ×
44.1/48/96 kHz × 1/2/4/8-channel matrix is covered locally. The native export
strip and semantic tree now retain the committed set and receipt paths after
completion so the result is visible and auditable from the editor surface. The
single-file master export command now publishes the same committed result and
receipt state rather than bypassing the native export surface. The voicebank
CLI also exposes `inspect-wav` for fail-closed duration/channel/finiteness/
non-silence verification to pair with the required external listening check.

Export-set publication now fails closed when a user-owned file from the
previous set cannot be restored into the replacement tree. The implementation
rolls the replacement back to the previous set instead of silently continuing
and reporting `Committed`; the regression covers a preserved path that
collides with the replacement receipt file. Release, full CTest, ASan/UBSan,
and TSan verification remain green.

Single-file publication now has the same rollback guarantee: if receipt
rotation or publication fails after the new master has been moved into place,
the previous master and receipt are restored together. A collision regression
proves that a failed replacement cannot leave mixed-generation output.

Standalone export failures now retain the underlying error and context in the
export progress surface. The native export strip and semantic `export.progress`
node expose that diagnostic instead of showing only `Failed`, so a destination
collision or publication error remains actionable after the operation returns.

Active export progress now exposes a shared native `CANCEL` affordance and an
`export.cancel` accessibility button wired to the standalone export worker.
Cancellation is offered through staging/preparation only; once atomic
publication begins, the control is withdrawn so the UI cannot imply that a
non-interruptible commit was cancelled.

Diagnostic strips and semantic diagnostic nodes now include the registered
message key alongside the severity/code. Recovery actions and stable IDs are
unchanged, but users and assistive-technology clients no longer receive only
an opaque `MEDIA_MISSING`/`RENDER_FAILED` label when the runtime has more
specific context.

The rebuilt standalone bundle has now completed a native File → Export Audio
journey for the Phase 2 fixture. The semantic export surface reported
`committed 1/1`, and the external `inspect-wav` check confirmed finite,
non-silent stereo 48 kHz audio with a matching receipt. The exact artifact
hashes and capture are recorded in `.omo/evidence/native-master-export.md`.

The native Export Set path now uses a save-style destination picker so its
atomic non-existent-directory contract is reachable from the UI. A live
AppKit journey committed a master plus vocal stem (`2/2`), and
`inspect-wav` verified both outputs. Evidence is recorded in
`.omo/evidence/native-export-set.md`.

### U7 — demo voicebank production tooling

Japanese CVVC inventory, recording-script, retake, acoustic-marker, pitch-mark,
hash, and operator-CSV tooling is repository-complete. Performer consent,
directed recording, listening QA, signed package, and clean installation remain
external and are intentionally fail-closed.

The rebuilt CLAP editor bundle passes the pinned official `clap-validator`
0.4.1 preflight on both Apple Silicon and a LinuxKit aarch64 runner shape: 44
tests ran, 26 passed, 0 failed, and 18 were skipped. The Linux raw log,
validator binary, exact plugin artifact, and runner metadata are retained in
`.omo/evidence/clap-validator-linux.md`. The v1 audio-port-config-info ABI and
broad validator sample-rate activation cases are now covered by the
implementation and host harness; the authoritative Phase 13A matrix still
requires the checked-in CI archive.

The Phase 13A VST3 wrapper now enables position-independent code for shared
Linux modules, discovers bundles emitted into the declared artifact directory,
and applies a pinned `clap-wrapper` CLAP-path compatibility patch so the
validator can load the canonical CLAP beside the VST3 bundle. The arm64
LinuxKit wrapper build reached 100% before the local Docker engine exhausted
its filesystem and stopped accepting validator reruns; the checked-in Linux
VST3 validator row therefore remains `NOT_RUN` until the GitHub target runner
produces a fresh hash-bound result.

All Phase 13A VST3 validator paths now preserve a self-authenticating packet
binding the validator result, raw logs, validator executable, VST3 tree,
canonical CLAP tree, runner metadata, and build manifest. This strengthens
release evidence without treating packet integrity as validator success.

The target-runtime CLAP host is now cross-platform: X11, AppKit/Cocoa, and
Win32 native parent windows share one harness, and module loading uses the
platform loader rather than a Linux-only `dlopen` path. The workflow runs the
native GUI CTest and records a hash-bound summary, screenshot, and four-channel
WAV. Apple Silicon local evidence passes with the explicit development fixture;
Windows still requires its target-runner result.

Target-runtime packet verification now also requires the declared schema,
platform, build state, `PASS` runtime result, and hash-bound runner metadata;
it rejects symlinked packet artifacts. The record writer enforces the same
boundary before upload. The Usable Alpha contract verifier applies the same
symlink boundary to repository evidence, preventing a hash of an external or
mutable target from being presented as self-contained release evidence.

The exact realtime soak path is now operationally wired as a manual Linux
workflow. It builds the live articulation and soak targets, requires the
`full` 7,200-second profile, validates the live summary and elapsed soak record
fail-closed, and uploads the evidence. The runner now drives note, expression,
MIDI, transition, voice-steal, and resource clear/republish events and emits
their counters in the evidence record; the validator rejects incomplete
workload records and mismatched profile/duration claims. The workflow now also
creates and verifies a hash-bound packet for the live summary, WAV, soak JSON,
runner identity, and soak executable. A local Ubuntu 24.04
arm64 LinuxKit preflight has now completed the exact 7,200-second profile with
`PHASE12C_EVIDENCE=PASS` and 7,003,003 processed blocks. P12-009 remains
partial only because the authoritative Phase 13A matrix still requires the
checked-in target-runner workflow archive rather than local container evidence.

The first-run documentation path is now bundled as `QUICK_START.md`, exposed
through the standalone Documentation menu, and required by both macOS and
Windows packaging checks. It guides audio setup, exact voicebank selection,
authoring, save/reopen, export, and privacy-safe support reporting without
changing the acceptance contract.

The Usable Alpha rendering-stack decision is now explicit in
`docs/adr/0020-usable-alpha-rendering-architecture.md`: retain the deterministic
first-party software rasterizer and native platform adapters for this gate,
with any GPU/iPlug2/Skia migration deferred until a measured post-Alpha defect
justifies it. This resolves P14-001 without weakening the external acceptance
requirements.

The built Apple Silicon bundle also has a reproducible unsigned package smoke:
the packaged AppKit application launches, resolves the development evaluation
bank, reports the physical CoreAudio backend, exits cleanly on its timer, and
writes a 2880x1800 frame. The run is recorded in
`.omo/evidence/standalone-package-smoke.md`; it advances launch/resource
verification but does not replace signing, Finder, clean-install, production
bank, or full Usable Alpha evidence.

The exact packaged ZIP was additionally extracted and launched through macOS
LaunchServices with `open`. Its real AppKit tree reached a responsive editor
with actionable missing-bank recovery controls. The package hash and capture
are recorded in `.omo/evidence/packaged-finder-launch.md`.

The same packaged bundle also accepted a `.seam` document through the
LaunchServices open-file event and replaced the provisional project with its
Japanese notes. The missing external bank remained an explicit diagnostic;
the open-file capture is retained alongside the package evidence.

The standalone package gate now rejects the all-zero placeholder source commit
identity. Release and private-alpha packaging must provide an explicit
40-character source commit; a positive package copy carrying the current Git
commit packaged and launched successfully. This prevents a valid-looking
unsigned bundle from losing its source provenance.

A copied staging bundle has now also passed nested Developer ID signing and
`codesign --verify --deep --strict` with the local `Developer ID Application`
identity. `spctl` correctly reports `Unnotarized Developer ID`; notarization and
stapling remain an authorized external-upload gate. The signed artifact and
exact result are recorded in `.omo/evidence/macos-signed-package.md`.
The package was re-staged after the latest technical-edit fix so its v2 hash
binds to the current bundle.

The physical CoreAudio path now negotiates its maximum render slice against
the selected device's current buffer size instead of assuming the requested
engine block size is also the hardware callback size. This removes the
`kAudioUnitErr_TooManyFramesToProcess` failure seen when the built-in output
requested 512 frames from a 256-frame configuration. The development-only
`--play` probe also waits for a startup project to publish audio before it
starts transport and fails closed on an unreadable startup project. A fresh
AppKit run delivered 116,224 rendered frames through 507 physical callbacks
with zero CoreAudio write failures; P12-008 remains partial because this local
development-bank run is not signed, installed, Windows-certified, or external
Usable Alpha evidence.

The matching CoreAudio capture adapter now applies the same hardware-slice
contract. The default Mac input reports a 512-frame buffer, so Voicebank
Studio sizes both `kAudioUnitProperty_MaximumFramesPerSlice` and its
preallocated mono capture buffer to the larger of that hardware value and the
requested 256-frame engine block. The new `--record-ms` probe automates the
existing arm, callback, stop, and WAV-export path without weakening the normal
interactive flow; export failures retain the captured buffer for retry. A
synthetic-input run completed 49 callbacks and 12,544 frames with zero read
failures and wrote a finite 48 kHz mono PCM WAV. The actual microphone was not
started, so TCC permission, physical input callbacks, acoustic quality, and
recording-session acceptance remain target-machine gates.

Phase 2, 3, and 4 generated saved-project fixtures now persist the computed
voicebank content identity instead of placeholder `phase*-synthetic-content`
labels. A fresh Phase 2 fixture opened through the packaged AppKit standalone,
resolved the exact development bank, published production audio, and delivered
105,984 physical callback frames with zero CoreAudio write failures. This keeps
fixture projects honest and makes them usable for saved-project runtime probes.

The offline documentation, onboarding, and support milestone is also complete:
the canonical EULA, privacy notice, Quick Start, user manual, limitations,
update/rollback policy, tester checklist, support policy, and security-response
policy are validated by the documentation contract and bundled by the macOS
and Windows packaging checks. This resolves P14-003; it does not alter the
separate target-runtime or external content/IP gates.

The responsive native header now derives project metadata bounds from the
available portrait gap. At the fixed physical 1440x900 window, 200% scale
enters the intentional compact 720x450 logical breakpoint and suppresses
the right-side metadata block when no safe text region remains; the compact
subtitle retains a fitted project identity while the portrait, controls, lanes,
and footer remain contained. The regression is covered by the native UI test
and fresh scale-1/scale-2 packaged captures.

The piano-roll geometry also preserves a positive inner bounds width for valid
subpixel-duration notes. This keeps short notes visible, hit-testable, and
present in the shared accessibility tree instead of collapsing them during the
existing one-pixel visual inset. The focused regression and full-suite evidence
are recorded in `.omo/evidence/subpixel-note-bounds.md`.

Native standalone windows now carry explicit logical minimums instead of
accepting a physical 320x240 surface that the editor controller would silently
clamp and clip. The editor floor is 480x320; Voicebank Studio declares 720x520.
AppKit content constraints, X11 size hints, and Win32 minimum tracking all use
the scaled physical equivalent. The current compact/CJK captures and rejection
traces are recorded in `.omo/evidence/native-window-minimums.md`; compact
editor headers retain a fitted project identity in the subtitle band; external
assistive-technology and Windows runtime traversal remain open.

The piano roll now scopes visuals to the selected region, while the accessibility
tree retains every note in that region even when it lies outside the current
viewport. Lazy note pages can therefore focus and activate offscreen notes
through the same dispatcher as visible notes; physical VoiceOver/Narrator
certification remains a target-runtime gate.

Accessibility rebuilds no longer call `allNotes()` and allocate a semantic node
for every note before pruning to the page limit. The piano-roll model exposes
bounded indexed lookup; the shared tree retains only the configured materialized
window and creates later page/focus nodes on demand. This preserves the
10,000-note virtualization contract instead of merely hiding an eager build.

The note spatial index now builds a monotonic prefix maximum of absolute note
ends after sorting by start tick. Viewport and hit-test queries use that bound
to skip the non-overlapping prefix before scanning candidates, while retaining
long notes that begin before the viewport. This keeps the 10,000-note render
interaction path from doing a full left-prefix scan on every query.

The Phase 12C live engine and every live-voice matrix, probe, soak, and demo
consumer now receive the same warnings and sanitizer options as the first-party
libraries they consume, so full ASan/UBSan and TSan build graphs remain
linkable and instrument the articulation path.
Voicebank manifest decoding also builds parsed styles in a
separate vector before replacing the default manifest value; this avoids the
macOS libc++ container annotation failure observed only in the sanitized live
demo while preserving fail-closed manifest validation.

Static semantic controls now accept their advertised SetFocus action through the
shared dispatcher, including window, lane, status, dock, and panel nodes. Focus
is retained in the shared semantic snapshot and replaces the previous focused
node, so AppKit and UIA clients can observe a real focus transition instead of
receiving a no-op success; the physical assistive-technology gate remains
unchanged.

The AppKit native editor view now answers `accessibilityFocusedUIElement` from
that same shared focus state and posts the focused-element notification after a
successful semantic focus dispatch. Focus lookup includes lazy note nodes, so a
screen reader can query the focused offscreen note rather than receiving only
the visible page shell.

Keyboard focus now follows the same semantic order: Tab and Shift-Tab cycle
through the current modal/editor controls, wrap at either end, and avoid
duplicating materialized notes that are also retained in the lazy-note store.
The shared CLAP runtime and embedded AppKit view expose the same focused
element query, keeping keyboard-only behavior aligned across standalone and
plug-in surfaces.

Semantic selection is now separate from keyboard focus. Selected notes,
arrangement rows, voicebank cards, and audio devices expose selection state to
AppKit/UIA without masquerading as the platform-focused element; explicit
SetFocus remains the only path that changes focus.

Controller dispatch now preserves that separation for materialized notes and
arrangement rows: SetFocus only updates the semantic focus target, while
Activate performs the corresponding note, track, or region selection. This
prevents keyboard and assistive-technology traversal from clearing or changing
the authoring selection as a side effect.

Enabled Play/Pause, Mute, and Solo controls now also advertise SetFocus, so
keyboard and assistive-technology traversal can reach every active transport
and track-mix action without requiring an activation side effect.

The compact arrangement/inspector dock now uses shared 9–10 px data typography
and primary-contrast values for track, region, inspector, and lane guidance
text. The 1280×720 CJK capture confirms the larger readable treatment remains
inside the existing dock geometry without clipping or overlap.

UTF-8 display-width and truncation now use the shared text module’s scalar and
common-cluster decoder: combining marks, variation selectors, ZWJ emoji,
regional-indicator flags, CJK/full-width code points, and malformed-byte
fallback are handled without splitting a multibyte or visible cluster.
Editor fitting therefore shares the same Unicode rules as the native text
renderer instead of maintaining a separate byte-length guess.
On macOS the software-raster font search also includes Apple Symbols, so
common monochrome emoji and symbol glyphs remain visible in compact cards.

The semantic technical lanes now clip each published lane rectangle to the
editor root and omit fully occluded or zero-sized focus targets. A recursive
minimum-window regression covers every nested semantic node at 480×320 and
repeats the check while diagnostic and export overlays consume the lower
surface. This resolves the narrow semantic-bounds finding in
`.omo/evidence/render-status-v2-clone-fidelity.md` without changing the
external acceptance status.

## Next implementation task

Continue code-owned UX/accessibility and target-runtime hardening while keeping
the external gates explicit. The next release claim must be based on actual
Apple Silicon, physical-device, licensed-host, signing/notarization, and
real-song evidence rather than the deterministic fallback test mode.
