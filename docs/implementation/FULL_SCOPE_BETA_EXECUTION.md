# Full-Scope Beta GO Execution Evidence

This ledger records implementation evidence for [the approved plan](../plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md). It does not replace the plan or authorize release. The complete R1–R20/V01–V18 scope remains mandatory.

## Starting state: 2026-09-05

- Branch: `codex/production-readiness-completion`.
- Source commit: `69901159a27b2935bb8e40c4c96eccde781f0b9f`.
- Origin report SHA-256: `635606cfd10be803612dfcb47cf84651796a06860ff34dc8343705eac20c9c01`.
- Pre-existing work: 40 modified tracked files, comprising 2,912 insertions and 339 deletions, plus untracked U60 crash/support headers, probe, schemas and fixtures. The source report and implementation plan were also untracked. These changes are preserved, not attributed to this implementation run.
- Existing `build/dev` uses Ninja/Debug. Toolchain: Apple clang 21.0.0 targeting arm64 macOS, CMake 4.1.1. Compiler warnings-as-errors remain enabled. Local generated build identity is development identity, not release provenance; the source commit plus working diff must accompany any evidence.

## Active implementation

2026-09-12 — Publication verification: accumulated implementation committed as
`fdd197005fad6b6b0fb33396854b4439be7ab18c`. Release build passed; the complete
121-target CTest run passed 119 targets in 143.97 s. Source closure identified ten
historical `test-details.log` evidence files excluded by the owner's global
`test-*.log` ignore rule; these exact files were explicitly staged and closure
then passed. The remaining failure was the Japanese-reading job test's short
Ready wait, which passed on an unchanged-source focused rerun. Both failed
targets passed on that rerun (1.99 s); the intermittent wait remains to repair.
License audit passed in an exact-commit temporary clone containing only master,
as required by the audit, without removing the owner's existing branches.
This is development publication evidence, not full Beta GO acceptance.

2026-09-12 — Resumed after interrupted sessions and recovered the completed native
manifest-loader verification from the CTest log. Address-derived bounded loading
now joins trusted expected metadata to package verification. A copied native
module fixture succeeds from an unrelated directory with PATH unavailable and
rejects substituted descriptor/manifest/dependency input. Neural, packaging and
core suites passed (11, 28 and 867 cases respectively). Publication requested by
the owner includes the accumulated implementation and its required new files;
full Beta GO remains unfinished. See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Release assembly/verifier now seal and recompute a per-surface neural
package inventory, explicitly distinguishing missing packages from verified file
metadata. Present manifests must match actual files, module paths and build ID;
forged status and stale helper bytes fail. All 138 phase13a tests pass (8.201 s)
with native probe enabled; Release regeneration/build and packaging CTest pass
(1.98 s). Existing development payloads remain release-ineligible. Native trust
delivery, qualified inference and installed-surface acceptance remain unfinished;
see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO stays open.

2026-09-10 — Payload-side neural manifest builder generates deterministic native-
compatible metadata from bounded real-file hashing. Six Python packaging tests
pass, including generated-byte acceptance and substituted-byte rejection through
the C++ probe. Registered CTest packaging check passes (0.09 s); neural/core
suites pass 2/2 (21.30 s). Release-sealing integration, trusted digest delivery,
signed-byte ordering and dependency closure remain open. No U37/Beta GO acceptance;
see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Address-based loaded-module discovery now feeds neural package
resolution. The real-binary fixture resolves and hashes its test module and
sibling probe, then executes the resolved helper. Release build and neural/core
suites pass 2/2 (22.90 s); all 11 neural cases also pass from `/private/tmp` with
`PATH=/nonexistent`. Windows and installed plugin surfaces remain unqualified;
trusted materialization and real inference are still required for U37/Beta GO.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Neural helper package manifest decoder now binds exact serialized
bytes to a supplied deployment digest and enforces strict schema/path/size limits.
The real-filesystem resolver fixture now consumes decoded metadata and covers
malformed and substituted inputs. Release build/neural-protocol/core suites pass
2/2 (22.69 s). Trusted digest delivery, loaded-module discovery, packaging and
native runtime integration remain incomplete; no U37/Beta GO completion claimed.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Typed neural package resolver validates a supplied SEAM module anchor,
matching build/protocol, contained helper/dependency paths and exact file digests.
Filesystem tests reject redirected, missing, duplicate, changed and incompatible
entries; Release build/neural-protocol/core suites pass 2/2 (21.98 s). Actual
module discovery, trusted manifest decoding, binary dependency closure, loader
control and installed runtime wiring remain required. This advances U37 without
claiming unit completion; see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Neural helper identity preflight now requires an expected canonical
executable digest and a positive size ceiling capped at 256 MiB; mismatches are
rejected before launch. Tests retain valid digest setup for response-binding
failures and add digest/size/cancellation rejection checks. Release build and
neural-protocol/core suites pass 2/2 (21.75 s). This advances U37 but does not
complete trusted package discovery, dependency closure, race-resistant launch,
actual inference or installed qualification. See
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO remains open.

2026-09-10 — Recovery-first continuation: all 2,078 paths in checkpoint
`session-preservation-dDpBP8` remain present; regular-file hashes differ only for
the pending neural request builder and its regression test. This comparison
cannot establish whether work was lost before that checkpoint. No restoration,
deletion, staging, commit or push was performed. Owning-note boundary conditioning
now preserves explicitly extended phones' pitch/dynamics with a two-second
limit. Final neural-protocol/core rerun passes 2/2 (21.28 s), following a disk-full
invalid run and an intermittent Japanese-reading Ready assertion failure.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Actual neural inference and
full Beta GO remain unfinished; no additional whole roadmap unit is accepted.

2026-09-10 — Neural syllable-order guard rejects onsets after and codas before
their associated nucleus even without span overlap. Malformed-anchor regressions
and a valid compiled coda pass; Release build/neural-protocol/core suites pass
(42.11 s). This is conditioning correctness, not neural inference or Beta GO
qualification. See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Complete neural score request builder now combines compiled timing,
verified vocabulary, phonetic voicing, F0 and dynamics/articulation gain with
bounded allocation and cancellation. Tests check silence/unvoiced/voiced features,
wire round-trip and score-derived probe subprocess exchange. Final Release build
and neural-protocol/core suites pass (22.15 s). Actual model/vocoder inference
and qualified singer delivery remain unfinished; see
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO stays open.

2026-09-10 — Neural score phonetic adapter maps compiled timing and phone symbols
through verified vocabulary IDs, with explicitly selected silence and strict
ownership/coverage checks. Real timing-fixture tests reject unknown/unresolved,
overlapping and clipped input. Release build/neural-protocol/core suites pass
(24.17 s). Full feature-request construction and real model inference remain
unfinished; see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Beta GO stays open.

2026-09-10 — Verified neural vocabulary: bounded immutable token lookup preserves
explicit IDs and validates exact bytes against the model hash. Conditioned helper
runs require that verified vocabulary and exact token count. Tests cover malformed
tokens, byte substitution, missing vocabulary, false size and successful bound
subprocess exchange. Release build/neural-protocol/core suites pass (22.25 s).
No model weights/inference qualification claimed; see
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO remains open.

2026-09-10 — Neural conditioned-response binding: response v2 carries the digest
of the complete canonical request, and the runner rejects missing/mismatched
bindings while retaining ID/model/shape checks. Real probe subprocess tests cover
correct, wrong and missing digests; codec tests cover v2 and invalid hashes.
Release build/neural-protocol/core suites pass (21.97 s). Correlation is not real
inference; score adapter and qualified model delivery remain open. See
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO remains unfinished.

2026-09-10 — Neural request metadata v2 now transports vocabulary identity and
bounded timed phoneme IDs alongside F0/dynamics, with strict field/version checks
and model vocabulary binding. Final Release build/neural-protocol/core suites
pass (24.17 s), including a real probe subprocess exchange. Probe PCM is not
neural singing. Complete conditioned-response binding, score adapter and actual
qualified inference remain open; see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.
Full Beta GO remains unfinished.

2026-09-10 — Full-scope reprioritization identified missing neural phonetic input:
v1 IPC has F0/dynamics but only a pronunciation hash. Added bounded explicit token
spans and model vocabulary-hash/budget validation as groundwork for a versioned
request. Release build and neural-protocol/core suites pass (22.73 s). Wire
integration, score adapter and actual qualified neural inference remain unfinished.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`; no U37 or Beta GO acceptance.

2026-09-10 — Voiced source removal lifecycle: required resonance-pose removal now
reports the explicit dependency without mutation. Tests verify source removal
preserves resonance poses, invalidates preview, and Undo restores identity and
rerendered PCM. Release build/Designer/core suites pass (22.87 s). Live removal
verification and full Beta GO remain open; see `docs/formats/VOICE_RECIPE_V5.md`.

2026-09-10 — Mixed voiced-source replay matrix: 48 rate/pitch/placement/gain
combinations verify exact whole/chunk/seek PCM, finite output and source-rate
rejection without silent retuning. Release build and voice-design/core suites
pass (23.60 s). This is local numerical coverage, not pitch-range or listening
qualification. See `docs/formats/VOICE_RECIPE_V5.md`; full Beta GO remains open.

2026-09-10 — Designer preview availability: voiced frication disables isolated
noise preview; CV/VC require selected vowel context, with explanatory semantic
labels. Shared availability tests cover voicing, pose/style mismatch and bounds.
Release build and Designer/core suites pass (28.36 s). Live disabled-action
verification remains open; see `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO
remains unfinished.

2026-09-10 — Full local post-voiced-frication regression: 119/120 tests pass
(133.37 s, -j 2). Only source closure fails: 523 existing unindexed required
inputs, zero missing-input records. All 2,075 checkpoint files remain present
with matching regular-file hashes. No gate bypass or Git publication performed.
See `VOICED_FRICATION_FULL_REGRESSION_2026-09-10.md`. This is engineering evidence,
not a product-completion percentage or full Beta GO qualification.

2026-09-10 — Voiced-frication coda round trip: English `aa1 z` now has end-to-end
regression coverage through normal rendering, exact WAV export/reload, typed
candidate-v5 markers, producer import/recovery and asynchronous Studio opening.
Take remains MarkerReview with no review. Release build/export/core suites pass
(24.48 s); bake/logs retained in `evidence/voiced-frication-coda-v5-2026-09-10/`.
This closes a coda verification gap, not listening or full Beta GO acceptance.

2026-09-10 — Live voiced-source authoring: rebuilt 720x520 Designer created an
explicit z resonance/source, accepted gain 0.3 and rendered CV. Gain 1.1 rejected
without mutation; zero invalidated preview; Undo restored 0.3; VC then rendered.
Numeric accessibility/creation/undo verified, not keyboard/drag/save/reopen or
listening acceptance. See `docs/formats/VOICE_RECIPE_V5.md`. No saved user data
changed. Full Beta GO remains open.

2026-09-10 — Native frication voicing gain: fourth source-control row routes numeric,
keyboard and drag edits through validated draft history; zero removes voicing.
Matching resonance remains explicit. Source/stop/nasal row offsets are updated.
Release build and Designer/core tests pass (22.80 s), including validation,
undo/redo/save/reopen and exact legacy identity restoration. Live native edit
qualification remains open; see `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO
and singer-quality acceptance remain unfinished.

2026-09-10 — Normal voiced-frication render/bake/import: recipe v5 is admitted by
default; export selects candidate v5 for voiced-frication markers. English `z aa1`
renders through the normal snapshot pipeline, reloads with exact Float32 PCM,
imports/reopens as MarkerReview with zero reviews and preserves typed lineage.
Designer CV/VC uses authored voicing; isolated noise rejects voiced bindings.
Final Release build and five suites pass (29.32 s), after correcting the expanded
fixture's final expected marker kind. WAV/metadata/test logs are retained under
`evidence/voiced-frication-v5-2026-09-10/`. See `docs/formats/PROCEDURAL_CANDIDATE_V5.md`.
Native voicing controls, listener qualification and full Beta GO remain unfinished.

2026-09-10 — Candidate-v5 loader: strict metadata parsing preserves voiced-frication
identity, requires explicit recipe voicing/resonance, rejects unvoiced relabeling
and requires appropriate mixed-renderer revisions. Vowel/nasal identity ambiguity
is rejected in recipe/planner validation too. Release build/voice-design/export/core
suites pass (31.81 s). Tests are metadata fixtures; writer, normal runtime admission
and real producer import remain unfinished. See `docs/formats/PROCEDURAL_CANDIDATE_V5.md`.
Full Beta GO remains open.

2026-09-10 — Voiced-frication marker transport: marker kinds share the planner's
type and projection preserves it directly, eliminating a new-kind-to-vowel
fallback. Scheduler completion/cache-hit tests preserve voiced-frication identity
and reject unknown kinds. Legacy candidate export explicitly refuses the new
kind pending a versioned format. Release build/snapshot/export/core suites pass
(27.40 s). See `docs/formats/VOICE_RECIPE_V5.md`; normal v5 admission and full Beta
GO remain unfinished.

2026-09-10 — Recipe-based voiced-frication development path: explicit opt-in now
propagates through recipe compilation and stream creation. Tests verify onset
equivalence, coda rendering/cropped replay, style/cancellation rejection and noise
seed isolation from preceding vowel PCM. Release build and four suites pass
(28.78 s). Default v5 admission and candidate integration remain closed/pending;
see `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO remains open.

2026-09-10 — Development mixed voiced-frication stream: ArticulatedStream revision
8 validates both frozen bindings, mixes authored voiced gain with noise, and
smooths contiguous gain transitions. Direct opt-in tests verify gain-dependent
PCM, stale-plan rejection, exact seek/checkpoint/block-size replay and cancellation.
Release build and four focused suites pass (28.95 s). Default v5 admission stays
closed pending candidate/marker and normal runtime integration; no singer-quality
acceptance is claimed. See `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO stays open.

2026-09-10 — Voiced-frication timing/noise stage: articulation revision 8 carries
explicit voiced-frication gain and rejects voicing mismatch; noise stream revision
3 includes mixed gestures instead of skipping all voiced gestures. Exact noise
PCM and seek comparisons pass. Final Release build and four focused suites pass
(29.73 s). Mixed voiced output, candidate identity and v5 runtime admission remain
unfinished. See `docs/formats/VOICE_RECIPE_V5.md`; full Beta GO remains open.

2026-09-10 — Voiced-frication recipe groundwork: schema 5 introduces explicit
voicingGain in (0,1] and requires same-phone/style resonance; unvoiced rows use
null and legacy recipes retain canonical encoding. Codec/validation and frozen
identity support are implemented; rendering-resource decode intentionally still
rejects v5 until voiced/noise DSP and lineage integration exist. Release build
and Designer/voice-design/export/core suites pass (29.34 s). See
`docs/formats/VOICE_RECIPE_V5.md`. This is contract groundwork, not audible voiced
frication, a completed U20, or Beta GO acceptance.

2026-09-10 — Articulation capability diagnostics now name the phone/note and
distinguish missing style bindings from unsupported voiced consonants. Regression
proves that binding noise as `z` cannot render a voiced `z` token. Release build
and voice-design/core suites pass (23.31 s). No new consonant synthesis or quality
acceptance is claimed; see `DESIGNER_FRICATION_CONTEXT_2026-09-09.md`. Full Beta GO
remains open, including voiced/noise articulation and reviewed singer resources.

2026-09-10 — Late inspection cancellation: a stop arriving after worker completion
but before UI adoption now discards the read-only score snapshot. Already-published
job preparation still reports success. Tests observe completed futures before
cancelling and verify both outcomes; final Release build/export/core suites pass
(26.11 s). See `DESIGNER_PREPARATION_DIALOG_RETENTION_2026-09-09.md`. Full Beta GO
and live worker-cancel verification remain open.

2026-09-10 — Visible producer cancellation: during active work the batch slot
becomes Cancel work, shared by painting, pointer and semantic controls; other
generation actions remain disabled. It delegates to the existing stop request,
not rollback or synthetic completion. Release build/export/core suites pass
(25.14 s). Live worker-cancel activation remains unqualified; see
`PRODUCER_GENERATION_BUTTONS_2026-09-10.md`. Full Beta GO remains open.

2026-09-10 — Generation accessibility: the inventory producer exposes generation
buttons/status with context-bound IDs and shared guarded mouse/AX dispatch.
Modal re-entry and unavailable actions are rejected. Release build/export/core
tests pass (24.02 s); live AX Prepare activation/cancel restores the controls.
Other producer semantic coverage and end-to-end generation remain unfinished.
See `PRODUCER_GENERATION_BUTTONS_2026-09-10.md`; full Beta GO remains open.

2026-09-10 — Live producer-button follow-up: at 720x520, all four generation
buttons opened their expected native chooser and returned on cancel. Labels fit
the intake panel without marker overlap in this fixture; no inputs were selected
and durable generation remained 2. Dedicated accessibility and end-to-end job
execution through these controls remain unqualified. Evidence boundary is recorded
in `PRODUCER_GENERATION_BUTTONS_2026-09-10.md`; full Beta GO remains open.

2026-09-10 — Producer generation buttons: inventory intake now paints Prepare,
Run job, Make batch and Run batch with shared geometry/enabled-state pointer
routing to existing guarded operations. Recording and active work disable them.
Release build and export/core suites pass (36.47 s). Live mouse/visual and dedicated
semantic accessibility remain unfinished. See `PRODUCER_GENERATION_BUTTONS_2026-09-10.md`.
This is partial U22 implementation; full Beta GO remains open.

2026-09-09 — Retained preparation context now invalidates on actual assignment
changes, including editable-row adoption; invalid/same-row selection preserves
it. A two-assignment A -> B -> A regression proves the snapshot cannot revive.
Release build and export/core/sample-review tests pass (33.25 s). Native row
interaction remains unqualified. See the assignment-switch follow-up in
`DESIGNER_PREPARATION_DIALOG_RETENTION_2026-09-09.md`; full Beta GO remains open.

2026-09-09 — Preparation dialog retention: native region/destination dialogs now
inspect rather than consume the frozen Designer score/recipe selection. Cancel
retains it; successful worker dispatch consumes it; obsolete assignment context
is rejected and cleared. Final Release build and Designer/export/core tests pass
(36.31 s). Live modal cancellation remains unqualified. See
`DESIGNER_PREPARATION_DIALOG_RETENTION_2026-09-09.md`. U21/U22 and Beta GO stay open.

2026-09-09 — Current full local regression: 119/120 Release CTest entries pass
(129.21 s, -j 2). Only tracked-source closure fails: 505 existing unindexed
required inputs, no missing-input records. No gate bypass or staging occurred.
Live rebuilt Studio confirms automatic scrolling/selection after noise and stop
creation; immediate keyboard focus remains unqualified after a tool key error.
See `DESIGNER_POST_CONTEXT_REGRESSION_2026-09-09.md`. This is engineering evidence,
not a product-completion percentage or Beta GO acceptance.

2026-09-09 — Source creation selection: successful Add noise/Add stop now selects
the new source's style-filtered parameter row; compatible selected vowels and
pitch are retained. Mixed-style index resolution and rejection behavior have
regression coverage. Live auto-scroll/focus remains unqualified. See
`DESIGNER_SOURCE_CREATION_SELECTION_2026-09-09.md`; U22 and Beta GO remain open.

2026-09-09 — Frication context previews: Designer now renders selected source/vowel
CV and VC pairs through ArticulatedStream, with mode-bound asynchronous state,
visible buttons and truthful labels. Release build and Designer/core suites pass
(22.71 s); live minimum-size VC rendering/playback activation and CV rendering
pass. Fixed 150 ms frication timing is a preview policy, not phonetic acceptance.
See `DESIGNER_FRICATION_CONTEXT_2026-09-09.md`. U20/U22 and full Beta GO remain open.

2026-09-09 — Compact Designer source workflow: source and generation-preparation
bars are visible at the supported 720x520 minimum, with six readable parameter
rows and shared paint/semantic/pointer geometry. Release build and Designer/core
suites passed (22.67 s); live mouse Add stop, navigation, VC/CV/Src render and VC
playback activation passed. Voice quality and full Beta GO remain unqualified.
See `DESIGNER_COMPACT_SOURCE_ACTIONS_2026-09-09.md`. The preceding AppKit completion
snapshot repair passed live pointer rendering and both suites (22.64 s); see
`SESSION_CONTINUITY_AND_AX_COMPLETION_2026-09-09.md`.

2026-09-09 — Accessibility dispatch safety: disabled controls/ancestors reject
non-focus actions, while explicit inspection focus remains supported. Dispatch
owns its callback ID across synchronous tree replacement. Two regressions from
an overly broad focus restriction were diagnosed against the existing semantic
and focus-ring contracts and corrected. Full Release build/Designer/core tests
pass (22.58 s). Visible-button live QA and full Beta GO remain open. See
`ACCESSIBILITY_DISPATCH_SAFETY_2026-09-09.md`. No Git publication occurred.

2026-09-09 — Visible Designer buttons: painted action bars and pointer dispatch
now use the same semantic nodes/bounds, with concise Src/CV/VC labels and disabled
styling. Full Release build/Designer/core tests pass (21.81 s). Live QA could not
create a draft through the UI tool; the still-live process sample showed normal
AppKit event waiting, not an observed renderer hang. New button interaction and
compact source controls remain unqualified. No missing checkpoint files or Git
publication. See `DESIGNER_VISIBLE_BUTTONS_2026-09-09.md`; full Beta GO stays active.

2026-09-09 — Vowel-stop Designer audition: added a typed final-stop preview mode
alongside isolated source and stop-vowel, using the selected vowel/pitch and
production articulation path. Modes retain separate readiness semantics and
explicit labels. Full Release build/four focused suites pass (25.84 s), covering
closure/burst bounds, reproducibility and mode switching. Native controls and
listener quality are not newly qualified. No missing checkpoint files or Git
publication. See `VOWEL_STOP_DESIGNER_AUDITION_2026-09-09.md`; Beta GO stays open.

2026-09-09 — Workspace-entry UI recovery: unsuccessful async opening now returns
to the retained Designer session with the error/cancellation reason, instead of
stranding an empty producer view. Live digest-mismatch rejection preserved an
unsaved 0.17 aspiration edit; immediate keyboard editing still worked. Full
Release build/three focused suites pass (22.90 s). Slow-I/O cancellation latency
and full Beta GO remain open. No missing checkpoint files or Git publication.
See `WORKSPACE_ENTRY_RECOVERY_2026-09-09.md`.

2026-09-09 — Async initial workspace recovery: the Designer handoff prepares
repository state and marker lineage on an isolated worker, then adopts it under
owner-thread epoch/empty-context checks. Busy conflicts, cancellation, retry and
shutdown preserve unadopted state. Multilingual candidate markers/generation
match after async recovery. Full Release build/four focused suites pass (24.74 s).
No fresh UI/large-workspace latency qualification, missing checkpoint files or Git
publication. See `ASYNC_WORKSPACE_RECOVERY_2026-09-09.md`; full Beta GO remains open.

2026-09-09 — Frication codas: configured unvoiced frication sources now render
after vowels with resolved, nonoverlapping coda timing and their sustained noise
envelope. Actual English aa1 s export reloads as candidate v2 with a final typed
marker and exact PCM equality. Full Release build/four focused suites pass
(30.06 s), including stop/frication envelope distinction and replay/rejection
checks. No missing checkpoint files or Git publication. See
`FRICATION_CODAS_2026-09-09.md`; voiced frication and full Beta GO remain open.

2026-09-09 — Released stop codas: explicit p/t/k bindings now admit bounded,
nonoverlapping post-nucleus closure/burst gestures. Normal simple-coda timing
feeds the renderer; an actual English aa1 k bake reloads with typed markers and
exact PCM equality. Full Release build/four focused suites pass (28.10 s), with
silence, burst, replay and rejection checks. Unreleased/language-specific stops
and listener qualification remain open. No missing checkpoint files or Git
publication. See `RELEASED_STOP_CODAS_2026-09-09.md`; full Beta GO remains active.

2026-09-09 — Windows workspace picker source implementation: added a native
folder picker with explicit context fields, cancellation/errors and scoped COM
cleanup. Shared macOS/Windows/Studio validation rejects malformed digests and
bounded-ID violations. Full macOS Release build/three focused suites pass
(23.52 s). Windows code is not compiled or runtime-qualified here; native parity
remains unproven. No missing checkpoint files or Git publication. See
`WINDOWS_WORKSPACE_PICKER_2026-09-09.md`; full Beta GO remains open.

2026-09-09 — Studio navigation focus repair: the AppKit bridge returns keyboard
focus to the canvas when a dispatched action removes a custom accessibility
surface, guarded against active text input/modals. Live accessibility and pointer
workspace handoffs now allow immediate Command-D without a canvas click; repeated
navigation also passed. Full Release build/three focused suites pass (26.11 s).
No capture, producer mutation, missing checkpoint files or Git publication.
See `STUDIO_NAVIGATION_FOCUS_2026-09-09.md`. Full Beta GO remains open.

2026-09-09 — Designer/workspace handoff: source-free Designer can open an
existing producer folder with an explicit inventory digest and registered
PRODUCER identity, preserving its unsaved voice draft. Invalid identity/digest
checks retain current state. Live opening restored the assignment and enabled
job preparation; aspiration 0.12 survived the round trip. Post-modal keyboard
focus still required a canvas click. Full Release build/four focused suites pass
(48.24 s). New-workspace creation, other platforms, async recovery and full Beta
GO remain open. See `DESIGNER_WORKSPACE_HANDOFF_2026-09-09.md`.

2026-09-09 — Live stop/vowel preview verification: native phrase render/play
actions reached the correct ready/playback labels; switching to isolated source
changed modes; a duration edit disabled playback and cleared readiness. Undo
restored saved values without reviving PCM. Normal exit reported unopened input
and no capture; saved recipe bytes were unchanged. This verifies focused native
dispatch, not perceived pronunciation or device-loopback fidelity. See the live
follow-up in `PLOSIVE_VOWEL_AUDITION_2026-09-09.md`. Full Beta GO remains open.

2026-09-09 — Stop/vowel Designer audition: added a context preview through the
production articulation renderer using the selected vowel/style/pitch, separate
from the isolated burst. Mode and selection identities protect async readiness.
Fixed the scratch score's missing lyric ownership through normal validation.
Full Release build and three focused suites pass (33.04 s). Native phrase-action
dispatch and musical qualification remain unverified. No checkpoint files were
missing or Git changes published. See `PLOSIVE_VOWEL_AUDITION_2026-09-09.md`;
full Beta GO remains active.

2026-09-09 — Source-free Designer startup: no-argument Studio launch now offers
visible New/Open controls without requiring a bank or initializing microphone
input. Explicit --designer preserves producer identity requirements and rejects
source-free timed recording. Live entry-button creation and vowel preview passed;
initial exit counters confirmed unopened input and no recording. Full Release
build/core tests pass (20.72 s). No missing checkpoint files or Git publication.
See `SOURCE_FREE_DESIGNER_STARTUP_2026-09-09.md`. Full producer onboarding,
source closure and Beta GO qualification remain open.

2026-09-09 — Post-plosive full regression: 119/120 configured Release tests pass
(136.91 s); only tracked-source closure fails, with 448 existing but unindexed
required inputs and no missing-file records. Kept that gate intact. Subsequently
fixed explicit Studio dimensions being overridden by saved window geometry;
full build/core tests pass (21.68 s), with focused live launch verification.
The full-run evidence predates that follow-up fix. See
`POST_PLOSIVE_FULL_REGRESSION_2026-09-09.md`. No Git publication, musical approval
or whole-unit acceptance occurred; full Beta GO remains open.

2026-09-09 — Designer readability/density: removed the six-row cap, enlarged
body/heading text, added a selection background and unified painted/semantic/hit
row geometry. Live macOS tall/minimum window checks showed sixteen/eight rows;
last-visible-row pointer/keyboard editing matched the intended parameter. Full
Release build and two focused suites pass (22.22 s). This is focused layout QA,
not a full redesign or cross-platform qualification. See `DESIGNER_DENSITY_2026-09-09.md`.
Full Beta GO remains open; no Git publication or whole-unit acceptance occurred.

2026-09-09 — macOS Studio bundle and live plosive QA: converted the macOS target
to its own app bundle, resolving native UI attachment. Live creation, duration
editing, source render/play dispatch, undo/redo, full-width seed dialog and save
passed; saved JSON independently matched. Closed test process exited normally
with no microphone capture. Visual review found undersized text and excessive
unused space, still requiring improvement. Full build/plist validation and two
focused suites pass (21.97 s). This is not Finder-first onboarding, distribution
signing or musical qualification. See
`STUDIO_MACOS_BUNDLE_AND_LIVE_PLOSIVE_QA_2026-09-09.md`. Full Beta GO remains open.

2026-09-09 — Dedicated plosive-source audition: selected stop controls now have
a separate finite closure/burst preview, render/play actions and source-only
status. Async results retain recipe/selection identity; edits and removals
invalidate PCM. Fixed overpainted preview/file status. Full Release build and
four focused suites pass (26.91 s), covering deterministic bounds, cancellation,
state separation and stale completion rejection. Live native interaction and
musical qualification remain open. No missing checkpoint files or Git publication.
See `PLOSIVE_DESIGNER_AUDITION_2026-09-09.md`; full Beta GO remains active.

2026-09-09 — Plosive Designer controls: added source creation/removal, full-width
seed editing and style-filtered spectrum/gain/duration rows, with native shortcut
and semantic-action wiring. Session history rejects stale/invalid edits and
preserves settings through undo/redo and save/reopen. Full Release build and
four focused suites pass (26.57 s). Native UI attachment was unavailable, so
live interaction remains unverified; dedicated stop audition also remains open.
No captured files were missing or Git changes published. Full Beta GO remains
open. See `PLOSIVE_DESIGNER_CONTROLS_2026-09-09.md`.

2026-09-09 — Initial plosive articulation integration: p/t/k recipe bindings now
drive silent closure and finite burst gestures before a voiced vowel. Typed
markers propagate through scheduler/cache and candidate-v4 bake/import/recovery.
Actual ka PCM, multi-rate source preparation, checkpoint/seek/reset/cancellation
and malformed metadata checks pass. Full Release build and four focused suites
pass (26.00 s). No checkpoint files were missing or Git changes published.
Native plosive editing, richer articulation, phonetic qualification and full
Beta GO remain open. See `PLOSIVE_ARTICULATION_2026-09-09.md`.

2026-09-09 — Plosive recipe persistence: schema 4 preserves explicit p/t/k
spectral sources, full-width seeds and nominal burst durations. Validation
rejects ambiguous frication/plosive bindings; history and save/reload preserve
the new data while prior schemas retain their canonical bytes. Full Release
build and five focused suites pass (27.73 s), confirmed from retained test logs
after session recovery. Hash comparison found no missing checkpoint files;
earlier historical loss remains unproven. This is persistence only: articulation,
candidate metadata and native controls remain open. No roadmap unit or Beta GO
acceptance is claimed. See `PLOSIVE_RECIPE_BINDINGS_2026-09-09.md`.

2026-09-09 — Plosive source primitive: added explicit silent closure followed by
a finite seeded spectral burst with smooth attack, decay and release. Bounds,
whole/chunk equality, checkpoint/reset and cancellation rollback are tested.
Full Release build and four focused suites pass (28.04 s). This is a source
component, not yet recipe/phoneme/candidate integration or stop pronunciation
qualification. No prior captured source paths were missing or Git changes
published. U20 and full Beta GO remain open. See `PLOSIVE_SOURCE_2026-09-09.md`.

2026-09-09 — Nasal timing display repair: the phoneme lane no longer replaces
resolved procedural coda/standalone-N spans with fixed 28-pixel estimates.
Inferred and manual labels, adjoining vowel bounds and boundary hit-testing are
verified at two zoom levels; sample-dependent estimates remain intact. Full
Release build and five focused suites pass (23.24 s). No captured files were
missing and no Git publication occurred. This is geometry/hit-testing proof,
not full native UX qualification or Beta GO. See `NASAL_TIMING_DISPLAY_2026-09-09.md`.

2026-09-09 — Automatic simple coda timing: procedural timing-policy revision 3
allocates one coda tail in a wholly untimed syllable, optionally with one onset.
The vowel ends at the inferred coda start; next-syllable edits are resolved first.
Authored groups, source-dependent timing and cluster rejection remain protected.
Articulation-plan revision 4 admits resolved inferred nasal codas. Actual aN
render/bake/import/recovery and timing regressions pass. Full Release build and
five focused suites pass (28.67 s). No captured paths were missing or Git changes
published. Phonetic quality, richer articulation and full Beta GO remain open.
See `AUTOMATIC_CODA_TIMING_2026-09-09.md`.

2026-09-09 — Standalone syllabic nasal: a sole Japanese N token now uses its
own note span without an invented vowel nucleus. Voiced snapshot/stream admission
and candidate-v3 all-N validation support the case; duplicate-note syllabic
markers and ambiguous multi-N allocation reject. Actual render/bake/load/import/
recovery runs against special:N without approval. Full Release build and five
focused suites pass (28.55 s). Plan revision 3 and stream revision 4 invalidate
old render identities. No prior captured file was missing and no Git publication
occurred. Broader pronunciation, other consonants and all Beta GO requirements
remain open. See `SYLLABIC_NASAL_2026-09-09.md`.

2026-09-09 — Initial nasal consonant gestures: explicit active m/n/ng/N poses
now use voiced nasal resonance/antiresonance with the oral output closed, not
renamed vowel coloration. Inferred onsets and explicitly timed nonoverlapping
codas bind a same-note vowel nucleus. Typed Nasal markers flow through scheduler
chunks/cache and candidate v3 baking/import/recovery without approval. Tests prove
nonzero onset/coda output, oral-band independence, checkpoint/reset/cancellation
continuity, exact scheduled PCM and a real unapproved ma candidate. Six focused
suites pass (29.15 s); full Release build and full CTest 119/120 pass (258.39 s),
only source closure with 405 unindexed required inputs. No captured paths were
missing and no Git publication occurred. Phonetic quality, nucleus-free nasals,
automatic coda/cluster timing, closures/bursts and full Beta GO remain open. See
`NASAL_CONSONANT_GESTURES_2026-09-09.md` and `../formats/PROCEDURAL_CANDIDATE_V3.md`.

2026-09-09 — Voiced-onset timing prerequisite: procedural timing-policy revision
2 allocates the existing bounded default to single voiced as well as unvoiced
onsets. Voicing, nucleus identity, authored timing and source-dependent behavior
remain unchanged; codas/clusters are not guessed. Actual Japanese m/n timing,
manual/short-note preservation and unsupported-renderer guards pass. Full Release
build and five focused suites pass (31.62 s). No source snapshot files were lost
or Git changes published. This does not complete nasal consonant synthesis or
accept U20/Beta GO. See `VOICED_ONSET_TIMING_2026-09-09.md`.

2026-09-09 — Multilingual candidate routing repair: removed two Japanese-only
vowel checks from procedural dispatch and candidate loading. Shared classification
now routes English/Korean vowel nuclei to sustained rendering and candidate v1;
the loader requires the exact pose and valid actual-rate tract. Renderer revision
8 invalidates old routing caches. A reproduced failing render/bake/load case now
passes through canonical producer import for English ah1 and Korean eo/eu, without
approval. Full Release build and five focused suites pass (31.91 s), core 841/841,
export 24/24 and snapshot 43/43. No prior snapshot files are missing. This fixes an
upstream reusable-bank defect found during consonant work; full language quality,
nasal/closure/burst articulation and all Beta GO requirements remain open. See
`MULTILINGUAL_CANDIDATE_ROUTING_2026-09-09.md`.

2026-09-09 — Nasal production integration: added scheduler transition/cache
coverage and moved the comprehensive frication/candidate/export/producer fixture
to a combined nasal/frication recipe. Actual stereo Final exports prove unchanged
100 ms consonant noise and changed vowel PCM; retained oral/nasal WAVs are finite,
unclipped engineering comparisons. Three focused suites pass (24.18 s), snapshot
43/43, export 23/23 and core 840/840. This is integration proof, not another DSP
feature or musician approval. No prior snapshot file was missing, no unit was
accepted and no Git publication occurred. Actual consonant articulation and all
remaining Beta GO requirements stay open. See `NASAL_PRODUCTION_INTEGRATION_2026-09-09.md`.

2026-09-09 — Audible nasal voice coloration: schema-3 recipes now bind explicit
nasal resonance/antiresonance frequencies and bandwidths. Stable pole/zero DSP,
independent-bank transitions, checkpoint/reset/rollback and zero-coupling exact
bypass connect to sustained/articulated rendering. Five native Designer controls
enable/edit the model with existing undo and saved-resource identity protection.
Live macOS loaded an oral recipe, pinned A, edited coupling to 0.8, rendered B and
saved a separate schema-3 recipe; no recording or quality approval occurred.
Five focused suites pass (26.97 s), design 16/16, Designer 24/24 and core 840/840.
Full Release run: 119/120 PASS (250.05 s), only source closure with 378 unindexed
required inputs at execution. No prior snapshot file was missing. This advances
U19 but does not synthesize nasal consonants or qualify a singer; full Beta GO,
remaining articulation, neural/classical and release work stay open. See
`NASAL_VOICE_DESIGN_2026-09-09.md` and `../formats/VOICE_RECIPE_V3.md`.

2026-09-09 — Native source registration: Studio now captures license bytes on its
serialized worker and exposes explicit AppKit/Win32 source-declaration forms.
All six choices (kind, rights, four permissions) require deliberate selection;
existing provenance, stale/recaptured context and late commit receipts remain
protected. Actual macOS source-free planned-Draft QA recorded only an unassessed
synthetic source with all permissions false, no recording and no unit approval.
Full Release build and seven focused suites pass (24.22 s), core 839/839 and
Studio draft/source 34/34. Eighteen control bounds and readable live form checked.
No missing prior snapshot files, unit acceptance or Git publication. Full initial
setup, Windows runtime and all remaining Beta GO requirements stay open. See
`NATIVE_SOURCE_REGISTRATION_2026-09-09.md`.

2026-09-09 — Explicit source registration: a source-free Draft now registers and
selects a new human/procedural/TTS source through a typed repository operation
and actual CLI. Exact project/evidence digests, registered producer attribution,
explicit permissions, cancellation and immutable prior take provenance are
preserved. No coverage/listening PASS or unit approval is inferred. The CLI
bank-to-installed-new-score journey now uses registration instead of a prefilled
source policy. Full Release build and five final focused suites pass (23.61 s);
Python parity/admission passes 20 tests. Three new producer regressions cover
negative/preservation boundaries. Native setup, assignment migration and the
full singer/product scope remain open. No unit acceptance or Git publication;
see `SOURCE_REGISTRATION_2026-09-09.md`.

2026-09-09 — Native source-quality assessment: added background evidence capture,
explicit AppKit/Win32 decision forms and Studio controls, with no default reviewer
or PASS outcome. Full captured identity prevents stale/recaptured modal adoption;
durable receipts survive late cancellation. Six new tests cover these boundaries.
Live macOS QA recorded only a synthetic Not assessed decision, leaving zero unit
approvals and no microphone activity. QA-discovered modal focus loss was repaired
and I/Escape/I plus D/Escape/D verified; Review typography/wrapping improved.
Final full build and seven focused suites pass (23.84 s), core 832/832 and Studio
draft/source 30/30. No prior snapshot file was missing. Windows runtime, real
source/music qualification and full Beta GO remain open; no unit acceptance or
Git publication occurred. See `NATIVE_SOURCE_QUALITY_2026-09-09.md`.

2026-09-09 — Source quality assessment workflow: added actual inspect/record
CLI commands and schema-3 append-only, policy/material/evidence-bound reviewer
decisions. Old schema-1/2 bytes remain unchanged; source permissions are not
granted or rewritten. Canonical writer transitions and C++/Python parity reject
stale/changed evidence, self-review and history rewriting. Reassessment revokes
affected current unit approval without deleting history. The real CLI fixture
now progresses from unassessed source quality through recorded review, draft,
unit review, package/install and new-score Final export without original input
paths. Full build passes. A full run before final readiness/query hardening was
119/120 (321.17 s), only source closure; latest five focused suites pass (25.76 s),
core 826/826, producer 42/42, CLI 4/4 and Python parity/admission 19/19. Native
assessment controls, actual source/music qualification, privacy/disclosure and
remaining Full-Scope units are not accepted. No Git publication or Beta GO is
claimed. See `SOURCE_QUALITY_ASSESSMENT_2026-09-09.md` and the format contract.

2026-09-09 — Responsive Studio unit selection: ordinary rail/Up/Down and
Review navigation now share a serialized background read/hash/decode/analysis
path, including external manifests without a producer. Captured context,
dirty edits, cancellation, stale rejection, current-window relayout and shutdown
joining are preserved. Four new regressions pass; current core is 822/822 and
Studio draft/close/selection is 24/24. Seven rebuilt focused CTest entries pass
in 23.06 s, including actual cold CLAP bounce and Phase12B. The preceding full
119/120 run remains historical for its checkpoint. No source inputs disappeared
relative to the preceding recovery snapshot; six source/test files changed
intentionally. See `STUDIO_BACKGROUND_SELECTION_2026-09-09.md`. No new unit,
native/Windows/latency qualification, Git publication or Beta GO is claimed.

2026-09-09 — Checkpoint integration and Studio close repair: fixed the Studio
test/private-API compile error and Phase12B's stale-plan fixture sequencing.
The now-reachable Final assertions exposed and repaired copy-on-write PCM
detachment in non-stereo preview conversion and the debounce interval during
which edited Final audio remained current. Phase12B now verifies exact captured
PCM and immediate edit invalidation through 1/2/8/4 output channels. Added native
sample Save/Discard/Cancel close handling with modal-context validation,
Designer/reentrant-close protection and four controller regressions. Full
Release build passes; full serial CTest is 119/120 (255.74 s), only source
closure failing with 347 unindexed inputs at execution. Core is 818/818;
Studio draft/close is 20/20. Local recovery comparison found no missing source
files and exactly 12 intentional changed source/test files. No Git publication,
Windows/native-modal runtime qualification, new unit acceptance or Beta GO is
claimed. See `INTEGRATION_AND_STUDIO_CLOSE_REPAIR_2026-09-09.md` and its retained
execution evidence. Earlier same-date review counts remain historical.

2026-09-09 — Audit-driven implementation and actual sample publication:
replaced the linked-engine matrix with a host that loads and processes the
canonical CLAP binary. The pre-repair run failed all 336 rows on pan; the
repaired binary passes all 336 plus text-binary rejection, explicit fixture
admission, leading-silence/status, state and event-boundary checks. Fixed CLAP
ABI/mix/addressing, persistent controllers, sustain/panic and default
polyphony; legacy legato is explicitly selected only by its engine workloads.
Repaired EN/KO context admission and cross-language retention, diagnostic-only
mixed-language review, and cached fallback counts. A new review-bound sample
candidate transaction publishes real WAV/manifest/marker/provenance bytes;
its positive test reopens a saved score and renders Final audio after hiding
producer inputs, then reproduces the result from disk cache. Studio/CLI
review/publication, package/install integration and qualified resources remain
open. Full Release build passes; full CTest was 108/110 (helper test-only
startup budget and source closure). The success-path helper test now uses its
existing production request budget, with negative deadlines unchanged;
focused protocol/helper tests and a fresh 716-case core run pass. The full
suite was not re-labelled as a new run. Source closure had 278 unindexed
inputs at that snapshot and remains an integration obligation. See
`AUDIT_REPAIR_AND_SAMPLE_PUBLICATION_2026-09-09.md` for precise boundaries.
No new Full-Scope unit or Beta GO acceptance is declared.

2026-09-09 — Read-only direction re-audit correction: the earlier 107/108
CTest run is historical, not verification of the current worktree. The
canonical matrix/soak runners hash the plugin but execute a linked engine;
a plain-text negative-control plugin also produces 336 PASS cases. The
strengthened verifier now rejects these legacy summaries. English/Korean
stale-context application and Japanese-to-Korean silent rebinding were
reproduced. An unfinished CLAP ABI/test patch predating the review pause
currently prevents the strict core test build through an external-header
warning; production fixes were not performed during this read-only review.
See `DEVELOPMENT_DIRECTION_DEEP_REVIEW_2026-09-09_KO.md` at the project root
for current findings and focused results. The U35 labels on the historical
Phase 12C entries below are not Full-Scope U35 acceptance: this plan's U35
is neural dataset/training. Preserve those entries as history, not current
canonical-host or neural implementation evidence.

2026-09-09 — Direction review and canonical-slice audit: the approved
Full-Scope plan remains the correct authority. The current implementation is
architecturally on-track but not Beta GO: Release CTest is 107/108 with only
tracked-source closure failing (264 unindexed local inputs at audit time),
`seam_tests` is 695/695, the Phase 12C canonical slice is 7/7, and strict
plugin/bank-bound verification accepts the 336-case matrix plus five-second
smoke soak. The current public-domain bank is explicitly a technical fixture,
not a release singer; neural model, full qualification, target-host evidence,
and unresolved resource/empirical criteria remain blockers. Detailed Korean
review: `docs/reviews/DEVELOPMENT_DIRECTION_REVIEW_2026-09-09.md`.

2026-09-08 — U27/U28 command and transfer migration: note/lyric mutation
reconciliation, generated-phone Find, technical-edit review, performance-take
validation and copied render-edit binding now dispatch through the region
language resolver instead of assuming Japanese. Explicit hint edits validate
against the selected English/Korean/Japanese inventory; shared resolver
admission applies the existing 10,000-note, 65,536-character and 4,096-edit
bounds to every language. English and Korean hint-command/search regressions,
transfer preservation and existing Japanese suites pass. This closes the
Japanese-only application boundary but remains bootstrap vocabulary/resource
and acoustic qualification work; U27/U28/Beta GO remain open and changes are
local/uncommitted.

2026-09-08 — U45 semantic full-product report validator foundation: added a
hash-bound report reader with duplicate-key/non-finite/depth/size limits,
no-follow regular-file checks, JSON-Schema validation, candidate/contract/
profile/resource-matrix identity binding, exact R1–R20 and 83-case coverage,
case dimension and platform/host checks, review/artifact/check/operation
coverage, and empirical-cell coverage checks. The release gate now validates a
provided `fullProductReport` instead of treating its content as opaque; the
legacy “semantic validator unavailable” diagnostic remains only when the
mandatory report reference is absent. The canonical contract is still
UNRESOLVED (resource matrix and empirical criteria), and no evidence or PASS
is fabricated. Reader, symlink, digest-tamper, and CLI tests pass. This is a
U45 foundation, not U45/Beta GO acceptance; changes remain local/uncommitted.

2026-09-08 — U35 canonical Phase 12C evidence contract foundation: added a
source-bound verifier for the canonical `com.project-seam.editor` CLAP target,
production live-engine linkage, 32-voice/1,024-event/256-MiB limits, pinned
clap-validator 0.4.1, and rejection of legacy/generated fixture symbols. When
artifact inputs are supplied it additionally rehashes the exact plugin and
voicebank, requires a 336-case finite matrix with source/build and bank
identity, validates pinned validator output, and requires canonical full-soak
identity/counters. Source-only CTest is green; the later runner-promotion
entry below records the canonical matrix/smoke identity results. This is a U35
evidence boundary, not U35/Beta GO acceptance; changes remain local/uncommitted.

2026-09-09 — U35 canonical runner promotion: Phase 12C matrix and soak
runners now consume the canonical `ProjectSEAMEditor.clap` bundle and the
production-bank root under CTest, rehash both artifacts, load resources through
the production `VoicebankCatalog`/`buildTrustedResource` path, and emit bound
plugin/bank/source/build identity. The 336-case matrix and five-second smoke
soak pass the strict canonical verifier; transition fallback is counted as an
explicit valid workload when the technical bank has no exact transition unit.
The official clap-validator, full 7,200-second soak and target-platform/DAW
acceptance remain open; changes remain local/uncommitted.

2026-09-08 — U27/U28 procedural language routing: procedural snapshot
construction now uses the shared language resolver rather than hard-coding the
Japanese service. English and Korean snapshots retain their language-bound
pronunciation identities and render only when the frozen recipe explicitly
contains matching vowel/frication poses; missing recipe coverage fails closed
instead of borrowing Japanese sounds. Release performance-snapshot and core
regressions pass for English and Korean fixtures. Native-speaker resource
review, complete dictionaries, consonant coverage and Beta GO remain open;
changes remain local/uncommitted.

2026-09-08 — U27/U28 multi-voice context routing: score voice compilation
now uses the generic language resolver for per-voice pronunciation instead of
hard-coding Japanese context. Overlapping English notes retain independent
phoneme timing/voice ownership and reject stale/custom context through the
existing coverage guards. Release compiler and core regressions pass. This
does not provide reviewed multilingual singer resources or complete language
rendering qualification; U27/U28/Beta GO remain open and changes are local/
uncommitted.

2026-09-08 — U27/U28 native coverage routing: the Style/Coverage sheet and
standalone selected-region coverage command now
uses the selected bank language's generic pronunciation service. English and
Korean regions can produce structural coverage when the bank declares matching
units; a mismatched language remains an explicit diagnostic with no fallback.
Release style-coverage, standalone workflow and core regressions pass. This is inventory/phoneme
coverage evidence only, not native-speaker review, acoustic qualification or
Beta GO; changes remain local/uncommitted.

2026-09-08 — U34 offline render authority/readiness gate: added a thread-safe
Final-only `OfflineRenderSession` that binds a host bounce to the exact project
digest, timing-authority digest, sample rate and render ABI. CLAP offline-mode
selection now prepares and waits for a current Final publication before
activation; process() emits bounded silence while offline audio is pending or
failed, and score/project/host-timeline changes invalidate the gate. The
session rejects stale completions, empty PCM and malformed provenance. Release
offline-session and Phase 12B integration tests pass. This is a non-realtime
preparation/readiness boundary, not complete Follow Host tempo-map capture or
installed DAW tuple qualification; U34/Beta GO remain incomplete.

2026-09-08 — U8 renderer capability and Raw trajectory repair: added an
explicit render-control capability contract for pitch, timing, dynamics,
vibrato, attack, release and advanced controls. Required unsupported controls
now fail before backend work and Raw fallback cannot claim pitch-preserving
transients. Raw rendering now applies an explicit bounded pitch curve on its
sustain trajectory, and the curve is included in render identity with a bumped
renderer revision. Release capability, synthesis-quality, Phase 12B and core
suites pass. This remains a capability/DSP correctness boundary, not acoustic
or listener qualification of every renderer or the full U8 acceptance.

2026-09-08 — U38 automatic-performance proposal foundation: added a
side-effect-free deterministic proposal generator bound to region,
performance-revision, pronunciation, resource, generator and seed identity.
It emits bounded Pitch/Dynamics/Attack/Release lanes as an unaccepted
`PerformanceTake`, and feeds the existing stale-safe proposal/accept commands.
Stale inputs, cancellation, duplicate channels and advanced channels without a
qualified generator fail before mutation. Release U38, proposal-command,
performance-job and compiler suites pass. This is lifecycle/contract evidence,
not a trained neural singer, acoustic qualification or full U38/U40 acceptance.

2026-09-08 — U30/U32 native USTX and interchange lifecycle: replaced the study-only YAML bridge boundary with a bounded native USTX 0.9 parser/encoder and typed project conversion. The parser rejects aliases/tags, multiple documents, duplicate keys, inconsistent indentation, non-finite values and hostile collection/scalar sizes before growing typed state; tempo/meter, note timing, pitch-in-milliseconds, vibrato, track controls and explicit conversion losses are retained. Added a stateless create-new file service with source digest, unsaved-draft preparation, explicit review callback and accept/reject replacement semantics, plus native Open USTX/MIDI and Export Score menu/file-dialog commands. Release USTX, service, SMF and core suites pass; this remains subset/loss-review evidence, not full USTX interoperability, native panel visual proof, or installed host acceptance. Details: `MIDI_INTERCHANGE_V1.md` and new USTX APIs under `libs/seam-interchange/`; U30/U32/Beta GO incomplete.

2026-09-08 — U37 neural deployment contract foundation: added a versioned little-endian length-prefixed worker frame with bounded JSON metadata, Float32 payload validation, exact model/pronunciation identity, shape/rate/channel limits and stale-response rejection. Added a model contract binding model/vocabulary hashes and runtime shape, an explicit-helper runner using the existing non-shell bounded process boundary, and a real probe process that exercises request→helper→response IPC. The helper runner now opts into an explicit bounded framed stdin ceiling while retaining the historical 4 KiB dictionary default. Release protocol/helper tests pass; this is contract and crash/resource-boundary evidence, not a trained/qualified neural singer, ONNX runtime, signed payload or Beta acceptance. Details: new `libs/seam-neural-synthesis/`; U35/U36/U37/Beta GO incomplete.

2026-09-08 — U40 harmony workflow foundation: added deterministic, side-effect-free interval harmony preparation with explicit source-note selection, MIDI range checks, copied lyric/language identity, source-vibrato preservation and fresh note/token IDs. `AddHarmonyCommand` applies a prepared layer atomically, rejects stale source revisions/identity collisions, and restores exact note/lyric state through undo/redo. Release focused harmony tests pass; native harmony review/scale constraints, neural generation and independent creator qualification remain open. U40/Beta GO incomplete.

2026-09-08 — U41 character-state binding repair: native editor scene semantics now derive Character 01 runtime states from verified voice identity and render lifecycle (warning, rendering, complete/error, focused/neutral) instead of leaving artwork permanently neutral/focused. The host-owned presentation selects the corresponding bundled state asset; focused native UI regression passes. Actual production asset rights, mouth/phoneme synchronization and installed-platform qualification remain open. U41/Beta GO incomplete.

2026-09-08 — U26 native review and Apply surface: bound the owner-thread Japanese reading job to the native replacement-review panel with preparing/ready/failure states, paged contextual-token rows, wrapped read-only detail, accessibility root/actions, explicit Apply/Cancel/Retry controls and a standalone Edit-menu command. Apply is now UI-gated by the same immutable single-note/known/no-hint ownership plan used by the command, then persists per-note phone hints plus pronunciation identity through one undoable performance transaction. Host injection points were added for a verified/staged resource in standalone and CLAP runtimes; no PATH/working-directory discovery or fake production dictionary is used. A focused native test covers real staged helper execution, accessibility row/detail navigation, Apply, JSON-visible state and exact undo/redo. Release focused native/Japanese/helper and core tests pass; this remains resource-injection and fixture evidence, not a shipped reader or native-language approval. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U27/U28 language service foundation: added bounded English and Korean phonemizers with explicit phone-hint precedence, stress-bearing English vowel symbols, bootstrap lexicon diagnostics, Hangul syllable decomposition, coda liaison and continuation handling. Versioned English/Korean resolver identities bind generated tokens to region/note/lyric context addresses; generic inspection now selects the explicit language service, and sample snapshots can resolve against an English or Korean voicebank language instead of hard-failing Japanese-only. The later command/transfer migration entry above extends this routing through mutation and persistence boundaries. Five focused language cases pass Release and are included in core. This is a rule-backed bootstrap vocabulary, not native-speaker-reviewed language resources or Beta acceptance. Details: `LANGUAGE_PHONEMIZER_2026-09-08.md`. Changes local/uncommitted; U27/U28/Beta GO incomplete.

2026-09-08 — U31 bounded SMF codec foundation: added Type 0/1 PPQ Standard MIDI import/export with pre-allocation chunk/VLQ limits, running status, FIFO equal-key overlap pairing, tempo/meter/lyric/text retention, deterministic output ordering, missing-note diagnostics and explicit loss records for unsupported events. SMPTE, malformed status/data, invalid UTF-8, hostile lengths, trailing bytes and invalid score fields reject without mutating caller state. Four focused cases pass Release and the same codec test is included in core. Details: `SMF_INTERCHANGE_2026-09-08.md` and `../formats/MIDI_INTERCHANGE_V1.md`. Native conversion lifecycle, USTX, held-file boundary and external-DAW interoperability remain open; U31/Beta GO incomplete.

2026-09-08 — U33 live-expression repair: separated CLAP vibrato and pan from timbre, added per-voice equal-power stereo panning with channel-10 MIDI support, a bounded 5.5 Hz vibrato pitch LFO and realtime-safe timbre/brightness shaping. Live output no longer duplicates one mono sum into every channel; pressure remains the amplitude control. Core CLAP/live regressions prove pan energy separation, centered stereo output and nonzero vibrato delta while existing allocation/live smoke tests pass. This is algorithmic correctness evidence, not installed-host mapping or acoustic/listener qualification. Changes local/uncommitted; U33/Beta GO incomplete.

2026-09-08 — U26 owner-thread reading worker: added serialized `JapaneseReadingJob` with immutable capture, background jthread helper/decoder/binding, request IDs, cancellation retirement, terminal diagnostics and current-result guards for region/revision/full project/performance generation/resource identity. Real staged helper fixture proves cross-note 学校 ownership and explicit-hint protection; concurrent start, cancel/restart, missing staged file and stale replacement cases pass. Release/Debug focused Japanese targets pass (0.80/0.51 s); Release core target previously 650 cases (16.54 s), builds/diff checks pass. No score mutation/UI Apply; latest-request orchestration, resource limits and host qualification remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 captured contextual reading and ownership: added immutable phrase assembly with exact lyric/note spans, shared-melisma reuse, rest separators and explicit-hint flags. Typed tokens bind to all owning notes and flag lyric/hint/unowned crossings without inventing mora allocation or applying commands. Current-document/resource/generation checks reject stale adoption. Real staged-reader probe resolves 学校 across two score notes, preserves hint protection and verifies no score/history mutation in R/D. All 650 Release core cases pass (16.54 s), 14 Japanese cases pass R/D (0.51/0.55 s); builds/diff checks pass. Latest-request worker lifecycle, helper CPU/memory limits, review/Apply/persistence and host/native-language acceptance remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 private verified resource staging: added independent streamed/hash-checked copies in a private directory, read-only publication permissions, shared consumer lifetime and fixed-file descriptor-relative cleanup. In-place original truncation/replacement does not alter staged bytes; last-owner cleanup preserves originals. Actual staged helper/dictionary -> stdin -> typed-decoder probes pass R/D. All 649 Release core cases pass (16.37 s), 13 Japanese cases pass R/D (0.38/0.41 s); builds/diff checks pass. This is not kernel immutability or protection from owner/root tampering; crash pruning, disk budgets, memory/CPU limits, supervisor and source-request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 reader resource verification: added exact helper/four-file dictionary integrity checks, canonical paths, bounded streaming hashes, no-follow/nonblocking regular-file verification on POSIX, extra-configuration rejection and repeat validation. Reading identity now includes the helper binary digest plus engine/dictionary identity. Real transport probe uses verified metadata and checks resources before/after execution; R/D pass against the pinned dictionary. Tests reject tampering, extra dicrc, oversized files, symlinks and FIFO. All 648 Release core cases pass (15.68 s), 12 Japanese cases pass R/D (0.48/0.42 s); builds/diff checks pass. Point-in-time verification is not immutable/race-free execution or release trust; staging, memory limits and request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 private stdin transport: added a 4096-byte opaque stdin channel with nonblocking input/output polling, exact EOF delivery and per-socket/send SIGPIPE suppression. Process tests cover UTF-8/binary bytes, boundary/overflow, simultaneous output, empty EOF, early exit and non-consuming timeout. Updated real MeCab probe/checker to stdin; four dictionary hashes, three reading/span fixtures and three admission checks pass. New explicit transport probe passes runner -> real dictionary -> bounded decoder -> typed validation in R/D, with synthetic identity clearly not attestation. Two process cases pass R/D (1.18/1.59 s), all 647 Release core cases pass (16.11 s); builds/diff checks pass. Resource/config verification, memory limits, host supervision and request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 helper process lifecycle primitive: added explicit-path/no-shell worker execution with clean environment, stdin/descriptor isolation, separate bounded nonblocking stdout/stderr, deadline/cancellation, process-group cleanup and direct-child reaping. macOS tests cover actual overflow, failures, timeout, descendant-held pipes, cancellation and injected environment/non-CLOEXEC descriptors; focused R/D passes (1.09/1.23 s), all 647 Release core cases pass (16.07 s). Builds/diff checks pass. This is not a verified Japanese helper service or sandbox: memory/CPU limits, resource/config verification, lyric stdin transport, exclusive plug-in-host reaping ownership, Windows/Linux qualification and stale-request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 bounded reading response decoder: added authoring-layer version-1 JSON decoding with explicit byte/depth/node/string/collection limits, exact schema/type/source checks and final typed span/identity/expansion validation. Tests cover matching wire data, 12 invalid mutations, duplicate/concatenated/deep/oversized JSON and cancellation; live probe fields match the decoder fixture. All 647 Release core cases pass (16.76 s), 11 Japanese cases pass R/D (0.52/0.55 s); native/core/focused builds and diff checks pass. No reusable production subprocess runner exists in the inspected libraries; verified helper launch, bounded I/O, process lifecycle and stale-request adoption are next, before resolver/editor integration. Caller-supplied identity is not JSON attestation. Details: `JAPANESE_READING_INTAKE_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 typed reading boundary: added resource/source-bound reading DTOs and validation for UTF-8-aligned exact spans, unknown/missing states, count/text/expansion limits and cancellation. Development probe now emits native MeCab node spans as JSON, eliminating surface/CSV ambiguity; third observed fixture covers comma/quote/ideographic whitespace and exposes isolated 歌→カ ambiguity without claiming native approval. Four dictionary hashes, three real reading/span fixtures and two admission checks pass. All 645 Release core cases pass (16.62 s), nine Japanese cases pass R/D (0.37/0.40 s); strict probe/scoped builds and diff checks pass. Production bounded decoder, isolated execution, editable readings, owner mapping and resolver/UI integration remain open. Details: `JAPANESE_READING_INTAKE_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 real reading dependency intake: cloned/pinned Open JTalk fork `462fc38e7520aa89e4d32b2611749208528c901e`, inspected MeCab/NAIST/UniDic-CSJ notices, built the front-end library and complete UTF-8 dictionary outside the repo, and added a development probe plus hash-pinned unreviewed reading fixtures/checker. Actual kanji/particle readings and unknown-reading behavior pass; four dictionary hashes and two input-admission checks pass. No acoustic model is required. Source review identified unbounded lattice allocation/no cancellation and ambiguous convenience CSV output; typed node spans and isolated bounded execution are the next integration requirements. No production resolver or shipping dependency change yet. License auditor fails only the existing master-branch policy; no bypass. Diff check passes. Details: `JAPANESE_READING_INTAKE_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 kana normalization: added targeted half-width kana/voicing-mark and decomposed dakuten/handakuten handling while preserving stored surface text and original scalar warning offsets. Explicit phone hints retain precedence; unknown readings are not guessed. Mapping facts checked against Unicode 17.0; phonemizer revision advances to 4 and existing source-digest identity changes. Two new shared-resolver cases and a focused Japanese target pass; Release core target passes (16.28 s), focused R/D (0.37/0.40 s), native/core/focused builds and diff checks pass. Dictionary intake, contextual readings, chunk/revision qualification and native-language review remain open; U25 qualification is not waived. Details: `JAPANESE_NORMALIZATION_2026-09-08.md`. Changes local/uncommitted; no U26/Beta GO acceptance.

2026-09-08 — U25 immutable bank snapshots: added owner-only immutable resolution snapshots, bounded one-entry session caching and invalidation on reference/track, catalog refresh and actual trust-policy changes. Failed refresh makes the snapshot path unresolved. Both native hosts use snapshot identity for routine sheet checks while retaining full project/context guards and full manifest/trust validation on Apply. Real signed 256-style/16,384-unit fixture covers reuse, invalidation, direct edits, refresh failure/recovery and exact Apply/undo; audio/export regression uses the new path. Release view construction is about 0.015 ms versus 6–7 ms by-value in this fixture. All 641 Release core cases pass (16.20 s); eight style cases pass R/D (2.70/22.64 s), 23 export cases pass R/D (2.09/11.53 s). Builds/diff checks pass. Initial preparation/weighted-query and live-host latency remain open. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 style inventory/choice performance: replaced per-style full unit scans with a single inventory index and cached pronunciation/work admission for the immutable draft; repeated same-style choices retain coverage while making provenance explicit. A 256-style/16,384-unit regression validates counts, choice correctness and source preservation. Focused Release prepare/choice/1000-repeat/view-average timings are 5.690/0.314/0.007/6.050 ms; Debug 49.280/4.588/0.262/47.495 ms. View copying/comparison remains a measured target; no stale/trust checks were relaxed. All 641 Release core cases pass (14.14 s), eight focused cases pass Release/Debug (0.55/1.72 s); scoped builds/diff checks pass. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 installed-bank style audio/persistence verification: added a real signed/installed two-style synthetic bank regression through native sheet selection, strict host-style resolution, Preview/Final rendering and shared cache. Draft PCM stays unchanged; Apply selects the correct different unit/audio with one exact style-only edit. Fresh catalog scan plus cold render, JSON reload, Float32 master/stem export and undo/redo reproduce expected PCM. All 640 Release core cases pass (13.98 s); 23 export cases pass Release/Debug (2.58/10.47 s); scoped builds and diff checks pass. This is synthetic sample-bank source-selection evidence, not shipping singer/listening, procedural/neural style or live host state qualification. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 coverage issue inspection: added paged issue list and complete wrapped detail view with note/phone/style/pitch/diagnostic/witness identity, unavailable-coverage explanation, original-page restoration and preserved draft choice. Browsing is read-only; stale source/action guards and explicit Apply/Cancel remain intact. Eight-issue pagination and unavailable-language regressions pass; all 639 Release core cases pass (14.10 s), seven focused cases pass Release/Debug (0.42/0.54 s). Scoped builds/diff checks pass; inspected 480×320 detail raster fits without overlap. Long style-ID inspection outside issue details, large-bank latency and acoustic/live-host qualification remain open. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 initial native Style/Coverage sheet: connected the captured model to paged native rows, explicit Apply/Cancel/Refresh, stale semantic/source guards, current standalone/embedded voicebank resolvers, standalone Edit menu and shared STYLE inspector button. Tests cover pointer entry, draft isolation, changed trust/source, exact undo/redo, page boundaries and menu dispatch. All 637 Release core cases pass (20.83 s); five focused cases pass Release/Debug (6.01/6.31 s); scoped builds and diff checks pass. Inspected 480×320 raster fits without row/button overlap. Detailed coverage-issue navigation, full visual long-text inspection, large-bank UI latency and live host/audio qualification remain open. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 Style/Coverage model: added the planned model files with captured track/region/project/bank identity, declared-order enabled/disabled inventory, explicit style choice, bounded Japanese structural coverage and one canonical style-only Apply/undo group. Missing choice/coverage is not promoted to readiness; procedural tracks reject the inactive sample-bank style route. Three cases prove exact undo/redo/no-op, stale/trust/hash/manifest rejection, cancellation and unavailable coverage/admission behavior. All 635 Release core cases pass (13.77 s); focused cases pass Release/Debug (0.50/0.42 s); native/core/focused builds and diff checks pass. This is the model, not a painted/host-wired sheet or audio/host approval. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 Style/Coverage prerequisite repair: found and removed false complete coverage from the union of overlapping unit spans. Analyzer now computes maximum non-overlapping coverage, reports sequence conflicts, checks all notes in a candidate span and propagates disabled/style/pitch/orphan diagnostics to interior phones. Note-pitch indexing and bounded per-token witness storage avoid retaining all candidate combinations. Two new regressions and stricter existing counts pass; all 632 Release core cases pass (14.38 s), and 16 U3 cases pass Release/Debug (0.51/0.61 s). Native/core/U3 builds and diff checks pass. Structural coverage is not renderer/source/trust/acoustic approval; the Style/Coverage sheet remains unimplemented. Details: `VOICEBANK_COVERAGE_CORRECTNESS_2026-09-08.md`. Changes local/uncommitted; no additional roadmap/Beta GO acceptance.

2026-09-08 — U25 connected measured-output inspector: bound standalone/embedded controllers to current render coordinators and added channel-cycling read-only RMS dBFS mode with numeric RMS/sample-peak/full-scale summaries and accessibility content. Exact source/request/region/frame-window keys drive cancellation/retirement/replacement; held publication metadata prevents mixed-source display. The real-render native regression proves nonzero-region frame origin, tempo-aware placement, zoom clearing, channel keyboard focus, unchanged score/history, 48→96 kHz same-revision replacement and stale-document suppression. Final verification: 630 Release core cases pass (14.05 s); focused workflow passes Release/Debug (0.53/1.03 s); native/core/focused builds and diff checks pass. Inspected 480×320 view fits. An earlier elapsed-time outlier coincided with verified system sleep. This is rendered output, not live device/microphone or isolated singer/F0 measurement; live host/input/listening and remaining U25 qualification stay open. Details: `MEASURED_DYNAMICS_INSPECTOR_2026-09-08.md`. Changes local/uncommitted; no Beta GO acceptance.

2026-09-08 — U25 document-bound measured-audio worker: ready render publications retain actual immutable project input; new capture binds that snapshot, PCM/request identity and editor session generation. Added owner-thread single-worker measurement lifecycle with guarded adoption/current reads, retirement-before-restart, cancellation discard and failure cleanup. Tests distinguish identical new sessions from revision-incrementing replacement, reject direct mixed-audio input changes, and verify stale/cancelled/invalid worker results never become current. All 629 Release core cases pass (13.54 s); 19 coordinator cases pass Release/Debug (1.81/6.37 s); native/core/coordinator builds and diff checks pass. Measured trace/controller wiring, frame-origin/stream-role presentation, viewport request lifecycle and live host qualification remain open. Details: `MEASURED_AUDIO_LEVELS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 measured-source identity: added project/request/coordinator-scoped identity to render publications, current-ready acquisition and source matching across quality/rate/channels/PCM storage. Same-revision resubmissions, cancellation and failed requests invalidate measurement-source eligibility without removing retained playback. Tests cover real renders, cross-coordinator collisions, mutated copied PCM and altered metadata. All 627 Release core cases pass (13.82 s); 17 coordinator cases pass Release/Debug (1.64/5.88 s); native/core/coordinator builds and diff checks pass. These are point-in-time in-process source guards, not document-generation binding, cryptographic provenance or an implemented measured overlay; worker/session/UI integration remains open. Details: `MEASURED_AUDIO_LEVELS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 strict measured-audio backend: added bounded read-only interleaved PCM window analysis with absolute frame bins, per-channel RMS/sample peak/full-scale counts and explicit silence dBFS semantics. Nonfinite samples reject rather than being substituted; work/output and stop checks are bounded. Three focused cases cover anti-phase stereo, headroom, frame partition/energy, invalid input and cancellation/budget admission. Native dynamics Final-render integration proves quarter-gain RMS/peak scaling and exact measured replay after project reload. All 626 Release core cases pass (14.91 s); three measurement cases pass Release/Debug (0.42/0.45 s), and 22 export cases pass Release/Debug (3.55/10.74 s); scoped builds and diff checks pass. This is the backend, not an inspector overlay: source/provenance binding, stale-result protection, off-thread publication and live host/audio measurement remain open. Details: `MEASURED_AUDIO_LEVELS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 horizontal dynamics point dragging: added Shift-drag time editing with gesture-start axis locking, captured viewport tick mapping, integer rounding/clamping and gain preservation. Existing collision rejection and explicit point/region commit stages remain intact. An extreme synthetic fixture exposed the distinction between standalone point validation and region-duration admission; draft/model field validation now rejects out-of-region times before staging, while a pure mapping test covers 64-bit arithmetic separately. Three regressions prove collision recovery, exact Apply/undo/redo, zoomed bounds, nonfinite rejection and early invalid-field feedback. All 623 Release core cases pass (13.81 s); fourteen focused cases pass Release/Debug (0.80/2.91 s); native/core/focused builds and diff checks pass. Live OS/host gestures, measured audio and remaining U25 scope remain open. Details: `DYNAMICS_TIME_NAVIGATION_2026-09-08.md`. Changes local/uncommitted; no Beta GO acceptance.

2026-09-08 — U25 viewport-adaptive dynamics sampling: added validated/clipped display windows and cached compiled voices, so zoom/pan/Fit increases local target/generated sample detail without recompiling unchanged score intent. Native-curve edits invalidate the cache and resample the staged viewport. Expanded tests prove the exact tick-level generated/manual transition, denser native zoom output, invalid/out-of-region window safety and recovery. Cached resampling in the 4096-note/16-voice fixture took 0.193 ms Release / 2.424 ms Debug for 368 samples. All 620 Release core cases pass (13.76 s); eleven focused cases pass Release/Debug (0.90/2.97 s); native/core/focused builds and diff checks pass. These remain score-only samples, not continuous/sub-tick or measured audio; metadata-heavy latency and live host qualification remain open. Details: `DYNAMICS_TIME_NAVIGATION_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 dynamics time navigation: added overflow-safe plot-local zoom/pan/Fit, visible and accessible navigation controls, keyboard/wheel routing, exact native boundary interpolation and viewport filtering for handles/generated/target markers. Point paging now uses two rows to fit compact navigation. Tests prove 64-bit range safety, pointer-anchored zoom, stale action/source rejection and unchanged score/history/main timeline. All 620 Release core cases pass (14.19 s); eleven focused cases pass Release/Debug (1.09/3.21 s); native/core/focused builds and diff checks pass. Inspected 480×320 render fits without overlap. Generated/target samples are not yet recomputed at zoom-dependent density; horizontal point dragging, measured audio and live host gesture qualification remain open. Details: `DYNAMICS_TIME_NAVIGATION_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 target capacity/latency audit: exposed the allocator's existing 4096-note bound, preflighted target refresh before a large project copy, and made the exact unavailable diagnostic visible. Added 4096-note/16-voice coverage regression (12432 samples covering every note) and 10000-note model/native editing proof with explicit target unavailability and exact Apply/undo. Final focused capture/compile timings: Release 0.419/8.093 ms, Debug 2.447/85.579 ms; simple fixture only, not metadata-heavy or whole-window qualification. All 618 Release core cases pass (13.81 s); nine focused cases pass Release/Debug (0.88/2.97 s); native/core/focused builds and diff checks pass. No renderer bounds were relaxed; target support above 4096 notes and worst-case latency remain open. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 selected-generated dynamics overlay: added inspection-only compiler evaluation that exposes accepted Dynamics before manual replacement, while retaining ordinary audio evaluation and the existing scope/offset/tempo/interpolation rules. Cached preview samples now draw generated orange markers separately from cyan post-ownership target samples. Generated zero remains visible; malformed null Dynamics is rejected. All 617 Release core cases pass (13.85 s); 18 separate compiler cases pass Release/Debug (0.89/4.13 s), and eight dynamics cases pass Release/Debug (0.65/0.54 s). Native/core/focused builds and diff checks pass; inspected 480×320 render shows the ownership difference without overlap. This is active-note, selected-generated sampled inspection, not all stored proposals, measured audio, continuous coverage or live host qualification. Details: `SELECTED_GENERATED_DYNAMICS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 compiler-backed dynamics target overview: added cached per-voice sampled targets from production `compileScoreVoices` on the captured score plus staged native curve, shown as cyan dots separate from native source/draft lines and the unsaved point. Preview refresh occurs on inspector open/point staging, not paint/drag; mutations invalidate old samples, and compiler failures report target unavailability without blocking native edits. Tests verify compiler parity, generated/manual precedence, polyphonic note/voice identity and capacity-failure behavior. All 617 Release core cases pass (14.48 s); eight focused cases pass Release/Debug (0.82/0.67 s); native/core/focused builds and diff checks pass. Inspected 480×320 target render fits. This is a synchronous, bounded, 48 kHz score-only sampled overview—not continuous coverage, raw generated/measured lanes, final audible amplitude, worst-case latency or live host qualification. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; U25/Beta GO remain incomplete.

2026-09-08 — U25 generated/native dynamics influence feedback: captured accepted-Dynamics/manual-Replace scope counts, visible override warning and accessible compiler-precedence explanation without silently claiming ownership. New native editing regression uses the actual performance compiler: after native gain becomes 0.25, generated-controlled tick 1200 remains 0.8 while manually owned tick 1680 becomes 0.25; whole-project preservation and undo/redo are exact. All 616 Release core cases pass (13.95 s); seven focused dynamics cases pass Release/Debug (0.77/0.61 s). Native/core/focused builds and diff check pass; inspected 480×320 warning/count render fits without overlap. This is scope feedback and compiled-control evidence, not generated/target/measured visualization, ownership editing, live acoustic/host or Beta GO qualification. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; U25 remains incomplete.

2026-09-08 — U25 dynamics audio/persistence integration: added graphical/field native editing → explicit draft/region Apply → persisted recipe-file resolution → Final rendering → project reload → committed Float32 master/stem export regression. Draft PCM remains unchanged; gain 0.25 produces expected quarter-amplitude samples and one-sixteenth energy within explicit tolerances, while preserving vibrato/project intent. Reload/export and undo/redo reproduce exact expected PCM. Extended embedded gesture regression through the CLAP editor-state codec and a fresh runtime: draft bytes stay unchanged and applied dynamics round-trip exactly. All 615 Release core cases pass (28.03 s); 22 export cases pass Release/Debug (5.01/23.50 s); scoped builds and diff check pass. Evidence is procedural constant-curve audio and codec/runtime persistence, not live playback/listening, classical/neural parity, arbitrary multi-point acoustic qualification or real host state-stream/OS matrix approval. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 graphical dynamics editing: added distinct applied-score/staged-curve/unsaved-point plot, three-row paging, graphical gain handles with nearest-point hit testing, exact keyboard/semantic field access and explicit point/region commit stages. Dragging clamps native gain and rejects stale documents, nonfinite vertical coordinates and changed geometry; missing held-button state retires a lost drag. Fixed embedded review routing to forward move/up while preserving background technical-edit isolation; added an in-process embedded gesture/Apply regression. All 614 Release core cases pass (13.39 s); six focused dynamics workflow cases pass Release/Debug (0.40/0.50 s). Release native app/core and Release/Debug focused builds pass; diff check passes. Inspected 480×320 raster fits point rows, legend, curve and actions without overlap. Actual generated/target/measured curves, horizontal/time navigation, this workflow's audio/persistence/plugin-state and live host qualification remain open. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 connected dynamics point inspector: added compact region-curve list and separate tick/gain fields, draft add/move/delete, explicit Save to draft then Apply region curve, Back/Cancel/Refresh, stale input/action guards and one exact undo/redo. macOS Edit menu and shared track-inspector pointer/semantic entry are wired; the latter retains the existing character-Off/available-dock visibility policy. Added planned `test_dynamics_lane_workflow.cpp` with three focused native cases and standalone menu-dispatch coverage. Final Release core rerun passes all 610 cases (20.80 s); three focused cases pass Release/Debug (0.61/5.29 s). Earlier in this checkpoint 33 lifecycle cases pass Release/Debug (0.30/1.22 s). Release native app/core, Release/Debug focused builds and diff checks pass. Inspected 480×320 raster has no point-row/action overlap. This is point-form/controller evidence, not graphical handles, full curve visualization, live IME/VoiceOver, audio/persistence/plugin-state or host qualification. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 dynamics lane model: added captured region-scoped add/update/move/delete/reset drafts with atomic collision/invalid-input rejection, domain capacity/interpolation semantics, guarded explicit Apply and one exact undo/redo command. Note selection does not redefine the continuous region curve; generated takes and ownership are not implicitly replaced. Two new cases exercise draft isolation, interpolation, invalid/missing/colliding points, full-capacity moves, reset/no-op, stale generation, cancellation and whole-project preservation. All 607 Release core cases pass (25.93 s); 30 creator cases pass Release/Debug (1.24/6.84 s); builds and diff check pass. Native lane handles, semantic/input integration and this path's audio/persistence/host qualification remain open. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 vibrato audio/persistence integration: added native seven-field editing → saved recipe-file resolution → Final procedural rendering → project save/reload → committed Float32 master/stem export regression. Drafts leave PCM unchanged; Apply changes expected vibrato only and materially changes finite same-length audio. Reload/WAV export reproduce edited PCM exactly, and undo/redo reproduce original/edited projects and PCM exactly. All 605 Release core cases pass (13.50 s); 21 export cases pass Release/Debug (3.11/9.70 s), builds/diff checks pass. This is a procedural fixture and codec/export path, not physical playback, listener/singer qualification, native file dialogs or plugin/host parity. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 native vibrato form: connected the captured draft to an expanded right-anchored inspector, existing vocal-inspector entry and Edit menu command. Seven fields use two pages and the shared native field editor; input changes drafts only, with mixed placeholders, invalid-fade recovery, selected-field reset, Refresh/Cancel and one explicit Apply to Selection. Shared review geometry supports 480×320 and normal dock sizes without changing other review layouts. Added source/selection/generation and semantic-interaction checks; keyboard test uses emitted row IDs and captures an enabled old Apply target to prove stale rejection. All 604 Release core cases pass (13.63 s); focused inspector tests pass Release/Debug, strict builds/diff checks pass. Inspected 480×320 render `/tmp/seam-vibrato-ui.zEIyDk/inspector.png`; shortest round-trippable numbers avoid unnecessary float digits. Direct handles/curve layers, capability feedback, live IME/host and audio/persistence qualification remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 existing inspector geometry repair: aligned painting, pointer routing and semantics through shared inspector visibility/top calculation, full dock-bottom overlay accounting and visible track/region bounds. Hidden inspector controls no longer remain actionable. Corrected top-left-versus-baseline hit-box offsets and split painted Mute/Solo labels into their actual target columns. Native tests verify short/normal heights with diagnostics, both pointer toggles with exact undo, and crowded-dock rejection of hidden targets. All 602 Release core cases pass (13.17 s); strict Release app/core and Debug native-UI builds/diff checks pass. Inspected system-font capture `/tmp/seam-inspector-layout.DhI155/inspector.png`. Existing visibility policy is retained; long-list reveal/scroll, vibrato field rendering/controller integration and full live/host qualification remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 inspector draft/input state: added seven-field vibrato form state with explicit mixed placeholders, exact float text round-trips, bounded full-value parsing, retained invalid drafts and coupled-fade correction before Apply. Field reset preserves source-specific values; active region, selection, revision/generation and closed-state guards prevent stale publication. Two focused inspector cases pass Release/Debug (0.57/1.00 s); all 600 Release core cases pass (13.21 s), strict builds and diff checks pass. This is the native inspector's draft model, not painted/controller-connected controls. Geometry, semantic/IME/host/audio integration and full U25 acceptance remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no Beta GO acceptance.

2026-09-08 — U25 large-selection performance editing: added exact 10000-note vibrato Apply/undo/redo regression, measured repeated note scans, then indexed requested expression targets per live/staged command state. Local Release Apply decreased 105.08→4.52 ms; no canonical validation, hint reconciliation or undo behavior was bypassed. Missing/repeated/ambiguous requested IDs reject atomically. All 598 Release core cases pass (12.97 s); 28 creator cases pass Release/Debug and 18/5 dedicated performance-command/edit-preservation cases pass. Strict builds/diff checks pass. Native inspector/handles, audible/persistence/host and worst-case latency qualification remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 mixed-selection vibrato model: added source-bound inspection and partial-field patches for every canonical vibrato parameter. Mixed values remain distinct; absent patch fields preserve each note's settings. Per-target domain validation rejects coupled fade conflicts atomically. Explicit Apply uses one canonical performance command and preserves hints, ownership, pitch/dynamics, unit/seam edits and unselected notes; undo/redo, no-op, selection/generation drift and cancellation are covered. Compiler regression confirms changed modulation/frequency inside the edited note, not acoustic acceptance. All 596 Release core cases pass (13.25 s); 26 focused cases pass Release/Debug (1.27/4.16 s), strict builds and diff checks pass. Native controls/handles, host/audio qualification and maximum-size Apply latency remain open; U23/U24 prerequisites are not implicitly accepted. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — Broad Release integration refresh: rebuilt all default targets (86 steps) and ran all 90 CTest targets. Initial run passed 88 and exposed five performance-snapshot fixture mismatches plus source closure. The shared fixture retained an explicit `a` hint while tests changed surface lyrics; corrected CV/multi-nucleus input setup without weakening renderer guards, added positive hint-precedence checks and strengthened a masked continuation test with an isolated-phrase counterexample. All 41 snapshot cases pass Release/Debug. Final full rerun passes 89/90 targets in 54.98 s: the sole failure is source closure for 146 unindexed required inputs. All functional targets, 595 core cases, singing-quality workflow, allocation/recovery/export/migration and CLAP host smoke pass together. No staging, commit, exclusions or gate changes. Native GUI tests are disabled and no release acceptance is implied. Details: `BROAD_RELEASE_REGRESSION_2026-09-07.md`. Changes local/uncommitted; full goal remains active.

2026-09-08 — Long diagnostic failure preservation: replaced render-message concatenation into a 128-byte key with stable keys plus a 4096-byte display-safe detail field. Generic error mapping now retains detail too. UTF-8 boundaries, visible byte escapes and truncation flags preserve readable bounded output; original-input digests prevent different truncated tails from coalescing. Detail participates in source identity/search and native inspection/explicit Copy Diagnostic, but the automatic support-field allowlist is unchanged. Regression covers boundaries, malformed bytes, long failure visibility/search, distinct aggregation/staleness and native reconstruction. All 595 Release core cases pass (13.56 s); 13 focused diagnostic cases pass Release/Debug (0.39/0.99 s), strict Release app/core and Debug diagnostic/native builds and diff checks pass. Hashing remains proportional to supplied text; full producer inventory, live copy/support/IME/host qualification and release gates remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native active-diagnostic Find: connected background diagnostic search to a dedicated Edit entry and the shared Find field cycle. Global issues work without a vocal region. Paginated full inspection exposes scope/references/severity/count/actions as read-only text, with keyboard/accessibility input, stale source/document guards, Refresh and cancellation; no note selection, recovery execution or history mutation. Tests cover global and long-CJK cases, result/detail pages, independent diagnostic drift, document replacement and no callbacks. All 593 Release core cases pass (13.74 s); strict Release app/core and Debug native-UI/platform builds and diff checks pass. Inspected system-font 480×320 capture `/tmp/seam-diagnostic-find.wdVito/detail.png`; trimmed paint-only line terminators to remove misleading ellipses while retaining full semantic text. Producer-wide diagnostic completeness/scope metadata, live menu/IME/host qualification and worst-case latency remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 diagnostic-search job/review: added bounded entry capture without panel callbacks, a single immutable-data worker, cancellation/discard, retire-before-restart admission and owner-thread publication. Reviews retain opaque document generation/revision and diagnostic content/counts; guarded lookup returns a diagnostic index only, rejecting stale/closed/missing targets without invoking recovery or changing notes. Tests cover lifecycle, independent diagnostic drift, same-content document replacement, score edits, invalid superseding input and worker UTF-8 failure. All 591 Release core cases pass (13.19 s); 11 focused diagnostic cases pass Release/Debug (0.66/1.21 s), strict builds and diff checks pass. Native Find controls/result presentation/navigation, interaction-ID guards and worst-case capture/publication/host qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 read-only active-diagnostic search model: added bounded immutable diagnostic snapshots with independent field matching, Unicode offsets and source/field provenance. Global/opaque-ID issues remain diagnostics, not guessed note targets; search never executes recovery actions. Full-content/count matching detects diagnostic changes independently of score revision. Tests cover visible text/codes/actions/CJK, source preservation, count/dismissal drift, cancellation and malformed/oversized later fields even after an early hit. All 589 Release core cases pass (13.29 s); the new 9-case focused diagnostic suite passes Release/Debug (4.95/5.46 s), strict builds and diff checks pass. Native Find integration, worker preparation, document-generation guards and scope-aware navigation remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — Active-diagnostic integrity prerequisite: source inspection found native/runtime code+message-only coalescing could lose different affected scopes, severity and recovery actions. Added shared exact-content diagnostic identity and saturating occurrence addition, used by both authoring-runtime producers and the native panel. Regression verifies separate scope/severity/action records, correct recovery target, blocked action rejection, critical visibility/dismissal and no overflow. All 587 Release core cases pass (13.09 s); strict Release app/core and Debug native-UI/runtime builds and diff checks pass. Full active-diagnostic Find remains open: opaque/unscoped IDs cannot safely be promoted to note targets, and diagnostic-state changes need separate snapshot validation. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 bounded 10000-note derived Find: expanded indexed shared resolution to 10000 notes/lyrics and 65536 output phones while retaining independent per-lyric/aggregate text and override limits. The adapter checks its supplied token budget before appending each note's output. Model regression resolves 9999 Japanese CV notes plus contextual continuation into 19999 phones, searches all 10000 notes and transfers the async result without changing song/history; 10001-note and 80000-phone expansion inputs reject. Native regression finds the uniquely hinted final note in a 10000-note region, inspects it, selects/reveals it and restores semantic focus with exact project preservation. All 585 Release core cases pass (12.64 s); 25 focused cases pass Release/Debug (1.52/4.41 s). Rebuilt compiler/reconciliation/edit-preservation suites pass 17/14/5 cases; strict builds and diff checks pass. One resolution plus two searches measured 317.70 ms Release / 2089.38 ms Debug locally, not universal latency or renderer qualification. Full diagnostic coverage, worst-case responsiveness and live language/IME/host/release gates remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 resolver cancellation/indexing prerequisite: propagated Find stop tokens through shared Japanese resolution, recursive base resolution and a cancellable kana-adapter overload, with cooperative checks during indexing, per-note phases and character generation. Indexed lyric/note lookups and per-note overrides remove repeated whole-region scans while preserving edit order and continuation behavior. Existing interface delegates with a non-cancelled token. New regression verifies output/identity parity, append/replace order feeding continuation, pre-cancellation and unchanged source. All 583 Release core cases pass (12.48 s); 24 focused cases pass Release/Debug (0.83/0.92 s). Rebuilt Release performance compiler, override reconciliation and edit-preservation suites pass all 17/14/5 cases. Strict Release app/core and Debug focused builds and diff checks pass. Existing collection/text/token limits remain unchanged: 10000-note derived capacity and worst-case cancellation latency are still open, as are live-language/host release gates. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 asynchronous native Find: added a single-worker immutable-snapshot search job with owner-thread result publication/source validation, exception reporting, nonblocking cancellation requests and retire-before-restart admission. Native preparing state enables only Close, cancelled results cannot revive the panel, and field/result controls become available only after validated publication. Both native paint paths use the shared poller; mode switches request cancellation. Model/native regressions cover 10000-note results, cancelled/stale/replaced sources, resolver failure, invalid superseding input, pending controls and Refresh recovery. All 582 Release core cases pass (12.53 s); 23 focused cases pass Release/Debug (0.87/0.95 s), strict builds and diff checks pass. Inspected 480×320 preparing render `/tmp/seam-find-async.mhFhpX/preparing.png`; corrected inherited replacement wording. Snapshot capture, publication validation and detail wrapping remain synchronous; resolver mid-call cancellation, worst-case latency and live-host/IME/derived-capacity/full-diagnostic qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native repeat Find: retained the reviewed result cursor after explicit selection and added Find Next/Previous application commands plus macOS Command-G/Command-Shift-G menu items. Both directions wrap through captured matches and reuse selection/reveal/accessibility-focus logic without changing song/history. Composition/review/dialog/drag guards and source revision/generation validation reject unsafe repetition. Native regression verifies both wrap directions, off-screen/low-pitch reveal, focus, cancellation/resume, source replacement and zero document notifications; dispatcher checks both routes. All 580 Release core cases pass (12.61 s); strict Release app/core and Debug native-UI/platform builds and diff checks pass. Live shortcut/non-Latin layout/cross-host, async Find, full diagnostics and derived-capacity qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native Find entry/results: added macOS Edit Find Notes/Command-F routing, captured query input without replacement, five-field cycling, paged results and complete 32-column/six-line field-text inspection. Explicit Select and Reveal uses guarded selection, pans to the note and restores note accessibility focus without modifying project/history. Escape backs out of detail without selecting. Regression covers 400-character CJK reconstruction, field selection, pagination, semantic/keyboard input, stale callbacks/revisions, replaced documents, error-versus-empty states and exact preservation. All 579 Release core cases pass (12.20 s); strict Release app/core and Debug native-UI/platform builds and diff checks pass. Inspected system-font 480×320 capture `/tmp/seam-find-native.AOusbU/detail.png`: text/actions fit and focus defaults to Back to Results. Live menu/IME/screen-reader/cross-host, async preparation, repeat-match shortcuts, full diagnostics and 10000-note derived capacity remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 captured Find navigation: added immutable source-bound results with explicit selection, forward/backward wrap, closed/cancelled/stale rejection and opaque document-generation validation. Navigation changes only selection, not musical data or undo history. Tests cover normal/empty/stale/replaced-document cases and 10000-note capture plus three selections (5.04 ms Release / 34.86 ms Debug locally, not a universal latency guarantee). All 577 Release core cases pass (11.95 s); 22 focused cases pass Release/Debug (0.96/0.94 s), strict builds and diff checks pass. Native general Find menu/input/results, focus/reveal and async preparation remain unconnected; derived-capacity/full diagnostic/multilingual requirements remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 additional search fields: added canonical note-ID, shared-resolver generated-phone and note-associated pronunciation-warning search, retaining independent lyric/hint fields and stable tick/ID ordering. Derived text has cancellation/output bounds; resolver failures propagate explicitly. Tests cover hinted phones, warning ownership, exact offsets, source preservation, cancellation and capacity errors. All 576 Release core cases pass (12.49 s); 21 focused cases pass Release/Debug (0.85/0.84 s), Release app/core and focused builds pass, and diff checks pass. The existing resolver's 4096-note cap is not raised: 10000-note derived capacity, native general Find/navigation, complete diagnostic aggregation and multilingual/live qualification remain required. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 region-native-dynamics clear: added an explicitly whole-region captured preview, native paged review and macOS Edit command. One canonical command clears only the native curve; generated Dynamics takes/selections and unrelated intent remain unchanged, with an explicit warning that generated dynamics may still apply. Model/native regressions cover exact preservation/undo, stale generation/revision, cancellation, no-op, pagination and selection-independent region scope at 480×320. Strict Release/Debug builds and 20 focused cases pass; core verification contains 575 cases. Selected-note-only reset, live/cross-host/audio and worst-case latency qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 auto-legato implementation: added selected-adjacent/same-group/one-grid-gap policy with staccato, explicit separation, unselected-neighbor and nested-overlap safeguards. Eligible geometry and both-end articulation changes share one canonical composite/undo group and dependency dry run. macOS Edit and native review expose the mode and endpoint changes. All 573 Release core cases pass (13.22 s); 19 focused cases pass Release/Debug, strict builds/diff checks pass. Inspected system-font review render; live/audio/large-selection qualification remains open, as does clear-dynamics. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — Shared review short-window repair: replaced fixed-height/off-screen placement with available-height panel sizing and adaptive row spacing while preserving action heights. All rows/actions fit without overlap at four supported sizes, including 480×320; semantic bounds and pointer Cancel are tested. Inspected a system-font minimum-size review capture. All 571 Release core cases pass (12.15 s); Release app/Debug native-UI builds and diff checks pass. Live IME resizing and full embedded/minimum-size qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native cleanup integration: added distinct macOS Remove Selected Overlaps/Close Selected Gaps commands and shared review modes with note outcomes, skipped reasons, dependency pages, captured grid, explicit Apply/Cancel/Refresh and index rebuilding. All 570 Release core cases pass (12.88 s); 17 focused cases pass Release/Debug, strict builds/diff checks pass. Inspected both 720×520 system-font renders. Live menu/unselected-neighbor/no-op rejection passed; box selection automation failed, so successful two-note live Apply remains unqualified. Owned test process exited 0/nonphysical; a later different session was left untouched. Full small-window/host/acoustic/latency acceptance and other actions remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 cleanup dependency preflight: cleanup preparation now runs canonical resizing on a private project and captures phone/unit/seam unresolved outcomes before publication. Shared bounded/cancellable per-key comparison replaces duplicated lyric-review logic and rejects duplicate source/result keys. A Japanese shared-lyric overlap regression verifies previewed continuation-related unresolved records match actual Apply and exact undo. All 569 Release core cases pass (12.68 s); 17 focused cases pass Release/Debug, strict builds/diff checks pass. Native cleanup presentation and worst-case preparation/audio/host qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 duration-cleanup previews: added Remove Overlap/Close Gap planning with per-note outcomes, adjacent-selected policy, fixed starts, downward overlap snapping and on-grid gap closure. Ambiguous starts, staccato gap extension, unselected neighbors and nonpositive durations are reported/skipped. Captured grid/selection/generation checks and canonical resize publication preserve exact undo/no-op behavior. All 567 Release core cases pass (11.87 s); 15 focused cases pass Release/Debug, strict builds/diff checks pass. Native review/menu/dependency diagnostics, auto-legato, clear-dynamics and large-selection qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 native clear-vibrato action: added macOS Edit command and shared review mode with paged targets/counts, explicit Apply/Cancel/Refresh, stale-selection guards and disabled no-op Apply. All 565 Release core cases pass (13.29 s); 13 focused cases pass Release/Debug, strict builds/diff checks pass. Live temporary-project menu→Apply→reopen-zero→undo→reopen-one flow passed; discard exited 0 with unchanged input hash and no physical audio. System-font 720×520 review inspected. Cross-host/acoustic/large-selection qualification and other creator actions remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 creator reset foundation: checked the referenced creator-plan requirements and added captured clear-vibrato preview/guarded application. It disables modulation while preserving settings and all unrelated musical data, uses the canonical performance command/receipt, and rejects stale/cancelled/reused targets. Full-project equality regression covers retained pitch/dynamics/hints/ownership/unit/seam data and unselected vibrato. All 564 Release core cases pass (14.69 s); 13 focused cases pass Release/Debug and strict builds/diff checks pass. Native clear-vibrato integration, dynamics/geometry actions and live/large-selection qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 reviewed distribution: extracted a shared read-only plan, added captured-selection distribution previews, and routed native batch submission through background review instead of immediate mutation. Explicit Apply/Cancel/Refresh, dependency/detail pages and exact undo are connected; mismatches show separate counts with Apply disabled. Added a semantic status/count child after live inspection found root-only text omitted by bridges. Final Release core passes 563 cases (12.02 s); 12 focused cases pass Release/Debug, strict builds/diff checks pass. Live Shift+L mismatch/review/Apply/undo/discard sequence passed with unchanged fixture hash and exit 0. Full IME/hint search/normalization/reset/cross-host acceptance remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 distribution semantics: default language is now preserved per token; explicit language/reset remains supported. Fully selected shared lyric groups receive one syllable/edit per distinct token; partial groups reject instead of modifying unselected notes. Reports distinguish notes/targets/changes, and no-op requests add no history/notification. Added admission/Unicode bounds and indexed lookup. All 561 Release core cases pass (11.99 s); 11 focused cases pass Release/Debug, including 10000-note distribution/undo (~4.24/35.54 ms local). Native app/Debug UI builds and diff checks pass. Reviewed distribution and live language/host qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 batch target safety: replaced boolean batch-input mode with captured document generation/revision/region/selected IDs and validates them before distribution. Selection/source drift cannot retarget input; equivalent selection ordering remains valid. Added bounded all-in-region admission, indexed membership, composition-key isolation and no lyric navigation after batch Tab completion. All 558 Release core cases pass (12.62 s); Release app/Debug native-UI builds and diff checks pass. Reviewed distribution/shared-lyric/language-policy and live IME qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 long-row visual review: result rows open paged complete Before/After text with context-specific actions and semantic IDs. Back/Escape cannot apply; returning to results restores explicit Apply. Added bounded cluster-aware UTF-8 byte-range wrapping with exact reconstruction/error tests. All 557 Release core cases pass (12.48 s); Release app/Debug native-UI builds and diff checks pass. Inspected system-font 720×520 Japanese detail raster; live/script conformance and worst-case detail preparation latency remain unqualified. U24/Beta GO still open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-08 — U24 native text maintenance: implemented responder-chain clipboard commands for active custom AppKit text input and changed left/right movement to composed-character boundaries. Release app/core and Debug native-UI/platform builds pass; all 555 existing core cases pass and diff checks pass. Live fixture test verifies emoji/combining-cluster navigation/deletion, cancellation, unchanged source hash and exit 0. The automation's direct non-ASCII typing still failed; paste/copy/cut execution and full IME qualification remain unverified, with no bypass introduced. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 native Find/Replace entry: macOS Edit command opens captured query/replacement stages, inline empty/length validation, guarded transition to background review and explicit-only application. Fixed nil accessibility values for empty editable fields in both AppKit bridges. All 555 Release core cases pass (11.57 s); strict Release app/Debug native-UI/platform/CLAP builds and diff checks pass. Live fixture workflow verified Japanese semantic query/replacement, dependency review, Apply, one-step undo and cancellation; source fixture hash unchanged and both sessions exited 0 without physical audio. Direct Japanese typing/IME, embedded/Windows live discovery, long-row expansion and remaining U24 acceptance still open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 native replacement review panel: connected background-job polling to standalone/embedded paint paths and implemented status/counts, six-row lyric/dependency pages, explicit Apply/Cancel/Refresh, isolated semantics and stale action IDs. Apply notifies once and forms one undo group; background input is guarded, including embedded technical-lane interception. Rebuilt Release core passes 554 cases (11.80 s); Release app/Debug native-UI builds and diff checks pass. Inspected 720×520 raster confirms row/button separation. Query/replacement text entry and menu opener remain unconnected; long-row expansion/live-host/multilingual qualification remains open. No U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 background review job: added immutable project/revision overloads and a single-worker preparation lifecycle using opaque editor generation context. Worker results are owner-polled and revalidated against current document/region; cancellation discards results without joining live work, and destruction joins safely. Replaced documents, live edits, duplicate apply and stale prior-query reuse reject. Eleven focused cases pass Release/Debug (0.77/1.07 s); rebuilt core passes 553 cases (12.30 s), strict builds/diff checks pass. Snapshot copy and canonical dry-run cancellation latency remain bounded only by current synchronous operations; no native worker/dialog adapter yet. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. No U24 acceptance; changes local/uncommitted.

2026-09-07 — U24 replacement-review model: added full-text six-row pagination, canonical private-copy lyric-command dry run and per-key before/after unresolved outcomes for phoneme/unit/seam records. Review application validates identity/revision/selected region/full source-region state; applied/cancelled instances cannot be reused. All 10 creator-batch cases pass Release/Debug (1.25/1.35 s); rebuilt core passes 552 cases (11.94 s), diff checks pass. 10000-note English review preparation measured 4.76/42.23 ms Release/Debug locally. Native worker/input/dialog integration and supported-language live workflows remain unfinished; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 macOS menu discovery: Edit now exposes Japanese pronunciation-hint entry through an explicit application command and shared selected-note opener. Callback errors propagate without document changes; menu rejection shows a native alert rather than only logging. All 550 Release core cases pass (12.44 s); Release app/Debug native-UI/platform builds and diff checks pass. Live exact-app verification observed the menu, empty-selection alert, OK dismissal and clean exit 0/revision 0 in disposable Untitled sessions. Successful live hint commit, Windows/embedded discovery and native search/replacement review remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. No U24 acceptance; changes local/uncommitted.

2026-09-07 — U24 active hint accessibility: added an isolated field/visible-Cancel semantic surface, bounded UTF-8 value submission, monotonic interaction IDs and stale/background callback rejection. Pointer/keyboard isolation prevents score shortcuts from mutating notes during hint composition. All 550 Release core cases pass (12.10 s; capture repeat 24.51 s); Release app/Debug native-UI builds and diff checks pass. Inspected 720×520 raster verifies distinct field/cancel bounds; live AppKit/VoiceOver and permanent inspector/menu opener remain unqualified/unimplemented. Native search/replacement review and U24 acceptance remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 native keyboard hint entry: Alt+Enter opens a distinct selected-note Japanese phone field; commit uses reconciled hint commands, empty clears, and Tab does not navigate lyrics. Captured project/region/revision/source-hint checks reject stale submissions; no-ops remain history/notification-neutral. Fixed the shared composition model's unconditional empty-text rejection with an explicit auxiliary-field opt-in, retaining nonempty lyrics by default. All 549 Release core cases pass (11.99 s); Release native app, strict Debug native-UI build and diff checks pass. Dedicated accessibility/inspector controls, live platform qualification and native search/replacement review remain open; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 mixed hint writer consolidation: EditPerformanceCommand now composes actual hint changes through the dedicated reconciler on private project/command state, preserving atomic coupled expressions and exact reverse-order undo. Combined ownership/pronunciation revisions are retained for redo; invalid hints cannot partially publish or capture history. Updated combined test and added mixed-ownership regression pass in Debug/Release. Native hint UI remains open; details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. No unit acceptance.

2026-09-07 — U24 dedicated hint command: added captured-value bounded hint batches with language-aware validation, stale/repeated-target rejection, phrase impact and shared staged dependency reconciliation. Hint changes now advance pronunciation state; old changed-phone edits remain unresolved rather than retargeting. Tests verify untouched displayed lyrics, resolved sh/a phones, exact undo/redo and clear/restore. Eight focused Debug/Release cases and 547 Release core cases pass (12.74 s); diff checks pass. Native hint UI and older mixed-command writer consolidation remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 Japanese hint semantics: discovered stored phonetic hints were ignored by the adapter. Added bounded explicit phone parsing from the built-in inventory, independent of displayed lyrics, with canonical validation and visible inspection errors. Hint presence/content enters resolver identity; resolver version advances to 2 and phonemizer revision to 3. Regression covers audible-input phone changes, unchanged lyric display, rejection and removal restoration. Release core/performance suites pass and Debug phonemizer builds. Undoable hint command reconciliation/native controls remain open; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 10000-note replacement: added exact apply/one-undo/redo test and indexed requested lyric targets once per live/staged command state. Measured Release apply improves ~71.6→3.8 ms on the same English fixture; undo/redo ~67→3 ms. Staging/validation/reconciliation remain; ambiguous targeted IDs reject while unrelated reused IDs are preserved. Six focused cases pass in Debug/Release. Measurements are local observations, not universal latency or multilingual qualification. Native review/IME remains open; details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 reviewed replacement API: immutable previews deduplicate shared lyric targets, retain exact before/after/language data and count affected notes/occurrences. Linear non-overlapping replacement has bounded output and rejects empty lyrics. Apply validates project/revision/original values then uses one canonical batch command; no-op previews add no history. Tests cover source/hint preservation, one undo/redo, stale/cancel rejection and expansion bounds. Native review, precise dependency diagnostics and 10000-note commit qualification remain open; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 bounded note search: added read-only region search separating displayed lyrics and hints, exact scalar matching/offsets, linear prefix-table search, stable note ordering, cancellation checks and 10000-note/4M-text admission. Results retain source identities/text for later reviewed edits; no mutation is implemented yet. Regression covers Unicode/shared lyrics, hint isolation, 10000 notes, malformed/oversized input and source preservation. Focused Debug/Release suites pass; native preview/replacement and U24 acceptance remain open. Contract: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — Full Release rerun after short-transition fixes: all default targets rebuilt (85 steps), then all 88 registered CTest targets ran in 62.46 s: 87 PASS, only tracked-source closure FAIL for unindexed local implementation files. Previously failing demo/schema/platform-contract and singing-quality bank/Raw workflow pass in the same run. No tests were excluded and no source-publication gate was changed. This is stronger integration evidence, not a project-completion percentage or Beta GO acceptance. Details: `BROAD_RELEASE_REGRESSION_2026-09-07.md`. Changes remain local/uncommitted.

2026-09-07 — Raw short-transition integration: mapped transient/release source positions while retaining ordinary/compiled Raw sustain pitch stepping, gain and loop print. Exact extent/vowel validation and no-sustain rejection remain; diagnostics disclose resampled transient pitch. Raw revision advances to 9. Multi-renderer and explicit root/octave phase-step/gain regressions pass; unchanged singing-quality workflow now passes both bank/Raw modes (20.74 s). Retained Raw unequal-rests output is 438000 frames/9.125 s, no clipping, actual Raw/no fallback. Acoustic release and broad-suite recheck remain open. Details: `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md`.

2026-09-07 — Classical short-transition integration: production timing opts into tagged simple single-nucleus short placements; default/legacy paths still reject. Concatenative PSOLA/Spectral/Stretch consume bounded marker maps with compiled performance and no Raw fallback; timing revision advances to 11 for cache provenance. Tests verify finite/nonzero PCM and exact vowel alignment. Unchanged unequal-rests bank rendering now succeeds (438000 frames/9.125 s/no clipping), while Raw remains explicitly unsupported. Details: `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md`. Full corpus/Raw/acoustic qualification and Beta GO remain open.

2026-09-07 — Short-unit mapping primitive: traced the unequal-rests failure to a 62.5 ms target versus ~70.11 ms source post-vowel transition. Added bounded marker-based compression for simple single-nucleus CV/sustain consumers, preserving vowel anchor/end while reserving transition/sustain/release frames. Multi-rate mapping/replay and invalid-source/rate/span regressions pass in Release. The solver guard and production renderer are intentionally unchanged pending integration; corpus failure remains open. Contract and next steps: `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md`. No unit acceptance.

2026-09-07 — Broad Release regression: rebuilt all default targets (168 steps) and ran all 88 registered CTest targets: 82 passed, 6 failed (78.14 s). Repaired obsolete Phase12B schema/migration assertions, two punctuation-bound platform contracts and an unintended Phase2 demo consonant end override; focused reruns pass. Singing-quality unequal-rests rendering still rejects a short selected CV transition, reproduced with retained logs; Git source closure still reflects unindexed local work. No checks were bypassed and no all-green/Beta GO claim is made. Detailed failures and next synthesis target: `BROAD_RELEASE_REGRESSION_2026-09-07.md`.

2026-09-07 — U23 preservation-suite reconciliation: ran the previously core-excluded dedicated suite and found obsolete expectations for interleaved duplication/unresolved complete spans/reused slur IDs. Updated them to assert common-translation/new-identity behavior while retaining all expression, context and exact undo checks; added the five-case file to the core target. Debug/Release preservation suites pass; rebuilt Release core passes 538 cases (13.48 s), and the separate performance-command suite passes its partial-span/collision coverage. Details: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. Coverage progress only; no new unit acceptance.

2026-09-07 — U23 event selection retention: refresh matches the selected tick/type instead of reusing its index or jumping to row zero; deletion selects its chronological successor/end fallback and updates the page. Cross-project opening does not preserve the old selection. Regression covers index-shifting insertion, later-page retention, deletion successor and same-tick type identity. Freshness/initial-event guards remain. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. No new unit acceptance; changes local/uncommitted.

2026-09-07 — Embedded AppKit text parity: retired old native fields before runtime callbacks, guarded reentrant end-edit notifications, added explicit Enter/Tab/Backtab/Escape delegation and missing panel key mappings. Release plugin/host/core and Debug embedded builds pass; core 532 cases pass (12.01 s). Real Cocoa fixture host smoke exits 0 with visible GUI, nonzero note energy, offline-render acceptance and exact state round-trip. Interactive embedded text/IME and actual DAW qualification remain unverified; no U23 acceptance. Evidence/boundaries: `TEMPO_METER_COMMANDS_2026-09-07.md`.

2026-09-07 — Live AppKit keyboard repair: fixed absent Cmd+A select-all handling plus missing ASCII/Korean-fallback A mappings. Retired native input before commit/cancel callbacks so two-stage event insertion retains the successor field. Final live typed 123.75 initial BPM, typed tick1920→90.5 insertion and Escape-cancelled 7/8 meter stage pass; normal discard of only disposable Untitled exits 0. Release core passes 532 cases (12.11 s); Release standalone/Debug native UI builds and diff checks pass. General IME/VoiceOver/embedded/physical playback and full U23/Beta GO remain open. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`.

2026-09-07 — Fixed-input live recheck: rebuilt AppKit app visibly paints time-map input; live semantic tempo 90.5/tick1920 and meter7/8/tick1920 insertion, refresh, isolated meter removal and normal-tree restoration pass. Ordinary test-window close shows Save/Discard/Cancel; discarding the disposable Untitled document exits 0. Release core passes 532 cases (11.74 s); Release standalone/Debug native UI builds and diff checks pass. Full int64 renderer capture fits. No physical audio, keyboard/IME or VoiceOver qualification is claimed; U23/Beta GO remain incomplete. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`.

2026-09-07 — Live time-map QA found missing painted input: AppKit semantic open/Add Tempo/tick transition worked, but screenshots showed static toolbar values rather than composition text. Fixed scene generation to supply shared explicit time-map input bounds and a dedicated active flag, and paint bounded input after the panel. Added geometry/value/cancel/19-digit regression. Original disposable test app exited 0 at its deadline with nonphysical/no-frame audio. Further evidence: `TEMPO_METER_COMMANDS_2026-09-07.md`. Live recheck and U23 acceptance remain pending; changes local/uncommitted.

2026-09-07 — U23 panel accessibility: added semantic opener and dedicated event-panel tree, row/action availability, active tick/value fields, cancellation and panel-confined Tab/Enter routing. IDs bind document/snapshot/page/selection/stage plus monotonic interaction identity; cancelled and prior-stage targets cannot revive. Dispatch explicitly rejects unavailable materialized controls and background actions. Added full semantic insertion/cancellation regression. Live VoiceOver/platform qualification remains open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 panel insertion: added visible ADD TEMPO/METER and N/Shift+N actions with tick→value native composition, stage-specific prompts, cancellation, exact bounded int64 parsing and collision/stale checks before both stages. Tick entry is nonmutating; final insertion uses one canonical command and never replaces an existing same-type event. Tests cover cancellation, same-tick independent kinds, between-stage changes and invalid ticks. Panel accessibility/live qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 visible event panel: added TIME MAP entry button, centered six-row painting, pointer/keyboard selection, paging, edit/remove/refresh/close actions and explicit stale state. Selected removal refreshes only after canonical commit; other stale lists reject. Background pointer-down/keyboard/scroll/accessibility actions are blocked while the panel is open. Added interaction regression and optional deterministic capture. Insertion controls, panel accessibility and live platform qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 event-list model/controller: added bounded chronological tempo/meter snapshots, six-row paging, protected initial rows and stable tick/type targets. Native selected-edit/removal dispatch checks project identity/revision/both map contents before canonical commands; stale lists require recapture. Regression covers interleaved pages, invalid indices, same-tick identity, edit/remove/undo and out-of-band rejection. Visible list/refresh UX and live qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 slur edge repair: guarded monotonic group allocation against UINT64_MAX wraparound before command execution. Regression verifies mixed disable preserves non-Legato articulation/lyrics/unselected notes, failed allocation preserves state/revision/history, undo/redo restores exact state, and selected existing groups can still be extended without allocation. Existing group-extension semantics remain unchanged and documented. Details: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. U23/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — U23 Final-render/export evidence: added integration regression driving native tempo/meter text commits through canonical commands, Final procedural rendering, project save/reopen and committed Float32 master/stem export. Tempo change inside a sustained note changes 48000→72000 frames with new phrase identity; meter preserves this fixture's PCM; undo/redo restores exact original/slowed samples. Musical geometry remains unchanged. This validates offline integration, not live GUI/background scheduling/device playback or full U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 native meter input: split the existing BPM area into nonoverlapping tempo/meter subfields and added meter painting, hit testing and editable semantics. Bounded signature parsing validates integers before narrowing; captured revision/tick/kind prevents cross-control retargeting. Commit uses the canonical meter command with shared cancellation and text-target reset. Tests cover application/undo, invalid signatures, nonzero targeting, isolation and layout geometry. Event-list/removal UI and live qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 exact tempo/event context: fixed six-decimal prefill quantization using shortest-round-trip formatting and shared bounded BPM parsing. Native tempo input now captures an explicit event tick plus revision; nonzero commit inserts/replaces that event, and initial-toolbar accessibility cannot retarget an open nonzero event. Regression covers precision, invalid input, captured-target preservation and undo/redo. Visible event selection/meter controls and live qualification remain open; no unit acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 visible initial-tempo input: connected BPM hit bounds and accessible text-field activation/value assignment to native composition and revision-guarded commands. Decimal input, Escape cancellation, Tab non-lyric completion, stale/invalid rejection and target reset when switching text workflows are implemented. Added native regression; arbitrary event/meter controls and live runtime qualification remain open. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. No unit acceptance or release publication; changes local/uncommitted.

2026-09-07 — U23 native tempo/meter dispatch: added revision-bound controller entrypoints using shared commands and success-only document-change/repaint notifications. Stale context, composition, drag and phoneme-review guards prevent background mutations. New native regression covers notification counts, stale/invalid requests, composition preservation and removal/undo. Existing standalone/embedded callbacks route to authoring runtime. Visible tempo/meter input remains to be connected; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 transactional tempo/meter commands: added optional-value insert/replace/remove commands using validated map copies, complete before/after history, stale-map rejection and project-wide audio impact. Tests cover sustained-note sample mappings, exact undo/redo, captured worker invalidation, meter/bar changes, invalid inputs and atomic rejection. Native tempo/meter controls and actual playback/export integration remain open; U23/Beta GO are not accepted. Contract: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 duplicate relationship preservation: copied notes now map shared source syllables to one fresh lyric token and source slur groups to fresh region-local IDs. Explicit AddNote ReuseExact mode validates the captured existing lyric and leaves ownership with its creator, preserving reverse-order composite undo/redo without weakening ordinary collision rejection. Added pronunciation/identity/undo and shared-token admission regressions. This completes the relationship repair after common phrase translation, not full U23; owned-edit edge coverage, slur edge cases and tempo/meter commands remain. Details: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — Designer handoff verification and U23 phrase translation: verified the UI-captured 0.25 recipe survives frozen preparation without changing its 0.05 source or plain score. CLI rendering/collection reached generation 3 with one unapproved MarkerReview take; retry returns AlreadyCollected. Fixed stale AppKit Designer accessibility children on mode exit (live checked) and duplicate save-panel extension (built, default not live checked). GUI-only completion remains unverified after the Mac locked. Continued U23 by replacing per-note-duration duplicate offsets with one overflow-checked phrase translation; regressions cover unequal durations/rests, selection order, undo/redo and atomic arithmetic rejection. Debug/Release focused four-case suites pass. Details and remaining scope: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. No additional unit acceptance; changes remain local/uncommitted.

2026-09-07 — Duplicate-pose modal source guard: inspection found that changing audition pose does not change recipe revision, so a naming dialog could copy a different selected pose after returning. The native flow now captures and supplies an expected source index; the session rejects mismatch before mutation. Regression verifies unchanged-revision selection changes, rejected stale source, correct copied resonances and undo. All 23 Designer tests and both Studio builds pass in Debug/Release; diff checks pass. This is cross-cutting safety maintenance, not new unit acceptance. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Height-aware Designer layout: shared 1–6-row viewport and footer geometry now drives painting, pointer hit testing and accessibility; compact status/error rendering avoids overlap and secondary commands retain keyboard/semantic access. Resize cancels an active drag before changing geometry. Geometry tests cover 320–900 heights and up to 224 controls; all 22 Designer tests pass in Debug/Release (2.71/1.10 seconds), both Studio builds pass and diff checks pass. Live supported-minimum 720×520 checks show controls/help/error fitting and invalid edits preserving SAVED; close exits 0 with unchanged producer/no recording. A 720×320 launch was correctly rejected under Studio's existing minimum. Details: `VOICE_DESIGNER_COMPACT_LAYOUT_2026-09-07.md`. Broader layout/accessibility and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Dedicated Designer frication audition: added direct seeded-noise preview with shared bounded/faded output finalization, separate ready buffer and shared cancellation/epoch/revision worker guards. Cmd/Ctrl+Space and source-index semantic actions render/play selected noise; vowel/reference buffers and A/B semantics stay separate. Tests verify deterministic finite/non-silent bounded PCM, source-seed effects, invalid/stale/cancel behavior and vowel/reference preservation. All twenty-one Designer tests and both Studio builds pass in Debug/Release; diff checks pass. New UI/device interaction and acoustic qualification remain live-unverified; full U20/U22/Beta GO remains incomplete. Changes local/uncommitted.

2026-09-07 — Frication lifecycle controls: added undoable source-index removal and exact per-source seed editing via Cmd/Ctrl+Shift+E/R plus semantic actions. Source-index/context binding prevents implicit retargeting; global seed and unrelated sources are preserved. Global/source seeds share canonical unsigned parsing. Added regression verifies precision, isolation, invalid/stale/index rejection and exact undo restoration across seed/removal changes. All twenty Designer tests and both Studio builds pass in Debug/Release; diff checks pass. New source UI/dialog interaction, dedicated frication audition and full U20/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer frication authoring: added Cmd/Ctrl+E/semantic creation with phone/style validation and one-time global-seed initialization, plus selected-style center/bandwidth/gain rows through keyboard/drag/accessibility edits. Existing canonical validation rejects duplicate/missing-style/invalid filters; edits are undoable and change frozen recipe identity. Tests verify creation, seed/version identity, parameter-dependent source PCM, invalid-state preservation and undo. Nineteen Designer tests and both Studio builds pass in Debug/Release; diff checks pass. New interaction remains live-unverified; oral audition excludes frication, and dedicated preview/removal/seed controls plus full U20/U22/Beta GO qualification remain incomplete. Changes local/uncommitted.

2026-09-07 — Exact Designer seed control: added canonical unsigned-64-bit decimal parsing, Cmd/Ctrl+R AppKit dialog and semantic text-field editing without float narrowing. Accepted edits use existing undo/preview invalidation; unchanged/invalid/stale inputs preserve valid state. The seed affects phonation noise and modulation phase, not independent frication seeds. Eighteen focused Designer tests pass in Debug/Release, including UINT64_MAX, overflow/malformed rejection, no-op preservation and PCM/undo reproducibility; both Studio builds pass and diff checks pass. New dialog/header live checks, non-AppKit dialog support and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer periodic modulation controls: exposed pitch depth, amplitude depth and rate through keyboard/drag/accessibility edits, preserving canonical validation, undo and preview invalidation. Labels reflect the existing sinusoidal DSP rather than claiming stochastic/measured jitter/shimmer; zero rate is explicitly off. Added regression verifies separate depth effects on PCM, exact undo restoration, zero-rate neutrality and invalid-rate preservation. Both Studio builds pass; focused Designer suites were rerun in Debug/Release. Live new-control/acoustic qualification and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Visible prepare-from-Designer action: Cmd/Ctrl+Shift+P and a semantic action capture current draft resource/style, validate Designer/producer modal context and start background score inspection, returning to producer view. The score selection owns the frozen recipe and can discover plain-score regions; region/destination steps pass that exact selection to preparation with explicit snapshot labeling. No saved files, producer state or old jobs are rewritten/recaptured. Added controller test verifies discovery→selection→preparation→reload with the exact draft hash and unchanged producer state. Both Studio builds pass; focused integration suites were rerun in Debug/Release. New handoff remains live-unverified and full U21/U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Explicit Designer recipe input to preparation: shared saved-score preparation and the native worker now accept an owned frozen resource/style selection. Only the job's decoded score copy changes; source score/recipe files and producer state remain untouched. Original score-digest, assignment, pitch and producer expectation checks remain; this does not refresh existing jobs. Integration tests verify exact selected recipe identity across preparation/reload/render, changed PCM, source preservation, invalid-style/digest rejection and explicit selection for a score without a saved recipe. Visible Designer-to-preparation action remains to be connected; full U21/U22/Beta GO remains incomplete. Changes local/uncommitted.

2026-09-07 — Extended Designer semantic actions: exposed audition render/play/stop/cancel, A/B pin/play/clear, Save As, pose duplication/removal and undo/redo with availability checks, explicit action routing and audition/reference status. Transport-only actions do not require an idle file session. Added independent preview cancellation so cancelling audition cannot cancel a simultaneous save; regression confirms exact recipe persistence and clean saved state after preview cancellation. Both Studio builds and sixteen focused Designer tests pass in Debug/Release; diff checks pass. New semantic actions remain live-unverified and full screen-reader/cross-platform/U22/Beta GO acceptance remains incomplete. Changes local/uncommitted.

2026-09-07 — Designer semantic accessibility: added custom non-score accessibility-tree support and Designer numeric fields, file/page actions, save/error status and context-bound IDs. Values parse fully/finite and use canonical edits; stale/hidden/busy/drag targets reject. Live AppKit AX checks verified semantic Open, numeric 0.62→0.65 edit, rejection of 2 with error status, next-page formants and undo to SAVED. Exit 0 retained producer generation 3/approved 0/no recording. Custom-tree regression and rebuilt Release core/native suite pass 506 tests (11.51 seconds); diff checks pass. Details/limits: `VOICE_DESIGNER_ACCESSIBILITY_2026-09-07.md`. Full screen-reader traversal, missing action semantics, embedded/other-platform qualification and U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer pose management: added selected-pose duplication with AppKit phone/style naming and automatic selection, plus undoable removal. Common recipe validation rejects duplicate identities, invalid values and orphaned frication styles; last-pose removal is explicitly rejected. Original resonance values are copied as a retuning starting point, not claimed as correct new phonetics. Epoch/revision guards preserve stale-dialog safety. Fifteen Designer tests pass in Debug/Release (1.94/0.88 seconds), both Studio builds pass and diff checks pass. Actual new dialog/header interaction and cross-platform naming remain unverified/unsupported; complete articulation/accessibility and U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Live Designer drag/viewport/audition QA: opened the saved synthetic recipe, dragged open quotient, verified one-step undo returned SAVED, navigated to the final formant with nonoverlapping six-row viewport, rendered preview and pinned A. Current B entered playback and returned ready without error through the linked CoreAudio path; after an F3-gain edit, A identity stayed pinned and reference playback entered its active state. The timed run ended before the planned mismatch check; programmatic test closure left the saved recipe untouched and producer generation/approval unchanged. No subjective listening/acoustic qualification is claimed. Detailed observations and test-close limits: `VOICE_DESIGNER_INTERACTION_QA_2026-09-07.md`. Rebuilt current Release core/native suite passes 504 tests (11.59 seconds), and diff checks pass. Smaller-window/accessibility, remaining interaction and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer mouse gesture wiring: numeric phonation/formant rows now capture a baseline/control/epoch and update via the shared keyboard adjustment mapping, with fixed viewport during drag, fine scaling, one-undo release and Escape rollback. Session gesture APIs guard identity/file work and invalidate preview results; invalid edits preserve the last valid value. Added regression verifies grouped history, file/replacement protection, stale-epoch rejection and audition suppression after cancel. Both Studio builds pass; Release fourteen-test Designer suite passes (0.83 seconds), with clean diff checks. Actual drag interaction and continuous audible feedback remain unverified/unimplemented respectively. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Designer A/B reference: added immutable ready-preview pinning with original recipe/PCM/pose/style/pitch, preserved across draft edits and cleared on voice replacement. Cmd/Ctrl+B pins A, Shift+Space plays A only with matched pose/style/pitch, Space plays B, and Cmd/Ctrl+Shift+B clears the reference without recipe history changes. Tests verify frozen reference retention, changed current PCM, selection mismatch, reset/lifetime and clear semantics. Both Studio builds pass; Release thirteen-test Designer suite passes (0.72 seconds). Actual A/B UI/hardware playback and loudness/listening qualification remain unverified; this is not source or bank approval. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Designer resonance controls: added per-formant frequency/bandwidth/gain editing for the selected pose/style, fine steps, full-list keyboard navigation and a six-row selection-following viewport. Accepted changes use canonical validation/undo and invalidate preview audio; invalid frequency crossings preserve state. Added regression proves each parameter changes PCM and undo restores exact original audio, with invalid edits preserving ready preview/revision. Both Studio builds pass; Release twelve-test Designer suite passes (0.75 seconds). Updated layout/live control interaction and physical listening remain unverified; mouse sliders, pose management/accessibility and full U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Designer audition pose/pitch selection: added session-owned pose/style and MIDI 36–96 controls without changing recipe revision/hash/history. Selection is epoch/revision-guarded, invalid/no-op choices preserve valid preview state, changes invalidate pending/ready audio, and completion admission checks pose/pitch alongside voice identity. Create/Open resets selection; removed pose indexes safely return to the first pose. Studio keyboard rows expose both choices. Added PCM/worker regression verifies selected output, stale selection suppression, pitch-dependent audio and pose-removal safety. All eleven Designer tests and both Studio builds pass in Debug/Release (1.00/0.53 seconds); diff checks pass. Actual new control/playback interaction, A/B/continuous audition and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer sustained-pose audition: added bounded one-second/48-kHz preview through existing compiled-F0 phonation and oral tract stages, finite/peak checks and edge ramps. Separate background rendering captures immutable resource plus document epoch/revision; edits/replacements invalidate old audio and stale/cancelled results are discarded. Studio Space now renders, then plays the ready first-pose/MIDI-69 preview through the existing device audition session at 0.25 gain; device startup stays outside paint. Tests prove deterministic/non-silent/bounded PCM, aspiration-dependent output, invalid/cancel rejection, stale worker suppression and immutable retained buffers. All ten Designer tests and both Studio builds pass in strict Debug/Release (0.77/0.56 seconds); diff checks pass. Actual new Space/device interaction and listening remain unverified; pose/pitch controls, A/B/continuous preview and full U22/Beta GO remain incomplete. Details: `VOICE_DESIGNER_AUDITION_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — Designer discard/replacement flow: New/Open/Close now offer explicit discard-or-cancel confirmation for dirty drafts, with Cancel default, context revalidation and nested-dialog suppression. Saved files and producer takes are not deleted. Replacement loading/validation must succeed before the old draft is removed. AppKit/Win32 adapters are source-implemented; actual new dialog interaction and Win32 runtime remain unverified. Added regression verifies authorized-but-failed replacement preserves edited content/epoch and valid replacement advances epoch/reset history. All eight Designer tests and both Studio builds pass in strict Debug/Release (0.43/0.38 seconds), with clean diff checks. Audition, full controls/accessibility and U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Initial visible Voice Designer: Studio now hosts the file session behind Cmd/Ctrl+D, with Create/Open/Save/Save As, keyboard open-quotient/tilt/aspiration adjustments and undo/redo. Hidden producer keyboard/pointer/scroll actions are isolated; recording/worker guards protect entry, and unsaved drafts prevent window close. Live AppKit checks verified create, 0.60→0.61 adjustment, undo/redo, Save As, another edit to 0.62, rejected unsaved close, same-path save and clean exit. Saved bytes corroborate 0.62; producer generation/approval remained unchanged and no physical input occurred. Both Studio builds and seven Debug/Release Designer tests pass (0.53/0.48 seconds); diff checks pass. Details: `VOICE_DESIGNER_NATIVE_UI_2026-09-07.md`. This is a single-pose starter with no audition, mouse sliders, full pose/style controls or accessibility semantics; discard-close choice and full U22/Beta GO remain unfinished. Changes local/uncommitted.

2026-09-07 — Voice Designer file session: added standalone draft ownership and asynchronous open/save, explicit dirty replacement protection, document epochs plus model revisions, exact saved-snapshot acknowledgement, new-file-only Save As and same-path external-change checks under a cooperative persistent writer lock. Failures preserve current draft/path/history; shutdown joins and collects actual completion. Tests cover save/edit/reopen, busy/stale document actions, failed opens, external changes, existing destinations, lock contention and shutdown cleanup. All seven Designer tests pass in strict Debug/Release (0.48/0.55 seconds), and diff checks pass. Details/lock limits: `VOICE_DESIGNER_SESSION_2026-09-07.md`. Studio controls/audition and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Voice Designer gesture transactions: added begin/update/commit/cancel with immutable preview resources and monotonic revisions. Continuous changes do not grow history; commit creates one undo item, cancel restores the baseline, and neutral/cancelled gestures preserve redo. Nested/conflicting edits, undo/redo and save acknowledgement reject while active; invalid previews preserve the last valid state. Tests exercise 100 updates as one undo, stale completion, invalid preview recovery, immutable resources and saved/redo restoration. All five focused Designer tests pass in strict Debug/Release (0.44/0.48 seconds), and diff checks pass. Visible controls, asynchronous audition and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Editable Voice Designer draft model: added validated frozen-recipe edits with expected monotonic revisions, immutable old resource snapshots, canonical dirty/save identity and bounded 128-entry undo/redo. No-op edits retain redo; invalid/stale changes preserve state; undo restores content without reviving old actions. Recipe/engine identity is fixed within a model and no song/bank/producer mutation or approval is provided. Three focused model tests cover phonation/resonance changes, stale/invalid rejection, schema transitions and history bounds; registered in the native suite. This is the model foundation for grouped controls, not finished Designer UI/audition or acoustic qualification. Details: `VOICE_DESIGNER_MODEL_2026-09-07.md`. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Native batch assembly: Shift+B now selects 1–64 prepared folders/references and a new batch JSON destination via AppKit panels. Modal recording/workspace checks precede the shared async worker; it verifies recovered state and requires every job's original producer-state hash to match the captured model before new-file-only manifest publication. Success is explicitly not generated and leaves model/inspection/undo unchanged. Tests cover assembly/reload, busy guards, duplicate/wrong-state rejection and external-change rejection without output. Strict Debug/Release export suites pass (7.01/2.74 seconds), Studio builds in both configurations and diff checks pass. Actual multi-selection/save interaction remains unverified; non-AppKit multi-selection is explicitly unsupported. Durable queue/registry and full U21/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Canonical batch manifest assembly: added `saveGenerationBatch` and CLI `prepare-generation-batch OUTPUT_JSON JOB_REFERENCE...`, accepting existing retained-digest job references without hand-written JSON. Shared admission rejects invalid/duplicate/different-state/over-budget jobs; relative paths, bounded manifest size and new-file-only publication preserve loader compatibility and existing output. No rendering, assignment or expectation recapture occurs. Library/actual CLI tests verify round-trip identity, cancellation/budget/duplicate rejection, no overwrite and unchanged producer/output state. Strict Debug/Release export suites pass (7.34/3.10 seconds), and diff checks pass. Native assembly controls and broader U21/U22/Beta GO qualification remain incomplete; changes local/uncommitted.

2026-09-07 — Live native preparation/generation/retry and Korean-input repair: exercised saved-score picker, background discovery, actual region popup, new destination, package creation, native job selection, generation/MarkerReview collection and read-only retry using a synthetic fixture. Preparation preserved generation 2; collection advanced to 3 with one unreviewed take; retry stayed at 3 and approved count stayed 0. Live QA exposed Latin-only AppKit shortcut mapping under Korean 2-Set input. Added a non-ASCII physical-key fallback without changing ASCII layouts or the separate active text-input path; after rebuilding, Cmd+Shift+I and Shift+P worked under the unchanged input mode. Release core/native suite passes 490 tests (11.36 seconds); native app exits 0 with no physical input/recorded frames; durable lineage corroborates the UI. Details and temporary evidence: `NATIVE_GENERATION_LIVE_QA_2026-09-07.md`. Multi-region/batch UI, embedded-host input, complete Designer/review/install/sing and full Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Visible saved-score preparation: Shift+P selects a saved score for bounded background inspection; after ready status, Shift+P selects its procedural region via a labeled AppKit popup and a new job-folder destination. Discovery returns retained score digest and workspace epoch/generation/selection; every modal boundary revalidates context/recording. Preparation now checks the exact owned score bytes against that digest before decoding, rejecting post-selection changes without publication. No heavy score/recipe work or automatic modal dialog is added to paint. Tests verify discovery/consumption, region/context identity and changed-byte rejection alongside existing preparation/CLI lifecycle cases. Strict Debug/Release export suites pass (7.09/3.06 seconds), both Studio builds succeed and diff checks pass. Fresh synthetic 720×520 capture verifies the shortcut label fits; exit 0, generation 4, approved 0, zero input callbacks/frames. AppKit full picker/popup/destination interaction remains unverified; other-platform region selection is explicitly unsupported. Durable queue/registry, Designer controls and full U21/U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Native background job preparation: added selected-planned-assignment preparation through the shared saved-score operation on Studio's existing worker. Captured/durable-state mismatch rejects before directory creation; preparation returns an unchanged producer model and explicitly reports not generated. Busy guards, cancellation checkpoints before publication and shutdown join apply; completed package writing is not hidden by a late stop. Tests prepare/reload a package without audio or producer mutation, verify busy guards, reject an externally changed workspace and reject a pre-cancelled request without output. Strict Debug/Release export suites pass (7.44/3.00 seconds), current Release Studio builds and diff checks pass. Native score/region/destination selection controls are still absent; this is controller integration, not completed in-app preparation or U21/U22/Beta GO acceptance. Changes local/uncommitted.

2026-09-07 — Shared saved-score job preparation: extracted the CLI's preparation pipeline into `prepareGenerationJobFromScore`, retaining exact selected track/region, planned assignment, saved recipe identity/style, score-relative path resolution, sample-rate validation and immutable package/expectation publication. CLI syntax remains unchanged. Direct tests cover relative loading, original producer/recipe identity, no producer mutation, no overwrite, wrong/ambiguous targets, changed recipe rejection and packaged-source independence; actual CLI lifecycle regressions also pass. Strict Debug/Release export suites pass (7.45/3.37 seconds), current Release Studio builds, and diff checks pass. This provides the common operation for native preparation; the native score/region/job destination selection workflow is not yet implemented. Full U21/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Native batch progress: added one coherent atomic phase/count snapshot shared with the worker, sampled only by the owner-thread status update. Studio distinguishes preflight, completed output count (including reuse) and collection pending; output completion is never labeled approval or producer commit. Future collection clears progress on success/error, while shutdown retains stop/join/result handling. Tests sample count bounds, require cleanup across success/failure/retry and exercise shutdown error cleanup without model changes. Strict Debug/Release export suites pass (5.98/1.93 seconds), both Studio builds succeed and `git diff --check` passes. Native visual observation of each phase, durable per-job queue/registry, in-app preparation and full U21/U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Visible native batch entry: Cmd/Ctrl+Shift+B opens a dedicated batch JSON picker and routes to the asynchronous batch controller; plain B and single-job import/generation remain intact. It shares recording/busy guards, audition stop and modal workspace/selection revalidation. Explicit selection captures a bounded 64-KiB manifest digest; the worker verifies those bytes and retains all original job/producer expectations. The default budget is 64 jobs/32 Mi frames, not yet UI-editable. Wrong-digest regression and all export cases pass in strict Debug/Release (6.92/2.30 seconds), both Studio builds succeed, and diff checks pass. A fresh synthetic-input 720×520 native capture shows the combined job/batch shortcut label fitting; exit 0, generation 4, approved 0 and input frames/callbacks 0. Successful batch-picker interaction remains unverified. See `GENERATION_BATCH_EXECUTION_2026-09-07.md` for trust and capture limits. Queue/progress, in-app preparation and full U21/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Native asynchronous batch generation/collection controller: added retained-digest batch loading, bounded shared input preflight, captured/durable workspace comparison and complete/partial-history recognition on the Studio worker. Fresh work reuses verified output or generates missing output, then publishes all takes through the canonical atomic batch importer. Complete-history retry preserves manual marker edits and undo; partial collection and external workspace changes reject without replacing the visible model. Tests exercise two pitch layers, one-generation MarkerReview publication, missing output generation, budget rejection, busy guards, retry preservation and partial/external-state conflicts. Strict Debug/Release export suites pass (5.98/1.82 seconds), both native Studio builds succeed, and `git diff --check` passes. Contract: `GENERATION_BATCH_EXECUTION_2026-09-07.md`. Native batch picker/queue/progress, in-app preparation and interaction/cancellation qualification remain open. No U21/U22/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Early candidate readiness admission and native dialog opening: procedural import now checks selected-strategy readiness before candidate loading/staging; final project validation already enforced this requirement. Regression verifies an unready strategy rejects even with missing input files, preserves the model and leaves the asset store empty. Rebuilt Debug/Release export and producer suites pass (7.19/3.09 seconds), the current Release Studio builds, and `git diff --check` passes. Native Cmd+Shift+I interaction opened the intended generation dialog and Cmd+Shift+G opened its path sheet. Automated clipboard entry timed out; successful file selection/generation is not claimed. The app exited 0 with synthetic input, zero recording frames, unchanged reported generation 4 and zero approved takes. Detailed limits are in `NATIVE_PREPARED_GENERATION_2026-09-07.md`. In-app preparation/registry, native batch controls and U21/U22/full Beta GO acceptance remain open; changes local/uncommitted.

2026-09-07 — Visible native prepared-job entry: new job preparation writes a bounded `job.seamjob` reference with the original manifest digest before final manifest publication. Added strict reference loading and Cmd/Ctrl+Shift+I native file-picker wiring, with retained digest use, recording guards, audition stop and modal workspace/selection revalidation before the background worker. Intake labels expose import and generation without increasing panel height. Tests cover reference identity/relative resolution, malformed fields and the controller path; strict Debug/Release export suites pass all 19 cases (18.08/15.99 seconds), and both native Studio builds succeed. A fresh configured 720×520 AppKit run was visually inspected: shortcut text fits, process exits 0, Threaded Silence Input reports zero callbacks/frames, producer generation remains 4 and approved count is 0 in the synthetic fixture. No picker/keyboard interaction or microphone/playback qualification is claimed. Reference trust/temporary capture details are in `NATIVE_PREPARED_GENERATION_2026-09-07.md`; `git diff --check` passes. In-app preparation/registry, batch UI and full U21/U22/Beta GO qualification remain open; changes local/uncommitted.

2026-09-07 — Native Studio prepared-job worker: added an async selected-assignment generation/guarded-collection operation sharing existing busy, Escape/finish/shutdown and owner-thread polling behavior. Workers verify retained job identity, selected row and captured/durable producer state; successful fresh imports adopt committed markers, while AlreadyCollected returns unchanged status without clearing manual bounds or undo/inspection caches. Tests cover wrong-row rejection before output, busy selection/save guards, real worker generation/collection, preserved manual bounds/undo on retry and external-state rejection without replacing the visible or durable model. Strict Debug/Release export suites pass all 19 cases (8.54/5.15 seconds), both native Studio builds succeed and `git diff --check` passes. Contract: `NATIVE_PREPARED_GENERATION_2026-09-07.md`. This is a controller operation, not yet visible job picker/registry UI or physical interaction/recording qualification. U21/U22 and full Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — CLI atomic batch collection and complete-history retry: added shared batch-input inspection and existing-output verification that can recover journal-owned publication but never renders missing output. `import-generated-batch WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]` verifies the original batch, rejects stale/partially collected sets, checks committed outputs and performs one canonical batch save. Fully collected retries return observed AlreadyCollected states without output folders, reassignment or model changes. Actual CLI tests publish two takes in one generation, preserve state across complete retries and absent output folders, and reject a separate partial-collection fixture unchanged. Strict Debug/Release export suites pass all 19 cases plus CLI help (6.66/1.56 seconds); `git diff --check` passes. Updated batch/CLI documentation. Native orchestration, dedicated batch-collection interruption/race/cancellation qualification and broader voice/release gates remain open; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Atomic generated batch repository collection: added bounded multi-candidate admission against one original producer hash, unique/current assignment targets and aggregate frames. Existing strict import logic stages into a private draft without per-item publication or context recapture; one writer-locked expected-generation save publishes all takes with an import-generated-batch journal event and individual expectation-bound lineage. Failed later input leaves the active model/generation unchanged; unassigned content-addressed staging may remain. Tests reject insufficient budget and a missing second WAV, verify exact model/recovery rollback, publish two unreviewed pitch-layer takes in exactly one generation and recognize both original requests. Stale repeat collection leaves state unchanged. Producer saves now reject serialized generations above the recovery reader's 64 MiB bound; deferred staging avoids redundant full rollback copies. Strict Debug export suite passes 19 cases (6.14 seconds); Release export/repository suites pass 19 + 3 cases (3.28 seconds); `git diff --check` passes. Updated `GENERATION_BATCH_EXECUTION_2026-09-07.md`. CLI/native atomic-collection orchestration and dedicated collection interruption qualification remain open; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Bounded generation batch execution: added digest-required batch-manifest loading, copied job references, full preflight of prepared inputs/shared producer context/unique IDs and assignments, and aggregate requested-frame admission. Sequential workers retain verified completed output across cancellation and reuse it on retry; the CLI exposes `run-generation-batch BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]` through the signal bridge. Tests cover two pitch-layer jobs, budget/duplicate rejection before output, cancellation after the first result, first-job reuse plus remaining rendering, relative manifest paths, retained digest checks and actual CLI budget/reuse behavior. Strict Debug/Release export suites pass all 19 cases plus CLI help (7.15/3.06 seconds); `git diff --check` passes. Contract: `GENERATION_BATCH_EXECUTION_2026-09-07.md`. Atomic batch producer collection remains explicitly unfinished: independent imports change sibling expectations and must not be bypassed by recapture. Native orchestration and wider qualification remain open; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Read-only recognition of collected generation requests: guarded imports atomically retain the canonical expectation digest as an optional seventh procedural-lineage field; legacy/manual six-field lineage remains readable but cannot prove a generated request. Added repository recognition against verified durable assets, exact request/target identity and strict candidate lineage. CLI retries return AlreadyCollected with observed state/active status instead of creating another take; fresh imports return Collected. Tests preserve producer bytes across retries and manual edits, recognize a superseded take without reactivating it, reject mismatched requests and permit recognition after disposable staging paths disappear. Strict Debug/Release export suites pass all 19 cases (5.94/2.68 seconds); Release producer repository suite passes (0.85 seconds), and `git diff --check` passes. Documentation records integrity versus authentication and older-reader limitations of the lineage extension. Native/batch orchestration and broader qualification remain incomplete; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — CLI generation signal cancellation: `run-generation` now uses scoped SIGINT/SIGTERM handlers with always-lock-free signal recording and stop requests dispatched from a normal watcher thread. Prior handlers restore on exit, setup failures prevent execution, and signal cancellation reports the conventional 128+signal status while retaining any fully committed output for verified reuse. Separate-process probe tests use the same bridge with real SIGINT before work and SIGTERM during journaled publication, verifying cancellation, expected output absence/preservation, exact audio on retry and no producer assignment. Strict Debug/Release export suites pass all 19 cases (10.46/2.58 seconds); `git diff --check` passes. Updated `GENERATION_JOB_PACKAGE_2026-09-07.md` distinguishes deterministic shared-bridge evidence from unverified interactive terminal/arbitrary timing/Windows delivery. Native/batch orchestration, pre-journal/arbitrary termination recovery and producer-commit recognition remain open. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Abrupt generation publication-exit recovery: added a dedicated test-only process probe exiting without destructors at all five journaled export publication phases. Parent-side tests observe terminal exit, rerun the same job, verify OS lock release, rollback/regeneration before receipt commit, reuse after commit, exact uninterrupted WAV digest and no producer assignment. A nonterminating fault test exposed and fixed a worker success-path gap: newly exported output now requires explicit Committed state, not merely a successful Result wrapper. Strict Debug/Release export suites pass all 19 cases (12.14/2.73 seconds); `git diff --check` passes. `GENERATION_JOB_PACKAGE_2026-09-07.md` records the evidence boundary. Power loss, arbitrary mid-write/pre-journal termination, Windows runtime, CLI signal cancellation, native/batch orchestration and producer-commit recognition remain open. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — CLI planned-assignment job preparation: added `prepare-generation WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY`. It recovers producer state, resolves exactly one planned take, verifies the selected score region and saved recipe, checks integer sample rate and prepares the frozen Final job. Capture rejects unready/nonprocedural strategies and existing take IDs. Actual POSIX CLI tests now exercise prepare → run → guarded collection into a separate producer workspace, recovering one unapproved MarkerReview take with matching gestures; unknown assignments, existing directories and duplicate IDs reject. Strict Debug/Release export suites pass all 19 cases plus CLI help (4.59/2.16 seconds); `git diff --check` passes. Build verification corrected explicit floating-point sample-rate admission before integer conversion. Native job orchestration, process-interruption qualification, batch generation semantics and already-committed result recognition remain unfinished. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Generation worker and verified output reuse: added `runGenerationJob` and CLI `run-generation JOB_DIRECTORY MANIFEST_SHA256`. Workers reload frozen inputs, acquire a persistent OS-backed exclusive lock, use existing export recovery/publication and strictly verify candidate identity/dimensions/origin/typed markers. Published output is reused only after committed receipt/file checks, without rewriting audio. Tests cover pre-cancellation, same-process and actual separate-process lock contention, first publication, library/CLI reuse, unchanged modification time, corruption rejection without replacement and exact-byte restoration. Producer assignments remain untouched. Strict Debug/Release export suites pass all 19 cases plus CLI help (7.65/6.41 seconds); `git diff --check` passes. Updated `GENERATION_JOB_PACKAGE_2026-09-07.md`. Actual process-kill recovery, signal cancellation, native/batch job orchestration and committed producer-result recognition remain open. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Frozen generation job package: added library preparation of a new manifest-last directory containing exact frozen score/recipe bytes and the original import expectation. A retained manifest digest binds job/source identity and all input digests. Bounded reload reconstructs the Final snapshot from owned bytes and requires the original render hash and exact dimensions; nominal score pitches must match the requested layer at preparation. Tests prove snapshot/source identity and exact PCM reconstruction, unchanged expectation, no replacement, wrong manifest digest rejection, changed score detection and missing recipe rejection. Strict Debug/Release export integration suites pass all 19 cases (4.16/1.75 seconds), and `git diff --check` passes. Build verification added the required explicit authoring-to-producer library dependency. Contract: `GENERATION_JOB_PACKAGE_2026-09-07.md`. Worker output publication, restart reuse, incomplete-preparation recovery and full process-interruption qualification remain unfinished. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Guarded generated-result CLI collection: added `import-generated WORKSPACE METADATA WAV RECIPE EXPECTATION SHA256 OPERATOR UTC`. It loads the retained digest-bound expectation, derives its target fields, recovers current producer state and calls the canonical guarded importer without recapturing. Actual POSIX executable tests reject stale expectations and wrong digests, accept a current request and reject a repeated call without duplicating takes or changing committed state. Strict Debug/Release export suites pass all 19 cases plus CLI help (7.02/1.63 seconds); `git diff --check` passes. Manual import remains separate. Documentation now distinguishes guarded collection from unfinished job submission, durable envelope binding and restart/idempotency orchestration. No U21 or Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Durable generation expectation storage: added strict v1 JSON encoding, new-file-only atomic publication returning a SHA-256, and bounded digest-required reload. Capture and guarded import share field validation. Tests preserve exact expectations across reload and enforce no-overwrite, wrong-digest rejection and semantic rejection of malformed versions, overflowed rates, control characters, missing/extra fields and zero dimensions even when malformed bytes have a matching supplied digest. Reloaded expectations participate in the existing stale-state/import lifecycle tests. Strict Debug/Release export integration suites pass all 19 cases (5.91/3.17 seconds), and `git diff --check` passes. Updated `GENERATION_IMPORT_EXPECTATIONS_2026-09-07.md`. The durable owning job envelope, retained digest binding, worker checkpoints and restart/resume orchestration remain unfinished; expectation files alone do not complete U21 or Beta GO. Changes local/uncommitted.

2026-09-07 — Generated-result admission expectations: added capture of canonical producer state, intended take/assignment/recipe and expected output style/render identity/dimensions. The canonical procedural importer optionally enforces that captured request before asset import, then retains its existing writer-locked durable-generation publication check. Tests reject altered outputs/targets, unsaved assignment changes and newer saved generations; early mismatches preserve model/assets, and an explicitly recaptured request imports normally as unapproved material. Strict Debug/Release export integration suites pass all 19 cases (3.70/1.67 seconds), and `git diff --check` passes. Contract: `GENERATION_IMPORT_EXPECTATIONS_2026-09-07.md`. This is a repository component, not an automatically wired durable job system: original expectation persistence, job IDs, cancellation/resume and partial-result reuse remain unfinished. No new roadmap-unit or Beta GO acceptance; changes local/uncommitted.

2026-09-07 — CLI project-to-candidate baking: added `bake-project PROJECT OUTPUT_DIRECTORY [SAMPLE_RATE]` using the existing Final export service, saved source references and project-relative recipe loading. Requires procedural references for every nonempty vocal track; emits unapproved candidate/source packages without producer assignment, approval or overwrite. Actual POSIX CLI tests compare mixed candidate PCM/markers with library export and verify relative-path resolution, unchanged source bytes, completed-output preservation, invalid rates and missing recipe selection. Strict Debug/Release export suites pass all 19 cases plus CLI help (3.81/1.58 seconds); `git diff --check` passes. Usage/boundaries: `docs/authoring/PROCEDURAL_CLI_BAKING.md`. U21 immutable assignment jobs, cancellation/resume, partial-result reuse and expected-generation stale-result admission remain unfinished; this command is not advertised as that complete workflow. No unit/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Explicit procedural timing edit preflight: extracted common affected-note geometry validation and applied it to both inferred-boundary materialization and subsequent explicit edits. Shared timing, resolved positive spans, note/nucleus bounds and nonoverlap must pass before publication. Unchanged explicit boundaries are no-ops. Expanded tests reject subsequent onset/nucleus/neighbor/note crossings without changing project, revision or render requests; a valid subsequent edit renders through the production pipeline and undoes exactly. Strict Release core suite passes 489 cases (10.36 seconds), native editor builds, and `git diff --check` passes. This addresses authoring consistency, not source/acoustic or native-interaction qualification. Full Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Source-aware timing lane and inferred-boundary editing: saved procedural tracks now project the same in-note timing policy into the phoneme lane and boundary hit testing. Inferred onset/nucleus geometry replaces onset placeholders, carries a distinct `^` indicator and preserves subpixel valid spans; sample-source geometry stays unchanged. Initial edits on an inferred note materialize dependent note timing in one composite undo step, reject nucleus/note crossings before publication, and preserve neighboring boundaries. Tests cover two zoom levels, hit testing, flag/geometry distinctions, source-policy restoration, conflict rollback, one render notification and exact undo/redo; the edited score also passes real procedural snapshot/rendering. Strict Release core suite passes all 489 cases (10.32 seconds), Release native editor builds, and `git diff --check` passes. See `PROCEDURAL_ONSET_POLICY_2026-09-07.md`. No physical native-input, accessibility or pronunciation/listening qualification is claimed. U20 and full Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Versioned default procedural onset timing: the shared timing/compiler APIs now expose an opt-in ProceduralInNote policy, selected by production procedural snapshots only. Wholly untimed single-unvoiced-onset syllables receive a bounded in-note onset, up to 60 ms/one quarter syllable, with a distinct inferred start rather than a forged explicit edit. Authored groups stay untouched; automatic prior ends stop before generated onsets. Backend recipe/source and own-note checks remain required. Compiler revision 9 and procedural timing-policy revision 1 enter render identity. Tests cover baseline timing, short/multiple syllables, explicit/partial timing preservation, ordinary untimed `さ` snapshots and actual v2 baking without saved overrides. Strict Debug timing/snapshot/export suites pass 15 + 41 + 19 cases (17.76 seconds); strict Release design/timing/snapshot/export suites pass 12 + 15 + 41 + 19 (4.58 seconds), plus 17 compiler cases (0.96 seconds). `git diff --check` passes. See `PROCEDURAL_ONSET_POLICY_2026-09-07.md`. Duration defaults remain engineering choices, not phonetic qualification; source-aware editor presentation, richer articulation and reviewed pronunciation remain open. No new U20/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Mixed candidate v2 and producer lifecycle: removed the temporary mixed-bake rejection by adding strict articulated metadata with typed vowel/frication markers and plan/source/stream revisions. Vowel-only output remains v1. The loader checks recipe/style bindings, frication rate support, both gesture classes and existing bounded hash-verified Float32 audio. Typed markers survive normal bake/import/recovery and Studio-controller manual boundary edits without changing original metadata or granting review. Tests reject incorrect kinds, bindings, styles, revisions, rate and complete attempted downgrade to v1. Final strict Debug export suite passes all 19 cases (3.00 seconds); rebuilt Debug snapshot suite passed 41 cases (13.64 seconds); final strict Release design/snapshot/export suites pass 12 + 41 + 19 cases (2.90 seconds). Release native Studio builds; `git diff --check` passes. Contract: `docs/formats/PROCEDURAL_CANDIDATE_V2.md`. Synthetic source flags are test scaffolding only. No perceptual/native-interaction qualification or new roadmap-unit acceptance; full Beta GO remains incomplete and changes are local/uncommitted.

2026-09-07 — Production articulated snapshot routing: integrated recipe-bound explicit frication/vowel preparation into the real snapshot factory, pipeline, checkpoint wrapper and typed-source Final audio export. Preserved the existing vowel renderer, active sample-edit rejection and shared timing. Plan-derived markers now carry onset-to-nucleus geometry and ownership clipping through workers/cache hits; scheduler validation checks bounded marker structure instead of a duplicate vowel whitelist. Updated procedural render identity with articulation-domain and DSP/plan revisions. Mixed candidate baking rejects before publication pending its truthful versioned format/strict ingestion. New tests exercise nonzero score origin, exact direct/pipeline/chunk PCM, cancellation/checkpoints, cache markers/work accounting, recipe identity changes, malformed markers and Final WAV equality. All 12 design + 41 snapshot + 19 export cases pass after strict Debug/Release builds (31.39/5.53 seconds); Release native editor builds and `git diff --check` passes. Verification found and fixed the scheduler's remaining vowel-only guard. Default onset policy, mixed candidate production, broader articulation and perceptual qualification remain open; no new unit or Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Recipe-bound articulation preparation: added `ArticulationPlan::compileRecipe` and `ArticulatedStream::createFromRecipe`, deriving only requested style-scoped frication settings from the verified frozen resource and using the compiled score's clock/context. Preparation validates vowel poses, complete score-note coverage and each gesture's own-note bounds, with cancellation checks and no guessed onset timing. Tests use the actual Japanese resolver plus saved phoneme overrides, compare prepared and explicit-plan PCM exactly, and reject missing timing/bindings/styles/poses, corrupt resource identity, incomplete note coverage and onsets extending into a score gap. Unused frication presets do not impose their rate constraints on vowel-only material. Strict Debug design tests pass 12 cases (2.56 seconds); strict Release design/snapshot/export tests pass 12 + 40 + 18 cases (3.84 seconds). `git diff --check` passes. Production snapshot/backend adoption, mixed-candidate metadata/baking and acoustic qualification remain open. U20 and full Beta GO remain incomplete; changes are local/uncommitted.

2026-09-07 — Frication recipe/resource identity: added bounded style-scoped source settings in schema/resource version 2, preserving canonical v1 bytes for vowel-only recipes. Articulated streams now reject plans whose frication settings differ from their frozen resource. Tests cover pre-change v1 hash preservation, full-width seeds, save/reload, invalid fields/styles/version, content-hash changes and plan mismatch. Strict Debug design tests pass all 11 cases (2.32 seconds); strict Release design/export tests pass 11 + 18 cases (2.12 seconds). Verification corrected the old v1-only resource guard; `git diff --check` passes. Production snapshot/mixed-marker/bake adoption and acoustic qualification remain open; changes are local/uncommitted and Beta GO is incomplete.

2026-09-07 — Explicit-plan articulated composition: added a worker stream combining compiled-F0 voiced tract output with scheduled frication, checking shared timing/rate/voicing and applying performance gain once after composition. Tests prove onset source separation, near-440-Hz vowel output, exact whole/chunk/checkpoint/reset behavior, cross-boundary ownership, cancellation, timing-conflict rejection and shared dynamics. Strict Debug/Release voice-design builds and all 10 cases pass (2.64/0.76 seconds); `git diff --check` passes. A diagnostic composed WAV is retained in the test's temporary directory. Production snapshot/recipe identity integration, truthful mixed-marker baking, broader articulation and CV/VC/listening qualification remain open. U20/full Beta GO are incomplete; changes remain local/uncommitted.

2026-09-07 — Scheduled frication lane: articulation plans now retain their clock/context, and `FricationGestureStream` renders absolute-time frication spans with bounded smoothstep edges, zero vowel/gap output and checkpointed filter continuity. Owned-window output and discarded-prefix DSP work are independently bounded; cancellation/failure leaves stream state unchanged. Tests cover exact whole/chunk/mid-ramp output, copies/reset, silence, invalid windows and excessive-prefix rejection. Strict Debug/Release voice-design builds and all 10 cases pass (1.74/0.64 seconds); `git diff --check` passes. Mixed voiced/aperiodic composition, recipe/snapshot binding and consonant/listening qualification remain open. U20/full Beta GO are not complete; changes remain local/uncommitted.

2026-09-07 — Explicit articulation plan: added immutable ordered vowel/frication gestures consuming shared phoneme timing. Frication requires explicit source bindings/start times and a consistent associated voiced nucleus; unknown classes, missing/duplicate timing, conflicting voicing and overlapping/out-of-context spans reject. The shared-timing fixture renders a source for its compiled onset extent. All 10 strict Debug/Release voice-design cases pass (1.77/0.65 seconds), and the final private-constructor adjustment rebuilds in both configurations; `git diff --check` passes. Mixed-source streaming, recipe/snapshot identity integration, default onset policy and CV/VC/listening qualification remain open. U20 and full Beta GO remain incomplete; changes are local/uncommitted.

2026-09-07 — U20 aperiodic excitation: added deterministic band-shaped `FricationSource` with bounded configuration/output, absolute-frame noise, independent copied filter state, reset and atomic cancellation/failure handling. Extracted the existing noise function without changing phonation arithmetic. All 9 strict Debug/Release voice-design cases pass (1.81/0.81 seconds); all 40 rebuilt Release snapshot cases pass (1.74 seconds), plus `git diff --check`. A diagnostic Float32 WAV is retained by the test. See `U20_ARTICULATION_STATUS_2026-09-07.md`. This component is not yet recipe/snapshot/phoneme integrated and does not qualify any consonant, CV/VC pair or singer. U20 and full Beta GO remain incomplete; changes are local/uncommitted.

2026-09-07 — Exportable pitch inspection evidence: added captured source/take/inventory/generation/operator/recipe context and new-file-only JSON export of bounded estimator settings, frame results and nullable summaries. Cmd/Ctrl+E uses a dedicated save-dialog purpose and rechecks modal context; the controller rejects stale/missing inspection and never modifies producer state or approves a take. Strict Debug/Release native Studio/integration builds and all 18 cases pass (2.67/0.95 seconds), including report identity parsing, invalid timestamps, no-overwrite preservation and invalidated-result rejection; `git diff --check` passes. Format is documented in `docs/formats/CANDIDATE_PITCH_INSPECTION_V1.md`. Reports are unsigned estimates, not authenticated QC admission or rights proof. Native panel/Windows execution, downstream admission and full Beta GO remain open; changes are local/uncommitted.

2026-09-07 — Candidate F0 contour: worker-prepared window-center cent estimates now render in a B-toggle pitch/wave view, sharing raw-frame zoom and boundary geometry. The view breaks unvoiced/missing/invalid-window gaps and marks vertically clipped outliers without changing model values. Tests verify centers, octave offsets, connectivity, invalid inputs, outlier retention and mode switching. Strict Debug/Release native Studio/integration builds and all 18 cases pass (2.66/0.96 seconds); inspected the 720×520 contour raster and `git diff --check` passes. This is estimator visualization, not acoustic alignment, durable QC admission or full Beta GO. Physical/native interaction qualification remains open; changes are local/uncommitted.

2026-09-07 — FFT candidate pitch analysis: added opt-in zero-padded FFT autocorrelation with prefix-energy normalization, cancellation and method-specific transform admission; Direct remains the default reference for existing callers. Candidate inspection uses FFT with 4,096-window/512-million-butterfly bounds. Five focused cases compare numerical output/voicing across rates and window shapes, verify exact budget checks, and complete an eight-second gesture that exceeded the prior candidate limits. Strict Debug/Release native Studio/integration builds and focused plus 17 integration cases pass (7.59/2.26 seconds); `git diff --check` passes. Numerical tolerance is not universal estimator qualification, and no physical-hardware speed benchmark is claimed. Durable QC, native/listening acceptance and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Selected candidate pitch inspection: added an N-key read-only worker bound to verified PCM digest, current gesture/boundary revision and inventory target. It uses adaptive unpadded windows, cancellation and explicit 256-window/500-million-term admission limits. Results expose estimated window voicing, median F0 and cents from target without approval or persistence; context changes invalidate them. The baked A4 fixture measures near 440 Hz and matches direct estimator output. Strict Debug/Release native Studio/integration builds and all 17 cases pass (3.50/1.61 seconds), covering scope, busy state, cancellation and short-span rejection. Inspected the 720×520 estimator panel and `git diff --check` passes. Efficient broader-range analysis, durable QC admission, contour review, physical interaction/listening and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Pitch-analysis safety prerequisite: added optional cancellation and output/correlation-work limits to the shared analyzer, finite configuration checks, bounded frame allocation and safe floating-point lag clamping before integer conversion. The estimator's normal output is unchanged; oversized work rejects before allocation instead of silently truncating. Dedicated tests verify exact budget admission, insufficient/zero budgets, silent-input charging, NaN/infinity/subnormal configuration, huge frame size, cancellation and normal 220-Hz output. Strict Debug focused build/test passes (2 cases, 0.96 seconds); strict Release rebuild plus all 486 core cases and 2 focused cases pass (10.82 seconds), with `git diff --check`. Candidate F0 presentation/job-specific budgets and acoustic qualification remain open; the default correlation limit preserves legacy callers. Changes remain local/uncommitted and full Beta GO is not complete.

2026-09-07 — Measured raw-signal inspector: background verified-audio loading now computes full-take peak/RMS/DC/near-full-scale count through the existing shared analyzer and presents rate/frame dimensions. The panel explicitly separates measured raw signal from gesture plans/manual bounds and approval. The recorded-PCM24 dry-take acceptance policy is unchanged; no QC approval or durable measurement record is fabricated. Independent PCM calculations pass across all 17 Debug/Release integration cases (2.42/0.95 seconds); both strict native Studio builds and `git diff --check` pass. Inspected the 720×520 measurement raster. Pitch/phonetic/listening qualification, durable QC admission and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Audition failure lifecycle: replaced app-local device ownership with an injectable owner-thread session that handles open/start failure, format changes, reported write failure, unexpected stop, normal completion and a three-second callback-progress watchdog. Failure clears active playback and releases the device before callback state. New injected-device tests exercise shutdown-time callback access and deterministic watchdog timing; the existing real-thread nonphysical test remains. Strict Debug/Release native Studio/integration builds and all 17 integration cases pass (2.31/0.95 seconds); rebuilt Release core passes all 486 cases (10.18 seconds), plus `git diff --check`. Actual hardware disconnect/reconnect, native interaction and audible qualification remain unverified. Earlier native captures refer to the older binary. Changes remain local/uncommitted; full Beta GO remains open.

2026-09-07 — Native runtime checkpoint: launched current Release Studio against an isolated copied synthetic workspace, both as its raw executable and as identical bytes in a temporary app bundle. Both runs completed their 600-second native event loop and exited 0, recovered generation 18 with manual bounds/MarkerReview and recorded zero input frames. Inspected the actual AppKit final capture; see `NATIVE_STUDIO_RUNTIME_CHECK_2026-09-07.md` for hashes and scope. The UI-control service failed to attach (raw executable unresolved, bundle attachment timeout), so keyboard/mouse/dialog/physical-audition qualification remains unverified. Both existing process handles were confirmed terminal; no restart was inferred from a timeout. This yields native startup/recovery/render/shutdown evidence, not interactive or Beta GO acceptance. Source changes remain uncommitted.

2026-09-07 — Candidate waveform zoom/pan: added bounded +/- zoom, Alt +/- half-window pan, selected-gesture recentering and cached exact-PCM viewport peaks. Paint and dragging share the visible source interval; clipped spans do not expose false boundary handles. View changes do not mutate producer data or audition ranges. Strict Debug/Release native Studio/integration builds and all 16 integration cases pass (2.84/1.01 seconds), covering zoom/pan limits, sample-exact peaks, zoomed drag mapping, clipped edges and busy rejection. Inspected the 720×520 zoomed raster; `git diff --check` passes. Owner-thread peak-build latency, native input/physical audio qualification, derived coordinates and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Candidate boundary dragging: added shared waveform geometry, selected start/end handles and a draft-only drag lifecycle. Movement clamps to raw/neighbor bounds without publishing; left release commits one undoable revision. Escape/key actions, resize and shutdown cancel before other work, and stale release restores original cached bounds/history. Tests verify no durable movement writes, cancel, commit/undo, end clamping, invalid coordinates, resize and concurrent-writer rejection. Strict Debug/Release native Studio/integration builds and all 16 integration cases pass (2.35/0.94 seconds); inspected the 720×520 draft raster and `git diff --check` passes. Native mouse/focus qualification, zoom, derived coordinates and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Candidate boundary undo/redo: added a bounded take-session history with owner-thread Cmd/Ctrl+Z, Shift+Z and Y wiring. Undo/redo append compensating attributed revisions rather than deleting lineage or reviving approval. History updates occur only after durable success; new edits clear redo, failures/no-ops preserve it, and context refresh resets session history while persisted bounds remain recoverable. Integration tests verify cross-gesture selection, generation advancement, review invalidation, redo branching, stale-writer failure without stack loss and reopen behavior. Strict Debug/Release native Studio/integration builds and all 16 integration cases pass (2.27/0.90 seconds); `git diff --check` passes. Durable undo-stack restoration, drag/zoom, derived coordinates, physical interaction/listening and full Beta GO remain open. Changes remain local/uncommitted.

Durable candidate boundary edits: added an ordered raw-boundary revision resolver and project-level chain validation, preserving original candidate lineage/PCM. Repository edits require the active take and matching journal attribution, clear prior marker/pitch review flags, and restore the prior project on rejection. Studio supports approximately-1-ms Alt-arrow start / Alt-Shift-arrow end nudges, manual-vs-planned labels, retained waveform and effective-range audition. Tests verify no-op, overlap/negative/stale rejection, original metadata preservation, review invalidation and reopen. Strict Debug/Release producer/export suites pass (3.53/1.86 seconds); final integration checks pass (2.10/0.88 seconds), rebuilt Release core passes all 485 cases (10.27 seconds), and native Studio builds in both configurations. Inspected edited 720×520 raster; `git diff --check` passes. Undo/redo, dragging/zoom, derived coordinates, physical interaction/listening and full Beta GO remain open. Changes remain local/uncommitted.

Candidate audition realtime audit: added a separately compiled CTest probe using the existing allocation harness, including an interception self-test, fixed 24-configuration/12,474-callback coverage and sample-exact output oracle. Debug/Release both report zero intercepted allocations/deallocations, registered lock/IO/logging calls and output mismatches, with executable hashes retained in build-local JSON reports. Strict builds and both candidate/original 100,000-callback playback probe CTests pass (10.72/2.16 seconds); `git diff --check` passes. The original probe contract is unchanged. Direct malloc/unregistered OS operations, physical-device timing, error-path coverage, editable boundaries and full Beta GO remain unqualified. Changes remain local/uncommitted; this evidence does not complete another roadmap unit.

Raw candidate audition: retained verified immutable PCM from background loading and added Space whole-take / Shift+Space selected-gesture playback through the existing system output adapter. The new bounded processor uses owner-prepared state, callback-owned cursor, fixed attenuation/peak protection and short boundary fades; no silent fallback or rate substitution is used. Context-changing UI actions and shutdown stop the device before releasing processor/audio. Strict Debug/Release native Studio and integration builds pass; all 16 export integration cases pass (1.78/0.84 seconds), including actual nonphysical output-thread completion and block/range/rate tests. Rebuilt Release core/native suite passes all 485 cases (9.90 seconds); `git diff --check` passes. Verification corrected a callback member typo and declared the new public platform header dependency. Physical-device playback, realtime allocation instrumentation, device interruption handling, acoustic/listening acceptance and full Beta GO remain open. Changes remain local/uncommitted.

Candidate gesture navigation: added bounded selection/window projection, Left/Right and gesture-area wheel navigation, selected waveform-span highlighting and stable-key rows. The minimum-height layout reserves a complete selected row with waveform loaded. A 24-gesture synthetic metadata fixture verifies all entries can be brought into view, endpoint/large-delta handling, busy rejection, reset behavior and unchanged live/durable project state. Strict Debug/Release affected builds and all 15 export integration cases pass (1.62/0.64 seconds); native Studio builds in both configurations and `git diff --check` passes. Inspected the final-gesture 720×520 scene capture with warnings/key/frame range intact. This completes overview navigation, not audible audition, boundary editing, acoustic review or full Beta GO. Changes remain local/uncommitted.

Raw candidate waveform preview: added strict stored-metadata audio loading and an isolated read-only waveform worker, exposed through Studio's existing P key mapping. It returns at most 1,024 exact min/max bins, preserves the producer generation/review state, rejects corrupted raw bytes and clears on selection/import changes. The scene overlays planned raw spans and labels display-only auto-scaling. Strict affected Debug/Release builds and all 15 export integration cases pass (1.53/0.65 seconds), plus `git diff --check`; native Studio builds in both configurations. Verification corrected an unsupported W-key reference and an incorrect test helper name without weakening checks. Inspected the final 720×900 raster capture. Audition, zoom/editing, full marker navigation, measured QC and full Beta GO remain open. Changes remain local/uncommitted.

Stored candidate marker overview: split reusable bounded metadata parsing from audio loading, retaining sample-rate/frame-count dimensions and null audio for metadata-only results. Studio caches validated raw-source planned markers for the selected take across open/selection/import and renders frame ranges/normalized bars without paint-time IO. Visible/total counts and separate NOT MEASURED/NOT APPROVED labels prevent an overview from implying acoustic review. Debug/Release affected builds and all 15 export integration cases pass (1.53/0.62 seconds); both native Studio builds and `git diff --check` pass. Inspected 720×900/1440×900 scene captures; fixed observed narrow-width instruction/warning clipping. Full-list navigation, waveform audition/editing, measured alignment/QC and full Beta GO remain open. Changes remain local/uncommitted.

Background producer intake: the native import action now launches an owned asynchronous worker with a private project/repository and captured inventory assignment/operator. Live publication is polled on the owner thread; managed conflicting operations and recording are disabled until collection. Escape requests cancellation. Shutdown cancels/joins/collects before reporting final durable state, and destruction joins rather than detaching. Integration tests exercise real worker import, busy selection/save/reopen/repeat rejection, malformed-recipe failure with preserved inspection/project/status, and cancellation racing the durable boundary with recovered/live equality. Strict Debug/Release export/native Studio builds pass; Debug export CTest passes (1.60 seconds), and Release export plus rebuilt core/native CTest pass (11.15 seconds, core 10.55 seconds). `git diff --check` passes. Native modal/focus/responsiveness, bounded filesystem cancellation latency, Windows runtime and measured singer QC remain unqualified. No additional roadmap unit or Beta GO completion is claimed; changes remain local/uncommitted.

Producer cancellation continuation: threaded optional stop tokens through native controller recipe loading, strict candidate verification, raw-bound import and repository save. Cancellation checks precede mutation/asset work, follow asset import and guard the first journal write; after publication starts the durable result wins. Candidate loader cancellation checkpoints now return Conflict rather than malformed-input errors. Regressions verify pre-cancelled save/import preserve durable and in-memory state, do not create initial assets, and retain controller inspection/status. Strict Debug/Release affected builds and both export/producer CTest entries pass (2.72/1.84 seconds); Release native Studio builds. A missing codec include in the new test was corrected after the first compile failed. No warning policy was weakened. Native asynchronous execution, bounded copy/hash cancellation latency, measured QC and full Beta GO remain open; this is a prerequisite, not another accepted unit. Changes remain local/uncommitted.

Latest continuation checkpoint: U3 is already locally accepted, as are U4/U5; work is not restarting those units. Completed producer writer safety before background intake: persistent OS locking, stale project/generation rejection under the lock, and generation-counter exhaustion rejection. Added competing-writer, stale-save, lock-release, interrupted-journal and symlink-lock regressions. Strict affected Debug/Release builds pass; all eight selected migration/command/persistence/context/reconciliation/timing/export/producer CTest entries pass (4.94/2.99 seconds), and `git diff --check` passes. The initial competing-writer regression also passed ten consecutive Debug runs before the additional interrupted-journal/symlink checks were added. Windows lock implementation is source-only here; native responsiveness, worker intake, measured QC and singer qualification remain open. This prerequisite does not complete another roadmap unit. Changes remain local/uncommitted.

U1's implementation and diagnostic-runtime criteria, U2's acceptance-contract implementation, U3's canonical vocabulary/migration/persistence implementation, U4's bounded shared-pronunciation/reconciliation implementation, and U5's ordered timing implementation are verified locally. The U5 requirement audit and post-fix broad/focused verification are in `U5_ACCEPTANCE_AUDIT_2026-09-06.md`; prior audits remain available. U6 is the next active implementation unit; U6–U48 remain uncompleted. The source-index closure check passed during the earlier user-requested checkpoint publication, not for the current uncommitted continuation. This does not certify any release-quality singer, installed host matrix or Beta GO.

### U1: reproducible build and auditory baseline

**Changes made:** explicit numeric conversions in WAV statistics, batch/streaming sample-rate conversion, CLAP PCM resampling, live-note fades and diagnostic-button width arithmetic. Corrected declaration-order aggregate initialization in standalone callback binding, native startup configuration and the render-status test fixture. The fixes preserve values/behavior and do not suppress compiler diagnostics.

**Pre-change evidence:** fresh strict builds failed on the implicit conversions and C++20 designated-initializer ordering. Each affected translation unit compiled after its correction. No new behavior test was added for mechanically equivalent casts; existing DSP, resampling, live-voice, native and lifecycle regressions provide behavioral coverage.

| Check | Observed result | Scope and remaining limit |
|---|---|---|
| `cmake --preset dev` | Exit 0 | Reused the verified Ninja cache; did not clear a build tree |
| `cmake --build --preset dev -j 4 -- -k 0` after repairs | Exit 0 | Full development build. Apple linker still reports duplicate static-library inputs; compiler warnings were not disabled |
| `cmake --preset release` and `cmake --build --preset release -j 4 -- -k 0` | Exit 0 | Full optimized build with the same strict compiler checks |
| Compiler negative probe using project warning flags and an unused local variable | Exit 1 with `-Werror,-Wunused-variable` | Confirms a genuine compiler warning still fails; probe read from stdin and produced no file |
| Four focused CTest suites: render coordinator, bank production, recovery/support, phase12c live voice | 4/4 passed | Freshly rebuilt binaries, not historical counts |
| `ctest --preset dev --output-on-failure --output-log build/dev/Testing/fullscope-baseline.log` | 64/65 entries passed, exit 8 | Sole failure: `seam_tracked_source_closure`, because plan and existing U60 files are not indexed. Do not waive the check or stage unrelated work solely to turn it green |
| `seam_tests` within the CTest run | 443 passed, 0 failed | Mechanical/native/domain regression coverage, not acoustic qualification |
| Release CTest: core suite, render coordinator, bank production, recovery/support, live voice | 5/5 entries passed | Optimized-build regression checks; not the entire Release CTest matrix |
| `python3 -m unittest discover -s tests/production -v` | 65 tests, 1 failure | Existing `test_support_bundle_hash_must_match_archived_raw_evidence` expects `PR-010-support-intake`; observed blocked IDs contain only `PR-002-root-chain`. The failing assertion remains intact pending focused diagnosis |
| Fresh native binary `--help` | Exit 0 | CLI argument surface |
| Native deterministic, paused, nonphysical-audio launch with isolated support root | Exit 0 through approved unsandboxed execution; AppKit frame emitted | Sandboxed launch aborted in macOS `_RegisterApplication`; the same binary/arguments worked through the approved GUI-capable path. This is not a synthesis crash or installed-release certification |

Native smoke-test output is local at `/private/tmp/seam-fullscope-u1.o1GgE3/native.ppm` and `native.png`. The run reported `window_backend=AppKit software raster + NSTextInputClient`, `audio_physical=false`, `voicebank_resolved=false`, and `render_state=idle`. The capture shows the empty score and the two-action missing-voicebank diagnostic. Full editor/viewport/character acceptance is still required by later units.

Two independent read-only visual reviewers passed this single captured diagnostic state. They confirmed intact labels, 112-by-28 buttons, an 8-pixel gap, and no text/button clipping or overlap. This does not certify interaction, CJK text, resizing, or the complete native UI. The temporary launch-debug journal was removed after recording the result here; no debug instrumentation or system-setting changes remain.

**Auditory baseline delivered:** [the retained packet](../../out/fullscope-beta/u1-auditory-baseline-20260905/input-provenance.json) contains two saved projects rendered in bank-selected and forced-Raw modes. Each of the two melody outputs is 41 seconds / 1,968,000 frames, and each unequal-rest output is 9.125 seconds / 438,000 frames. All four outputs have nonzero finite measured RMS. The packet retains 24 output artifacts, verified against its output manifest, plus exact input bytes, source patch/untracked-source archive, build configuration, executable identities, command logs, target timing, actual placements and fallback records. No fallback was reported for this baseline; that is not proof of pronunciation or musical quality.

The first native run rejected a missing schema-7 technical-lane field in the new fixtures. The fixtures and their locked hashes were corrected; the project validator was not weakened. Failed process diagnostics now point to retained command/stderr records.

Registered `seam_synthesis_quality_tests`, `seam_singing_quality_contract_tests`, `seam_singing_quality_workflow`, and `seam_public_release_python_tests` in CMake. The Python admission/runner suite passed 13 tests; its four optional native cases are exercised separately by the mandatory workflow target. That workflow executed all four native tests successfully in Debug (73.09 seconds) and Release (20.81 seconds), including the complete real render/analyze/save/provenance chain and invalid-input rejection. The focused synthesis executable also passed in both configurations.

CMake initially selected the bundled Python 3.12 without existing `jsonschema`/PyYAML test dependencies. Explicitly configuring `-DPython3_EXECUTABLE=/usr/local/bin/python3` selects the installed Python 3.14.3 environment that passed the complete production suite. No dependency was silently skipped or installed into the bundled runtime. The production suite now also passes through CTest. Another machine must supply an interpreter with the repository's existing test dependencies; the interpreter path is machine-local, not hardcoded into project CMake.

An independent read-only review found no actionable defect in the corpus admission, frozen snapshot/hash verification, diagnostic output or support-evidence repair. Builds, native workflows, Ruff checks and `git diff --check` were run by the root executor; review alone was not treated as runtime proof. Baseline audio remains diagnostic and does not qualify an original female singer.

U1 evidence binding: corpus SHA-256 `b5a8ab6f6a75a31e6322d23da88dfd8ea93ddc04a73d5e4445cfa48bd4275e6b`; source-evidence archive SHA-256 `cceefb2bdf98ef3c1c3ba98c0a68c208d06b3666af02e8c0e4f9e4745d918a0e`; executed Debug driver SHA-256 `6291ad5e6132591f17a6b3abefa178edb5c57512a5a30cfc863d4628d23e1d99`. The source archive contains the versioned working-tree patch and untracked-source archive against the HEAD above. Later edits invalidate reuse as evidence for a different source state.

### U2: full-scope acceptance contract

Implemented the mandatory EB-009 requirement in the External Beta contract and central READY evaluator. The typed registry covers all 20 R requirements, 18 V packages and 83 child cases; the closed evidence envelope requires a hash-bound full-product report. Both READY and CLOSED reject legacy eight-row candidates and forged ninth-row PASS summaries until U45's semantic validator is genuinely implemented. The reference hashes actual full-contract bytes, and the outer acceptance/candidate-root commitment binds that content transitively. The authority amendment preserves historical creator-study results while recording the user's superseding full-scope decision.

The root reader review reproduced an indefinitely waiting FIFO and oversized/ambiguous JSON reaching definition validation. `full_product_contract.py` now checks regular-file type and a 1 MiB ceiling before opening, checks opened-file identity/type/size, and reads at most the limit plus one byte. It rejects duplicate keys, nonfinite constants and exponent overflow, and nesting beyond 64 levels. All six reader tests pass, including exact-limit acceptance into definition validation and FIFO rejection in a timeout-bounded child process. The combined reader/gate suite passed 19 tests before the later contract-definition revisions. Ruff is clean, and an independent narrow security recheck found no actionable issue. This is reader-boundary evidence, not release authorization.

The separate contract-definition review identified missing canonical nonnumeric protocols, typed empirical result dimensions, and explicit whole-phrase versus forced-chunk continuity proof. A full suite run during those edits observed 140 tests with one canonical-definition mismatch; this intermediate state is retained as a failed run, not counted as a completed integration check.

Those definition repairs are now verified on the stable handoff. Versioned canonical protocol/check definitions prevent prose replacement from waiving counterbalancing, independent review, complete-song production or current-audio bounce requirements. Eleven empirical criteria require 175 typed cells with exact dimensions, units, comparators and environment/resource/provider/precision bindings. All checked-in qualification values remain unresolved. Eight existing cases explicitly require whole-versus-forced-chunk phoneme/timing/F0/phase evidence and neighbor-edit invalidation; no R/V outcome or existing case was removed.

The root executor ran all 147 External Beta tests and all 68 production Python tests successfully after handoff. Both registered suites also passed through Debug CTest (28.74 seconds total). Ruff passed on the changed gate/definition/reader modules and focused tests. The independent adversarial recheck reported no remaining finding in the three repaired areas. Root verified full-contract SHA-256 `d97e07403fbdc11a866eeb4b79ce5e1b5725cdd7a6c418f342a7c580b79d633f` (246,977 bytes); reader SHA-256 is `cff0ab840db37adb665f4df3d94cf52aa19c163a1ef2ff4b6e6775238966b3f0`.

Manual CLI checks: `--help` exited 0; the existing blocked candidate exited 3 for READY and CLOSED; a missing input exited 2 with structured diagnostics. The legacy eight-row test candidate sent through stdin also exited 3, with only EB-009 among blocked requirement IDs. It additionally reported the required unverified-archive diagnostic; no archive verification was fabricated. Rejection outputs are retained at `out/fullscope-beta/u2-ready-rejection.json`, `u2-closed-rejection.json` and `u2-legacy-ready-rejection.json`. These are rejection/engineering receipts, never accepted product evidence. U45/U46 remain responsible for actual raw-evidence semantics and all promotion-path closure.

### Support evidence repair discovered during baseline verification

The production-suite failure above identified a missing comparison, not a stale expectation. The support evidence record's `supportBundleSha256` was never compared with the intake's `bundleSha256`. A mismatched record triggered the generic root-chain check, while direct support-evidence validation returned no finding.

Added three focused tests in `tests/production/test_public_support_evidence.py`. Before implementation, missing and mismatched hashes both failed their rejection assertions; the matching-hash characterization passed. Added the missing semantic comparison in `tools/public_release/evidence_validation.py`, and included the matching hash in the test fixture's archived record before computing its roots. No assertion or signature/root check was removed.

The focused evidence, existing gate and restored-archive suites then passed 16 tests, including the original failing test. The complete production Python suite subsequently passed all 68 tests. This closes that observed binding defect only; U44's full support/crash/privacy and installed-platform acceptance remains incomplete.

### U3: bounded musical value types, partial

Added `NoteVibrato` and typed `DynamicsAutomation` as isolated domain values. Vibrato validates finite fractions, combined fades, depth 0–200 cents, period 5–500 milliseconds and phase in [0,1), even while disabled. Dynamics validates nonnegative ticks and linear gain from silence through +12 dB, holds endpoints, interpolates linearly and defaults to unity. Curve replacement rejects duplicate/unsorted points before mutation; edits retain ordered unique ticks. The current per-region bound is 16,384 dynamics points; replacing an existing point remains allowed at that limit.

The new `seam_performance_contract_tests` target was compiled first against declarations only and failed at linking the unimplemented methods. The initial six real-domain tests passed in Debug and Release. A seventh test loads the actual historical schema-2 and schema-3 vocal files, checks their score data and host-offset migration, and verifies complete canonical equality after encode/decode. It also passed in both configurations.

Added `performance_intent.hpp/.cpp`: typed parameter channels, manual replacement or explicit pitch-only offset mode, note-ID or half-open time-range ownership, and captured musical/pronunciation/ownership revisions. Four tests first linked against declarations and failed for missing implementations, then passed through the actual domain library. The revision helper reports Conflict when any captured dimension differs. It is not a take-acceptance transaction, and these ownership values are not yet persisted or consumed by rendering. Independent scoped review found no actionable value-semantics issue.

Three additional regressions exposed genuine migration-boundary defects. Notes and regions previously accepted overflowing end ticks; their validators now reject overflow before any end-tick addition, preserving the exact `INT64_MAX` boundary. The project decoder previously narrowed MIDI key 256 to 0 before validation; it now validates 0–127 before conversion. All three failures were observed before their fixes. After the final decoder repair, both the 14-test focused executable and the core suite passed through CTest in Debug (86.18 seconds total) and Release (10.18 seconds total). The focused library driver also passed by direct invocation in each configuration. Independent review of these small changes found no actionable regression.

Executed focused-binary SHA-256 identities: Debug `ec20c61c7b6cf4370f8105d56f09736279467a66a82541a663653f1acc0b4892`; Release `08efb0545819abf6ef44262b706b2c77b453bcc1767cf4256a3949fecff70464`. These bind the verified value-type/decoder slice, not an installed release or an audible expression workflow.

At this earlier isolated-value checkpoint the writer remained schema 7, with no new saved-performance field or renderer behavior claimed. The schema-8 integration below supersedes that persistence status; pronunciation identity, proposed/accepted takes and revisioned ownership remain required before U3 completes.

After the requested push, continued U3 with typed `VoiceStyleSelection` provenance and a shared sample-bank style resolver. It requires exact ID/version/SHA-256 and trusted-installed status even when the general catalog permits a development fixture. Legacy unresolved intent remains unchanged for missing, mismatched, untrusted or opaque-hash resources. An exact trusted legacy bank preserves its declared first style; a new multi-style bank requires a deliberate choice, and an absent selected style remains missing without substitution. Invalid provenance, unbounded/malformed UTF-8 style IDs and malformed manifests are rejected.

Five new scenarios first compiled against declarations and failed with missing implementations. After implementation the actual catalog/resolver driver passed all 19 performance tests in Debug and Release; Debug CTest also passed. A scoped independent reviewer found no actionable defect. At that checkpoint these were library-level intent/resolution contracts, not completed project persistence or UI migration. The continuation below wires the post-load/relink paths.

### U3 continuation: schema-8 expression/style integration, still partial

This uncommitted continuation is based on `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`. The [schema-8 implementation contract](../formats/PROJECT_JSON_V8.md) explicitly remains an in-development contract, not a frozen public format. The approved implementation plan and fixed auditory corpus were not modified.

**Implemented:** note vibrato and nullable phonetic hints, region dynamics, track style provenance, and five persisted technical-lane presentations. The codec writes schema 8, preserves version-specific legacy defaults and rejects omitted/malformed new fields, invalid numeric bounds, malformed hints/styles and future/fractional versions. Schema 7 retains its exact four-lane shape. Stored float round trips and raw decimal bounds are tested separately; tiny out-of-range values cannot become valid through narrowing. Historical schemas 2/3/5/6/7 are loaded from fixed files; schema 1 is an explicit characterization fixture and schema 4 is derived from a fixed older fixture, not mislabeled historical evidence.

The shared legacy resolver is wired into open, autosave recovery, initial runtime setup, plugin replacement and later relinking. Integration tests create, sign, pack, install and verify a real synthetic test bank. They do not fabricate trusted receipts or qualify its sound. Opening/recovering preserves input bytes and the durable base hash; safe save materializes schema 8. Missing/untrusted/mismatched exact banks stay unresolved. Plugin replacement refreshes the catalog and migrates its local replacement before publication, preserving the current document on failure.

`EditPerformanceCommand` applies bounded note/hint/dynamics/style batches as one revision with precise audio impact and field-scoped undo/redo. Bank and style changes are also coupled: unrelated singers cannot inherit legacy provenance; known style choices survive same-singer updates without substitution; a changed unresolved legacy reference resets to new intent. Verified single-style assignments select their sole style, while multi-style assignments require choice. New-project creation uses the same trusted resolution before the first save. Later legacy relinking is undoable and repeat resolution does not add a redundant style revision.

Source review found and the continuation repaired three integration defects: note duplication dropped vibrato/hints; region splitting retained unshifted full dynamics in both halves; bank replacement could misattribute a different bank's first style as legacy. Split dynamics now preserve interpolated boundary gain, right-side coordinates, empty curves, endpoint hold and full 16,384-point one-sided curves. Snapshot extraction preserves only effective dynamics plus required interpolation anchors, propagates validation errors, freezes all new note/style fields, and invalidates identity for relevant changes without including faraway dynamics edits. No new audible DSP or persisted-style rendering behavior is claimed here.

**Observed verification:**

| Executable / check | Debug | Release |
|---|---:|---:|
| Full strict build | Exit 0 | Exit 0 |
| `seam_performance_contract_tests` | 23/23 | 23/23 |
| `seam_schema8_performance_tests` | 13/13 | 13/13 |
| `seam_style_migration_tests` | 9/9 | 9/9 |
| `seam_plugin_style_migration_tests` | 3/3 | 3/3 |
| `seam_performance_command_tests` | 7/7 | 7/7 |
| `seam_performance_snapshot_tests` | 7/7 | 7/7 |
| `seam_performance_edit_preservation_tests` | 5/5 | 5/5 |
| Core `seam_tests` | 445/445 | 445/445 |
| Existing project lifecycle, voicebank and standalone suites | 32/32, 13/13, 1/1 | 32/32, 13/13, 1/1 |
| Lifecycle and voicebank source-contract checks | Both passed | Both passed |

The first broad run found two old literal schema-7 expectations in current-writer routing/export tests. They now explicitly expect schema 8; historical inputs were not blindly relabeled. The later broad run passed all production/core paths but exposed an ordering error in the new duplication test: selection IDs are unordered. The test now uses the duplicate API's returned identity, confirms pitch/source correspondence, and retains all expression/undo assertions. Only test code changed after the successful core runs; all seven focused suites were rebuilt/re-run and passed (67 cases each configuration). No failing product assertion was removed or weakened.

Retained local logs: `build/dev/Testing/u3-schema8-integration-detailed.log` and the corresponding Release path preserve the broad run, including that test defect. `u3-schema8-focused-green.log` in both Testing directories records the final seven-suite pass. Core executable SHA-256: Debug `b1d672ea8d7011549adbb0781857cb33c013ba22070831ff74b85566d9d31a78`; Release `350e81c0c8e3f3ee7432d515e4e76ee169008ddaaac5615c488e2f3da2b2aaa4`. Schema test executable SHA-256: Debug `0e923587925c8577efe0f1770a01e835e301fe9d95e1ab370b985701e0629a7b`; Release `71f69262060b7e5ae38000d522988b760f2e78c00504a055aef8f9813cad129d`. These bind development binaries, not installed-release provenance.

Independent read-only reviews found no remaining actionable issue in the atomic batch command, repaired bank/style migration and numeric-codec paths. The root executor reviewed snapshot and split/duplicate changes and ran the actual native library/integration drivers. No new visual-layout or installed-host certification is claimed. The Dynamics presentation is persisted, but the visible native technical editor still renders four lanes; the planned Dynamics editor remains outstanding.

At that checkpoint, pronunciation/ownership/take persistence remained required. The next continuation below supplies its bounded state and topology preservation. U4/U6/U8 and later units still own pronunciation compilation, audible performance, backend capabilities and the complete effective render-input projection. The study-only Python USTX bridge still rejects schema 8 and requires deliberate interchange implementation, not a blind version bump. Neither checkpoint completes U3 or increases the completed roadmap-unit count.

### U3 continuation: region-owned performance data and topology preservation

Added `RegionPerformanceState` to the canonical vocal region. It stores three durable revision values, optional pronunciation identity, manual note/range ownership, immutable proposed/rejected take payloads and separate accepted channel/scope selections. Takes bind generator/version/seed, source region, captured revisions, resource kind/identity and pronunciation resource/input/sequence digests. Whole-take acceptance is deliberately not represented by a boolean: selected channels/ranges reference payloads independently. Old proposals remain saveable without becoming accepted or current.

The new domain validation enforces typed channel units, finite values, explicit unvoiced pitch, unique ordered points, bounded current references, unambiguous ownership/selections and checked signed source-time mapping. Limits are 4,096 ownership records, 16 takes, 4,096 selections, 16,384 points per lane and 65,536 points across one region's take payloads. Distinct overlapping notes can own independent pitch; duplicate same-note or ambiguous note/range and range/range bindings cannot depend on vector order. The manual eligibility helper keeps pitch offsets separate from replacement and excludes generated pitch when manual vibrato owns the note. The runtime/compiler still needs to call this policy as part of the actual acceptance/render workflow.

The schema-8 region object now requires `performance`. Counters, source region IDs and seeds use canonical unsigned hexadecimal strings, preserving all 64 bits without JSON-number precision loss. The closed nested structures reject unknown fields/discriminants, wrong units, invalid null values, malformed counters and oversized collections. The decoder counts the aggregate point budget before constructing each domain point vector. No PCM or tensors are embedded. Saved identity fields are structural data, not proof that a resource exists, is trusted or produced the claimed output.

`transformRegionPerformance` provides shared topology handling. Delete-note commands capture one before/after aggregate per affected region, remove live note-owned bindings, preserve explicit time ranges and historical takes, and restore exact state on undo. Region/track duplication remaps current note IDs while retaining original take provenance. Splitting intersects range scopes, translates the right side and adjusts the accepted source offset without rewriting take payloads. Changed contexts clear the current pronunciation identity. The helper validates complete source notes and mappings before arithmetic or mutation.

Render snapshot extraction now preserves relevant ownership and accepted selections, omits unselected takes and clears current revision bookkeeping. Adding an unused proposal does not invalidate the snapshot; adding an accepted selection does. The same canonical data survives plugin encode/decode/replacement and a real autosave write/discover/recover path, with recovery still dirty and no extra selection silently accepted.

**Test-first evidence:** five domain cases initially linked against declarations and failed for absent implementation; they then passed. Canonical round-trip and project-reference validation subsequently failed their assertions before codec/validator wiring. Delete, split and duplicate commands each failed with nonempty live state before their shared transform was integrated. The snapshot test reproduced accepted state being dropped before the projection repair. Independent review found an overlap-validation defect that conflated different note identities; its regression failed before the identity-aware repair. A signed-offset regression also failed before replacing the unnecessary nonnegative restriction with checked signed arithmetic. Additional malformed-codec, collection-budget and real lifecycle checks exercise the implemented boundary without fabricated resources or generator output.

**Final observed verification for this continuation:** full strict Debug and Release builds both exited 0. All 14 selected CTest entries passed in Debug (91.30 seconds) and Release (19.45 seconds). The eight focused U3 executables contain 83 passing cases in each configuration: 23 existing value contracts, 15 region-state/domain/codec/topology/plugin/recovery cases, 13 schema cases, 9 style migrations, 3 plugin-style cases, 7 command cases, 8 snapshot cases and 5 edit-preservation cases. The core suite passed 445 cases in both configurations; existing lifecycle, bank and standalone suites passed 32/13/1 cases, and both source-contract checks passed. `git diff --check` passed.

Detailed retained logs are `build/dev/Testing/u3-region-performance-detailed.log` and the corresponding Release path; the CTest summaries are `u3-region-performance.log`. Region-state executable SHA-256: Debug `b854164a20ae7c9c3f0a78444a4b11617bea2d31646f4df90d093e9bae41a12d`; Release `a04b8b739f9a6dc9233ad93173fae6f1601571101b36d0d0714d746563cec406`. The source remains an uncommitted continuation of `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`; prior binary hashes are not reused for this source state.

Independent read-only recheck found no remaining actionable defect in the overlap rule, signed offsets, transform/callers or snapshot projection. That review is not runtime proof; the root ran the actual native drivers and integration tests above. No new model, production voice, listening judgment, visible ownership editor or audible advanced-expression backend was delivered by this state-persistence work.

**Remaining U3 integration:** allocate fresh durable stamps after relevant transactions without reviving stale jobs after undo/reopen; bind runtime work to document-instance/request and resource identity; persist stable phoneme-edit correspondence and explicit unresolved legacy bindings; couple manual ownership/hint/expression changes atomically; complete selected-note duplication and move/resize behavior for live ownership/selected take timing. These are not deferred from Beta GO. U38's acceptance transaction and U6/U8's audible/effective-input consumers remain mandatory downstream work. U3 is still in progress.

### U3 continuation: live-result freshness, 2026-09-06

Added an opaque `PerformanceJobContext` with an immutable source-project snapshot, a session/generation identity and a single-use completion receipt. `EditorSession` rotates generation identity after successful musical changes, including undo/redo, based on actual input comparison rather than trusting impact labels. Replacement always creates a new generation. An old context cannot become valid in another or reopened session, even with identical project bytes; retaining its identity object prevents allocator address reuse from reviving it. View-only and cosmetic/mix changes do not invalidate it, and unused proposals are excluded from musical input comparison. Ordinary transactions skip the extra comparison when no current-generation context is outstanding.

The guarded execution API validates the context before applying a command and consumes that job's receipt only after success. `ProjectDocument` forwards it with dirty-state synchronization and preserves the durable base hash. `AuthoringRuntime` uses a shared post-command path so successful results schedule the usual preview, while stale results do not mutate the score or submit another render. Validation/publication remains serialized on the editor owner thread; workers only read their captured immutable source.

Test-first checkpoints reproduced missing context, document-publication and runtime-publication implementations before their respective integration. The resulting 13-case native driver tests successful publication, duplicate completion, edit→undo/redo, view-only edits, failed commands, replacement, different/destroyed sessions, visible mutable-accessor changes, misleading impact labels, resource/tempo/style/ownership changes, selected versus unused take payloads and durable document identity. Its runtime case creates a synthetic Raw test bank, performs the real initial preview, publishes a valid result and waits for the new revision's real preview, then verifies stale completion leaves both score and render-submission count unchanged. This is workflow proof using diagnostic audio, not pitch-quality or singer qualification.

Both full strict builds exited 0. All 15 selected CTest entries passed in Debug (98.80 seconds) and Release (21.11 seconds), including 96 focused U3 cases and 445 core cases in each configuration, plus the existing 32/13/1 lifecycle/bank/standalone cases and two source-contract checks. `git diff --check` passed. Logs are `build/dev/Testing/u3-job-context-20260906.log` and the corresponding Release path. Job-context executable SHA-256: Debug `39735577af92fbcef3ca3bde405b96ec82afc5deb7e1589ec2abbebef8ef9778`; Release `e9c7ff72228652d63a17da9559b1c9eed9cdab554dfd9296f2dd85a8164a905c`.

The source remains local and uncommitted. This guard does not serialize authorization, verify resource bytes, generate a voice, or implement the U38 take-admission policy. Durable musical/pronunciation/ownership revision/hash reconciliation, stable edit bindings, coupled ownership commands and remaining topology consumers are still required. All musical mutations must use managed transactions; the legacy mutable project accessor can be checked for currently visible changes but cannot expose a transient external mutation already restored before validation. The completed roadmap-unit count is unchanged and the full Beta GO objective remains active.

### U3 continuation: coupled ownership and selected-note duplication, 2026-09-06

Implemented explicit `RegionOwnershipEdit` in the existing atomic performance command. Ownership can now change with hint/vibrato/dynamics/style in one transaction, with prepared-state conflict checks, bounded validation before mutation, overflow-safe ownership revision advancement and exact undo/redo. Ownership intent is not inferred from note values: hint-only edits do not silently claim pitch, and disabled manual vibrato may retain explicit pitch ownership. This is not yet global musical/pronunciation revision reconciliation.

Implemented `CopyNotePerformanceCommand` and wired it into the actual piano-roll duplicate composite after note creation. It copies note-scoped ownership and accepted selections, translates selected source offsets with checked arithmetic, retains immutable takes and fixed region-time scopes, and clears current pronunciation identity. Undo restores the prior aggregate before removing duplicated notes. Invalid/repeated/cyclic mappings, ownership conflicts, collection limits and offset overflow reject the operation. Duplicate start arithmetic is now checked before constructing notes. The existing UI integration test now verifies copied ownership, accepted source mapping, unchanged takes, fixed ranges and exact history, in addition to basic note expressions.

Verification: affected targets rebuilt with strict compiler settings in Debug and Release. All four selected CTest entries passed in both configurations: 12 performance-command, 5 edit-preservation, 13 runtime-context and 445 core cases (475 cases per configuration). Debug elapsed 85.03 seconds; Release 12.23 seconds. `git diff --check` passed. These were targeted builds/tests, not a new full release certification; existing duplicate-library linker warnings remain.

U3 remains open: global durable musical/pronunciation revision reconciliation, stable phoneme-edit bindings and move/resize treatment of selected take timing still require implementation. U4 has not been declared started or completed; the requested U3-to-U4 handoff is not yet fulfilled. Changes remain local and uncommitted, and the completed roadmap-unit count is unchanged. No new voice or acoustic qualification is claimed.

### U3 continuation: move/resize accepted-source mapping, 2026-09-06

Replaced the move/resize mutation loops with shared staging of affected regions. Moving a note subtracts its translation from note-scoped accepted source offsets, so the same captured phrase follows the note. Edge resizing retains the source offset, trimming or extending in that source timeline instead of slipping the source onset. Immutable take payloads, note ownership records and explicit region-time scopes remain unchanged. Complete region validation happens before any affected region is published, including for direct command calls; conflicting ownership, missing captured-source coverage, duplicate targets, invalid note geometry and checked-offset overflow reject without partially applying a multi-note edit. Undo/redo preserve exact project equality in the new regression tests.

Three new region-performance cases cover translation with fixed range selections, edge trimming, source coverage rejection, and ownership-collision rejection after another note has been staged. Affected targets compiled under strict Debug/Release settings. Both configurations passed 18 region-performance cases, 13 runtime-context cases, 5 edit-preservation cases and all 445 core cases (481 cases per configuration). Debug region/core CTest elapsed 84.47 seconds; Release's combined four entries elapsed 10.65 seconds. The Debug runtime-context/edit-preservation entries also passed separately. Explicit standard-library includes were compiled after the semantic regression run. No full-release or acoustic certification is inferred from these tests.

U3 remains open for durable musical/pronunciation revision reconciliation and stable phoneme-edit binding integration. The remaining work is implementation, not an external blocker. U4 has not yet been started. All changes remain local/uncommitted and the full-scope Beta GO goal remains active.

### U3/U4 shared prerequisite: bounded phoneme-edit correspondence, 2026-09-06

Inspection confirmed that the current Japanese adapter applies overrides by ordinal and can therefore attach an old timing lock to a newly inserted sound. Added `override_reconciliation.hpp/.cpp` in the planned phonemizer module as the matching foundation for stable saved bindings. It operates on one note's verified-compatible, unedited base sequences and retains every original edit. A rebound copy is proposed only when that exact old-to-new token pair occurs in every maximum-length ordered alignment. Symbol, role and voicing must match; timing and custom symbol payload are preserved rather than used as base identity. Repeated/removed/reordered ambiguities and missing original tokens remain explicit unresolved outcomes.

The algorithm validates note identity and contiguous ordinals, rejects edited base tokens and duplicate edit identities, and bounds inputs to 256 tokens/edits per side and 1,024 bytes per symbol before allocating the quadratic alignment tables. It does not use a greedy first-match or unique-symbol heuristic. The output is a correspondence proposal, not authorization to apply it: resolver/resource compatibility and durable source identity must be established by the integrating resolver.

Verification: the new strict Debug and Release target builds succeeded and all five focused tests passed in both configurations. Tests include the actual Japanese adapter's vowel-to-onset/vowel conversion, ambiguous repeated/deleted/reordered sounds, role/voicing changes, malformed and limit inputs, and an independent exhaustive enumeration of every ordered alignment for all 961 pairs of binary-symbol sequences of length zero through four. The oracle agrees with every proposed match. `git diff --check` passed. No full core regression rerun is claimed for this additive, not-yet-called module.

This does not yet repair the live ordinal consumer: schema bindings, pronunciation/resource revision integration and common editor/render consumption remain open. U3 is not complete and U4's dependent end-to-end behavior is not complete. The helper prepares that shared boundary without declaring a milestone handoff. Changes remain local/uncommitted; the full goal stays active.

### U3/U4 integration: live lyric reconciliation and unresolved persistence, 2026-09-06

Connected the correspondence algorithm to `BatchSetLyricsCommand` and routed `SetLyricCommand` through that same transaction. Changed regions are phonemized without overrides on both sides; compatible Japanese/unspecified-language contexts propose rebinding from base tokens, while unsupported/warned/ambiguous cases retain original edits unresolved. Already unresolved edits stay unresolved. Until stable edit IDs replace ordinal storage, key collisions preserve all originals unresolved rather than dropping an edit. Batch duplicate targets are rejected, dependencies are staged/validated before publication, and undo/redo restore lyric and override fields together.

Added persisted boolean `PhonemeOverride::unresolved`. The in-development schema-8 writer always emits it and its reader requires a boolean; versions 1–7 retain their previous false default, which is explicitly not a verified historical binding. The Japanese adapter skips unresolved edits with an explicit warning instead of applying their symbols/timing/locks. Tests exercise the real adapter after a vowel-to-onset/vowel lyric edit, unresolved repeated vowels, save/load and malformed resolution-state rejection, no automatic reactivation, batch collisions, and exact undo/redo.

The first broad regression exposed a native-editor pointer-lifetime defect: publishing the staged whole project invalidated retained region references. Repaired publication to swap only validated lyric/override fields, retaining object addresses; the same native batch-lyrics case and complete core suite then passed. A test helper typo was also corrected to the existing `stringifyJson` API. No failed check was suppressed.

Final verification: strict affected Debug/Release builds succeeded. Four selected CTest entries passed in each configuration: 21 region-performance, 13 schema-8, 5 correspondence and 445 core cases (484 per configuration). Debug elapsed 85.41 seconds; Release 13.27 seconds. `git diff --check` passed. Existing duplicate-library linker warnings remain. These tests prove the implemented lyric path, not a complete pronunciation architecture or release qualification.

U3/U4 remain incomplete: stable token/edit identities and verified legacy bindings, durable musical/pronunciation/resource revision handling, hint/articulation/resource-change reconciliation, unit/seam correspondence and a common revisioned editor/render sequence still require implementation. The model's existing pronunciation metadata is not automatically refreshed by this slice. Source remains local/uncommitted and the full Beta GO objective remains active.

### U3/U4 prerequisite: unit-span and seam-neighborhood correspondence, 2026-09-06

Inspection confirmed the next dependency gap: unit selection verifies spelling/count against the chosen bank unit but still looks up overrides by start ordinal, while phrase rendering looks up seam overrides by incoming ordinal. A matched incoming phoneme alone does not prove that the old join remains valid when its predecessor changes.

Added `reconcilePhonemeSpan` alongside the existing correspondence algorithm. A proposed start key is returned only when every token of the original span is an unambiguous ordered match and all mapped tokens remain contiguous. Insertions inside a span, removed/repeated/reordered ambiguity and missing spans return explicit unresolved `nullopt`; malformed identities, zero/excess counts and invalid base inputs return errors. Bounds are checked before arithmetic/allocation. A two-token span can verify a within-note seam neighborhood rather than matching its incoming sound alone. Cross-note seams still require the upcoming region-level identity integration.

Strict Debug/Release target builds succeeded. All eight focused correspondence cases passed in both configurations, retaining the exhaustive 961-pair oracle and adding unit movement/noncontiguity, seam predecessor changes and span-boundary tests. `git diff --check` passed. This additive API is not yet called by unit/seam consumers; their persisted resolution state, whole-span rebinding and undo integration remain required. No full-core rerun or live unit/seam repair is claimed. U3 remains open and the full goal remains active; changes are local/uncommitted.

### U3/U4 integration: unit/seam resolution state and lyric transactions, 2026-09-06

Extended unit and seam overrides with persisted `unresolved` state and included all three override collections in the lyric command's staged/captured dependencies. Complete within-note unit spans rebind only when all mapped tokens remain contiguous; within-note seams require an unchanged matched two-sound neighborhood. Ambiguity, unsupported contexts, missing spans and key collisions retain original payloads unresolved. Already unresolved entries do not silently reactivate. Undo/redo restores all dependent collections together without replacing region objects.

The unit selector ignores unresolved selections, and the concatenative renderer ignores unresolved seam parameters and unresolved unit loop/pitch-residual settings. The latter renderer path was found during consumer inspection and guarded as well. Existing synthesis tests now prove fallback selection with a retained missing unit ID, default versus active seam settings through the real concatenative renderer, and exact PCM equality when unresolved unit parameters are retained but inactive. These are diagnostic synthesis tests, not singer qualification.

Two new region-state cases cover complete unit/seam movement, changed sounds retained unresolved, codec round-trip, no revival and exact undo/redo. Strict affected builds passed. Debug and Release each passed 23 region-state cases and all 445 core cases (468 cases per configuration). The final Debug core rerun, including the additional renderer assertions, passed in 83.29 seconds; Release's combined entries passed in 11.19 seconds. `git diff --check` passed. No full-release suite is claimed.

Both new schema-8 resolution fields are required booleans; older schemas retain false defaults without claiming verified historical binding. Within-note matching is implemented, but cross-note spans/joins still need the region-level resolver: changed cross-note joins remain unresolved rather than guessing. Stable resource/token identities, other pronunciation-changing commands and a dedicated unresolved-edit UI remain open. U3 and the full Beta GO goal remain active; changes are local/uncommitted.

### U3/U4 integration: cross-note correspondence, 2026-09-06

Replaced the blanket changed-cross-note fallback in lyric transactions with `RegionPhonemeCorrespondence`. It validates region streams (maximum 4,096 tokens, unique contiguous note groups, maximum 256 tokens per note), aligns each original note against the same note ID once, and stores optional target positions for reuse. Unit spans must map fully and contiguously across the new region stream. A seam must retain both original neighboring tokens at adjacent positions; the initial boundary must remain initial. Identical sounds in another note cannot inherit an edit. Every note participating in the applied unit/join must have supported language and warning-free base resolution on both sides.

New tests cover cross-note span movement, insertion breaking adjacency, changed predecessor sounds, note-identity isolation and region/group limits. An actual lyric-command test preserves a cross-note vowel unit/join after adding sounds earlier in the first note, then makes both unresolved when the joining vowel changes, with exact undo/redo. Strict affected Debug/Release builds passed, followed by all 24 region-state, 10 correspondence and 445 core cases in each configuration (479 cases per configuration). Debug CTest elapsed 84.94 seconds; Release 11.97 seconds. `git diff --check` passed. No full-release qualification is claimed.

This supersedes the prior cross-note fallback boundary, but not the remaining durable identity or shared pronunciation compilation work. Changes remain local/uncommitted and U3/the full Beta GO goal remain active.

### U3/U4 integration: source-bound pronunciation resolver and snapshot context, 2026-09-06

Implemented `pronunciation_resolver.hpp/.cpp`. The shared Japanese entry point produces a valid `PronunciationIdentity` with bounded, length-prefixed SHA-256 input and output-sequence identities. CMake hashes the explicit bundled adapter/shared-rule/resolver/phoneme source list and tracks it as configure dependencies. Both configurations embed source-resource digest `d9524f38bfd4aaea73d117598220e3310b65208b52c021ff77d34f32900698d2`. This is bundled-source identity, not a production voice, external dictionary or compiled-binary attestation.

Bounds cover note/lyric/override counts, per-lyric and aggregate Unicode text, repeated lyric references, override symbols and resulting token count. Invalid Unicode/duplicate identities fail explicitly; empty lyric resolution retains pause/warning behavior. Input identity distinguishes unresolved retained work from the resulting active sequence and excludes non-consumed cosmetic/pitch fields.

Technical inspection now uses the shared resolver. Render snapshots resolve the full source region before projecting segment tokens, fixing lost continuation-vowel context when the preceding vowel is outside the rendered segment. They freeze the produced identity and include the resource hash plus projected token sequence in content identity. Tests prove deterministic/source/input-bound identity, unresolved versus active output distinction, expansion/Unicode bounds and a real split-segment continuation snapshot matching full-region resolution. A strict-build shadowing error was repaired by giving the pronunciation result a distinct name; no diagnostic was suppressed.

Final verification: strict affected Debug/Release builds passed. Both configurations passed all 12 correspondence/resolver cases, 9 performance-snapshot cases and 445 core cases (466 cases per configuration). Debug CTest elapsed 85.29 seconds; Release 11.31 seconds. The embedded resource hashes were independently inspected in both generated build descriptions and match. `git diff --check` passed. No full release or acoustic qualification is claimed.

This supplies runtime identity and two real consumers, not durable model reconciliation. Saving current identity after every relevant edit, stable historical edit bindings, remaining consumers/commands, hint interpretation and take-admission authorization remain open. U3/the full goal remain active and changes remain local/uncommitted.

### U3/U4 integration: transactional saved pronunciation identity, 2026-09-06

Lyric transactions now capture and publish current pronunciation identity and revision alongside phoneme/unit/seam dependencies. Each changed region advances its pronunciation revision exactly once and stores the actual bounded resolver result after reconciliation. Unsupported language or resolution failure clears prior identity rather than retaining a misleading value; valid user text remains saveable. Revision overflow rejects before publication. Undo/redo restores the exact before/after metadata and dependent edits, without replacing region objects or overwriting musical/ownership revision components.

Reconciliation now also calls the bounded shared resolver for its unedited source sequences. On failure all retained dependencies become unresolved rather than undergoing an unbounded raw-adapter pass. Tests verify saved identity equals fresh resolution, project round-trip, current identity through real plugin encode/decode/replacement and autosave recovery, exhaustion rejection, unsupported/over-limit resolution, and exact history. The old live job is explicitly verified to remain invalid after undo even though the saved identity/revision is restored: durable fields alone do not authorize publication or acceptance.

Final verification: affected strict Debug/Release builds passed. Both configurations passed 25 region-state cases and all 445 core cases (470 per configuration). Final Debug core rerun elapsed 82.72 seconds and the focused suite 0.69 seconds; Release's combined entries elapsed 10.16 seconds. `git diff --check` passed. Existing duplicate-library linker warnings remain; no full-release certification is claimed.

Other pronunciation/musical/resource edits still require consistent identity/revision maintenance, and stable historical token/edit bindings plus generated-take admission remain open. This closes the lyric persistence path, not U3 or Beta GO. Changes remain local/uncommitted and the full goal stays active.

### U3/U4 integration: phoneme-command identity maintenance, 2026-09-06

Extracted shared pronunciation refresh logic and applied it to direct phoneme upsert/reset as well as lyric edits. Changed phoneme overrides stage and validate the resulting region, advance the pronunciation revision, and save the freshly resolved identity (or clear an unavailable identity) before publishing only the relevant fields. Unchanged upserts consume no pronunciation revision. Overflow/missing/invalid operations do not partially mutate project state.

The two phoneme commands now retain exact before/after override vectors and pronunciation metadata. Undo/redo preserves original vector order and identity/revision rather than reinserting and sorting a single record, while leaving musical and ownership revision components intact. New tests cover sequential upsert/reset, current identity comparison with fresh resolution, project serialization, exact multi-step history, exhaustion rejection and unchanged edits at the counter limit.

Final verification: strict affected builds passed in Debug and Release. Each configuration passed 27 region-state cases and all 445 core cases (472 per configuration); Debug CTest elapsed 82.83 seconds and Release 9.99 seconds. `git diff --check` passed. No full release or acoustic qualification is inferred.

Direct phoneme-symbol changes still need unit/seam correspondence against their effective changed sounds; other musical/resource mutations and stable historical edit identities also remain open. This is identity maintenance for two real command paths, not U3 or Beta GO completion. Changes remain local/uncommitted and the full goal stays active.

### U3/U4 integration: direct phoneme dependency reconciliation, 2026-09-06

Direct phoneme upsert/reset now reconcile unit/seam dependencies against effective resolved before/after sounds. Temporary matching copies remove timing/lock attributes while retaining symbols, roles and voicing; this preserves valid unit/join correspondence for timing-only edits while rejecting changed sounds. The explicitly edited phoneme is not rebound or silently marked unresolved. Bounded resolution failure preserves its requested payload and marks dependent units/seams unresolved. Command history now includes all three override vectors and pronunciation metadata in the same staged publication/undo transaction.

New cases exercise symbol upsert and reset of an active symbol override with cross-note units/joins, retained payloads and fresh identity, as well as timing-only upsert/reset preserving dependencies. Both verify exact multi-step undo/redo. Strict affected builds passed in Debug/Release, and each configuration passed all 29 region-state cases plus 445 core cases (474 per configuration). Debug CTest elapsed 83.06 seconds; Release 10.20 seconds. `git diff --check` passed. No new singer or acoustic qualification is claimed; source remains local/uncommitted.

Milestone boundary recheck: the authoritative plan's U3 is canonical vocabulary, migration and coupled edit persistence, while resolver/correspondence behavior belongs to U4. These recent shared integrations are not additional completed units. The next U3 completion decision must audit its explicit migration, combined project/plugin edit round-trip and rejection/source-preservation scenarios against current code and evidence; later U4 completion work must not silently become an ever-expanding U3 prerequisite. Neither unit nor the full Beta GO goal is declared complete by this entry.

### U3 acceptance audit: historical writers and combined persistence, 2026-09-06

Added genuine historical-writer outputs for schemas 1, 4, 5, 6 and 7, with exact commits, reproducible drivers, seed/output distinctions and byte hashes under `tests/fixtures/projects/generators/README.md`. Each original codec emitted and reloaded its fixture with exact domain equality; the imported bytes were compared against generated files. Existing Phase 2/3 outputs complete the schema range. No current JSON was merely relabeled to create this evidence, and original corpus/seeds remain unchanged. Historical codec/domain/core sources were built unmodified; a schema-4 full-demo WAV warning was avoided by building/linking only the relevant original libraries, not by suppressing diagnostics.

Added one acceptance case for a coupled ownership/hint/style/dynamics/vibrato edit through actual project save/load, plugin encode/decode/runtime replacement, undo and redo; another drives invalid/future project opens and checks unchanged source bytes and live document state. Extended historical migration assertions for original note/lyric/timing/routing values and neutral new fields. Full current Debug/Release builds passed. All 15 selected U3 acceptance entries passed in both configurations (Debug 89.49 seconds, Release 16.88 seconds), with final expanded historical fixture coverage rechecked through the rebuilt schema-8 target in both. `git diff --check` passed.

The explicit audit is `U3_COMPLETION_AUDIT_2026-09-06.md`. Persistence scenarios are verified, but U3 is not declared complete: inspection found default render style selection still falls back to the manifest's first style without consulting saved track intent. Correcting that concrete consumer behavior is next. U4's remaining pronunciation work remains U4 work; the full Beta GO scope is unchanged. Source remains local/uncommitted.

### U3 complete locally; U4 continuation, 2026-09-06

Default snapshot rendering now uses saved style intent, rejects missing selected styles, rejects unresolved legacy defaults until migration and requires choice for unselected multi-style banks. Whole-project rendering no longer injects the first manifest style. Explicit API style arguments remain temporary deliberate overrides; default single-style behavior remains supported without mutating saved provenance. Tests verify the actual selected unit and cache identity, whole-project selected units, and missing-style failure without substitution. The shared fixture supplies both actual declared styles/units.

Full strict Debug/Release builds succeeded. All 15 selected U3 acceptance entries passed after this fix in both configurations (87.78/15.12 seconds). The initial project-render test expected a partial result when every track failed; inspection confirmed the existing API returns an error with style diagnostics in its context, and the test now asserts that contract. No production failure was suppressed. `git diff --check` passed.

The U3 acceptance audit now passes against the authoritative milestone scope. U4 remains active with the already implemented resolver and reconciliation work; its remaining stable historical identities, shared consumers and other edit paths are not silently counted complete. All downstream Beta GO units remain mandatory. Source remains local/uncommitted and publication/release qualification is not claimed.

### U4 continuation: shared native/embedded inspection, 2026-09-06

Removed direct Japanese adapter calls from native scene population, native phoneme/unit hit-testing and embedded-editor inspection. These paths and the technical editor now use `inspectJapanesePronunciation`, a shared UI adapter over the bounded resolver. It preserves ordinary warnings and emits explicit `ResolutionFailure` on resolver failure instead of falling back to raw phonemization. Source inspection confirms no direct adapter calls remain in the inspected native/embedded/technical paths.

Added a real native-controller scene test comparing resolved tokens/warnings and verifying over-limit text yields the shared bounded failure, plus a resolver-inspection test distinguishing ordinary empty-lyric warnings from resolution failure. The source-bound resource hash automatically changed as expected; both generated configurations embed `00251ec8895ec6d39e75cdd6a7fef6259e4346801252922c44da9190e6f2bab9`. This is current source identity, not a release attestation.

Final verification: affected strict Debug/Release builds passed, followed by 13 resolver/correspondence cases and all 446 core cases in each configuration (459 per configuration). Debug CTest elapsed 84.06 seconds; Release 9.89 seconds. `git diff --check` passed. No full-release suite or visual/acoustic qualification is inferred.

U3 remains accepted at its implementation boundary. U4 still requires stable historical token/edit/resource bindings and remaining pronunciation-changing paths; this does not claim all U4 scenarios complete. Changes remain local/uncommitted and Beta GO remains unfinished.

### U4 continuation: resolved token context addresses, 2026-09-06

Added runtime `PhonemeToken::contextId` and `lyricOwner`, populated by the shared resolver and propagated through existing editor inspection and frozen snapshot tokens. Addresses bind exact source resources, region/note/lyric ownership, the note's effective sound sequence and ordinal. Timing/lock-only edits and unrelated-note sound changes retain the address; changed note sequences invalidate it, including ambiguous repeated-sound insertion. Bound tokens require a lowercase SHA-256 address and valid lyric owner; raw adapter output remains explicitly unbound.

Sequence hash format v2 includes context addresses/owners, and input hashing includes region identity. Source configuration regenerated the same resource hash in Debug and Release: `4edb5a1fb259b6ded9967422be6001e70c943fad9649bcf1ca6c4537ca48b2be`. Tests cover address stability/invalidation, distinct duplicate sounds, owner validation and existing editor/snapshot token equality. These are context-scoped content addresses, not a claim of persistent edit lineage or authorization to apply arbitrary saved work.

Final verification: strict affected Debug/Release builds passed; each configuration passed 14 resolver/correspondence cases, 11 snapshot cases and all 446 core cases (471 per configuration). Debug CTest elapsed 84.57 seconds; Release 11.09 seconds. `git diff --check` passed. No full release or acoustic qualification is inferred.

Stable persisted historical bindings and their validation/reconciliation remain required for U4. No unit completion or Beta GO is claimed; changes remain local/uncommitted and the full goal stays active.

### U4 continuation: persisted phoneme source-context bindings, 2026-09-06

Added nullable `PhonemeOverride::sourceContextId`, bounded/validated as lowercase SHA-256 and required as a nullable field by the in-development schema-8 codec. Explicit new/changed phoneme edits obtain a binding from unedited base resolution; deliberately appended slots have a separate base-relative address. Supplied stale bindings reject changed commands before mutation. The shared resolver verifies bound records against current base contexts and suppresses mismatches in its effective copy while retaining saved user data. This bounded extra resolution has no override recursion beyond the cleared base.

Lyric reconciliation now verifies a bound original context before moving its edit, then records the matched new context. Failed verification remains unresolved rather than rebinding by ordinal. Binding data participates in input hashing and command history. Null legacy bindings retain the existing compatibility behavior and are explicitly not verified historical work; their migration and copy/split/resource-change rebinding still require implementation.

Tests cover actual command creation, project save/load equality, suppression after an out-of-band lyric change, stale-command conflict without mutation, verified lyric rebinding/undo, appended-slot versus new-base-token confusion, and malformed binding rejection. Strict affected builds passed in Debug/Release. Each configuration passed 33 region-state cases and all 446 core cases (479 per configuration). Debug core elapsed 84.07 seconds; final focused Debug/Release runs elapsed 0.93/0.48 seconds; Release core passed in 9.95 seconds. `git diff --check` passed. No full-release qualification is claimed.

U4 and Beta GO remain unfinished; U3's prior implementation acceptance is not a claim of historical-binding completion. Changes remain local/uncommitted and the full goal stays active.

### U4 continuation: region copy/split binding transfer, 2026-09-06

Added bounded `rebindTransferredPhonemeContexts` and connected it to region/track cloning and both sides of region splitting. The explicit note map is validated before mutation. Active bound records transfer only when the original payload and source binding verify, source/destination base sounds match in a supported unchanged language, and resolution has no note warning. Destination addresses reflect new region/note/lyric IDs. Stale or already unresolved bindings are never promoted; a split that removes continuation context preserves the old edit unresolved. Unbound legacy records are not invented into verified bindings.

Tests execute actual duplication/split commands and verify active copied locks, fresh addresses, unchanged original records, stale-binding retention, continuation-context loss, preserved timing payloads and exact undo/redo. Strict affected Debug/Release builds passed; each configuration passed 35 region-state cases and all 446 core cases (481 per configuration). Debug CTest elapsed 86.09 seconds; Release 11.57 seconds. `git diff --check` passed. No full-release certification is claimed.

Immutable take provenance and existing performance-state transformations remain unchanged. Selected-note duplication and historical/resource-change binding migration remain open; U4 and Beta GO are not complete. Source remains local/uncommitted.

### U4 continuation: selected-note phoneme edit copying, 2026-09-06

Extended the actual piano-roll duplication composite's `CopyNotePerformanceCommand` to copy note-scoped phoneme overrides and rebind verified contexts to copied IDs, alongside existing ownership/accepted-performance preservation. Candidate regions are validated before publication; copied key collisions and collection limits reject without partial changes. Before/after phoneme vectors are stored with performance state so undo restores dependencies before the composite removes duplicated notes. Binding work is skipped when there are no active bound records.

The existing real piano-roll duplication test now starts with a command-created timing lock and verifies its copied source-context address, preserved offset and active resolved lock, plus exact undo/redo. A new direct command regression rejects a phoneme-key collision with unchanged project state. Strict affected Debug/Release builds passed; each configuration passed 13 command cases, 5 edit-preservation cases and all 446 core cases (464 per configuration). Debug CTest elapsed 90.24 seconds; Release 11.94 seconds. `git diff --check` passed. No full-release qualification is inferred.

Selected-note cross-note unit/seam span copying and historical unbound-record migration remain open; no U4 or Beta GO completion is claimed. Changes remain local/uncommitted.

### U4 continuation: explicit pre-binding schema migration, 2026-09-06

Schemas 1–7 now migrate existing phoneme/unit/seam overrides as unresolved, preserving original keys, symbols, timing, locks, unit IDs and seam settings without inventing verified context bindings. Newer resolution/binding fields injected into an old schema do not bypass this policy. This intentionally supersedes the earlier false-default compatibility behavior: legacy manual edits remain saved but require deliberate resolution before affecting audio again. Canonical state/cache identity changes accordingly; no original project file is rewritten by decoding.

Historical-writer migration tests now check unresolved/manual-binding defaults across the actual fixture set. An explicitly labeled characterization seeded by the historical schema-4 fixture adds unit/seam controls and false binding claims, verifies payload preservation and inactive resolved timing, then saves/reopens schema 8 with exact equality. A test initializer brace error was corrected before successful builds; no production validation was weakened.

Final verification: strict affected Debug/Release builds passed; each configuration passed all 14 schema-8/migration cases and 446 core cases (460 per configuration). Debug CTest elapsed 84.84 seconds; Release 11.45 seconds. `git diff --check` passed. No full-release or acoustic qualification is claimed.

Review/rebinding UI and schema-8 unbound-record handling remain required, along with remaining U4 edit paths. This is pre-binding schema migration, not U4/Beta GO completion. Source remains local/uncommitted and the full goal stays active.

### U4 continuation: deliberate phoneme review/rebind transaction API, 2026-09-06

Added read-only `reviewPhonemeBindings` to the technical editor, returning retained unresolved/unbound/stale edit payloads, current base target tokens/context IDs and resolver warnings. Added explicit `rebindPhonemeOverride`: it verifies the reviewed source still matches, verifies the chosen target context, rejects occupied destination keys and publishes retained payload/key/context changes through the normal command/dirty/notification path. Moving to another key composes remove/upsert into one editor undo step; no automatic review acceptance is performed.

New controller tests verify read-only review, retained timing applied to the deliberately chosen current vowel, a bound resolved result, exactly one editor revision/edit notification, exact undo/redo, and unchanged project/revision/notification count for stale source, stale context or occupied-target rejection. Strict affected Debug/Release builds passed, followed by all 448 core cases in each configuration (83.38/9.83 seconds). `git diff --check` passed. No full-release qualification is inferred.

This is the backend transaction API, not a completed visible review panel. Unit/seam review and remaining U4 binding/consumer work remain open; source is local/uncommitted and Beta GO remains unfinished.

### U4 continuation: review target eligibility, 2026-09-06

Inspection found that the new review API exposed fallback tokens from unsupported/missing lyrics as if they were valid rebinding targets. It now excludes warning-affected notes and unsupported declared languages while retaining their original edits and warnings. Unaffected notes remain available. The existing commit-time review refresh enforces this eligibility as well as context identity, including language changes that leave kana/token context bytes otherwise identical.

A new regression covers unsupported declared language, empty lyric and unknown-character fallback, preserving retained payloads and unrelated targets, and rejecting the stale target without project/revision/edit-notification mutation. Strict affected Debug/Release builds passed and all 449 core cases passed in each configuration (82.45/9.46 seconds). `git diff --check` passed. No full-release qualification is claimed.

This prepares the review API for UI exposure; it does not implement the visible panel or complete U4. Changes remain local/uncommitted and Beta GO remains unfinished.

### U4 continuation: visible native/embedded phoneme review, 2026-09-06

Added a compact Review phoneme edits overlay and lower-right entry control, wired to the checked technical-editor API in standalone and embedded hosts. It displays retained key/symbol/timing/lock data, allows source and current-sound navigation, requires explicit target selection before Apply, reports stale-review failures without mutation, refreshes after success and closes with Close/Escape. Embedded callback publication also updates its dirty state. A user guide is `docs/authoring/PHONEME_EDIT_REVIEW.md`.

Mouse, Tab/Shift-Tab, Enter and accessibility actions share the same controls/geometry. Opening cancels pending drag mode, modal input blocks background edits/scroll/accessibility actions, and disabled buttons do not execute. Visual inspection exposed disabled-control focus and background-note virtualization; these were repaired and asserted in tests. A drawText overload ambiguity was fixed with explicit point types, without suppressing compiler diagnostics.

Tests drive the actual native controller/backend through open, explicit target selection, keyboard Apply, exact undo, stale-error presentation and no mutation on failure; an embedded-runtime test verifies the real callback and dirty state. Strict affected Debug/Release builds passed and all 451 core cases passed in each configuration (81.65/9.17 seconds). `git diff --check` passed. A 480x320 software-rendered capture was inspected before and after the focus repair; panel text and controls fit without overlap, and initial Tab focus lands on enabled Close. The final preview is `out/fullscope-beta/u4-review-20260906/phoneme-review-480x320.png`. This is software-frame QA, not installed-host or complete visual/acoustic qualification.

Unit/seam review and remaining U4 identity/edit-path work are still outstanding. U4 and Beta GO remain unfinished; source remains local/uncommitted and the full goal stays active.

### U4 continuation: review modal draft preservation, 2026-09-06

Opening phoneme review previously cancelled active text composition silently. The controller now rejects opening until the user finishes or explicitly cancels the text edit, preserving the pending lyric draft and avoiding the review callback entirely. Pointer move/up events are also ignored while the overlay is open, preventing hover or gesture updates in the score underneath.

A native controller regression verifies preserved composition text, unchanged project/revision, no review callback on rejected opening, successful opening after explicit cancellation, and no background pointer mutation. All 452 core cases passed in both Debug and Release (82.70/9.33 seconds). These results qualify this local regression repair, not installed-host or acoustic readiness.

Unit/seam review and remaining U4 identity/edit-path work remain open. U3 implementation acceptance remains complete locally; U4 and Beta GO remain unfinished. Changes remain local/uncommitted.

### U4 continuation: retained render-edit review and explicit seam rebinding, 2026-09-06

Added a read-only retained unit/seam inventory with the effective pronunciation identity, complete token sequence and warnings. Keeping the full sequence avoids inventing adjacency by filtering unavailable notes. The new seam-rebinding transaction checks the reviewed source payload, current pronunciation and target sequence, target occupancy, and warnings affecting either the incoming token or its predecessor. It preserves seam settings and publishes an explicit target change as one undoable editor action. Unsupported language boundaries cannot be accepted through fallback tokens.

Inspection also exposed seam command undo sorting original payload vectors. Seam upsert/remove commands now capture and restore the original vector, preserving exact saved ordering during rebinding undo. Regression coverage includes unsorted retained data, exact undo/redo, a single revision/notification, stale pronunciation, stale source payload, occupied destinations and a warning on the preceding note. An invalid empty-seam test fixture was corrected to contain an actual seam setting; production validation was not relaxed.

Final verification: affected strict Debug/Release builds passed, followed by all 454 core cases in each configuration (82.48/9.16 seconds). `git diff --check` passed. This is a backend seam transaction and retained-unit inventory, not the combined review UI or unit-rebinding implementation. U4 and Beta GO remain unfinished; changes are local/uncommitted.

### U4 continuation: explicit retained-unit span rebinding, 2026-09-06

Added `rebindUnitOverride` to the technical editor. The transaction requires the reviewed source payload and current complete pronunciation sequence to match, preserves the saved token count and unit/renderer/tuning/lock settings, and checks the whole destination span. Incomplete spans, warning-affected notes and overlaps with other active or retained unit edits reject without publication. Same-key confirmation and cross-note spans are supported. The target change publishes as one editor action; unit upsert/remove now restore original vector ordering on undo, matching the seam repair.

Regression tests cover same-key confirmation, moved binding, cross-note spans, exact project undo/redo with unsorted saved vectors, one revision/notification, stale pronunciation, changed source payload, incomplete spans, unsupported-language coverage and overlaps with both active and unresolved records. Rebinding verifies the pronunciation address, not installed unit availability or acoustic suitability; resource validation remains a separate obligation. Combined review UI and remaining U4 identity/edit paths are still open. Changes remain local/uncommitted and Beta GO remains unfinished.

Verification for retained-unit rebinding: affected strict Debug/Release builds passed; all 456 core cases passed in each configuration (82.20/9.46 seconds). `git diff --check` passed. These are local implementation checks, not installed-host or singer qualification.

### U4 continuation: combined retained-edit review UI, 2026-09-06

Extended the existing native review overlay to navigate retained phoneme, unit and seam records in one list. Unit summaries show saved ID, span, renderer and lock state; seam summaries show key, amount and overlap. Target summaries identify the end of a unit span or the preceding seam sound/initial boundary. Changing records clears target selection; Apply uses the checked backend transaction for that record type and refreshes the list. Standalone and embedded hosts wire both new callbacks, including embedded dirty-state publication. Existing modal keyboard/accessibility behavior remains shared; internal accessibility IDs stay compatible while the visible entry/title now say Review retained edits.

New tests drive the mixed native panel through navigation, explicit selection, unit/seam application, exact undo and stale-review failure without mutation. Embedded-runtime tests exercise both real host callbacks and dirty state. A 480x320 software frame was inspected with a unit record displayed: title, source, status and six controls fit without overlap. This is software-rendered UI evidence, not installed-host qualification. The authoring guide now documents the combined workflow and pronunciation-versus-resource limits.

Verification: affected strict Debug/Release builds passed and all 458 core cases passed in each configuration (81.87/9.35 seconds). `git diff --check` passed. Remaining U4 identity/edit-path and resource obligations are still open. This UI does not certify installed unit availability or singer quality. Changes remain local/uncommitted; Beta GO remains unfinished.

### U4 continuation: note-add pronunciation metadata, 2026-09-06

`AddNoteCommand` previously changed the token-producing note/lyric inputs without updating saved pronunciation metadata. It now stages and validates the region before publishing note/lyric vectors, refreshes the identity through the shared resolver for supported referenced lyrics, and increments the pronunciation revision with overflow rejection. Unsupported languages retain an absent identity rather than claiming Japanese resolution. Undo restores the exact prior identity/revision; redo restores the captured result. Other performance channels are not replaced.

A regression compares the saved identity against actual shared resolution, verifies it differs from the pre-add sequence, checks exact project undo/redo, unsupported-language invalidation and atomic revision-exhaustion rejection. Remaining note geometry/topology and dependent-edit correspondence paths still require U4 work; this change is not a complete topology reconciliation claim. Source remains local/uncommitted and Beta GO remains unfinished.

Verification for note-add metadata: affected strict Debug/Release builds passed, followed by 459 core cases and 5 performance-edit-preservation cases in each configuration. Debug CTest elapsed 83.09 seconds; Release 9.79 seconds. `git diff --check` passed. No release or installed-host qualification is inferred.

### U4 continuation: move/resize pronunciation metadata, 2026-09-06

The staged note-geometry path now captures before/after pronunciation metadata for regions whose note start times change. Moves and left-edge resizing refresh the identity through the shared resolver and advance its bounded revision; undo/redo restore captured metadata alongside the existing source-offset transformations. Pitch-only moves and duration-only resizing leave pronunciation revision unchanged because neither is an input to the current pronunciation resolver. Overflow rejects before any staged region is published.

A four-scenario regression compares persisted identity with actual resolver output, checks revision behavior, exact project undo/redo and overflow behavior (including allowing edits that do not consume a new pronunciation revision). This addresses metadata maintenance, not all dependent unit/seam correspondence after note reordering; that remains explicit U4 work. Changes remain local/uncommitted and full Beta GO remains unfinished.

Move/resize verification: affected strict Debug/Release builds passed; 460 core cases and 5 performance-edit-preservation cases passed in each configuration (Debug CTest 82.62 seconds, Release 9.40 seconds). `git diff --check` passed. These results do not establish U4 or release completion.

### U4 continuation: geometry-dependent edit correspondence, 2026-09-06

Note start-time geometry edits now reconcile dependent records before capturing the refreshed pronunciation identity. The application reuses existing bounded correspondence: phoneme bindings follow verified base-sound matches, then unit spans and seam joins follow effective sounds. The second pass starts from original unit/seam records, avoiding mapping an already remapped key twice. Noncontiguous spans, changed predecessors and other unverified matches remain saved unresolved. Captured before/after override vectors restore exact payloads and ordering on undo/redo together with pronunciation metadata.

A command regression contrasts an ordinary time shift with moving the middle note after its neighbor. The ordinary shift preserves bindings; reordering retains the broken cross-note unit and seam unresolved, while preserving a separate unit, the unchanged initial boundary and a bound timing edit. It also checks current saved pronunciation identity and exact project undo/redo. Add/delete/articulation topology paths still need separate examination; this is not U4 completion. Source remains local/uncommitted and Beta GO remains unfinished.

Geometry correspondence verification: affected strict Debug/Release builds passed, followed by 461 core cases and 5 performance-edit-preservation cases in each configuration (Debug CTest 83.01 seconds; Release 9.40 seconds). `git diff --check` passed. No installed-host or acoustic qualification is claimed.

### U4 continuation: insertion-dependent edit correspondence, 2026-09-06

`AddNoteCommand` now reconciles retained override records before computing the new pronunciation identity, using the same base-then-effective correspondence as geometry changes. Before/after phoneme, unit and seam vectors are captured with metadata; publication remains staged and undo/redo preserve original payloads and order. The shared helper is named `reconcileRetainedNoteOverrides` to describe its requirement that all original notes/records survive; it is not yet a deletion adapter.

A three-scenario command regression covers appending an unrelated note, inserting inside an existing cross-note unit/seam, and inserting a different vowel before a continuation. Unchanged bindings remain active; interrupted spans/joins and the changed continuation timing lock remain retained unresolved. Actual resolver identity and exact project undo/redo are checked. Delete/articulation paths remain separate U4 work; source is local/uncommitted and Beta GO is not complete.

Insertion correspondence verification: affected strict Debug/Release builds passed, then 462 core cases and 5 performance-edit-preservation cases passed in each configuration (Debug CTest 84.15 seconds; Release 10.16 seconds). `git diff --check` passed. No release qualification is inferred.

### U4 continuation: deletion-dependent edit correspondence, 2026-09-06

Deletion capture now stages surviving notes with the original override vectors, reconciles base/effective correspondence, then prunes records belonging to deleted notes and unused lyrics. This ordering preserves enough original context to identify broken cross-note spans and changed seam predecessors. The candidate is validated and its pronunciation identity/revision refreshed before normal command publication. Before/after override vectors are captured with the existing performance-state transformation, preserving exact surviving payloads and undo/redo ordering. Revision exhaustion rejects before deletion.

A regression deletes the middle of three notes, covering a cross-note unit, changed seam predecessor, unaffected initial boundary and both ordinary and continuation timing locks. Broken surviving relationships remain unresolved; deleted-note data follows the existing undoable removal policy. Saved identity is compared with actual shared resolution, and exact undo/redo plus overflow rejection are checked. This supersedes the previous helper restriction against deletion: original override vectors must remain intact until reconciliation, but original notes may be absent in the staged target. Articulation/lyric-reference paths still require examination. U4 and Beta GO remain incomplete; source is local/uncommitted.

Deletion correspondence verification: affected strict Debug/Release builds passed; 463 core cases and 5 performance-edit-preservation cases passed in each configuration (Debug CTest 83.16 seconds; Release 9.94 seconds). `git diff --check` passed. No release qualification is inferred.

### U4 continuation: lyric-reference and articulation transaction path, 2026-09-06

`SetNotePerformanceCommand` now builds staged note replacements instead of directly mutating note fields. Changing a lyric reference triggers shared pronunciation refresh and dependent-edit reconciliation; slur-only changes preserve pronunciation metadata because slur is not consumed by the current resolver. The correspondence helper now looks up the target note's actual lyric ID rather than reusing the old note's lyric ID, so unsupported reassigned language cannot be treated as a verified old-language match. Candidate validation and revision-exhaustion checks precede publication; captured overrides and identity support exact undo/redo.

A three-scenario regression checks slur-only preservation, reassignment to a different Japanese sound and reassignment to an unsupported language. It checks unresolved manual/unit edits, actual resolver identity or explicitly absent unsupported identity, exact project undo/redo, and bounded revision behavior. The remaining U4 acceptance needs a fresh source-level review across all consumers and topology paths rather than treating this command repair as milestone completion. Source remains local/uncommitted; Beta GO remains unfinished.

Lyric-reference transaction verification: affected strict Debug/Release builds passed, followed by 464 core cases and 5 performance-edit-preservation cases in each configuration (Debug CTest 84.44 seconds; Release 10.00 seconds). `git diff --check` passed. No U4 or release completion is inferred.

### U4 source-level acceptance review, 2026-09-06

`U4_ACCEPTANCE_REVIEW_2026-09-06.md` records a fresh requirement-by-requirement review of the current worktree. U4 remains incomplete for specific reasons: split unit/seam transfer lacks relationship verification, selected-note duplication omits those records, and phrase projection needs a span/join regression beyond the existing continuation test. The first two are source-confirmed omissions; the third is explicitly an unverified boundary risk. Next implementation order is split transfer, selected-note transfer, then phrase-boundary verification. This review inspected the existing passing logs (464 core plus 5 preservation cases per configuration); it did not rerun tests or claim broader completion. `git diff --check` passed.

### U4 continuation: mapped split/clone render-edit validation, 2026-09-06

Added bounded `validateTransferredRenderEdits` after phoneme-binding transfer in region/track cloning and both split destinations. It verifies original payload equality, supported unchanged language, warning-free effective sounds and explicit note-ID mapping. Units require every constituent token to match contiguously; seams require both predecessor and incoming sounds, or preservation of the original initial boundary. Missing/changed relationships retain payloads unresolved and never promote already unresolved records.

A split regression checks a left-side cross-boundary unit, lost right-side seam predecessor, unchanged initial and internal boundaries, changed continuation context and exact undo/redo. Broader region-state testing exposed two outdated expectations from earlier metadata changes: left-edge resize now refreshes identity/revision, and deletion now produces a current resolved identity. Assertions now compare fresh resolver output while retaining complete unchanged-performance checks where applicable. No production validation was weakened. Selected-note unit/seam copying and phrase-boundary verification remain open; U4/Beta GO remain unfinished and source remains local/uncommitted.

Split-transfer verification: affected strict Debug/Release builds passed. The unchanged 464-case core suite passed in both configurations (83.14/9.68 seconds). After correcting the two stale test expectations, all 36 region-state cases passed in each configuration (0.85/0.67 seconds). The initial combined runs failed on those two assertions; they are not represented as all-green runs. `git diff --check` passed. No installed-host or acoustic qualification is inferred.

### U4 continuation: selected-note unit/seam copying, 2026-09-06

`CopyNotePerformanceCommand` now copies unit and seam payloads whose start/incoming notes are selected. Bounds and occupied destination keys reject before publication. Only copied records are passed to mapped effective-token validation, with only the selected-note map: an uncopied neighbor cannot complete a partial copied span/join. Original records remain unchanged. Before/after vectors are captured alongside existing phoneme/performance state for exact undo/redo; fixed-region ownership semantics are unchanged.

Direct tests cover complete and partial cross-note units, internal and cross-note seams, renderer/loop-print payload preservation, unit/seam target collisions and exact undo/redo. The actual piano-roll duplication regression checks copied retained unit/seam records as well as existing phoneme and performance preservation. Its interleaved placement breaks cross-note adjacency, so those copies must be unresolved; an initial assertion incorrectly expected them active and was corrected. The contiguous direct-copy scenario independently requires active relationships. Phrase-projection relationship verification remains open; U4 and Beta GO are not complete. Source remains local/uncommitted.

Selected-note copy verification: strict affected Debug/Release builds passed. All 464 core cases passed in both configurations (82.55/9.79 seconds). After correcting the interleaved-placement expectation, 15 performance-command and 5 preservation cases passed in each configuration (focused CTest 0.65/0.77 seconds). Initial combined runs failed on that expectation and are not reported as all-green. `git diff --check` passed. No installed-host or acoustic qualification is inferred.

### U4 continuation: explicit phrase-projection relationship checks, 2026-09-06

Render snapshot construction now checks active projected unit spans and seam predecessors against the full resolved region before filtering tokens. Incomplete relationships reject with an explicit phrase-boundary conflict; the original project is not mutated and edits are not silently disabled. Already unresolved records retain their existing inactive behavior. The selector already rejected incompatible unit phone sequences, but this adds a relationship-specific boundary check and covers seams as well.

A manifest/WAV-backed snapshot regression covers units and seams split across rest-separated phrases, verifies the boundary error and unchanged source, then includes the complete relationship in one phrase and requires successful snapshot construction. It also checks rendering with explicitly unresolved retained records. This is a correctness guard, not the completed product solution: dependency-aware segmentation must still group active relationships while respecting bounded maximum duration. U4 and full Beta GO remain unfinished; changes are local/uncommitted.

Phrase-projection verification: affected strict Debug/Release builds passed, followed by 464 core cases and 12 performance-snapshot cases in each configuration (Debug CTest 83.33 seconds; Release 10.24 seconds). `git diff --check` passed. These checks do not establish dependency-aware segmentation or release qualification.

### U4 continuation: dependency-aware phrase segmentation, 2026-09-06

Phrase segmentation now resolves active unit/seam dependencies into forbidden note-boundary cuts. Overlapping relationships form atomic groups; their full duration is considered before choosing a duration split, allowing a cut before the group instead of through it. Required multi-note groups exceeding the configured maximum duration reject explicitly, as do invalid active addresses/spans and excessive dependency counts. Already unresolved edits do not force grouping. Source data is unchanged.

The manifest/audio-backed projection regression now requires default segmentation to group complete unit/seam relationships and construct successful snapshots. Deliberately partial segments still exercise the defensive snapshot rejection. A separate regression tests overlapping span/seam dependencies, duration lookahead, stable grouping, over-limit rejection and inactive-record behavior. All three findings from the U4 source review now have implementation follow-ups, but final matrix-wide verification remains required before marking U4 complete. Source is local/uncommitted; full Beta GO remains incomplete.

Dependency segmentation verification: affected strict Debug/Release builds passed, then 465 core cases and 12 performance-snapshot cases passed in each configuration (Debug CTest 83.10 seconds; Release 9.94 seconds). `git diff --check` passed. This is focused implementation evidence, not final U4 or release sign-off.

### U4 implementation acceptance and transition to U5, 2026-09-06

The final consumer sweep found and repaired the raw-phonemizer bypass in standalone coverage analysis. Coverage now uses shared bounded resolution; its existing workflow test also verifies rejection of an oversized lyric. The approved U4 criteria were reviewed against command, topology, persistence, native/embedded and render code, including the three findings and their follow-ups in `U4_ACCEPTANCE_REVIEW_2026-09-06.md`.

After rebuilding all eight relevant targets, both Debug and Release passed 574 cases across 8/8 CTest entries (85.95/12.74 seconds). `git diff --check` passed. U4 implementation acceptance is PASS locally. The next active implementation unit is U5: ordered phoneme timing, including sequential nuclei in multi-syllable notes, deterministic offset-to-frame behavior and actionable short-note conflicts. This milestone does not complete the full goal, qualify a singer or publish the uncommitted source.

### U5 start: sequential nucleus timing allocation, 2026-09-06

Added `phoneme_timing_plan.hpp/.cpp` and wired the compiler into `TimingSolver`. The bounded compiler groups tokens by note, assigns successive nucleus anchors in equal elapsed-time portions between tempo-resolved note endpoints, and retains explicit offsets relative to note start. Source sample markers remain separate. Unit placement now uses the first covered nucleus and the last covered token's end rather than giving every unit the whole note. Invalid unit spans, noncontiguous token groups, invalid offsets, insufficient frame space and absolute-tick overflow reject explicitly.

A solver regression uses actual Japanese phonemization and unit selection for `かき` within one note, requires nucleus anchors 12,000 frames apart at 48 kHz/120 BPM, and verifies a 30 ms edit shifts the second nucleus by exactly 1,440 frames. It also rejects zero-length unit coverage and an offset beyond its allocated end. A test-only `Cv` enum spelling error was corrected before successful compilation.

This is the first U5 timing layer, not full ordered-phoneme/audio qualification. Onset/coda-specific placement, source-transition feasibility, coarticulation/clipping, cross-token offset ordering, editor timing presentation and real rendered-audio/undo evidence remain to be completed. U5 and full Beta GO remain open; changes are local/uncommitted.

Initial U5 timing verification: affected strict Debug/Release builds passed; all 466 core cases passed in each configuration (81.57/9.40 seconds). `git diff --check` passed. No full U5, listening or release qualification is inferred.

### U5 continuation: ordered nucleus edits and dependent automatic ends, 2026-09-06

The timing compiler now requires strictly increasing nucleus anchors within each note. Automatic ends for the preceding syllable follow an edited next nucleus; explicit ends crossing that nucleus reject with a keyed conflict instead of silently clamping user data. A final positive-span check rejects edits leaving no space before the next nucleus. This resolves inter-nucleus ordering for the current default allocation, not full onset/coda/coarticulation constraints.

The existing actual phonemizer/selector/solver regression now moves the second nucleus earlier, verifies the preceding automatic end follows it, rejects an explicit crossing end, and rejects a reversed nucleus with a specific diagnostic. Source markers remain untouched. Short source-transition feasibility and real audio/undo verification still remain for U5. Source is local/uncommitted and full Beta GO remains open.

Ordered-edit verification: affected strict Debug/Release builds passed and all 466 core cases passed in each configuration (81.92/9.87 seconds). `git diff --check` passed. Full U5 and release qualification are not claimed.

### U5 continuation: short-transition feasibility, 2026-09-06

`TimingSolver` no longer silently extends a target end to satisfy the selected source unit's transition length. It preserves the timing-plan end and returns an actionable keyed conflict if the transition plus positive destination space cannot fit. Source sample-rate bounds and unit validation precede marker-to-frame conversion. Existing negative-preutterance reporting remains; full clipping/coarticulation policy is still outstanding.

A regression selects a real CV unit for a short note, requires a too-short conflict with unchanged project state, lengthens the note and verifies the exact tempo-resolved target end, then checks rejection of a zero source sample rate. Onset-specific edits, rendered-audio/undo verification and the remaining U5 constraints still need implementation. Source remains local/uncommitted; Beta GO is not complete.

Short-transition verification: affected strict Debug/Release builds passed and all 467 core cases passed in each configuration (81.80/9.26 seconds). `git diff --check` passed. This does not complete U5 or qualify rendered singing quality.

### U5 continuation: rendered nucleus edit and exact audio undo evidence, 2026-09-06

Tracing both phrase renderers showed that requested starts are realigned to the rendered vowel offset. Therefore changing a solver start alone cannot implement independent consonant/onset timing; that requires an explicit audio-retiming contract and remains open. No onset support is claimed from metadata changes.

Extended the existing WAV-backed raw phrase regression through an actual `EditorSession` timing command, shared pronunciation resolution, deterministic unit selection, timing compilation and raw rendering. A committed +30 ms vowel edit must move the rendered vowel, aligned unit start and composed audio start by exactly 1,440 frames at 48 kHz. Undo must restore the exact audio start and sample vector; redo must reproduce the edited output exactly. This tests the nucleus edit already implemented, not independent onset timing or singer quality. Source remains local/uncommitted; U5 and full Beta GO remain open.

Rendered-edit verification: strict Debug/Release test builds passed; all 467 core cases passed in each configuration with the extended raw-audio/undo assertions (81.63/9.91 seconds). `git diff --check` passed. This is deterministic diagnostic-audio evidence, not perceptual or production-singer qualification.

### U5 continuation: independent onset-start waveform retiming, 2026-09-06

Explicit starts on tokens preceding a unit's nucleus now set the requested placement start and must precede that nucleus. A placement flag instructs both raw and dispatched phrase renderers to retime their rendered waveform around the vowel landmark before alignment. The bounded two-segment linear resampler preserves sample count and endpoints while moving the waveform's vowel landmark to the required offset; resulting alignment honors the explicit onset start without moving the desired vowel anchor. Invalid/non-interior landmark pairs reject without changing the supplied waveform.

The WAV-backed command/audio regression now performs onset edits through both renderer paths, checks fixed vowel anchors, exact requested/aligned starts, changed waveform output and exact audio restoration on undo. A focused resampling test checks endpoint preservation, marker relocation and unchanged output on invalid input. This deterministic resampling baseline can alter timbre/pitch within warped material; it is not a claim of perceptually qualified onset processing. Coarticulation/clipping, additional phoneme-boundary semantics and remaining U5 acceptance work remain open. Source is local/uncommitted; Beta GO remains incomplete.

Onset-retiming verification: strict affected Debug/Release builds passed, followed by all 468 core cases in each configuration (81.73/11.99 seconds). `git diff --check` passed. These tests establish deterministic placement/undo behavior, not perceptual or release qualification.

### U5 continuation: timing/onset cache revision boundary, 2026-09-06

Advanced `SEAM_TIMING_SOLVER_REVISION` from 1 to 2 for sequential nuclei, explicit timing constraints and onset-aware placement, and `SEAM_RAW_RENDERER_REVISION` from 2 to 3 for vowel-landmark onset waveform retiming. `addAlgorithmRevisions` already includes both values in every snapshot identity, including dispatched rendering, so the revised audio rules cannot share the old algorithm identity. The PCM storage format itself is unchanged; no cache files were deleted.

Both regenerated build headers were inspected and contain timing revision 2 / raw revision 3. This is a manual algorithm-version boundary, not an automatic source-attestation system or a claim that future changes need no revision bump. Remaining U5 clipping/coarticulation and timing-boundary work stays open; source is local/uncommitted and full Beta GO is incomplete.

Cache-revision verification: affected strict Debug/Release builds passed; 468 core cases (including PCM cache behavior) and 12 snapshot cases passed in each configuration (Debug CTest 84.04 seconds; Release 11.68 seconds). `git diff --check` passed. No U5 or release completion is inferred.

### U5 continuation: negative-preutterance arithmetic safety, 2026-09-06

Source inspection confirms the existing timeline policy: phrase composition retains negative preutterance; region mixing clips samples before project frame zero rather than shifting surviving audio. Two extreme-position overflow hazards were repaired: composed span length is now computed with unsigned subtraction before its allocation bound, and negative region starts are converted to clipped magnitude without signed negation of the minimum frame value. Ordinary output arithmetic/policy is unchanged, so no audio-algorithm revision bump was made for this invalid/extreme-input repair.

A composer regression verifies normal negative preutterance bounds and rejection of a minimum-frame-to-positive-frame extent without allocating the impossible span. The region clipping conversion was checked by source inspection; this regression alone is not a dedicated end-to-end extreme cached-PCM test. Full project-zero acoustic alignment, coarticulation and remaining timing-boundary work remain U5 obligations. Source is local/uncommitted; Beta GO remains incomplete.

Negative-preutterance verification: strict affected Debug/Release builds passed; all 469 core cases passed in each configuration (182.46/20.12 seconds). Debug took longer than recent runs; the original session completed successfully without restarting. `git diff --check` passed. This does not complete U5 or certify acoustic clipping behavior.

### U5 continuation: end-to-end frame-zero clipping evidence, 2026-09-06

Added a WAV-backed regression through actual snapshot construction, `PhraseRenderPipeline`, `ProductionRegionRenderer` and disk PCM cache. At both 44.1 and 48 kHz, with default and explicit onset timing, the phrase must retain negative preutterance and place its vowel at frame zero. Region output must equal every surviving phrase sample after the exact negative prefix is removed (subject only to the existing mix clamp), with no time shift. Clearing memory and replaying the disk cache must produce identical output; the source project must remain unchanged.

Both strict test-target builds passed and all 13 snapshot cases passed in Debug and Release (0.85/0.39 seconds). No production code changed in this turn, so the unrelated core suite was not rerun or represented as freshly verified. `git diff --check` passed. This closes the specific frame-zero clipping evidence gap for diagnostic audio; broader U5 timing-boundary/coarticulation acceptance and perceptual singer qualification remain separate. Source is local/uncommitted and Beta GO remains incomplete.

### U5 remaining-contract source review, 2026-09-06

`U5_TIMING_ACCEPTANCE_REVIEW_2026-09-06.md` records concrete remaining gaps: intermediate token edits inside a selected unit can be ignored by placement; the editor lane uses independent role-weight geometry and shifts a default end with start-only edits; release/coarticulation policy needs a complete boundary contract; and the planned dedicated timing-test artifact is not yet present. Next work is compatible unit selection/alignment for intermediate edits, then shared display geometry and boundary policy. This source/documentation turn did not change runtime behavior or rerun tests. `git diff --check` passed. U5/Beta GO remain incomplete and source remains local/uncommitted.

### U5 continuation: explicit-boundary-compatible unit selection, 2026-09-06

Added `supportsExplicitPhonemeTiming` to describe the current renderer landmarks: first-token start, first-nucleus start and final-token end. Candidate generation excludes units that would hide other explicit boundaries. Forced incompatible units produce a keyed conflict; `TimingSolver` repeats the capability check for externally supplied/stale plans. No source phone landmarks are invented. Selector algorithm revision advances from 1 to 2 so cache identity tracks changed selection behavior.

A regression starts with a preferred long `かき` unit, edits an interior start or end, and requires two compatible CV units instead. It verifies the corresponding frame placement, renders WAV-backed audio and checks the actual aligned start/unit end, then rejects the stale long plan and a forced incompatible unit. Remaining shared display geometry, release/coarticulation and dedicated timing-matrix work remain open. Source is local/uncommitted; U5/Beta GO are incomplete.

The first full regression run exposed an authoring-runtime test expecting an interior consonant-end edit on a forced CV unit to become Ready. That expectation relied on the formerly ignored boundary. The positive submission/dirty-impact test now edits the supported vowel end; the new forced-unit regression separately requires rejection of unsupported interior edits. Neither the boundary guard nor renderer validation was relaxed.

Interior-boundary verification: strict affected Debug/Release builds passed. After updating the unsupported positive-test expectation, all 470 core cases passed in each configuration (82.68/9.74 seconds). Initial failed runs are not claimed as passing. `git diff --check` passed. Full U5 and release qualification remain open.

### U5 continuation: start-only lane edit preserves the end boundary, 2026-09-06

The phoneme lane now computes its end coordinate independently from its start and derives width afterward. Previously, changing only the start retained the old width and inadvertently moved the displayed end. Explicit end overrides still replace the end coordinate. The existing lane regression now checks preserved default ends and matching end-boundary hit targets at two zoom levels.

This fixes one confirmed UI boundary defect, not the broader shared timing-display contract. Role-weight defaults and invalid-span presentation still need replacement with a target-timing-derived model. Source is local/uncommitted; U5 and Beta GO remain incomplete.

Lane-end verification: strict affected Debug/Release builds passed. The initial test incorrectly required a specific owner at a shared boundary; it now verifies the resolved coordinate while preserving existing tie behavior. All 470 core cases then passed in each configuration (84.35/9.40 seconds). `git diff --check` passed. No complete shared-geometry or U5 acceptance is claimed.

### U5 continuation: dedicated compiler matrix and trailing-coda repair, 2026-09-06

Added the planned `tests/test_phoneme_timing.cpp` artifact and strict `seam_phoneme_timing_tests` CTest target. Five focused cases cover equal elapsed-time nucleus allocation across a tempo change at four sample rates, note-relative offset conversion at 44.1/48 kHz, trailing-coda grouping, malformed ordinals/reversed nuclei/invalid rates, and a note with fewer available frames than nuclei.

The first focused run reproduced a trailing-coda bug: clamping a lower-bound lookup to the last nucleus and then decrementing attached the coda to the penultimate syllable. Coda lookup now directly chooses the preceding nucleus using an upper bound. Timing solver revision advances to 3 to separate the changed output. Existing end-to-end synthesis and snapshot tests remain in place. Shared timing display and release/coarticulation work are still open; U5/Beta GO remain incomplete and source remains local/uncommitted.

Dedicated-timing verification: strict affected Debug/Release builds passed; after the reproduced coda failure was repaired, 5 compiler-contract, 13 snapshot and 470 core cases passed in each configuration (Debug CTest 88.70 seconds; Release 11.28 seconds). `git diff --check` passed. This does not complete the U5 contract or release qualification.

### U5 continuation: separate explicit token starts from nucleus anchors, 2026-09-06

`PhonemeTimingAnchor` now publishes `explicitStartFrame` separately from the syllable's actual `nucleusFrame`. Previously an onset override occupied the field named nucleusFrame, making it unsafe for a shared display model. The compiler retains an absent explicit start when no such target was supplied and publishes the edited shared nucleus for every token in the group. The solver consumes the explicit onset field for onset-aware placement; nucleus-free units use an explicit start when present. Timing revision advances to 4 for the changed anchor semantics.

A dedicated regression combines a negative consonant start with a positive vowel edit and checks their distinct coordinates, shared nucleus identity and absence of an invented explicit start on an unedited onset. This establishes the API distinction needed by the UI; lane integration and release/coarticulation policy are not yet complete. Source is local/uncommitted and full Beta GO remains open.

Separated-anchor verification: strict affected Debug/Release builds passed; 6 dedicated timing and 470 core cases passed in each configuration (Debug CTest 84.51 seconds; Release 9.90 seconds). `git diff --check` passed. Shared UI geometry and full U5 acceptance remain open.

### U5 continuation: shared timing-derived phoneme lane, 2026-09-06

The phoneme lane now compiles the same target timing as synthesis for nucleus spans and explicit boundaries. Unresolved source-dependent onset/geminate/coda extents are fixed-width estimated labels in an upper band, distinguished by `~`; nucleus spans occupy the lower band. Compiler failure or reversed display bounds mark tokens with `!` while retaining editable fallback geometry. `PHONEME_TIMING_DISPLAY.md` explains these semantics and the 48 kHz display reference rate.

UI regression coverage now checks shared `かき` boundaries, updated automatic ends, estimate/conflict flags and two-band separation, alongside the existing zoom/end-hit checks. Native integration testing exposed unit-row hit testing inheriting vertical bands; it now uses horizontal coverage. Software-frame inspection exposed double-added keyboard offsets and label clipping problems; phoneme/unit/seam painting now uses absolute model coordinates and clips compact text inside each band. No renderer audio behavior changed in this UI integration. Release/coarticulation contract work remains open; U5 and Beta GO are incomplete and source is local/uncommitted.

Shared-lane verification: strict affected Debug/Release builds passed; after repairing the unit-row regression and final paint bounds, all 470 core cases passed in each configuration (82.55/9.58 seconds). A 960x720 software frame was inspected iteratively; final bars align with notes and compact labels remain inside their bands. This is software-frame evidence, not installed-host visual certification. `git diff --check` passed. Full U5/release qualification remains open.

### U5 continuation: region-owned release boundaries, 2026-09-06

The timing compiler validates owning region bounds and note containment, permits an explicit release beyond its note within that region, and rejects a release past the region with an actionable conflict. It does not clamp saved offsets. Distinct overlapping notes retain independent anchors rather than inheriting within-note ordering constraints. Timing revision advances to 5 for the new policy; the authoring guide now describes the release boundary and unchanged frame-zero clipping behavior.

New compiler tests check valid extended releases, unchanged overrun input, note containment and overlapping-note independence. A WAV-backed snapshot/pipeline regression verifies the actual audio tail ends at the requested extended release. Strict affected Debug/Release builds passed, followed by 30 synthesis, 8 timing-contract and 14 snapshot cases in each configuration (52 cases; CTest 4.66/1.99 seconds). The broad core suite was not rerun or claimed as freshly passing. `git diff --check` passed. Coarticulation interactions and final U5 acceptance remain open; source is local/uncommitted and Beta GO remains incomplete.

### U5 continuation: post-vowel transition feasibility independent of onset edits, 2026-09-06

The short-transition check now measures the required stable-region transition from the vowel anchor rather than from the requested consonant start. Previously, a very early explicit onset could make an impossible short vowel span pass, while a late/compressed onset could make an otherwise valid post-vowel interval fail. Default placement remains algebraically equivalent; explicit onset feasibility is corrected. Timing revision advances to 6.

The short-note regression now rejects an early onset that cannot rescue the short post-vowel span and accepts a shortened onset when sufficient post-vowel time remains. Strict affected Debug/Release builds passed, followed by 30 synthesis, 8 timing-contract and 14 snapshot cases per configuration (52 cases; CTest 4.24/2.03 seconds). `git diff --check` passed. The broad core suite was not rerun or claimed as fresh evidence. Final U5/coarticulation acceptance remains open; source is local/uncommitted and Beta GO is incomplete.

### U5 continuation: checked source-marker frame conversion, 2026-09-06

Source marker conversion now uses checked integer quotient/remainder rescaling with nearest-frame rounding rather than calling `llround` on a potentially out-of-range floating-point product. Preutterance subtraction and timed-unit extent are checked before downstream signed subtraction/allocation, using the existing 100-million-frame composition bound. Timing revision advances to 7 for explicit rounding/range semantics.

A regression verifies exact 48 kHz-to-44.1 kHz marker rounding and rejects structurally valid but enormous marker metadata at an upsampled rate before unsafe conversion. Strict affected Debug/Release builds passed; 31 synthesis, 8 timing-contract and 14 snapshot cases passed in each configuration (53 cases; CTest 4.53/2.16 seconds). `git diff --check` passed. The broad core suite was not rerun or claimed as freshly verified. Final U5/Beta GO acceptance remains open and source is local/uncommitted.

### U5 continuation: explicit syllable membership and boundary provenance, 2026-09-06

The compiled timing contract now includes note-local syllable indices, optional real-nucleus keys and explicit-end provenance, alongside the existing optional explicit start. Nucleus-free material keeps a fallback timing anchor without inventing a vowel identity. This fills a data-contract requirement from the virtual-singer roadmap without changing placement or audio calculations, so no algorithm revision was advanced.

A dedicated test checks membership across two syllables, start/end provenance and a breath-only group without a real nucleus. Strict Debug/Release timing/synthesis/snapshot targets and the editor timing consumer compiled successfully. All 31 synthesis, 9 timing-contract and 14 snapshot cases passed per configuration (54 cases; CTest 4.25/1.99 seconds). `git diff --check` passed. The broad core/native runtime suite was not rerun or claimed as freshly passing. Final U5 and full Beta GO acceptance remain open; source is local/uncommitted.

### U5 continuation: bounded seam-window composition evidence, 2026-09-06

Added a deterministic composer regression using constant outgoing/incoming samples. It checks exact handoff positions for 64- and 256-frame windows, immediate incoming replacement for a zero window, negative-window rejection, preserved overall start/length, and unchanged incoming placement/landmark/source samples. The guide now states that the setting bounds crossfading, not incoming-unit duration, and distinguishes phase-basis processing from target timing.

No production behavior changed. Strict Debug/Release synthesis-test builds passed and all 32 synthesis cases passed in each configuration (3.46/1.00 seconds). `git diff --check` passed. Other suites were not rerun or claimed as fresh evidence. This closes the specific overlap-window behavior check; final U5 acceptance still requires a consolidated review. Source is local/uncommitted and full Beta GO remains incomplete.

### U5 consolidated review: remaining multi-nucleus unit mapping, 2026-09-06

Rechecked the approved U5 criteria against current selection, timing placement, source markers and regression coverage. Explicit interior edits now force compatible units, but unedited multi-nucleus units can still be selected while `TimingSolver` consumes only their first nucleus anchor. The existing long-unit selection test and one-landmark placement/source contracts directly establish the gap. The separate-CV and first-nucleus audio tests do not prove later anchors within a single unit.

`U5_TIMING_ACCEPTANCE_REVIEW_2026-09-06.md` now records the remaining completion path: carry all target anchors, use verified per-phone/nucleus source landmarks, map them through rendering, and verify nonfirst-anchor audio/undo/cache behavior. Smaller-unit fallback or explicit capability rejection is acceptable for unaligned resources but cannot replace general multi-nucleus support in the full product. This was a source/documentation review, not a new runtime implementation or test run. `git diff --check` passed. U5/Beta GO remain incomplete; source is local/uncommitted.

### U5 continuation: retain all phoneme targets in unit placement, 2026-09-06

`TimedUnitPlacement` now carries the complete ordered compiler records for its covered tokens, preserving later nucleus positions, syllable membership and explicit-boundary provenance instead of discarding them at placement. The solver requires ordered, exactly-once token coverage and bounds entry count, preventing malformed duplicate plans from amplifying target storage. Existing valid audio behavior is unchanged; renderers have not yet consumed the additional landmarks.

The long-unit regression checks all four `かき` target keys, both distinct nucleus identities/positions and the shared boundary, and rejects duplicated coverage. Strict Debug/Release targeted builds passed; 32 synthesis, 9 timing-contract and 14 snapshot cases passed per configuration (55 cases; CTest 4.54/2.00 seconds). `git diff --check` passed. The broad core suite was not rerun. Verified source-landmark support and waveform mapping are the next required work; U5/Beta GO remain incomplete and source is local/uncommitted.

### U5 continuation: audio-bound source phoneme alignment contract, 2026-09-06

Added `SourcePhonemeAlignment` and `SourcePhoneLandmark` as a bounded authored-source contract. Validation requires a matching unit ID, lowercase SHA-256 equal to the caller's verified encoded-audio hash, complete phone-string coverage (maximum 256 landmarks), strict frame ordering and containment within the unit and decoded audio. Empty/partial mappings, mismatched audio/phones, duplicate/reversed frames and out-of-bounds positions reject. No uniform source-position inference or claim of acoustic/phonetic review is made.

Strict Debug/Release timing-target builds passed and all 10 timing-contract cases passed per configuration (0.41/0.47 seconds), including valid four-phone alignment and seven malformed variants plus audio/hash bounds. `git diff --check` passed. This is a new validation component; source-alignment persistence and waveform consumption are not yet implemented. Other suites were not rerun. Source remains local/uncommitted; U5/Beta GO are incomplete.

### U5 continuation: bounded source-alignment JSON codec, 2026-09-06

Added version-1 source-alignment encode/decode functions with exact root/landmark shapes, integer-only frame positions, bounded parsing and domain revalidation against the caller's actual unit, verified audio hash and decoded frame count. Encoding validates first and checks final size. The separate format is documented in `docs/formats/SOURCE_PHONEME_ALIGNMENT_V1.md`; existing manifest schema and installed banks are unchanged. A compile-time initializer brace error was corrected without relaxing checks.

Strict Debug/Release timing-target builds passed; all 11 contract cases passed per configuration (0.45/0.35 seconds), including round-trip equality, stale audio, future schema, fractional frame, unknown field, empty coverage and oversized input rejection. `git diff --check` passed. This delivers serialization, not file/package registration or renderer integration; those remain required. Other suites were not rerun. Source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: multi-anchor source-to-target map construction, 2026-09-06

Added `SourceTargetMap` construction from validated audio-bound source alignment and complete unit target records. It includes unit endpoints, real nucleus anchors, explicit starts and explicit ends; an interior end maps to the next authored phone boundary, while the final end maps to the exclusive source audio end. Identical knots coalesce; contradictory same-source targets, crossed times, repeated target keys, coverage mismatch and out-of-bounds target extents reject. No source landmarks are fabricated.

Strict Debug/Release timing-target builds passed and all 12 contract cases passed per configuration (0.53/0.34 seconds). The new case verifies both nuclei in a four-phone unit and rejection of crossed nuclei/conflicting adjacent explicit boundaries. `git diff --check` passed. This is map construction only: waveform application, package loading and cache identity integration remain required. Other suites were not rerun; source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: bounded multi-anchor waveform mapping operator, 2026-09-06

Added `applySourceTargetMap` for mono sample spans. It validates ordered source/target knots against source bounds, caps source and output at 32 Mi frames and maps at 770 knots, checks finite input and cancellation, and uses piecewise linear interpolation with exclusive-end handling. It returns the mapped absolute start and samples without mutating source data. This is deterministic local-rate resampling, not pitch-preserving DSP or perceptually qualified singing synthesis.

A regression places two distinct waveform landmarks at exact output frames, moves the later target independently, and rejects cancellation, crossed targets, out-of-bounds source ends, oversized output and NaN input. Strict Debug/Release timing-target builds passed; all 13 contract cases passed per configuration (0.66/0.40 seconds). `git diff --check` passed. The operator is not yet connected to source-resource discovery, render dispatch or cache identity. Other suites were not rerun; source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: callable aligned raw-unit renderer, 2026-09-06

Added `renderAlignedRawUnit` to combine source/audio-bound alignment validation, multi-anchor map construction, waveform mapping and unit gain in one cancellable call. It rejects requested root-pitch transposition rather than ignoring `targetMidi`, validates gain output for finiteness, and retains the documented limitation that local-rate mapping itself is not pitch-preserving. This is an explicit raw path, not automatic dispatcher fallback or complete F0 synthesis.

The source-map regression now calls the combined renderer with diagnostic samples at two authored nucleus landmarks, moves the second target by 1,440 frames while preserving the first, checks gain, and rejects stale audio identity and unsupported transposition. Strict Debug/Release timing builds passed and all 13 contract cases passed per configuration (0.42/0.33 seconds). `git diff --check` passed. Resource discovery, normal phrase-dispatch wiring and cache identity remain open; other suites were not rerun. Source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: alignment discovery, frozen resources and render identity, 2026-09-06

Snapshots now discover optional selected-unit sidecars at `alignments/<sha256(unit-id-bytes)>.json`, reject unsafe/non-file or invalid present resources, validate against frozen encoded-audio hashes and decoded bounds, and freeze each unique unit's alignment once. Sidecars are bounded to 512 KiB each and 4 MiB aggregate. Frozen unit audio retains both the alignment and verified audio digest. Exact alignment bytes are included in selected-unit identity, and the render identity domain advances to v4. No cache files or existing bank manifests were rewritten.

A WAV-backed snapshot regression checks legacy absence, valid freezing, sidecar-sensitive cache keys, immutability after file edits, stale-audio rejection and symlink rejection. Strict Debug/Release targeted builds passed; 32 synthesis, 13 timing-contract and 15 snapshot cases passed per configuration (60 cases; CTest 4.86/2.55 seconds). `git diff --check` passed. Normal phrase dispatch and bank-level content identity/package promotion remain open; the broad core suite was not rerun. Source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: frozen aligned phrase rendering, 2026-09-06

The normal concatenative frozen phrase path now consumes authored source alignment and maps all retained nucleus targets through the bounded raw operator. It avoids applying legacy onset retiming a second time. Renderer selection shares the dispatcher's explicit-unit/global-policy/bank-hint precedence; requests for non-raw backends reject rather than silently substitute raw alignment. Root-pitch, pitch-automation, raw-override and source-buffer constraints remain explicit. Raw renderer revision advances to 4.

A WAV-backed regression verifies both nucleus samples at exact requested frames after deleting the test-created sidecar, explicit Raw precedence over a global PSOLA policy, explicit PSOLA rejection under ForceRaw, and unsupported loop-print/additional-gain rejection. Strict affected Debug/Release builds passed; 32 synthesis, 13 timing-contract and 16 snapshot cases passed per configuration (61 cases; CTest 4.13/2.14 seconds). `git diff --check` passed. Broad core/native/installed-host suites were not rerun. U3/U4 retain their recorded local acceptance; U5 remains incomplete pending alignment-aware interior-edit selection, edit/undo/cache and cross-note acceptance, and package identity integration. The raw warp is not pitch-preserving. Source is local/uncommitted; full Beta GO remains incomplete.

### U5 continuation: evidence-aware interior timing capability, 2026-09-06

Added borrowed source-alignment evidence to candidate generation, forced-unit validation and timing solving. It checks actual unit/phone coverage and validates the authored landmarks against the supplied verified WAV digest and decoded frame count, instead of trusting a unit-ID capability flag. Legacy callers retain the existing smaller-unit/conflict behavior. Frozen phrase rendering supplies its owned evidence to the solver. Selector revision is now 3; timing revision is 8.

The WAV-backed selection/pipeline regression edits the second vowel to 280 ms, retains the same long unit, checks its actual sample at a 1,440-frame displacement while preserving the first target, and rejects stale selection evidence and missing frozen alignment. Strict affected Debug/Release builds passed; all 61 focused synthesis/timing/snapshot cases passed per configuration (4.96/1.85 seconds). `git diff --check` passed. No broad core/native/host rerun is claimed. Snapshot creation still discovers sidecars after selection: moving bounded resource discovery ahead of selection is the next integration step, followed by actual command/undo/cache and cross-note coverage. U5 and full Beta GO remain incomplete; source remains local/uncommitted.

### U5 continuation: snapshot discovery before selection and edit/cache workflow, 2026-09-06

Refactored snapshot resource freezing into one reusable, bounded loader. Before selection it probes enabled, matching-style, phone-matching candidates requiring interior timing capability. Present sidecars cause audio-bound validation/freezing and evidence publication; absent sidecars do not load candidate WAVs. Final selection reuses those frozen resources and emits only selected resources into the snapshot. Candidate and final resource loads share aggregate budgets; malformed present candidate resources fail explicitly.

Expanded the WAV-backed regression through actual phoneme command apply/revert/reapply, normal snapshot creation and production region rendering. It checks the same long unit, a 30 ms second-vowel displacement, changed edited audio/cache identity, restored original identity/audio on undo, restored edited identity/audio on redo, and byte-equivalent disk-cache audio after memory eviction. Frozen snapshots also still render after the test-created sidecar is removed. Strict affected Debug/Release builds passed and all 61 focused cases passed per configuration (3.58/1.19 seconds). `git diff --check` passed. No broad core/native/host rerun is claimed. Cross-note coverage, unaligned multi-nucleus policy, package identity and consolidated U5 acceptance remain open; source is local/uncommitted and full Beta GO remains incomplete.

### U5 continuation: cross-note alignment and pitch-change integrity, 2026-09-06

Extended the same waveform/command/cache workflow over two adjacent same-pitch notes. The second phoneme uses its own note-relative 30 ms offset; exact target samples, long-unit selection, frozen-resource rendering, undo/redo and disk-cache replay are verified in both the one-note and two-note variants.

The changed-pitch regression initially failed because aligned raw rendering silently ignored the second note's pitch. The renderer now validates every covered note against the placement pitch and explicitly rejects unsupported within-unit changes. Raw renderer revision advances to 5. This prevents incorrect success; pitch-preserving multi-note melody synthesis remains required downstream and is not replaced by rejection.

After fixing a test fixture field-name typo and then the actual reproduced pitch defect, strict affected Debug/Release builds passed. All 61 focused synthesis/timing/snapshot cases passed per configuration (4.50/1.90 seconds); the expanded snapshot case exercises both variants. `git diff --check` passed. Initial failures are not claimed as passing; no broad core/native/host rerun is claimed. Unaligned multi-nucleus policy, package identity and final U5 acceptance remain open. Source stays local/uncommitted and full Beta GO remains incomplete.

### U5 continuation: bank/package alignment identity, 2026-09-06

Bank content identity now includes an optional sorted per-unit alignment digest section. Banks without matching sidecars retain their legacy hash. Files are bounded to 512 KiB each and 64 MiB per bank; symlink/non-file paths reject. This hashes exact bytes rather than claiming semantic alignment validation. Updated the installer service's separate signed-entry identity calculation to match disk/catalog identity and limits.

New regressions verify absence/empty-directory compatibility, byte-sensitive hashes, oversized and symlink rejection, valid alignment pack/install preservation, source/installed identity agreement, idempotent reinstall, and loss of receipt-backed trust after installed sidecar modification. Strict affected Debug/Release builds passed. All 76 cases across synthesis (33), timing (13), snapshots (16) and installer/U3 (14) passed per configuration (3.70/1.18 seconds). `git diff --check` passed. Broad core/native/host qualification was not rerun. Unaligned multi-nucleus behavior and consolidated U5 acceptance remain open; source remains local/uncommitted and full Beta GO is incomplete.

### U5 continuation: unaligned multi-nucleus selection policy, 2026-09-06

Normal snapshot selection now opts into complete nucleus-alignment capability checks. Matching multi-nucleus candidates participate in preselection sidecar discovery even without timing overrides. Missing landmarks exclude automatic candidates, allowing compatible smaller-unit selection; forced unaligned units report an actionable conflict. The frozen pipeline independently rejects an unaligned multi-nucleus legacy plan. Resource-free selector APIs preserve planning behavior by default, not a rendering guarantee. Selector revision advances to 4.

New regression covers successful smaller-unit rendering, forced long-unit conflict, absent alternatives and rejection of a manually supplied legacy plan. An existing complete forced-relationship fixture initially failed under the new policy; it now supplies validated source landmarks while preserving all projection/boundary assertions. Strict affected Debug/Release builds passed; 33 synthesis, 13 timing and 17 snapshot cases passed per configuration (63 total; 3.59/1.33 seconds). `git diff --check` passed. Initial failures are not claimed as passing. Consolidated U5 acceptance and broader core/native regression checks are next; source remains local/uncommitted and full Beta GO is incomplete.

### U5 acceptance audit and dependent-boundary repair, 2026-09-06

The consolidated audit reproduced a false timing conflict when both nuclei move beyond their original equal-time boundary. The compiler now resolves an automatic end from the edited next nucleus before checking the span; explicitly reversed ends and crossed nuclei still reject. Timing revision advances to 9. A dedicated failing-then-passing regression and normal snapshot/command waveform assertions cover 300/400 ms anchors and restoration.

Strict affected Debug/Release builds passed. Final four-suite runs passed in both configurations: 474 core/native cases, 33 synthesis cases, 14 dedicated timing cases and 17 snapshot cases (overlapping suites, not a unique-test total). Debug elapsed 88.96 seconds; Release 11.98 seconds. `git diff --check` passed. `U5_ACCEPTANCE_AUDIT_2026-09-06.md` maps every explicit U5 criterion to source and runtime evidence and records local implementation acceptance. No fresh installed-host/visual/listening or full release-matrix claim is made. Source remains local/uncommitted. U6 complete F0/expression compilation is next; all remaining full Beta GO units stay mandatory.

### U6 started: bounded unit-independent score evaluator, 2026-09-06

Added the planned performance compiler files and dedicated test target. The first component freezes bounded absolute note spans, tempo, manual pitch offsets and dynamics, and evaluates score pitch plus note vibrato statelessly at absolute frames. It has no bank/unit input and returns both C4/G4 score plateaus. Score frequency is explicitly not a phonetic voicing decision. Accepted-take evaluation and overlapping voice allocation currently reject pending their full implementation; articulation is retained but not yet applied as a gate.

Strict Debug/Release dedicated builds passed; all three compiler cases passed per configuration (0.84/0.46 seconds), including tempo-dependent boundaries, immutable score capture, neutral/edited dynamics, analytic vibrato/offset and reverse-block phase invariance. `git diff --check` passed. Renderer consumers/audio qualification and the rest of U6 remain open; other suites were not rerun. `U6_PERFORMANCE_COMPILER_STATUS_2026-09-06.md` records the remaining exact scope. Source is local/uncommitted and the complete Beta GO objective stays active.

### U6 continuation: accepted pitch/dynamics and manual ownership, 2026-09-06

The score evaluator now consumes validated selected pitch/dynamics lanes through note/range scopes and source offsets. Proposed-only takes remain inactive. Generated MIDI-cents pitch is the selected base; only explicit additive ownership applies a manual offset to it. Replace ownership and enabled manual vibrato use the score/manual base instead, preventing generated oscillation from being added to manual vibrato. Accepted null pitch remains absent frequency with retained note identity. Dynamics obeys channel replacement ownership. Numeric interpolation and discrete null transitions are documented in the U6 status file.

After correcting a test initializer syntax error, strict Debug/Release compiler builds passed and all four cases passed per configuration (0.78/0.41 seconds), covering note/range ownership, source offsets, null values, proposals, pitch replacement/offset, dynamics and vibrato suppression. `git diff --check` passed. Other suites were not rerun; no renderer-level acoustic result or U6 completion is claimed. Remaining channels, lookup efficiency, phonetic voicing, articulation and renderer consumption remain required. Source remains local/uncommitted and the full Beta GO goal stays active.

### U6 continuation: exact ownership/selection sample boundaries, 2026-09-06

Reproduced a real early handoff: inverse-tempo tick rounding released manual pitch ownership one output frame before the declared end. The compiler now freezes note/range ownership and accepted-selection scopes as half-open absolute-frame intervals. Sample evaluation uses those intervals for Replace/PitchOffset and accepted selection eligibility, retaining note IDs for note scopes; lane interpolation remains tick-domain.

Strict Debug/Release builds passed. All four expanded compiler cases passed per configuration (0.49/0.78 seconds), including before/at start and before/at end at 44.1/48/192 kHz with a tempo change, plus accepted-selection range boundaries. The pre-fix regression failed as expected and is not claimed as passing. `git diff --check` passed. Other suites were not rerun. This repairs the evaluator contract; U6 renderer/voicing/articulation integration and the full Beta GO goal remain incomplete. Changes remain local/uncommitted.

### U6 continuation: compiled melody drives PSOLA sustain pitch, 2026-09-06

Classic PSOLA now consumes immutable compiled performance at an explicit absolute output origin and selects sustain pulse pitch from the complete score evaluator. Duplicate pitch curves, mismatched rates and overflowing origins reject; accepted-unvoiced pitch explicitly requires a different processing path. PSOLA revision advances to 3. Snapshot creation rejects caller-injected compiled performance until snapshot-owned freezing/identity is integrated, with a cache-safety regression.

A real PCM/pitch-analysis regression verifies C4 and G4 sustained plateaus inside one rendered unit with no manual pitch curve. Its first run exposed the generic fixture's 19,200-frame raw release beginning before the second note. The fixture now declares sustained source material spanning both notes; raw attack/release retargeting remains an explicit implementation gap, not a passing claim. Strict affected Debug/Release builds passed; all 56 focused cases passed per configuration (33 synthesis, 5 compiler, 18 snapshot; 4.20/1.30 seconds). `git diff --check` passed. Normal phrase integration, remaining renderer families, phonetic voicing, articulation and full U6 acceptance remain incomplete. Source is local/uncommitted and the full Beta GO goal remains active.

### U6 continuation: snapshot-owned performance reaches normal PSOLA rendering, 2026-09-06

Effective Classic PSOLA selections now cause snapshot-owned score/performance compilation. The frozen pipeline forwards that immutable output internally; phrase dispatch sets each unit's absolute origin and avoids adding manual pitch a second time. Snapshot identity v5 includes compiler revision 1 alongside existing frozen score/options/resources. Caller-injected compiler objects remain prohibited. Compiled PSOLA errors cannot degrade to raw fallback, and explicit onset warping rejects pending pitch-preserving mapping.

The normal snapshot/phrase audio regression measures 440 Hz and a single +100-cent shift, verifies frozen behavior after live-curve removal and identity restoration, and requires runtime rejection of missing sustain-mark capability. Its initial zero-mark fixture failed manifest validation; replacing it with valid out-of-sustain marks reaches the intended backend failure without weakening validation. Strict affected Debug/Release builds passed; all 57 focused cases passed per configuration (4.27/1.42 seconds). `git diff --check` passed. Aligned multi-nucleus PSOLA, voiced attack/release processing, other render families, voicing/articulation/dynamics consumption and full U6 remain open. Source is local/uncommitted; full Beta GO remains active.

### U6 continuation: compiled dynamics affect PSOLA PCM, 2026-09-06

PSOLA now consumes compiled dynamics after DC correction/fades, avoiding a later processing stage distorting the gain envelope. Unity is transparent; gain application is cancellable and rejects non-finite/overflow output. PSOLA revision advances to 4. The evaluator's current neutral behavior outside active score notes is retained explicitly; preutterance/release expression is not claimed complete.

Extended direct audio tests verify exact unity PCM and every sample of a 0.25→0.75 ramp. Normal snapshot/phrase tests verify half-gain PCM and identity restoration after removing the curve. Strict affected Debug/Release builds passed; all 57 focused cases passed per configuration (5.19/2.48 seconds). `git diff --check` passed. Other backends, articulation/voicing and full U6 remain open; no broad core/native/host rerun is claimed. Source remains local/uncommitted and the full Beta GO goal remains active.

### U6 continuation: audible staccato gate and bounded release, 2026-09-06

Compiler revision 2 adds staccato gate/release frames using a documented baseline of half elapsed note duration and a release of up to 10 ms within the gate. PSOLA revision 5 consumes the articulation envelope along with dynamics. Stored note duration remains unchanged; a closed gate remains closed through a gap/extended source tail and reopens for the next active note. Legato/slur/reattack and phonetic voicing remain separate unfinished work.

Direct PCM verification checks every staccato sample against the normal waveform times the compiled envelope, exact release midpoint, and the shortened gate. Dedicated tests cover tempo changes, 8/44.1/48 kHz and gap/next-note behavior. Strict affected Debug/Release builds passed; all 58 focused cases passed per configuration (4.81/2.45 seconds). `git diff --check` passed. This is a deterministic articulation baseline, not listening qualification or full U6 acceptance. Source stays local/uncommitted and the full Beta GO goal remains active.

### U6 continuation: pronunciation-aware explicit vowel continuation, 2026-09-06

Compiler revision 3 consumes the supplied shared phoneme sequence to identify adjacent compatible explicit continuation notes, retaining reattack for repeated lyrics and breaking links across gaps/staccato. Linked score notes receive a bounded smoothstep pitch transition (up to 20 ms/half note duration). Normal snapshot compilation supplies its resolved tokens; the compiler does not independently phonemize.

New checks distinguish repeated Legato-labeled syllables from actual continuation, assert pitch-transition endpoints/interior and gap/staccato behavior, and compare PSOLA boundary PCM with/without the transition. Strict affected Debug/Release builds passed; all 59 focused cases passed per configuration (5.33/2.55 seconds). `git diff --check` passed. Separate-unit reattack suppression, shared-lyric/slur editor repair, full voicing and other renderers remain incomplete; no full legato or U6 acceptance is claimed. Source remains local/uncommitted and full Beta GO stays active.

### U6 continuation: editor melisma produces vowel continuation, 2026-09-06

Added the shared domain rule for adjacent Legato notes with a common lyric owner. Japanese phonemization and performance compilation now agree that this is vowel continuation, not repeated consonants. The immediate predecessor must supply the vowel; an unrelated older vowel is not borrowed for shared-lyric continuation. Slur disable clears Legato instead of setting it. Duration/articulation now participate in pronunciation input identity and command reconciliation; the new domain helper sources enter resource identity. Phonemizer/compiler revisions are 2/4.

The editor regression proves `k a / k a` becomes `k a / a`, compiles a non-reattack continuation, and preserves exact project undo/redo and slur-disable pronunciation revisions. Strict affected Debug/Release builds passed; all 67 focused cases across synthesis, melisma/editor workflow, compiler, snapshot and edit preservation passed per configuration (6.95/4.09 seconds). `git diff --check` passed. No broad core/native/installed-host rerun or complete acoustic legato claim is made. Separate-unit attack suppression, full voicing and remaining U6/Beta GO work stay open; source remains local/uncommitted.

### U6 continuation: broad regression repair and continuation context protection, 2026-09-06

Rebuilt the broad core/native target in both configurations. The initial Release run found six failures: an old expectation that duration changes cannot affect pronunciation, and five runtime fixtures forcing PSOLA on `demo.ja.g4.o.01` without pitch marks. Updated the duration assertion to match the actual resolver inputs. Success fixtures now request capable PSOLA units, while a new negative runtime test requires unsupported PSOLA to fail without successful publication. Production fallback safeguards remain intact. Test waits now report terminal diagnostics immediately; the coordinator's non-null idle result object is tested according to its actual contract.

Source review also found shared-lyric continuation could be split at maximum phrase duration. The segmenter now preserves those adjacency relationships as atomic groups or reports a bounded over-limit conflict; snapshot intake rejects manual cuts through them. Context-complete splitting remains U7 work, not waived by this protection.

Final strict affected Debug/Release builds passed. All three regression suites passed per configuration: core/native 476 cases, authoring runtime 10 cases, snapshots 20 cases (overlapping targets). Debug elapsed 85.89 seconds; Release 9.89 seconds. `git diff --check` passed. Initial failures are not passing evidence. U6 aligned PSOLA mapping, phonetic voicing, other renderer integrations and complete acceptance remain open. No installed-host/listening qualification or full Beta GO is claimed; source remains local/uncommitted.

### U6 continuation: aligned long-unit PSOLA through normal snapshots, 2026-09-06

Added validated forward/inverse source-map evaluation and wired authored maps into PSOLA. Inverse positions choose nearest source sustain marks while compiled score/performance sets output pulse spacing, separating source duration mapping from target pitch. Mapped boundaries govern stable/release placement and transient fallback sampling. The frozen phrase path now supports aligned PSOLA and binds output origin/nucleus offsets internally; caller-supplied source maps cannot bypass snapshot identity. PSOLA revision advances to 6.

A normal snapshot selects and renders one aligned two-vowel unit across C4→G4, with both sustained plateaus measured from PCM and no raw fallback/manual curve. Source-map tests verify knot/inverse behavior and invalid input rejection. Strict affected Debug/Release builds passed; all 75 focused cases passed per configuration (5.90/2.95 seconds). `git diff --check` passed. Nearest-mark mapping is not single-sample phonetic-event qualification; voiced attacks/releases, unvoiced processing, separate-unit attack suppression and remaining renderer integrations remain required. U6/full Beta GO remain incomplete; source is local/uncommitted.

### U6 continuation: spectral performance integration and off-bin pitch correction, 2026-09-06

Spectral snapshots now own compiled performance; spectral frame processing consumes full-score pitch and the shared gain stage applies dynamics/articulation. Duplicate manual pitch, external injection and fallback that would discard compiled intent are rejected. PSOLA shares the gain helper. Renderer revisions are PSOLA 7 and spectral 3.

An audio regression found the old bin-center phase advance rendered about 422 Hz for a 440 Hz request. The compiled path now tracks measured inter-frame source frequency and uses it for pitch-scaled output phase; phaseReset contributes at initialization rather than continually detuning the compiled trajectory. The original pitch tolerance was retained. After correcting a shadowed local in the shared-helper refactor, strict affected Debug/Release builds passed. All 61 focused cases passed per configuration (5.52/1.56 seconds), including spectral C4/G4 sustain plateaus and normal snapshot pitch/half-gain checks across PSOLA and spectral. `git diff --check` passed. Aligned spectral mapping, voicing/transients, remaining renderer families and full U6 acceptance remain open; source stays local/uncommitted and full Beta GO remains active.

### U6 continuation: aligned spectral rendering through normal snapshots, 2026-09-06

Spectral revision 4 now uses authored maps for source analysis centers, stable/release placement, transient fallback and uncovered samples. Frequency tracking accounts for actual mapped source advance, keeping pitch independent of nonuniform duration mapping. Unit/source/output bounds validate before map use; external source-map injection remains rejected. The frozen phrase path dispatches aligned spectral or PSOLA explicitly without raw substitution.

The aligned long-unit normal-snapshot regression now covers both classical backends, measured sustained C4/G4 pitches, a 30 ms second-vowel edit that changes target frames/PCM/identity, and identity restoration on undo. Strict affected Debug/Release builds passed; all 61 focused cases passed per configuration (6.02/1.72 seconds). `git diff --check` passed. Voicing/transients, exact phonetic-event qualification, attack suppression and remaining renderer families stay open. U6/full Beta GO remain incomplete; source stays local/uncommitted.

### U6 continuation: sample-exact accepted voicing transitions, 2026-09-06

Reproduced accepted pitch becoming null one frame early because lane segments were selected through inverse tick rounding. Compiler revision 5 now chooses segments at forward-mapped output-frame boundaries, preserving discrete voiced/null transitions and source offsets. Numeric lane interpolation remains clamped tick-domain interpolation. Validated source windows bound destination-time arithmetic.

Expanded regression checks both transition directions before/at the boundary at 44.1/48/192 kHz with a tempo change and nonzero source offsets. Strict affected Debug/Release builds passed; all 28 compiler/snapshot cases passed per configuration (4.13/1.85 seconds). `git diff --check` passed. The pre-fix failure is not passing evidence. Phonetic unvoiced rendering and the remaining U6/full Beta GO requirements stay open; other suites were not rerun and source remains local/uncommitted.

### U6 continuation: preserve authored unvoiced material in classical rendering, 2026-09-06

Timing targets now retain optional resolved phoneme voicing, and source maps derive contiguous phone spans from authored landmarks. PSOLA masks known-unvoiced source/target grain positions; spectral masks known-unvoiced analysis/output contributions. Mapped source material remains in those intervals. Accepted null pitch can use a known-unvoiced source path, while unsupported voiced-to-unvoiced conversion remains explicit. Revisions are timing 10, PSOLA 8 and spectral 5; no source JSON schema or measured acoustic labels were invented.

Tests verify onset/nucleus map labels, bounded contiguous coverage and source-relative sample preservation in a mixed diagnostic fixture through both classical backends, contrasted with periodic treatment. Strict affected Debug/Release builds passed; all 76 focused cases passed per configuration (7.45/3.03 seconds). `git diff --check` passed. Acoustic voicing analysis, natural transition quality, attack suppression and remaining renderer work stay open; U6/full Beta GO are incomplete and source remains local/uncommitted.

### U6 continuation: separate vowel units consume continuation attack intent, 2026-09-06

For single-vowel units without authored maps, PSOLA/spectral now consult compiled reattack intent at the vowel landmark. Continuation begins from sustain processing rather than replaying the recorded attack, while preserving output length and vowel placement. Repeated pronunciation retains the attack. Renderer revisions are PSOLA 9 and spectral 6; authored-map/multi-phone continuation remains open rather than being claimed covered.

A diagnostic impulse regression verifies preserved versus suppressed recorded attack in both backends and unchanged output extent/landmark. Strict affected Debug/Release builds passed; all 63 focused cases passed per configuration (7.65/3.01 seconds). `git diff --check` passed. This is not phase-continuity/listening qualification, a broad core/native rerun, or full U6 acceptance. Remaining continuation variants and renderer work stay required; source remains local/uncommitted and full Beta GO remains active.

### U6 continuation: authored-map PSOLA continuation attack suppression, 2026-09-06

Extended PSOLA continuation attack suppression to single-vowel authored maps without rewriting source landmarks or changing output extent/vowel placement. PSOLA revision is 10. The direct impulse matrix includes mapped PSOLA; a normal lyric-command→snapshot→phrase test verifies changed continuation PCM, unchanged vowel onset and restored identity on undo.

Strict affected Debug/Release builds passed; all 31 compiler/snapshot cases passed per configuration (4.66/2.18 seconds). `git diff --check` passed. Spectral mapped continuation, multi-phone transitions, perceptual continuity and full U6 remain unfinished. Other suites were not rerun; source remains local/uncommitted and full Beta GO stays active.

### U6 continuation: mapped spectral continuation with held-source pitch tracking, 2026-09-06

Spectral revision 7 now handles mapped single-vowel continuation without replaying its recorded attack. It holds the sustained source entry, estimates its local frequency with one adjacent FFT window, and retains that estimate while source motion is zero. This avoids detuning to FFT-bin centers during the hold. The authored map and target extent remain intact.

Direct tests cover attack preservation/suppression, unchanged landmarks, and held-interval pitch for mapped/unmapped classical paths. Normal lyric-command/snapshot/phrase checks now exercise PSOLA and spectral identity restoration. Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (10.05/5.05 seconds). `git diff --check` passed. General transition quality, measured voicing and remaining renderer-family/U6 requirements remain open; source is local/uncommitted and full Beta GO stays active.

### U6 continuation: compiled granular stretch performance and grain phase repair, 2026-09-06

Stretch revision 3 now consumes snapshot-owned compiled pitch and shared dynamics/staccato gain. An initial C4 audio result near 297 Hz exposed phase inconsistency between drifting grains. Compiled grain centers now align to accumulated target phase using the declared source-root period; drift still controls source-content traversal. The original audio tolerance remains unchanged. Unsupported compiled paths cannot degrade to raw fallback or accept duplicate/untracked pitch inputs.

Direct C4/G4 sustain tests and normal snapshot pitch/half-gain/identity checks pass for the granular backend alongside PSOLA/spectral. Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (9.06/3.70 seconds). `git diff --check` passed. Authored stretch maps, unvoiced/continuation processing, measured source-pitch/transient qualification and full rendering-family integration remain required. This is not full U6/Beta GO acceptance; source remains local/uncommitted.

### U6 continuation: authored granular timing and unvoiced preservation, 2026-09-06

Stretch revision 4 consumes authored source maps for grain content, stable/release placement and fallback positions while compiled pitch sets phase/ratio. Known-unvoiced source/target spans bypass pitched grains and retain mapped source material. Unit bounds and map identity remain enforced; nondefault source-drift overrides conflict with authored timing rather than being silently ignored. Normal aligned rendering now supports all three classical backends.

The normal C4/G4 aligned-unit and second-vowel edit/undo-identity regression includes granular stretch; mixed-source tests verify its unvoiced relative samples. Strict affected Debug/Release builds completed and final post-build runs passed all 64 focused cases per configuration (9.26/3.41 seconds). `git diff --check` passed. Delayed live operations were waited on, not restarted. Granular continuation, transient/voicing qualification and full U6/Beta GO remain unfinished; source stays local/uncommitted.

### U6 continuation: granular single-vowel continuation consumes attack intent, 2026-09-06

Stretch revision 5 suppresses the recorded attack for compatible single-vowel continuation, including authored maps that hold the sustained source entry. Output length/vowel placement remain fixed and phase follows compiled pitch. Tests now compare mapped/unmapped repeated versus continued attacks in all three classical backends, verify held-interval pitch, and exercise normal lyric-command/snapshot/phrase undo identity with granular rendering.

Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (9.42/3.21 seconds). `git diff --check` passed. Raw-only performance consumption, overlapping voice allocation, remaining channels/families and general transition quality remain explicit U6 gaps; no broad core/native rerun or full U6/Beta GO acceptance is claimed. Source remains local/uncommitted.

### U6 continuation: raw backend consumes compiled pitch and expression, 2026-09-06

Raw revision 6 adds per-frame compiled playback-rate integration and shared dynamics/articulation gain. Raw cancellation now propagates through dispatcher/render/gain stages. This remains raw resampling with its existing unit-section boundaries, not pitch-preserving time warping. Snapshot intake rejects externally injected raw compiler objects; normal raw-only snapshot integration remains pending rather than bypassing ownership/cache identity.

Direct audio regressions verify C4/G4 pitch plateaus, sample-exact gain multiplication and cancellation. Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (9.07/2.67 seconds). `git diff --check` passed. Raw snapshot ownership, aligned raw pitch, overlapping voices and remaining U6/full Beta GO requirements remain open; source stays local/uncommitted.

### U6 continuation: raw-only snapshots consume owned performance, 2026-09-06

Normal raw snapshots now compile/freeze performance and forward it internally. Raw revision 7 derives expression origin from the requested vowel landmark and actual pitch-dependent rendered offset, preventing transposition from shifting the gain timeline. Mapped raw output applies compiled gain but explicitly rejects unsupported vibrato/manual/accepted pitch demands rather than ignoring them. Raw mapping remains non-pitch-preserving.

The normal pitch/half-gain/identity matrix includes Raw, and a transposed direct regression verifies every sample's gain at the derived origin. Strict affected Debug/Release builds passed; all 65 focused cases passed per configuration (10.44/3.66 seconds). `git diff --check` passed. Overlap allocation and unsupported-channel boundaries now also apply to raw-only snapshots; broader core/native compatibility, authored raw pitch and remaining U6/full Beta GO work remain open. Source remains local/uncommitted.

### U6 continuation: raw vowel continuation consumes reattack intent, 2026-09-06

Raw revision 8 now enters looped sustain for compiled single-vowel continuation while preserving its pitch-dependent rendered vowel offset. Repeated pronunciation retains the recorded attack. The transposed direct attack test and normal lyric-command/snapshot/undo-identity matrix include unmapped Raw alongside the existing mapped classical cases.

Strict affected Debug/Release builds passed; all 65 focused cases passed per configuration (10.71/3.66 seconds). `git diff --check` passed. Authored raw-map continuation/pitch, overlapping voices and remaining family/channel/U6 requirements stay open; no broad core/native or listening qualification is claimed. Source remains local/uncommitted and full Beta GO remains active.

### U6 continuation: bounded deterministic score voice allocation, 2026-09-06

Added a score-overlap allocator that preserves note IDs/timing, sorts by start/ID, reuses free voices deterministically, and prefers a unique compatible continuation predecessor. Ambiguous shared/literal vowel predecessors conflict instead of attaching silently to another voice. Allocation is bounded to 4,096 notes and 1–64 voices (default 16), distinct from permitted source preutterance overlap.

Strict Debug/Release compiler builds passed; all 12 compiler cases passed per configuration (3.14/1.62 seconds), including overlap, order invariance, limits, immutable input and continuation/ambiguity behavior. `git diff --check` passed. Per-voice projection, snapshot/unit-selection integration and audio summing remain unfinished; the current monophonic compiler still rejects overlaps. No broader tests or U6/Beta GO completion are claimed; source remains local/uncommitted.

### U6 continuation: independent per-voice performance projection, 2026-09-06

Added bounded per-voice compilation using the deterministic allocation plan. Projection retains note-owned edits/accepted selections only on their voice, preserves range ownership, omits unaccepted proposals from compiled copies and leaves the source project untouched. Supplied phonemes validate with full-score coverage before filtering; no independent phonemization or source time shift is introduced. Aggregate retained points are bounded across voices.

Strict Debug/Release compiler builds passed; all 12 expanded cases passed per configuration (3.12/1.60 seconds), covering simultaneous pitches, accepted/manual ownership isolation, immutable source state and incomplete phoneme rejection. `git diff --check` passed. Snapshot selection, rendering and mixing of independently allocated voices remain unfinished; other suites were not rerun. Source remains local/uncommitted and full U6/Beta GO remains active.

## Release status

### Requested checkpoint publication: 2026-09-05

The user explicitly requested committing and pushing the current project work, including the preserved crash/support changes, then continuing implementation. U2 was committed as `79d4faef`. Staging the approved source/test/plan files made `verify_tracked_source_closure.py` pass; that prior integration obligation was satisfied without weakening its check. Build directories, local audio evidence, private temporary files and unrelated branches/worktrees are not included in the commits.

Pre-publication review found a crash-handler lifetime race: native teardown drained writers before disabling handle acquisition. A shared lock-free `CrashWriterSlot` now invalidates acquisition before draining, with sequentially consistent ordering; both native destructors and macOS installation rollback close only afterward. A deterministic driver using the original order returned the old handle to a late writer for both descriptor/handle types (exit 1); the repaired order returned the invalid sentinel (exit 0). The root ran nine recovery/support tests successfully, including real macOS crash subprocesses. Independent recheck confirmed the ordering repair; Windows runtime integration remains unverified.

The main checkout retains its existing development branches and another developer's worktree. The repository's master-only publishing audit runs in an isolated checkout of the exact committed source, which is fast-forwarded and re-audited before pushing to `origin/master`. No branch deletion, force push or history rewrite is authorized or used.

Checkpoint commits `79d4faef` and `2f88761c8b43a893f1b4fcb1210d2b7aac036f7b` were pushed by fast-forward. `git ls-remote` independently confirmed the latter as `origin/master`. The isolated exact-commit checkout passed license/branch-policy and source-closure audits. The full Debug build, nine recovery/support tests in both configurations, and the core Debug suite passed after the race repair; the final three-entry core/recovery/closure CTest run took 97.34 seconds. This establishes source-checkpoint publication, not release qualification.

The subsequent isolated U3 style-resolver checkpoint `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d` was also fast-forwarded and independently verified as `origin/master`. The schema-8 continuation described above remains local and uncommitted; this ledger does not represent it as pushed source. Existing branches/worktrees remain intact.

No model was trained, no new production singer or commercially qualified voicebank was delivered, no independent listening/creator study ran, and no signed installed platform/host matrix was completed in this baseline work. Beta GO is not achieved. Continue independent implementation while keeping these acceptance obligations explicit.

U6 continuation: multi-voice performance compilation now rejects active forced-unit and seam dependencies that would cross voice boundaries or lose their original phoneme context during projection. Valid same-voice edits remain accepted; unresolved edits remain inactive. Strict Debug/Release compiler builds and all 13 compiler cases passed (2.65/0.79 seconds), plus `git diff --check`. U3–U5 remain locally accepted; U6 remains open pending renderer integration and its other acceptance obligations. These changes remain local/uncommitted.

U6 region-render continuation: validated voice projection now feeds independent snapshot/selection/render paths, with per-voice pronunciation and sequential audio summation. Tests prove exact three-voice sum/cache behavior and correct held-vowel context under overlap. Strict affected Debug/Release builds and 37 focused compiler/snapshot cases pass (8.51/1.91 seconds); `git diff --check` passes. Live editor scheduling, other U6 obligations and full Beta GO qualification remain open. Changes remain local/uncommitted.

U6 authoring continuation: the existing asynchronous coordinator already uses the voice-aware project/region path; corrected its monophonic phrase-progress estimate. A new runtime regression verifies overlapping-voice publication, pitch edit, exact undo PCM/cache restoration, progress counts and finite non-silent transport-buffer output. Strict Debug/Release authoring builds and all 11 focused cases pass (1.91/0.80 seconds), plus `git diff --check`. Installed UI/host/hardware and acoustic qualification are not claimed; U6 and Beta GO remain incomplete. Changes remain local/uncommitted.

U6 accepted-attack continuation: compiler revision 6 consumes accepted Attack milliseconds as an absolute-time amplitude ramp, respecting manual replacement, continuation and staccato gates without accelerating long attacks. Sample-by-sample regressions cover all four existing sample renderers; all 38 focused compiler/snapshot cases pass after strict Debug/Release builds (8.22/2.12 seconds), plus `git diff --check`. Accepted Release/Timing/timbral controls, procedural/neural-family integration and remaining U6/full Beta GO requirements stay open. This does not qualify recorded transients or listening quality. Changes remain local/uncommitted.

U6 attack normal-path verification: four-backend coverage now exercises manual ownership through EditorSession, snapshot/PCM changes, exact undo/redo restoration, frozen snapshot immutability and a real region-cache hit after undo. All 25 snapshot cases pass after strict Debug/Release builds (6.04/1.21 seconds), plus `git diff --check`. This uses pre-existing accepted fixture data; it does not implement the still-required proposal/take-acceptance workflow. No additional production change or broad-suite qualification is claimed. U6 remains open; changes remain local/uncommitted.

Take-selection continuation: implemented an undoable SetAcceptedPerformanceCommand for existing proposals, with expected-state, captured-revision, pronunciation and selection-validity checks. Manual ownership and proposal data remain intact. The normal attack/render/cache regression now uses the actual acceptance command. All 41 focused command/snapshot cases pass after strict Debug/Release builds (6.60/1.58 seconds), plus `git diff --check`. Proposal generation/review UI/async delivery and other-language integration remain unfinished; no full U40/U6/Beta GO acceptance is claimed. Changes remain local/uncommitted.

Proposal-delivery continuation: added AddPerformanceProposalCommand for inert, undoable proposal storage with bounded source/identity/revision validation. Tests deliver it through a one-time live job context, verify stale/duplicate rejection and unchanged snapshot identity, then explicitly accept through the tested render path. Strict Debug/Release affected builds and all 55 focused job-context/command/snapshot cases pass (7.06/2.41 seconds), plus `git diff --check`. No proposal generator or review UI was implemented; U6/full Beta GO remain incomplete. Changes remain local/uncommitted.

Live proposal verification: AuthoringRuntime now has a regression proving inert delivery causes no render submission, explicit acceptance publishes changed PCM with one submission, and undo restores original cached audio. Removing the inert proposal also schedules no render. A redundant test-owned preview was removed after the initial failure; all 12 authoring cases pass after strict Debug/Release builds (2.11/1.42 seconds), plus `git diff --check`. Existing production dispatch needed no change. Generator/review UI and full U6/Beta GO remain open; changes remain local/uncommitted.

U6 evaluator indexing: replaced per-sample linear ownership/selection scans with compiled per-channel/mode time indexes and prebound take/lane indexes. A 2,048-interval regression checks exact boundaries, tempo/rate changes, unordered input and copy/move stability. Strict Debug/Release affected builds and all 40 compiler/snapshot cases pass (12.21/2.07 seconds), plus `git diff --check`. No quantified whole-engine speedup or production-load qualification is claimed. U6 remains incomplete; changes remain local/uncommitted.

U6 voice-context consistency: multi-voice compilation now resolves verified Japanese input independently per projected voice, matching region rendering, instead of inheriting the wrong globally preceding vowel. Explicit overrides remain authoritative; custom/non-Japanese multi-voice context remains an explicit unsupported integration requirement. Strict Debug/Release affected builds and all 41 compiler/snapshot cases pass (12.37/2.15 seconds), plus `git diff --check`. U6/full Beta GO remain open; changes remain local/uncommitted.

U6 independent-context API: callers can now supply validated complete per-voice phoneme sequences without Japanese re-resolution. Coverage, assignment, ordering and aggregate-size checks prevent ambiguous projection. Expanded tests preserve custom continuation/reattack and reject malformed input; all 41 compiler/snapshot cases pass after strict Debug/Release builds (12.15/2.17 seconds), plus `git diff --check`. New-language resolver/render adapters and remaining U6/full Beta GO work remain open. Changes remain local/uncommitted.

Broad integration checkpoint: strict Debug/Release `seam_tests` builds succeeded and all 478 core/native cases passed (84.60/9.51 seconds), plus `git diff --check`. No repair was needed. Inspection established the next U7 migration boundary: sample-specific snapshot fields, factory hashing/freezing and pipeline timing/render consumption must migrate together to typed resources. No U7 implementation, installed-app qualification or U6/full Beta GO completion is claimed. Source remains local/uncommitted.

U7 typed-resource migration started: moved manifest/unit-plan/frozen-audio/selected-identity/bank-path ownership into SampleSingerResource within a variant; migrated factory, pipeline, region renderer, quality-tool consumers and tests. Sample dispatch rejects Procedural/Neural descriptors rather than falling back. Identity v6 includes the sample-resource contract tag. Strict Debug/Release core/snapshot/quality-tool builds passed; all 478 core/native plus 26 snapshot cases passed (combined 91.78/10.95 seconds), plus `git diff --check`. See `U7_TYPED_RESOURCE_STATUS_2026-09-06.md`. Non-sample payloads/backends and context-complete chunks remain unfinished; U6/U7/Beta GO are not accepted. Changes remain local/uncommitted.

U7 sample-option separation: moved PhraseRenderOptions into SampleSingerResource and updated factory/pipeline consumers. A normal-snapshot regression proves frozen option copying, identity restoration and -6 dB PCM scaling. Strict Debug/Release affected builds and all 27 snapshot cases pass (8.18/1.40 seconds), plus `git diff --check`. No fresh broad-suite/quality-tool run is claimed. Shared backend contracts, non-sample payloads/adapters and U6/U7/Beta GO acceptance remain open. Changes remain local/uncommitted.

U7 non-sample byte freezing: added private immutable payload storage and typed factories with kind/digest/bounds checks, deep-copy ownership and cancellation-aware hashing. Tests cover source mutation, mismatches, missing/invalid input and unsupported dispatch; all 28 snapshot cases pass after strict Debug/Release builds (8.15/1.28 seconds), plus `git diff --check`. Payloads are opaque bytes, not validated patches/models or working singer backends. U6/U7/Beta GO remain incomplete; changes remain local/uncommitted.

U7 output ownership: introduced common backend audio/frame-range finalization with exact context coverage, finite-PCM/bounds/cancellation checks and owned-only slicing. Sample rendering uses its full legacy extent, preserving output; independently declared snapshot chunk windows and identity binding remain unfinished. Strict Debug/Release affected builds and all 62 synthesis/snapshot cases pass (12.73/2.97 seconds), plus `git diff --check`. Context-complete splitting and non-sample backends are not claimed complete. U6/U7/Beta GO remain open; changes remain local/uncommitted.

U7 snapshot-owned windows: factory-owned absolute output ranges now enter identity v7 and control publication while retaining full phrase musical/source context. Four-renderer linked-note tests reconstruct exact whole-phrase PCM from two chunks and verify identity/invalid-window behavior. All 30 snapshot cases pass after strict Debug/Release affected builds (10.05/1.56 seconds), plus `git diff --check`. Full-context re-rendering per slice is not efficient context trimming or automatic scheduling; these and non-sample backends remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

U7 snapshot scheduling: added immutable snapshot submission with window-specific job IDs and pre-cache family validation. Cache/worker PCM must match declared owned endpoints before publication. Two-worker tests verify sibling coexistence, exact reconstruction, cache reuse and wrong-extent rejection; all 31 snapshot cases pass after strict Debug/Release affected builds (9.93/1.43 seconds), plus `git diff --check`. Automatic planning/group invalidation, context efficiency and native runtime adoption remain unfinished. U6/U7/Beta GO stay open; changes remain local/uncommitted.

U7 revision groups: snapshot jobs now use project-scoped keys and stable region revision groups. New revisions cancel obsolete group work, late unseen old windows are stale, and queued old successful/cache-hit PCM is removed at delivery. All 32 snapshot cases pass in strict Debug/Release (10.29/1.54 seconds); strict Release core/native build and 478 cases also pass (9.67 seconds), plus `git diff --check`. Automatic planning, lifecycle cleanup/reset, no-replacement invalidation and native adoption remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

U7 no-replacement invalidation: added explicit bounded, monotonic group revision floors for deletion/mute-style transitions without dummy jobs, plus a shared snapshot group-key helper. Tests verify old queued/late jobs cannot publish, other groups remain valid and cached content remains reusable. All 33 snapshot cases pass after strict Debug/Release affected builds (9.89/1.41 seconds), plus `git diff --check`. Lifecycle event wiring and reset/reclamation remain open; U6/U7/Beta GO are incomplete. Changes remain local/uncommitted.

U7 chunk planning: added deterministic bounded output-window planning and frozen-source snapshot subdivision with shared resource objects, identity parity and aggregate metadata limits. Tests reconstruct exact four-backend PCM after source WAV removal from its expected path and reject invalid/expanding plans. All 34 snapshot cases pass after strict Debug/Release affected builds (11.61/1.95 seconds), plus `git diff --check`. Project-level extent derivation/submission and efficient context rendering remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

U7 scheduler reset: added epoch-isolated reset for queued/active work, completion delivery and revision-group tracking, preserving reusable PCM. A held-worker regression proves old work cannot enter a new revision-zero session and discarded queued work never executes. All 35 snapshot cases pass after strict Debug/Release affected builds (16.04/1.61 seconds), plus `git diff --check`. Lifecycle event wiring and automatic chunk orchestration remain unfinished; U6/U7/Beta GO stay open. Changes remain local/uncommitted.

U7 owned-output assembly: added bounded exact-coverage assembly from unordered chunk PCM views, rejecting incomplete/overlapping/non-finite output and supporting cancellation without partial publication. Real scheduler completions reconstruct full PCM through the helper. All 36 snapshot cases pass after strict Debug/Release affected builds (10.65/1.41 seconds), plus `git diff --check`. Completion-manifest provenance checks and native project orchestration remain unfinished; U6/U7/Beta GO stay open. Changes remain local/uncommitted.

U7 completion-manifest gate: assembly now requires exact expected job/hash/revision/rate/window matches and complete successful PCM delivery before coverage validation. Real worker/cache-hit regressions and altered-metadata rejection pass across all 36 snapshot cases after strict Debug/Release affected builds (10.97/1.68 seconds), plus `git diff --check`. Caller manifest freshness and final native revision-gated publication still require orchestration integration. U6/U7/Beta GO remain incomplete; changes remain local/uncommitted.

Procedural recipe foundation: added seam_voice_design with an initial versioned VoiceRecipe and bounded, strict JSON codec. Roundtrip tests preserve controls, style/phone poses and a full uint64 seed; malformed ranges, duplicate poses, unsupported engine/schema, invalid UTF-8, unknown fields and malformed seeds reject. Strict Debug/Release dedicated builds and the regression case pass (0.47/0.39 seconds), plus `git diff --check`. See `docs/formats/VOICE_RECIPE_V1.md`. This starts a U18 prerequisite, not DSP, a qualified voice or U18/U6/U7/Beta GO acceptance. Changes remain local/uncommitted.

Procedural recipe/resource bridge: strict recipes now freeze into identity-bound immutable procedural payloads and decode through format/version/ID validation. Tests distinguish semantic recipes from arbitrary hash-valid bytes and preserve prior data across draft edits. Both dedicated voice-design cases pass after strict Debug/Release builds (0.47/0.42 seconds), plus `git diff --check`. Actual synthesis/dispatch, UI, provenance and acoustic qualification remain unfinished; Beta GO stays open. Changes remain local/uncommitted.

U19 oral-resonance foundation: implemented a stateful linear parallel resonance bank with recipe selection, Nyquist/stability checks, relative band weights, bounded normalized input and atomic failed/cancelled-block state handling. Diagnostic tests verify spectral reweighting, tone-frequency preservation, block invariance and parameter extremes. All three voice-design cases pass in strict Debug/Release (0.56/0.35 seconds), plus `git diff --check`. See `U19_VOCAL_DSP_STATUS_2026-09-06.md` for the primary formula source and limits. Phonation, nasal modeling, interpolation, articulation, renderer integration and listening qualification remain unfinished; no complete voice or Beta GO is claimed. Changes remain local/uncommitted.

U19 compiled-F0 excitation: added sequential bounded harmonic excitation, deterministic filtered aspiration, recipe modulation and transactional state advancement from immutable compiled performance. Actual PCM tests measure C4→G4, block/reset reproducibility, seed variation, silence, failure/cancellation and one steady-state folded-harmonic diagnostic. All four voice-design cases pass after strict Debug/Release builds (1.01/0.48 seconds), plus `git diff --check`. This is not a qualified alias-free glottal source or complete singer; tract/phrase integration and acoustic acceptance remain open. Changes remain local/uncommitted.

Sustained-pose audition integration: connected frozen recipe decoding, compiled-F0 excitation, oral resonance, post-filter dynamics/articulation and owned-window finalization with resource/algorithm metadata. Tests verify block equality, exact slices, half gain and closed staccato gates; all five voice-design cases pass after strict Debug/Release builds (1.42/0.55 seconds), plus `git diff --check`. This is not consonant/phrase synthesis or procedural snapshot dispatch; native integration and acoustic/intelligibility qualification remain open. U19/U20/Beta GO are incomplete; changes remain local/uncommitted.

Source-project identity repair: the internal canonical render ProjectId was incorrectly being used for scheduler isolation. Added real sourceProjectId metadata, used it for job/group/manifest identity and preserved canonical content hashes for audio reuse. A two-project regression verifies independent revision behavior despite equal acoustic hashes; all 37 snapshot cases pass in strict Debug/Release (10.71/1.42 seconds), plus `git diff --check`. This corrects an earlier isolation claim. Procedural snapshot factory/dispatch and native orchestration remain unfinished; Beta GO stays open. Changes remain local/uncommitted.

Procedural snapshot/dispatch: implemented whole-region frozen recipe/pronunciation/performance capture and explicit sustained-vowel pipeline dispatch with independent resource/cache identity and no fake sample units. Guards reject unsupported consonants, vowel transitions, multi-nucleus notes, retiming and sample-specific edits. Exact DSP parity, immutable state, owned slices and sample-file independence are covered; all 43 focused snapshot/voice-design cases pass in strict Debug/Release (11.98/2.37 seconds), plus `git diff --check`. Scheduling/native selection, fuller articulation and acoustic acceptance remain open; Beta GO is not achieved. Changes remain local/uncommitted.

Procedural scheduling: supported procedural snapshots now enter workers and cache lookup only after shared semantic preflight; full/owned frame extents and matched completion assembly remain enforced. Worker/direct PCM parity, cache reuse, invalid cached-request rejection and exact two-chunk assembly pass across all 43 focused cases in strict Debug/Release (12.01/1.70 seconds), plus `git diff --check`. Native resource selection, automatic procedural subdivision and full articulation/quality work remain open. Beta GO is incomplete; changes remain local/uncommitted.

Procedural subdivision: automatic owned-window derivation now shares the procedural identity builder and immutable source objects, with output/context and metadata budgets enforced. Derived chunks schedule and reconstruct exact PCM; all 43 focused cases pass in strict Debug/Release (11.74/1.72 seconds), plus `git diff --check`. Full-context DSP repetition, native integration and articulation/quality qualification remain unfinished. Beta GO stays open; changes remain local/uncommitted.

Procedural causal-window optimization: stops DSP at owned end and discards pre-roll PCM while retaining exact source/filter state. The checked 4,000–9,000 slice processes 9,000 frames rather than 24,000 and retains only its 5,000 samples. All 43 focused tests pass in strict Debug/Release (13.10/2.99 seconds), plus `git diff --check`. No whole-app speedup or noncausal-lookahead shortcut is claimed; repeated pre-roll/native/quality work remain open. Beta GO is incomplete; changes remain local/uncommitted.

Procedural streaming/checkpoints: added sequential sustained-pose rendering with independent DSP checkpoint copies, reset, forward-gap processing and whole-window rollback. Two adjacent 12,000-frame windows advance 24,000 rather than 36,000 DSP frames and match full PCM exactly. Gap/tail, checkpoint independence, invalid-window and zero/nonzero-position failure recovery regressions pass. Strict affected Debug/Release builds and all 43 focused cases pass (11.94/1.76 seconds), plus `git diff --check`. Independent snapshot jobs still use one-shot rendering; checkpoint identity binding and scheduler/native adoption remain open. Clarified the historical U3 handoff: U3–U5 are locally accepted, U6 is the next unaccepted unit. No new unit or Beta GO acceptance is claimed; source remains local/uncommitted.

Procedural snapshot checkpoint binding: added a single-owner stream that requires exact immutable context objects and matching resource/lifecycle/render metadata before reuse, and refuses output ownership expansion. Tests prove rejected/cancelled requests preserve state and valid sibling chunks reconstruct full PCM with no repeated DSP frames. Strict Debug/Release affected builds and all 38 snapshot cases pass (11.02/1.48 seconds), plus `git diff --check`. Scheduler worker sharing, out-of-order checkpoint selection and native lifecycle adoption remain unfinished; U6/U7/Beta GO stay open. Changes remain local/uncommitted.

Procedural scheduler checkpoint reuse: workers now copy the nearest compatible completed checkpoint, render outside the scheduler lock and retain at most 16 states. Reverse-order/unavailable checkpoints fall back to context-origin rendering; reset and revision invalidation discard obsolete state, and epoch/cancellation gates prevent stale retention. Worker teardown now joins before member destruction. Sequential, reverse-order and reset tests verify exact PCM and actual DSP-frame counts. Strict affected Debug/Release builds and all 38 snapshot cases pass (11.16/1.49 seconds), plus `git diff --check`. Concurrent starts may still repeat pre-roll; native integration, full expressions/articulation and acoustic acceptance remain unfinished. U6/U7/Beta GO remain open; source is local/uncommitted.

Typed project-rendering integration: introduced sample/procedural track-source variants and shared project rendering, preserving the old sample entrypoint. Procedural regions now enter real project routing at their absolute frame origin without fake sample metadata. New tests verify direct PCM equality, leading silence, pan, gain and invalid source/capability rejection. All 39 snapshot cases pass in strict Debug/Release (11.92/1.57 seconds), and the rebuilt Release core/native suite passes (9.47 seconds); `git diff --check` passes. Native coordinator selection/preflight, persisted recipes, procedural project caching/chunk scheduling and overlap allocation remain unfinished. U6/U7/Beta GO remain open; changes are local/uncommitted.

Typed preview coordinator: added explicit resolved-source submission, procedural recipe preflight, typed project rendering and procedural phrase-progress counting while preserving sample trust checks and revision-gated publication. Async tests verify exact PCM, truthful non-bank metadata, corrupt-resource failure with last-good-audio preservation, recovery and old-revision rejection. All 28 coordinator/runtime cases pass in strict Debug/Release (7.58/2.87 seconds), plus `git diff --check`. Native UI/session selection and recipe persistence are not yet implemented; project caching/chunk scheduling, articulation and acoustic acceptance remain open. U6/U7/Beta GO stay incomplete; source is local/uncommitted.

Recipe file persistence: implemented validated durable atomic save and bounded read-only loading into canonical immutable resources, optionally requiring exact singer identity. Tests preserve original bytes on invalid/cancelled/pre-replacement-injected save failures, reject stale identity/oversized/future/symlink input and retain old frozen resources across valid draft changes. All six voice-design cases pass in strict Debug/Release (1.59/0.61 seconds), plus `git diff --check`. This supplies a durable loader for upcoming native selection; saved project references, UI, relinking and lineage remain unfinished. No new unit or Beta GO acceptance is claimed; changes remain local/uncommitted.

Saved procedural selection contract: project schema 9 adds a validated track-level recipe path, exact identity and style; schemas 1–8 migrate with no procedural selection. Strict decode rejects malformed references and version-downgraded non-null selections. Project rendering/snapshot factories prevent sample substitution and mismatched procedural identity/style. All 54 codec/snapshot cases pass in strict Debug/Release (12.22/1.62 seconds for the selected entries); the rebuilt Release core/native suite passes 479 cases (9.26 seconds) after updating two obsolete schema-8 test literals and the required include. `git diff --check` passes. See `docs/formats/PROJECT_JSON_V9.md`. Runtime path resolution, native selection/undo/relinking and procedural export integration remain open. No additional unit/Beta GO acceptance is claimed; source remains local/uncommitted.

Saved-recipe runtime resolution: authoring requests now carry unresolved typed file references into the render worker, which resolves project-relative paths, verifies exact saved identity and renders without sample fallback. No file I/O was added to UI request construction. An actual saved/reopened project previews through transport with no sample bank, follows note edits/undo, rejects changed recipe bytes while preserving last-good audio, and recovers after restoration. Unsaved relative paths reject without CWD guessing. All 29 runtime/coordinator cases pass in strict Debug/Release (8.16/3.00 seconds), plus `git diff --check`. A non-copyable test-factory construction issue was corrected without changing production ownership. Native recipe selection/relinking UI, selection commands, packaging and export remain open. U6/U7/Beta GO stay incomplete; changes remain local/uncommitted.

Undoable recipe selection: added a pure expected-value-guarded command for selecting/clearing/repointing recipe references, with track audio invalidation and exact undo/redo. Sample-bank selection now clears and restores procedural selection appropriately. Fixed live performance-job identity comparison to include recipe references; pending jobs cannot survive singer selection or be revived by undo. Command/runtime regressions prove exact state/audio restoration, stale/invalid rejection and sample switching. All 45 focused cases pass in strict Debug/Release (4.09/2.54 seconds), plus `git diff --check`. Native picker/relink UI, full asynchronous picker lifetime guards, packaging and export remain open. No new unit or Beta GO acceptance is claimed; changes remain local/uncommitted.

Native recipe picker/relink: added macOS File-menu actions and controller file-dialog handling. Selection commits an absolute external recipe reference; relinking enforces the prior identity/style. A captured live job context rejects obsolete modal-dialog results, including same-content document replacement. Injected-dialog tests cover cancellation, selection, identity mismatch, relink, undo and stale results. Strict Release core/native build and suite pass (10.26 seconds), plus `git diff --check`. Actual native-panel visual interaction was not tested. Multi-style selection still requires explicit style-choice UI, and packaging/export/cross-platform parity remain open. U6/U7/Beta GO stay incomplete; changes remain local/uncommitted.

Procedural export integration: typed sources now flow through single-file final export and transactional master/stem export sets, including both standalone actions. Set receipts separate procedural identities/styles from sample banks. A real WAV regression verifies exact Float32 single/master/stem PCM and preservation of an existing committed set after changed recipe content is rejected. All 15 export cases pass in strict Debug/Release (1.19/0.52 seconds); the rebuilt Release core/native suite passes (10.45 seconds), plus `git diff --check`. Packaging, native-panel visual verification, multi-style selection and acoustic qualification remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

Multi-style native recipe selection: added a bounded, accessibility-labeled macOS style popup backed by the loaded recipe's explicit style set. Controller validation rejects unknown styles and obsolete dialog results; cancellation remains side-effect free. Injected-dialog tests cover non-default selection/undo, cancellation, invalid responses and same-content document replacement during style selection. Strict Release native/core build and all 482 cases pass (10.14 seconds), plus `git diff --check`. Actual native-popup visual interaction, other-platform parity, packaging and acoustic qualification remain open. No new unit or Beta GO acceptance is claimed; changes remain local/uncommitted.

Save As recipe-reference safety: fixed silent base-directory retargeting by verifying relative recipe identities at the destination before writing a relocated project. Missing or changed recipes preserve existing destination bytes and all live document state; an exact destination recipe permits save without clearing undo history. Strict Release core/native build and all 483 cases pass (10.21 seconds), plus `git diff --check`. No automatic copying or portable-package completion is claimed; a proper packaging transaction remains open alongside acoustic/full-scope work. U6/U7/Beta GO remain incomplete; changes remain local/uncommitted.

Transactional recipe/project snapshot packaging: added opt-in export preparation that freezes canonical recipe resources, deduplicates bounded bytes by hash, writes relative references into a project copy and publishes those files through the existing export transaction. Audio outputs use the same frozen resources. A regression removes the original recipe mid-export, then reopens the packaged project and reproduces exact PCM, including receipt/recovery coverage. All 15 export cases pass in strict Debug/Release (1.18/0.56 seconds), plus `git diff --check`. The working project is unchanged. Native option exposure and bundling of external sample/backing dependencies remain open; this is not universal portable-project completion. U6/U7/Beta GO stay incomplete; changes remain local/uncommitted.

Native packaging opt-in: macOS Export Set now offers Audio Only (default), Include Project, and Cancel for procedural projects, with explicit scope/privacy wording. Document-generation checks cover both modal steps. Controller tests verify cancelled/stale requests create no export, packaging commits asynchronously without mutating the live project, and audio-only output excludes editable project/recipe files. Strict Release core/native build and all 483 cases pass (11.10 seconds), plus `git diff --check`. Actual native-panel visual verification, cross-platform parity, broader dependency packaging and acoustic/full-scope acceptance remain open. Beta GO is incomplete; changes remain local/uncommitted.

U6 accepted amplitude release: compiler revision 7 accepts Release lanes through shared ownership/interpolation, applies a within-note end fade, preserves linked continuations and prevents released source tails from reopening. Manual Replace and zero/absent release remain neutral. Multi-rate evaluator/shared-gain tests and actual procedural PCM multiplication checks pass, along with all snapshot regressions: 62 focused cases in strict Debug/Release (18.11/3.39 seconds), plus `git diff --check`. Release-consonant modeling, remaining expression channels and Neural/acoustic qualification remain unfinished. U6/U7/Beta GO stay open; changes remain local/uncommitted.

U19 oral-pose crossfade primitive: added bounded transitions between independently stable resonance banks, with smoothstep output blending, differing formant-count support and atomic bank/progress rollback. Tests verify exact block invariance, blend oracle/target convergence, cancellation/invalid input and reset behavior. All 46 voice-design/snapshot cases pass in strict Debug/Release (14.02/2.19 seconds), plus `git diff --check`. This primitive is not yet scheduled by phonemes in the phrase adapter; nasal/retargeting/articulation and listening work remain open. U19/U20/Beta GO remain incomplete; changes are local/uncommitted.

Procedural vowel-sequence integration: revision 2 schedules validated oral-vowel poses from compiled note-bound phonemes, splits DSP at absolute events, uses bounded 20 ms/short-note crossfades and preserves transition state across chunks/reset. Required poses validate before cache admission; same-vowel notes retain their bank. A real a-to-i sequence changes only the post-boundary audio and reconstructs exactly across a mid-transition scheduler checkpoint without repeated DSP. All 47 voice-design/snapshot cases pass in strict Debug/Release (14.37/2.70 seconds), plus `git diff --check`. Consonants, multi-nucleus note timing, nasal behavior and acoustic/intelligibility qualification remain unfinished. U19/U20/Beta GO stay open; changes remain local/uncommitted.

Shared multi-nucleus vowel timing: compiler revision 8 retains immutable shared phoneme timing anchors; procedural revision 3 schedules from those anchors rather than note-only boundaries. Multiple oral vowels in one note now render with strict key/coverage checks and phoneme-span-bounded transitions. Tests verify same-note a-to-i timing/audio and a very short a-i-a sequence, while existing checkpoint/reset cases remain passing. All 64 focused cases pass in strict Debug/Release (19.06/3.35 seconds), plus `git diff --check`. Explicit retiming, consonants, nasal behavior and acoustic qualification remain open; U6/U19/U20/Beta GO are incomplete. Changes remain local/uncommitted.

Explicit within-note procedural timing: revision 4 consumes shared edited vowel spans, gates excitation/publication outside them and splits DSP at activity boundaries. Invalid/overlapping/out-of-note timing rejects through common preflight before cache admission. Tests verify delayed onset, internal silence, early end, exact owned-chunk reconstruction and early/late out-of-note rejection. All 47 voice-design/snapshot cases pass in strict Debug/Release (16.83/2.93 seconds), plus `git diff --check`. Extended phonation context, consonants, nasal behavior and acoustic/click-free qualification remain unfinished. U19/U20/Beta GO stay open; changes are local/uncommitted.

Edited vowel-boundary ramps: procedural revision 5 replaces hard authored gate edges with at-most-5-ms smoothstep ramps, shortened for brief spans. Contiguous vowels retain uninterrupted gain; silence remains exact outside activity. Boundary-zero and mid-ramp checkpoint reconstruction checks pass across all 47 voice-design/snapshot cases in strict Debug/Release (14.85/2.78 seconds), plus `git diff --check`. This is an artifact-reduction implementation, not listening/intelligibility acceptance. Extended context, consonants, nasal behavior and full Beta GO remain unfinished; changes are local/uncommitted.

Procedural output markers: revision 6 returns planned vowel gesture keys/labels and owned-window-bounded spans with explicit clipping flags through the phrase pipeline, separate from sample placements. No markers are fabricated for unscheduled auditions or gap-only windows, and no acoustic measurement/approval is implied. Full/cropped/gap marker regressions pass across all 47 voice-design/snapshot cases in strict Debug/Release (14.83/2.76 seconds), plus `git diff --check`. Durable baking, scheduler marker delivery, consonants, nasal behavior and full acoustic/Beta GO acceptance remain unfinished; changes are local/uncommitted.

Scheduler marker delivery: typed snapshot requests now carry bounded frozen-window marker projections through workers and cache hits. Stale/cancelled completion gates clear them with PCM; manifest assembly compares them against the expected snapshot and rejects tampering. All 40 snapshot cases pass in strict Debug/Release (13.21/1.78 seconds), plus `git diff --check`. PCM cache bytes are unchanged; markers derive from current frozen input metadata. Durable baking, native marker presentation, consonants/nasal behavior and full Beta GO remain unfinished; changes are local/uncommitted.

Explicit procedural candidate baking: added opt-in mono Float32 region candidates, planned-marker metadata, exact audio/resource/render lineage and unapproved status to the existing export transaction, including source project/recipe snapshots. Candidate-only requests are supported with bounded candidate/note/file counts and duplicate-source rejection. Tests prove exact Final pipeline PCM, hashes/relative marker bounds, recovery and unchanged source project. All 15 export cases pass in strict Debug/Release (1.25/0.57 seconds); rebuilt Release core/native tests pass (10.00 seconds), plus `git diff --check`. See `docs/formats/PROCEDURAL_CANDIDATE_V1.md`. Native bake actions, producer ingestion/QC, consonants/nasal behavior and acoustic/full Beta GO acceptance remain open; changes are local/uncommitted.

Native candidate-bake action: added the macOS File-menu command and dedicated save-dialog purpose, routing candidate-only/source-snapshot requests through the existing asynchronous export worker and document-generation guard. Tests verify empty input, cancellation, stale modal results, committed WAV/metadata/source files, no master audio and unchanged working project. Strict Release core/native build and all 483 cases pass (10.90 seconds), plus `git diff --check`. Win32 dialog routing is source-updated but unverified; real panel interaction, producer ingestion/QC, consonants/nasal behavior and full Beta GO remain open. Changes are local/uncommitted.

Strict candidate loader: added read-only loading against an expected recipe, bounded metadata/audio, canonical phoneme keys, ordered marker/frame validation and owned-byte digest/Float32 decoding. No approval or repository mutation is returned. Real-bake and malformed metadata/audio regressions pass across all 15 export cases in strict Debug/Release (1.25/0.63 seconds), plus `git diff --check`; a decimal-vs-hex key parser mistake was repaired against the domain formatter. Producer repository integration/lineage/QC and full consonant/acoustic/Beta GO acceptance remain open; changes are local/uncommitted.

Producer candidate import/lineage: added strict candidate loading into the existing raw-asset/take transaction with stored-digest recheck, mandatory MarkerReview/no review, procedural-strategy gating and one-generation lineage persistence. Exact candidate/recipe bytes and hashes are bound to the raw asset; retakes clear prior review flags. Tests verify import/recovery, approval rejection, lineage tamper rejection, retakes and failed-save state preservation across all 17 export/producer cases in strict Debug/Release (1.46/0.61 seconds), plus `git diff --check`. Verification caught and resolved the missing journal-action registration and JSON include. Native/CLI producer wiring, measured QC/admission, consonants/nasal behavior and full Beta GO remain open; changes are local/uncommitted.

Producer CLI import: added `import-procedural` with explicit existing workspace, candidate/recipe files, inventory assignment, MIDI layer, operator/time and optional retake predecessor. It delegates to the strict repository import and prints MarkerReview/digest/generation without approval flags or strategy creation. A real shell-free subprocess regression verifies committed retake/lineage recovery and malformed MIDI rejection. Export integration plus CLI help/validate/inspect CTest entries all pass in strict Debug/Release (2.84/1.48 seconds), plus `git diff --check`. Windows subprocess execution, native producer import UI, resumable generation/QC and full Beta GO remain open; changes are local/uncommitted.

Native producer intake: completed the interrupted Studio import integration with Cmd/Ctrl+I metadata/recipe dialogs, selected-row retake binding, strict repository import and inspection clearing. Centralized epoch/generation/row checks now run after each dialog; workspace reopening invalidates prior context. Recording must finish before import. All 15 controller/export cases pass in strict Debug/Release (1.44/0.61 seconds), the Release native Studio executable builds, and `git diff --check` passes. Non-fatal host Xcode filesystem/cache warnings appeared during successful builds; compiler policy was unchanged. Real panel interaction, asynchronous intake/responsiveness, measured QC, consonants/nasal behavior and full Beta GO remain open; changes are local/uncommitted.
