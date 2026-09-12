# U25 vibrato editing model

Status: captured mixed-selection inspection, partial-field Apply and native paged inspector/field entry are implemented locally. Active-note handles/curve presentation, whole-host persistence/audio qualification and complete U25 acceptance remain open. U23/U24 qualification is not implicitly accepted by beginning this implementation dependency.

## Contract

`seam/ui/vibrato_model.hpp` and its implementation expose all canonical vibrato fields: enabled, start fraction, fade-in, fade-out, depth in cents, period in milliseconds and phase in turns. Inspection returns a value only where all selected notes agree; absence means mixed. In an edit patch, absence instead means preserve each note's existing value. Enabling vibrato or changing depth does not reset per-note phase, period or fades.

Preparation captures a validated immutable project and opaque document-generation context for 1–10000 selected notes in an active region containing at most 10000 notes. Every target must belong to that region. Preview rows follow tick/ID order and contain exact before/after values only for changed notes; inspection reports the whole selection. All proposed note values pass the canonical domain validator before a preview is published. In particular, a fade-in change is checked against each note's retained fade-out; one invalid target rejects the entire preview without applying any other target.

Explicit Apply rechecks source generation/revision/musical inputs, active region and selection, then uses one canonical `EditPerformanceCommand` through guarded performance publication. It preserves hints, manual pitch/dynamics, ownership, units/seams and unselected notes. Applied/cancelled previews cannot be reused. Empty/equivalent patches are history-neutral. Source capture and command application remain synchronous, with cancellation before publication rather than a claim of interruptible command mutation.

## Evidence

The regression uses mixed enabled/depth/phase/fade values and a shared period. It verifies exact whole-project preservation before Apply, rejection of a fade combination invalid only on one target and of NaN depth, precise enabled/depth changes, unrelated-field/ownership/hint/unit/seam preservation, one-step undo/redo, no-op history, changed-selection rejection, same-content document replacement rejection and cancellation.

After Apply, the canonical score performance compiler produces nonzero vibrato modulation and a changed score-frequency value inside the edited first note, where the baseline was unmodulated. This proves compiler-input behavior, not acoustic quality or every renderer/host path. Build/test results are recorded in the execution ledger.

## Remaining work

### Native form to final audio, saved project and WAV export

An integration regression now creates a saved procedural vowel recipe and a project with its typed relative recipe reference, edits all seven vibrato fields through `NativeEditorController`, and renders with `ProductionProjectRenderer` at Final quality using that recipe file. Draft-only changes leave both project data and rendered PCM unchanged. Explicit Apply changes only the expected note vibrato, emits one document notification, changes phrase hashes and produces finite, same-length audio with a material waveform difference (difference energy exceeds 1% of baseline energy).

The test saves and reloads the complete project through `ProjectJsonCodec`, resolves the saved singer reference again, and requires exact edited PCM replay. Committed Float32 master/stem WAVs are read back and match that Final render exactly. One undo restores the original complete project and original PCM; redo restores the edited project and edited PCM exactly.

Verification: all 605 Release core cases pass (13.50 s); the 21-case export suite passes Release/Debug (3.11/9.70 s), builds and diff checks pass. This is procedural-render, codec and export integration evidence—not native file-dialog execution, physical playback/listener acceptance, classical/neural backend parity, plugin-state recovery or the host matrix. The fixed test vowel is not a qualified shipping singer. Those remaining boundaries are not waived.

### Native field/form integration

The existing vocal-track inspector now exposes Edit Selected Vibrato when notes are selected, and the Edit menu owns the always-available `Edit Selected Vibrato…` command. It opens the same draft as an expanded right-anchored inspector form, with six fields on page one and phase on page two. A short-window layout keeps rows and Apply/Cancel controls in bounds at 480×320; larger windows use the available vertical inspector area. This is one active edit surface, not another native window.

Pointer, keyboard and semantic row activation opens the shared bounded native field editor. Mixed values start with empty input rather than writing a literal mixed marker. Submission updates only draft text/validation; cancellation returns to the form. Page changes identify the first field on that page as the selected reset target. Reset removes only the selected field's patch. Refresh explicitly recaptures the selection and discards the old draft. Apply to Selection dispatches the canonical command once and notifies documentChanged once; Cancel discards without changing notes. Other score shortcuts are isolated during this active edit.

Mode/page/field/interaction IDs prevent old semantic callbacks from applying a newer draft. Source and selected-region/selection/revision/generation checks remain in the model, including when a document is replaced while a field is open. Per-field text uses shortest round-trippable float formatting, so values such as 0.65 remain readable without changing their binary value on an unchanged commit.

Native regression enters through the inspector, edits enabled/depth and invalid-then-corrected coupled fades, pages to phase, cancels field input, resets only phase, rejects stale callbacks, blocks Delete, applies exactly once, preserves the complete expected project and undoes exactly. A separate case rejects replaced-document input and checks all form semantic bounds at short/normal sizes. Dispatcher coverage includes the menu route. Render and build/test evidence is in the ledger.

This supersedes the earlier unconnected-form status below. Actual live menu/IME/VoiceOver and embedded/Windows interaction, direct active-note handles, source/target/generated/measured curves, capability feedback and audio/reload/plugin-state qualification remain open. U25/Beta GO are not accepted.

### Existing inspector geometry prerequisite

Before placing editable vibrato fields, inspection found that arrangement painting hid the inspector when the visible track list filled the dock, but semantics always exposed its controls. Pointer handling also used the piano bottom instead of the full dock bottom and ignored diagnostic/export insets. This could make an invisible control actionable or make a visible control miss its pointer target.

Painting, semantics and pointer handling now share `resolveArrangementInspectorTop`, including room for the divider. Track/region target materialization stops at the same visible-list boundary as painting. Pointer routing uses the resolved dock width and full content bottom after overlays. This retains the current visibility policy; it is not a new always-visible inspector or complete long-list scrolling/navigation solution.

Render inspection additionally showed field hit boxes were offset upward by a font height because legacy names implied baseline coordinates, while `RasterCanvas::drawText(Point, ...)` uses a top-left origin. Mute/Solo/route bounds now use the painted row top. Mute and Solo are painted in separate columns matching their separate targets, rather than one combined left-aligned string.

Regression checks visible/hidden controls at 320- and 640-pixel heights with/without diagnostics, pointer Mute and Solo changes with exact undo, and a 41-track crowded dock with no hidden inspector/off-screen track targets. The captured system-font dock render is recorded in the ledger. Vibrato controls remain to be wired/rendered in this inspector; large-list reveal/scroll access, touch-target sizing and live host/screen-reader qualification remain open.

### Native inspector draft/input state

`seam/native_ui/vibrato_inspector.hpp/.cpp` adds the draft form state for all seven canonical fields. This is not yet a painted/controller-wired inspector. It exposes labels, source/mixed values, retained field text, validation errors, selected/changed counts and explicit Apply availability for the existing inspector to consume.

Each field admits at most 64 input bytes. Numeric parsing requires the complete trimmed value to be finite and valid; enabled accepts On/Off, lowercase variants or 1/0. Canonical numbers are formatted with enough precision to round-trip the original float exactly. Mixed placeholders are identified separately from an explicitly typed invalid `Mixed` string. A reset removes that field's patch, restoring per-note source values rather than assigning a default.

Bounded invalid input stays in the draft and disables Apply without touching the song. This allows a temporarily invalid fade-in/fade-out combination to be corrected by editing the other field; every rebuild evaluates the complete draft against each target's retained values. Oversized/unknown-field input is rejected without replacing the prior draft. Source selection/revision/generation and explicit active-region guards apply to editing, resetting and Apply; stale/closed/cancelled drafts cannot publish. A successful Apply remains one canonical undo group.

The new focused `seam_vibrato_inspector_tests` target verifies mixed placeholders, invalid text retention, coupled-fade recovery, exact partial-field changes/undo/redo, canonical text round-trips, malformed and nonfinite inputs, bounds, field reset, active-region/selection/document replacement guards and cancellation. Native geometry, controller/semantic bindings, actual IME entry and host/audio qualification remain open; this layer must be integrated into the single compact inspector, not treated as completion of U25.

### 10000-note command indexing follow-up

The shared `EditPerformanceCommand` now indexes requested note IDs once per live/staged project state for hint-change detection, expression validation, before-state capture, mutation and mixed-hint restoration. Index storage is bounded by the 10000-edit admission; unrelated notes are scanned but not retained. Missing/repeated edit IDs and ambiguous requested IDs reject before mutation. Indexes are local to a command phase and never survive a project replacement. Canonical hint reconciliation, ownership validation and region/style paths remain intact.

A 10000-note fixture verifies enabled/depth updates and exact one-step undo/redo. On the local Release run, Apply decreased from 105.08 ms to 4.52 ms after indexing; preparation was 1.76 ms, undo interval 2.26 ms and redo interval 1.84 ms. Debug measured 10.98/31.14/14.33/14.29 ms respectively. The undo interval also includes after-state copying/checks, and redo includes the preceding equality check; these are local regression observations, not isolated microbenchmarks or host-independent guarantees. A separate regression verifies atomic ambiguous/missing/repeated-target rejection and successful exact revert on a valid target.

Verification: 598 Release core cases pass (12.97 s); 28 focused creator cases pass Release/Debug (1.08/4.77 s), and the dedicated performance-command/edit-preservation suites pass 18/5 cases. Strict builds and diff checks pass. Changes remain local/uncommitted.

Connect mixed values, bounded field input, active-note handles and explicit Apply to Selection to the compact native inspector using this model. Qualify all input modalities, reload/plugin state and audible output. The 10000-note model/command path is now measured above; native interaction and worst-case latency still need qualification. Retain separate source/target/generated/measured curve presentation and resource-capability feedback required by U25. No unit or release gate is accepted by this model checkpoint.
