# Designer semantic accessibility

## Implemented surface

The Designer now supplies a bounded custom `AccessibilityTree` to the existing native bridge. Custom trees do not require a piano-roll model or fabricate virtual notes. The visible six numeric controls are exposed as editable text fields with names, values, descriptions and focus actions. New/Open/Save/Back and previous/next control-page actions are exposed as buttons; save state and current errors are exposed as status nodes. Buttons are disabled when unavailable, and their bounds fit the current width.

Field/action identities include voice epoch, recipe revision, selected pose and selected MIDI pitch. Dispatch rejects a stale identity, hidden Designer state or busy/dragging context. Numeric input is length-bounded, completely parsed and finite; pose/pitch require integral values and existing selection bounds. Recipe fields use the same validated session edit path as keyboard/mouse controls, preserving undo and invalidating obsolete preview audio. Accessible focus can reveal subsequent controls through page actions. Keyboard actions restore normal control focus behavior.

This implementation uses numeric text fields, not platform sliders. It does not yet provide complete semantic actions for audition/A-B, pose duplication/removal, Save As or every status transition. Full VoiceOver/Narrator keyboard traversal, embedded hosts, narrow-window behavior and accessibility conformance remain open.

## Verification

### Extended actions and independent cancellation

The semantic surface now also exposes Render, Play current B, Pin reference A, Play reference A, Clear reference, Stop playback, Cancel render, Save As, Duplicate/Remove pose and Undo/Redo. Availability follows session state, ready/reference audio, history and pose count. Dispatch maps explicit known actions only; unavailable current/reference audio is rechecked before playback. Audition state and full reference identity are exposed as status nodes.

Transport-only Stop/Cancel-render actions remain available independently of a simultaneous file operation. `cancelAudition()` affects only the preview stop source; shutdown/general cancellation retains its existing all-work behavior. A regression starts preview rendering and a recipe save together, cancels preview and verifies the save remains successful with exact persisted recipe identity. Keyboard Escape also cancels preview independently when one is pending.

All sixteen focused Designer tests and both Studio builds pass in Debug/Release; the new actions themselves have not yet been exercised live. This extends the earlier field/navigation check, not a complete assistive-technology conformance claim.

The custom-tree regression verifies numeric edit actions, focus traversal, replacement clearing, invalid-target rejection and absence of virtual notes. The rebuilt Release core/native suite passed **506 tests, 0 failures** (11.51 seconds).

A current Release Studio was exercised through native accessibility at 1000×700:

1. Designer exposed New/Open/Save/Back and control-page buttons.
2. Activating semantic Open opened the native picker; selecting the saved synthetic recipe exposed six numeric fields and SAVED state.
3. Setting open quotient from 0.62 to 0.65 through the accessibility value API changed the draft and advanced its target IDs from revision 0 to 1.
4. Setting 2 was rejected; the valid 0.65 remained, and an error status reported recipe bounds failure.
5. Activating Next controls exposed later fields, including F1 bandwidth/gain and F2 frequency.
6. Keyboard undo restored SAVED and advanced revision to 2. Clean close exited 0; producer generation stayed 3, approved count stayed 0 and no physical input or recorded frames occurred.

Temporary input recipe: `/var/folders/j4/41h_5mjj7j9d8f2j2bzcsngw0000gn/T/project-seam-articulated-export-76491-0/native-live-prepared.seamjobdir/designer-live.json`. Capture: `/private/tmp/seam-studio-job-qa.gvOic0/designer-accessibility.ppm`. Intermediate AX trees are recorded in this task's tool results. Post-check button availability/bounds refinements were rebuilt but not separately exercised live.

The test draft was restored without saving the temporary edit. No auditory screen-reader experience or acoustic result was evaluated. Full U22/Beta GO remains incomplete.
