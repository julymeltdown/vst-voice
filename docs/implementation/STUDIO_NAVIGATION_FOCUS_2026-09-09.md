# Restore keyboard focus after leaving a custom Studio surface

The workspace handoff removes Designer's virtual accessibility controls and
returns to the producer canvas. AppKit could retain focus associated with the
removed surface, so Command-D did not work until the canvas was clicked.

The shared AppKit window bridge now records whether a custom accessibility
surface existed before dispatch. After successful accessibility activation,
pointer-down dispatch or ordinary key dispatch, it returns first responder to
the real canvas only if the custom surface disappeared. It also requires a
visible owner window, no active modal and no active text-input session. Ordinary
actions that retain their surface or intentionally open text editing do not use
this focus reset. The general file-dialog helper was not broadened.

## Verification

Full Release build passed. Designer, aggregate-core and Studio manifest-draft
CTest suites passed in 26.11 seconds. This is not a new full-suite run.

Live macOS verification reproduced the workspace-opening sequence using the
existing engineering workspace, expected inventory digest and registered
producer. Immediately after its modal completed, Command-D returned to Designer
without any intervening canvas click. A repeated accessibility Back-to-producer
and Command-D cycle also passed. A second app launch exercised the visible
workspace button by coordinates; after completing its dialogs, immediate
Command-D again worked without clicking the canvas.

Both sessions closed normally. Their process results reported unopened input,
zero capture/recording, unchanged producer generation 2, one MarkerReview take
and zero approvals. No producer mutation, recording or source approval occurred.

This resolves the focused navigation defect reported in
`DESIGNER_WORKSPACE_HANDOFF_2026-09-09.md`. It is not blanket qualification of all
modal, IME, text-editing or cross-platform focus behavior. Full Beta GO remains
open and no whole roadmap unit is accepted.

Logs are retained under `evidence/studio-navigation-focus-2026-09-09/`. No files
were missing relative to `session-preservation-0XhTg2`; existing dirty work was
preserved without staging, commit or push.
