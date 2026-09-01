# Project SEAM and OpenUtau Comparative Product and Architecture Audit

- **Audit date:** 2026-09-01
- **Project SEAM source:** `6b002df561e63ba3c4246fc135f1049b7d4fb9de`
- **OpenUtau source:** `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`
- **OpenUtau wiki:** `c993663440a8b598ce3cd89200d693a86b693bd1`

## Executive conclusion

OpenUtau is the right competitive and interoperability reference for Project SEAM, but it is not the right implementation base.

Project SEAM should preserve its C++ realtime architecture, explicit renderer diagnostics, data-only voicebanks, signed trust model, standalone and plug-in parity, and evidence-bound release process. It should selectively trial the creator workflows that OpenUtau makes plausible: persisted vibrato, typed dynamics, stronger lyric and note productivity, USTX interchange, voice-style and multipitch selection, and singer diagnostics. Competitor presence is not proof of demand, so the companion plan begins with a target-creator task study before freezing schema 8.

The current production-readiness plan is strong as a release-engineering program but overstates the completeness of the authoring product. Its statement that most required authoring work already exists is true for a minimal sample-concatenative vertical slice, but not for a credible creator-facing singing editor measured against OpenUtau. The production plan therefore needs a separate creator-workflow gate before candidate freeze.

The correct adoption verdict is:

- **Trial selective interoperability:** adopt USTX concepts, truthful typed capabilities, vibrato and dynamics workflows, and singer diagnostics in a bounded first-party implementation.
- **Reject wholesale migration or fork:** do not replace SEAM with OpenUtau, embed its application runtime, or copy its trust and extension model.
- **Reject executable extensions in the Beta critical path:** do not load arbitrary phonemizer, resampler, wavtool, or package DLLs into the SEAM process.

## Audit method and limitations

This audit reviewed source, wiki documentation, CI, test inventory, release scripts, project models, renderer and phonemizer interfaces, import and export code, package management, voicebank installation, and update handling. It then compared those findings with Project SEAM's domain model, project codec, editor models, rendering pipeline, voicebank model, accepted ADRs, tests, and U53-U67 production plan.

OpenUtau was not built locally because no `dotnet` SDK is installed on this machine. The audit therefore makes source-level and official-CI observations about OpenUtau, not a claim that its current commit passed locally. Project SEAM's unchanged Development and Release builds and both 65-test CTest suites had already passed before this audit; no source file changed during the research phase.

Seven independent document-review lenses then stress-tested the proposed roadmap. Their convergent corrections are incorporated into the final plan: defer MIDI from Beta, replace the generic expression framework with typed dynamics, split unsigned pre-freeze readiness from signed-installed Beta readiness, harden rapidyaml callbacks and file-handle identity, preserve legacy style rendering, add Raw pitch-curve support, require a hermetic OpenUtau verifier, enumerate the visual matrix, and ratify scope with target creators. Four optional cross-model review jobs were attempted but returned provider credit errors, so they contributed no findings or corroboration.

### Review coverage and incorporated disposition

| Review lens | Incorporated result |
|---|---|
| Product | Name the initial creator segment; run a pre-schema task study; require real creators to finish and continue the imported-song journey; defer MIDI without a distinct job. |
| Scope | Remove producer-only Studio work, future-engine schema generality, and External Beta rewrites from the creator critical path. |
| Design | Define mixed-note vibrato, compact-menu command ownership, one conversion-review modal, one style sheet, continuous-control accessibility, and an exact responsive matrix. |
| Security | Use handle-bound no-follow I/O, complete dependency provenance, a hermetic OpenUtau profile, shared report budgets, and escaped untrusted display text. |
| Coherence | Split the creator states, unify candidate identity, make hash invalidation effect-scoped, state pitch composition order, and align the amended production narrative. |
| Adversarial | Prohibit rapidyaml default callbacks, define vibrato units, preserve legacy style output, make Raw pitch-capable, and replace generic expressions with typed dynamics. |
| Feasibility | Target the real CLAP editor-state codec, use document-lifecycle semantics for USTX open, map USTX millisecond fields explicitly, and avoid inventing a manifest default. |

No P0 or P1 review finding remains intentionally unresolved in the revised documents. This is a document-review result, not evidence that the planned implementation or release gates have passed.

## Progress truth

Several percentages have been used for different denominators. They must not be merged into one number.

| Measure | Current result | Meaning |
|---|---:|---|
| AppleClang repair requested as “95% to 100%” | **100%** | Development and Release builds, 65/65 CTest in each preset, CLAP gain QA, and cancellation publication QA passed. |
| Existing U53-U67 release-program units | **4/15 = 26.7%** | U53-U56 are committed. This measures units in the public-production delta plan, not product functionality. |
| Competitive creator-product readiness | **about 40-50%** | Engineering estimate against the workflows exposed by OpenUtau, not a formal gate. |
| Public-production readiness | **about 35-45%** | Risk-weighted estimate dominated by the missing real bank, signed artifacts, target QA, DAW evidence, cohort, and operations. |
| Formal promotion evidence | **0%** | Canonical Usable Alpha and External Beta evidence gates are not accepted. |

If the seven-unit creator-workflow companion plan is added, the combined active implementation denominator becomes **4/22 = 18.2%**. That apparent decrease is scope discovery, not regression or lost code.

## What OpenUtau actually provides

OpenUtau's product surface is materially broader than a piano-roll demo:

- It opens `.ustx`, `.ust`, `.vsqx`, `.mid/.midi`, `.ufdata`, and `.musicxml` projects; imports vocal and audio tracks; and exports stems, mixdown, UST, and MIDI.
- It autosaves every 30 seconds and offers next-launch crash recovery.
- Its piano roll includes pitch bends, note vibrato, phoneme envelopes, slur and extender semantics, batch edits, lyric transformers, and limited legacy plug-ins.
- It has renderer-dependent numerical, option, and curve expressions, including dynamics, gender, breathiness, tone shift, per-phoneme renderer selection, and voice color.
- It supports classic UTAU banks and neural singer families through Classic, WORLDLINE-R, ENUNU, Vogen, DiffSinger, and VOICEVOX renderers.
- It exposes singer setup, encoding, default phonemizer, OTO editing, voice-color mapping, voicebank merge and publish, and singer error reporting.
- It includes a dependency manager, transcription workflow, localization, themes, and minimal track effects.
- Its CI tests Windows, macOS, and Linux targets and produces installer or archive artifacts for all three operating-system families.

The primary documentation and implementation anchors are the [OpenUtau README](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/README.md), [Getting Started wiki](https://github.com/openutau/OpenUtau/wiki/Getting-Started), [renderer interface](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Render/IRenderer.cs), [phonemizer API](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Api/Phonemizer.cs), and [format dispatcher](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Format/Formats.cs).

## Feature gap matrix

The matrix classifies product behavior, not code volume.

| Capability | OpenUtau | Project SEAM | Beta decision |
|---|---|---|---|
| Note creation, move, resize, selection, quantize | Mature | Implemented | Preserve SEAM. |
| Slur and melisma editing | `+`, `+~`, phonemizer grouping | Implemented with articulation and slur groups | Add interoperable extender semantics at import and export boundaries. |
| Overlapping note visibility and selection | Usable piano-roll behavior | Implemented with stable groups, three visible bands, overflow indicator, detail cycling, and accessibility | Do not duplicate; require installed visual QA. |
| Unicode text fitting | Localized UI and lyric editing | UTF-8 display-width truncation and ellipsis exist | Do not duplicate; add CJK and scaling visual acceptance. |
| Persisted note vibrato | Full note model and editor | Missing from project model; only live MIDI CC vibrato exists | **Beta-critical gap.** |
| Typed dynamics automation | Renderer-aware generic curves | Pitch automation plus fixed technical controls | **Beta-critical gap:** add one typed dynamics contract; defer a generic registry. |
| Pitch, phoneme, unit, and seam inspection | Present, centered on UTAU envelopes and flags | Strong explicit technical lanes and renderer diagnostics | SEAM advantage; retain. |
| Batch lyric and note transforms | Broad built-in and extensible batch edits | Quantize, slur, melisma, duplicate, delete, and lyric distribution | Expand a bounded first-party set; no arbitrary plug-ins. |
| Search and phonetic assistance | Search bar and phonetic assistant | No equivalent user workflow found | Add search and actionable pronunciation diagnostics. |
| Project interchange | USTX, UST, VSQX, MIDI, UFData, MusicXML | Canonical `.seam` JSON and audio import only | **Beta-critical:** USTX only. Defer MIDI until a separate creator job and allocation-safe parser are proven. |
| Voice styles and multipitch mapping | Voice Color and subbanks | Voicebank manifests already contain styles, root MIDI layers, and takes, but projects do not persist an explicit selected style | **Beta-critical gap:** expose existing capability rather than invent a second bank model. |
| Singer setup and diagnostics | Encoding, type, OTO, publish, merge, error report | Strong production Studio and signed package tooling, weaker end-user bank diagnostics and style selection | Add end-user diagnostics; keep SEAM publishing and trust rules. |
| Phonemizer breadth | Many languages and third-party DLL discovery | One first-party Japanese phonemizer interface and implementation | Preserve the first-party boundary; add more language modules after Beta. |
| Renderer breadth | Classic plus several neural backends | Four explicit sample-based renderer backends | Neural backends are a post-Beta trial, not Beta critical path. |
| Extension ecosystem | In-process managed DLLs, external resamplers, wavtools, legacy plug-ins, packages | No arbitrary code in voicebanks and fixed first-party backends | Preserve SEAM security boundary. |
| Render cancellation and pre-render | Cancels stale tasks and pre-renders phrases | Immutable phrase snapshots, revision ordering, bounded scheduler, content cache, stale-audio state | SEAM advantage; retain. |
| Track effects | Minimal EQ, compression, and reverb | Gain, pan, routing, export; no effect rack | Defer. Both products should remain DAW companions. |
| Transcription | Optional dependency model | Missing | Defer. |
| Character use | Singer portrait can appear behind piano roll | Exact bank-character binding, Full/Minimal/Off, dock portrait, mismatch suppression | SEAM advantage; validate purpose and composition on installed UI. |
| Signed content and update trust | Hash checks in some package paths; executable extensions; ad-hoc macOS artifact | Data-only signed banks, exact content identity, Ed25519 policy, hostile-path tests, sealed installer handoff | SEAM advantage; never regress. |

## OpenUtau implementation analysis

### Project and command model

OpenUtau keeps the active `UProject` in a process-wide `DocManager`. Commands support execute and unexecute operations, undo groups, project validation, crash save, autosave, plug-in discovery, and pre-render notifications. This centralizes orchestration and is effective for a desktop editor, but Project SEAM should not import the singleton design. SEAM already has a cleaner split among canonical domain state, reversible editor commands, authoring runtime, render coordinator, and platform controller.

Useful concept to borrow: a project mutation should declare its validation and render impact, and grouped transformations should remain one undoable action. Existing SEAM command-impact and editor-session patterns are the correct home.

### Phonemizer boundary

OpenUtau's `Phonemizer.Process` receives note groups, neighboring notes, tone, position, duration, phonetic hints, and singer resources, then returns positioned phonemes and optional expressions. This is substantially richer than SEAM's current `IPhonemizer::phonemize(VocalRegion)` contract.

Useful concepts to borrow:

- note-group semantics for extender and multisyllabic lyrics;
- phonetic hints separate from the displayed lyric;
- declared language, phonetic system, singer requirements, and expression capabilities;
- actionable diagnostics tied to a note and character range.

Implementation to reject: OpenUtau discovers managed assemblies and loads them with `Assembly.LoadFile`. SEAM's accepted `docs/adr/0007-data-only-voicebanks.md` intentionally prevents third-party code from entering the realtime process. Beta should use first-party compiled modules and data-only dictionaries. A future third-party SDK requires an out-of-process, resource-limited design and a separate security decision.

### Renderer boundary

OpenUtau's `IRenderer` is phrase-oriented and declares singer type, supported expressions, layout, asynchronous render, rendered pitch, real curves, and suggested expressions. This capability negotiation is worth adopting. Its renderer registry, however, is still a hardcoded switch over singer types and renderer names, so it is a reference interface rather than a complete plug-in architecture.

For Beta, SEAM should add a first-party compiled capability record that truthfully names pitch-curve, note-vibrato, and typed-dynamics support for Raw, Classic PSOLA, Spectral Classic, and Stretch. Schema 8 should persist typed `DynamicsAutomation`, not a generic descriptor framework. Unknown imported expressions belong only in the bounded conversion report. A generic vocabulary should wait until a second implemented expression proves common scope, units, and interpolation semantics.

### Project interchange

OpenUtau's native USTX format is YAML 1.2 with a fixed 480 PPQ resolution. The pinned source currently writes USTX version 0.9. SEAM uses schema 7 JSON and a default 960 PPQ, but its tempo, meter, track, region, note, lyric, pitch, audio-track, and bank-reference structure has a clear mapping.

The safest interoperability layer is a new adapter module, not a change to SEAM's canonical codec:

- pre-bound file bytes before parsing;
- parse USTX with a pinned, permissively licensed, non-recursive parser such as [rapidyaml 0.16.0](https://github.com/biojppm/rapidyaml/releases/tag/v0.16.0), using a custom allocation budget and node, depth, scalar, collection, alias, and document limits;
- reject YAML tags, aliases, multiple documents, and unknown structures that exceed the supported USTX subset;
- convert 480 PPQ to 960 PPQ exactly on import and require an explicit rounding diagnostic for non-integral export values;
- never resolve or execute singer, phonemizer, renderer, resampler, wavtool, package, or external path references during import;
- preserve unsupported information only in a bounded provenance report, not as opaque executable state.

The USTX adapter must also use per-import rapidyaml callbacks and a bounded allocator so malformed syntax or allocation exhaustion becomes `core::Result` rather than process termination. File safety must bind validation, hashing, parsing, and export publication to opened handles rather than pathname prechecks. Valid drafts use one bounded, virtualized conversion-review modal; parse failures remain ordinary diagnostics.

Standard MIDI File import and export are deferred from the current Beta critical path; live MIDI and CLAP input remain separate supported capabilities. The inspected [`MidiFile::readSmf()` implementation](https://github.com/craigsapp/midifile/blob/98917df5b1bf0d6e8d4c0e5fff86d6b05343e793/src/MidiFile.cpp#L384-L507) reserves storage from untrusted track count and chunk length before a wrapper can enforce SEAM's allocation budget. No distinct general-score-transfer job has yet been validated: the creator study is still `NOT_RUN` and intentionally does not test MIDI. A later MIDI plan must first establish creator demand and an allocation-safe reader at the allocation site.

### Voicebank and package workflow

OpenUtau unifies classic and neural voicebanks as singers and gives users setup, OTO, subbank, merge, publish, and error-report workflows. SEAM's bank production and trust design is stronger, while its musician-facing style and diagnostic workflow is thinner.

SEAM already stores style, take, root MIDI, renderer, and coverage information in the voicebank manifest. The missing work is to persist the selected style in the project, expose compatible style and pitch layers in the inspector, explain fallbacks, and surface coverage and package diagnostics without opening Voicebank Studio.

SEAM should not copy OpenUtau's package behavior. The reviewed OpenUtau source supports package entrypoints such as DLL loaders, loads managed assemblies in process, installs archives with overwrite enabled, and uses simple `..` substring checks in some archive paths. Those observations do not establish an exploitable vulnerability, but they are not an acceptable trust template for SEAM.

### Release engineering

OpenUtau's CI breadth is useful: its pull-request matrix runs tests on Windows x64, macOS x64 and arm64, and Linux x64, while its release matrix also covers Windows x86 and arm64 plus Linux arm64. Its local codebase has about 410 C# files, 122,000 C# lines, 29 C# test files, and 124 xUnit fact or theory declarations at the pinned commit.

SEAM should not weaken its release bar to match OpenUtau's current packaging. OpenUtau's release workflow allows `create-dmg` failure, applies ad-hoc signing to the DMG, and tells users to clear quarantine attributes when macOS reports damage. SEAM's notarization, stapling, exact installed hashes, signed metadata, and candidate-root evidence remain required.

## Reassessment of the prior visual concerns

The three previously identified design concerns are real UX risks, but source inspection changes their classification.

### Overlapping notes

This is no longer an unimplemented feature. `libs/seam-editor-ui/src/note_visual_layout.cpp` creates stable overlap groups, uses up to three visible bands, exposes hidden-member counts, and draws an overflow indicator. `tests/test_ui.cpp` and `tests/test_native_ui.cpp` verify stable cycling and accessibility.

Remaining gap: installed visual QA must prove the bands, badge, detail panel, selection state, and lyric labels remain readable at dense overlaps, narrow windows, and 100%, 125%, 150%, and 200% scaling.

### Text overflow

This is also structurally implemented. `libs/seam-native-ui/src/editor_scene.cpp` truncates by Unicode display width and preserves important stale-audio suffixes; the pixel surface supports bounded text rendering and ellipsis.

Remaining gap: visual QA must cover long Korean, Japanese, Chinese, Latin, file-path, bank-name, diagnostic, and export strings. Truncation must never hide the only error action or identity discriminator, and full text must remain accessible through focus detail or accessibility metadata.

### Character assets

The current code has a shared character dock, exact voicebank-character binding, mismatch suppression, Full/Minimal/Off modes, reduced-motion transitions, and geometry tests. It is not accurate to say the asset is unused.

Remaining gap: the installed product needs a purposeful content rule. The character should reinforce selected-bank identity, readiness, missing-bank recovery, and non-blocking status without reducing the piano-roll work area or appearing when identity does not match. The creator-workflow plan therefore adds visual acceptance, not a new character subsystem.

## Missing plan content, prioritized

### P0: required before candidate freeze

1. Persisted note vibrato and a usable editor for note-relative start, vibrato-relative fades, cents depth, millisecond period, and phase turns.
2. Typed dynamics automation plus truthful pitch, vibrato, and dynamics support for the four current renderers; no generic expression framework yet.
3. A bounded creator productivity pass: search, lyric transforms, phonetic hints, overlap and legato normalization, and one-command batch undo.
4. USTX open and export with deterministic tick, millisecond, vibrato, and dynamics conversion; lossy-conversion review; non-terminating bounded parsing; handle-bound file I/O; and hermetic OpenUtau fixtures.
5. Persisted voice style selection and multipitch diagnostics using the existing manifest model.
6. Installed visual acceptance for note density, text overflow, CJK, scaling, keyboard, accessibility, and character composition.
7. A target-creator scope checkpoint plus `CREATOR_PRE_FREEZE_READY` for exact unsigned installed evidence before reproducibility and `CREATOR_BETA_READY` for exact signed installed evidence during U65.

### P1: after the first closed creator cohort

1. Additional first-party phonemizers and G2P dictionaries.
2. An out-of-process renderer and phonemizer extension protocol with signed packages and resource limits.
3. A DiffSinger ONNX adapter trial after model rights, redistribution, runtime size, GPU and CPU support, and package trust are resolved.
4. MIDI, UST, VSQX, MusicXML, and UFData bridges when user evidence justifies them and their parsers enforce limits before allocation.
5. Expanded bank merge and publish workflows that preserve SEAM identity and signature rules.

### Explicitly deferred

- Arbitrary in-process DLL, Lua, legacy UTAU plug-in, resampler, or wavtool loading.
- Full OpenUtau UI or .NET runtime embedding.
- A theme editor, transcription, dependency marketplace, or full mixer and DAW replacement.
- Feature-for-feature parity with every OpenUtau language, singer type, expression, and file format.

## Roadmap decision

Do not renumber or rewrite completed U53-U56 release units. Create a separate seven-unit creator-workflow plan and add one two-stage cross-plan gate to the existing U53-U67 program:

1. The creator units may run in parallel with U57-U62.
2. `CREATOR_PRE_FREEZE_READY` must be reached on the exact unsigned installed candidate and U58 trusted bank before U63 reproducibility work.
3. U65 must execute the same journey on the exact U64 signed installed candidate and reach `CREATOR_BETA_READY` before public activation.
4. Neural engines and arbitrary extensions stay outside this critical path.

This preserves release traceability while correcting the assumption that release engineering was the only remaining delta.

## Sources

- [OpenUtau repository at the audited commit](https://github.com/openutau/OpenUtau/tree/8c0dc4007e6e8c8181f3a12c10205671800eeb8b)
- [OpenUtau MIT license](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/LICENSE.txt)
- [OpenUtau UProject](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Ustx/UProject.cs)
- [OpenUtau UTrack](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Ustx/UTrack.cs)
- [OpenUtau UNote and vibrato model](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Ustx/UNote.cs)
- [OpenUtau renderers](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Render/Renderers.cs)
- [OpenUtau render engine](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/Render/RenderEngine.cs)
- [OpenUtau package manager](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/OpenUtau.Core/PackageManager.cs)
- [OpenUtau build and release workflow](https://github.com/openutau/OpenUtau/blob/8c0dc4007e6e8c8181f3a12c10205671800eeb8b/.github/workflows/build.yml)
- `docs/adr/0003-backend-independent-editor-model.md`
- `docs/adr/0007-data-only-voicebanks.md`
- `docs/adr/0008-phrase-scoped-render-snapshots.md`
- `docs/adr/0011-explicit-multi-renderer-backends.md`
- `docs/adr/0017-bound-untrusted-data-before-allocation.md`
- `docs/plans/2026-08-21-1901-feat-project-seam-external-beta-plan.md`
- `docs/plans/2026-08-30-2246-feat-production-readiness-completion-plan.md`
