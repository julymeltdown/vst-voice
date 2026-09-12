# Initial native Voice Designer controls

## Implemented

Studio now hosts `VoiceDesignerSession` in a separate draft view, entered/exited with Cmd/Ctrl+D. Entry requires production work and recording to be finished and stops audition. Keyboard and pointer/scroll routing keep hidden producer controls inactive while Designer is visible.

Within Designer:

- Cmd/Ctrl+N creates an unsaved `voice-draft` with one neutral a pose. This is a technical starter recipe, not a complete or qualified female singer.
- Cmd/Ctrl+O opens a recipe through background session loading. Dirty drafts require explicit discard confirmation before replacement begins.
- Cmd/Ctrl+S saves the current path or opens Save As for a new draft. Cmd/Ctrl+Shift+S always opens Save As. Save As remains new-file-only; the file-dialog result is checked against the captured Designer epoch/revision.
- Up/Down or Tab selects open quotient, spectral tilt or aspiration. Left/Right adjusts within the recipe's validated bounds; Shift reduces step size to one tenth. Each key adjustment is an ordinary undoable edit. Mouse sliders and continuous gesture forwarding are not implemented yet.
- Cmd/Ctrl+Z undoes; Cmd/Ctrl+Shift+Z or Cmd/Ctrl+Y redoes.
- Escape requests cancellation during file work. Window close is refused during file work; dirty drafts now present an explicit discard-or-cancel choice. Cancel returns to the draft so it can be saved.

The view shows saved/unsaved state and explicitly says `DRAFT ONLY / NOT A PUBLISHED BANK / AUDITION NOT CONNECTED`. It preserves the producer workspace when entering/leaving. It does not currently provide pose/style editing, audible preview, A/B comparison or generation directly from the draft.

## Live verification

### Pose-dialog source guard

The duplicate-pose naming flow now captures the source pose index as well as voice epoch/revision. The session rejects a supplied expected source index that no longer matches selection. This closes a gap because audition-pose selection is intentionally not a recipe edit and therefore does not advance recipe revision. Immediate API callers may still omit the optional expectation to duplicate their current selection; the modal native caller always supplies it.

A regression switches between differently tuned poses without changing recipe revision, rejects the old source expectation without adding history, accepts a current expectation and verifies copied resonances and exact undo. All 23 Designer tests and both Studio builds pass in Debug/Release. This is a stale-dialog safety repair, not another completed roadmap unit or broader acoustic/UI qualification.

### Frication removal and independent seed editing

Select any numeric row of a frication source, then use Cmd/Ctrl+Shift+E to remove that source or Cmd/Ctrl+Shift+R to edit its exact unsigned 64-bit seed. Matching semantic actions carry the global source index in their context-bound IDs, so changing list selection cannot silently retarget an already captured action. The seed dialog explicitly distinguishes the selected source from the global voice seed. No implicit source renaming, deletion cascade or global-seed change occurs.

Both operations are epoch/revision-guarded, validated recipe edits with undo and preview invalidation. Shared canonical integer parsing rejects malformed/overflowed source seeds without precision loss. Regression coverage verifies UINT64_MAX for one of two sources, preservation of the global seed/other source, invalid/stale/index rejection, removal and exact two-step undo restoration. All twenty Designer tests and both Studio builds pass in Debug/Release. The new source actions/dialog remain live-unverified; dedicated frication audition and acoustic qualification remain open.

### Frication-authoring follow-up

Cmd/Ctrl+E and the semantic Add frication action open an AppKit phone/style dialog. A new source requires a distinct frication identity and an existing oral-pose style. It starts with the engine's bounded noise defaults (5,000 Hz center, 3,000 Hz bandwidth, gain 0.15), copying the current recipe seed once. It is an undoable draft edit; invalid/duplicate/style-missing requests preserve the recipe. Success selects an oral pose in that style so its source controls are visible.

Frication center frequency, bandwidth and gain rows follow the oral-formant rows for the selected style. Keyboard/drag steps are 10 Hz, 10 Hz and 0.005 gain, with Shift fine scaling. Accessibility fields set exact numeric values through the same recipe-validation path, including filter-ratio constraints. These rows participate in the bounded six-row viewport and invalidate current preview state on accepted edits. Independent source seeds remain stored unchanged after later global-seed edits.

Tests cover style/duplicate rejection, seed initialization, v1/v2 identity changes, filter editing, changed frication-source PCM, invalid-gain rejection and undo restoration. All nineteen Designer tests and both Studio builds pass in Debug/Release. New dialog/control interaction is live-unverified. Sustained oral audition does not include frication; use generated phrase output for consonant inspection until a dedicated preview mode is added. Source removal, dedicated source-seed editing and full consonant/intelligibility qualification remain unfinished. Defaults and labels are not approval or a claim of phonetically correct consonants.

### Exact seed editing

Cmd/Ctrl+R opens an AppKit seed dialog, and the seed is also exposed as an editable semantic text field. Session parsing accepts canonical decimal integers from 0 through 18446744073709551615 without a floating-point conversion. Empty, signed, fractional, leading-zero, whitespace and overflowing inputs reject without changing recipe/history/audio. Epoch/revision checks apply to both dialog and accessibility results. The seed controls the existing phonation noise and periodic-modulation phase; separate frication-source seeds are not implicitly changed.

Accepted seed changes are ordinary undoable recipe edits and invalidate current audition. Unchanged seeds preserve revision and ready audio; pinned references and existing generation packages remain frozen. Tests cover the maximum value, malformed inputs, stale epoch, changed PCM and exact original PCM after undo. New seed-dialog/header interaction remains live-unverified; non-AppKit seed dialogs are explicitly unsupported.

### Modulation-control follow-up

The control list now exposes periodic pitch depth (0–100 cents), periodic amplitude depth (0–1) and shared modulation rate (0–20 Hz, zero off). These map to the recipe's existing `jitterCents`, `shimmerAmount` and `rateHz` fields, but the UI names reflect the renderer's sinusoidal behavior rather than claiming measured/random jitter or shimmer. Normal steps are 0.5 cents, 0.01 amplitude and 0.1 Hz; Shift uses one-tenth steps. Keyboard, drag and accessibility numeric edits all route through recipe validation/undo and invalidate preview audio. The formant rows follow these controls in the same six-row viewport.

The added regression verifies separate pitch/amplitude modulation changes PCM at nonzero rate, undo restores exact original audio, nonzero depths with zero rate remain sample-identical to the unmodulated fixture, and invalid rate preserves the last valid revision/audio. New control interaction and acoustic character remain live-unverified; these are design parameters, not performance-lane authoring or singer-quality acceptance.

### Pose-management follow-up

Cmd/Ctrl+L duplicates the selected pose through an AppKit phone/style naming dialog. The new identity must be distinct and valid under the recipe contract. Its resonances and nasal-coupling value initially copy the selected pose; it must be retuned/auditioned rather than treated as an acoustically correct new vowel merely because its label changed. A successful duplicate selects the new pose, creates one undo item and invalidates preview audio.

Cmd/Ctrl+Shift+L removes the selected pose as an undoable recipe edit. The last pose cannot be removed. Common recipe validation also prevents deleting the last pose for a style still referenced by frications; there is no implicit cascade deleting those sources. Epoch/revision checks reject obsolete dialog results and mutations. Unsupported platforms report pose naming unavailable rather than silently selecting an identity.

The new regression covers duplicate-identity and last-pose rejection, copied resonance values, new-pose selection/rendering, removal and undo/redo, stale actions and preservation of dependent styles. All fifteen Designer tests pass in Debug/Release (1.94/0.88 seconds); both Studio builds and diff checks pass. Actual naming-dialog interaction and the newly adjusted header layout remain live-unverified.

### Mouse gesture follow-up

Numeric phonation/resonance rows now support horizontal drag editing. The starting recipe, pose, control, session epoch and viewport position are captured at pointer-down. Eight logical pixels correspond to one normal keyboard step; Shift applies fine scaling. The keyboard and drag paths use one parameter-adjustment function. Pose/pitch rows are selectable by click but remain keyboard-adjusted.

Pointer moves submit validated draft previews through session gesture operations; invalid positions/values cannot overwrite the last valid state. Release commits one undo item, while Escape restores the baseline. Other key actions first cancel an outstanding drag before proceeding. The viewport stays fixed during dragging, avoiding moving the control underneath the pointer. File save/replacement and close cannot interrupt an active gesture. Session preview results are invalidated across begin/update/end; pinning an A/B reference is not silently modified.

A new regression covers session-level multi-update commit as one undo, file/replacement guards, stale-epoch completion rejection, cancel/redo preservation and pending audition suppression. Actual mouse-drag interaction and continuous audible playback remain unverified; there is no automatic re-render during dragging yet.

### Resonance-control follow-up

The Designer control list now includes frequency, bandwidth and gain for every formant in the currently selected audition pose/style. Up/Down/Tab navigates the complete list; Shift+Tab reverses. Left/Right adjusts frequency by 10 Hz, bandwidth by 5 Hz or gain by 0.5 dB; Shift uses one-tenth steps. Existing recipe validation rejects out-of-range or crossed resonance frequencies without replacing the last valid state. Accepted edits go through the session's revision-guarded edit/undo path and invalidate old audition audio.

A six-row viewport follows keyboard selection and displays its index/count and pose identity. This supports all 3–8 formants without adding overlapping rows; the updated viewport has not yet received live visual QA. A new regression changes each resonance parameter, confirms changed preview PCM, undoes and requires exact original PCM restoration. Invalid frequency crossing preserves revision and ready audio. All twelve Designer tests pass in Debug/Release (1.53/0.75 seconds), both Studio builds pass and diff checks pass. Mouse sliders, pose creation/removal and full accessibility semantics remain unfinished.

Follow-up implementation: New, Open and Close now use a shared discard confirmation with Cancel as the default. It describes loss of unsaved draft changes only, not deletion of saved recipes or producer takes. Context is checked after the modal decision, and nested confirmation is suppressed. Open chooses its file before confirmation and retains the previous draft if loading fails. AppKit and Win32 adapters implement the choice; Win32 remains runtime-unverified. An additional session regression verifies failed loads and invalid replacements preserve edited content/epoch even after discard authorization, while a valid replacement resets history and advances epoch. All eight Designer tests and Studio builds pass in Debug/Release (0.43/0.38 seconds); diff checks pass. The new confirmation dialogs have not yet been exercised live. The earlier live run below predates this follow-up and verified the previous save-before-close guard.

A rebuilt Release development bundle was exercised through actual AppKit keyboard/save interactions at 1000×700, with Korean input still enabled and forced synthetic recording input:

1. Cmd+D opened Designer; Cmd+N created the draft.
2. Right changed open quotient from 0.60 to 0.61. Undo restored 0.60; redo restored 0.61.
3. Native Save As wrote `designer-live.json`; the view changed to SAVED.
4. Another Right changed the value to 0.62. Clicking window close retained the app and displayed the unsaved-draft warning.
5. Cmd+S saved the current path; SAVED returned. Closing then exited 0.

The saved file was read and contained `openQuotient: 0.62`. Temporary fixture path:
`/var/folders/j4/41h_5mjj7j9d8f2j2bzcsngw0000gn/T/project-seam-articulated-export-76491-0/native-live-prepared.seamjobdir/designer-live.json`.

Final diagnostics retained producer generation 3, one MarkerReview take, zero approved takes, physical input false and zero input callbacks/frames/recorded frames. Screenshots were inspected during edit, undo, save and rejected close. Native capture: `/private/tmp/seam-studio-job-qa.gvOic0/designer-live.ppm` (temporary). Post-verification pointer-up/scroll isolation guards were also added and rebuilt.

Both Studio configurations build; all seven focused Designer tests pass in Debug/Release (0.53/0.48 seconds); diff checks pass. This is focused interaction evidence, not a complete accessibility or responsive-design review. The current canvas does not yet expose semantic Designer controls to assistive technology. Full U22/Beta GO remains incomplete.
