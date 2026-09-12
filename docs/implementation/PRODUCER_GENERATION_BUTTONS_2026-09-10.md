# Visible producer generation actions

The inventory-only producer view now exposes Prepare, Run job, Make batch and
Run batch buttons in place of two tiny generation-shortcut lines. The existing
record/import instruction line is retained. Two columns fit inside the central
intake panel at Studio's supported 720-pixel minimum; action bounds end before
candidate diagnostics and markers begin.

Painting and pointer hit-testing share `studioGenerationControls`. Buttons are
disabled without a selected assignment, during producer work, or while recording
or uncommitted recorded frames exist. Clicking a disabled button does not fall
through into marker editing. Enabled actions call the existing dialog workflows,
which retain their context/digest checks and canonical generation operations.
No alternate generation engine, mutation path or approval bypass was added.

Tests cover four control bounds, column separation and vertical limits at widths
720, 1040 and 1600, plus recording and active-worker disabled states.
Full Release build and export/core suites passed 2/2 in 36.47 seconds.

Live follow-up at 720x520 verified all four labels fit within the central panel
and do not overlap the candidate markers in the retained test fixture. Clicking
each button opened its intended native chooser:

- Prepare: Choose Saved Procedural Score.
- Run job: Generate and Collect an Unapproved Candidate.
- Make batch: Select Prepared Job Folders or References (1–64).
- Run batch: Generate and Collect an Unapproved Batch.

All dialogs were cancelled without selecting inputs or publishing outputs.
The fixture's durable generation remained 2. One initial post-modal coordinate
click reached Make batch instead of the intended Run job because the tool's last
window context was the dialog; reobserving the main screenshot and retrying
verified Run job correctly. This is not evidence of an application misroute.
The check proves visual placement, initial dialog routing and return on cancel,
not successful end-to-end generation or cancellation of a running worker.

## Accessibility follow-up

The inventory producer now exposes all four generation buttons and operation
status through a custom semantic tree. Action IDs bind workspace epoch, durable
generation and selected row. Activation rejects stale identifiers, rechecks
current enabled state and blocks re-entry while a generation modal is open.
Mouse and accessibility activation call the same guarded action function.

Release build and export/core suites passed 2/2 in 24.02 seconds. Live rebuilt
Studio at 720x520 exposed Prepare, Run job, Make batch, Run batch and PRODUCTION
RECOVERED status. Accessibility activation of Prepare opened Choose Saved
Procedural Score; cancelling restored all four accessible controls. No score
was selected and no generation was started.

This qualifies the focused Prepare accessibility entry/cancel path, not every
generation action or full assistive-technology operation. Inventory selection,
recording, marker editing and other producer controls still need their own
semantic coverage. Existing shortcuts remain; the manifest-backed advanced
editor is unchanged. This is partial U22 work, not full Beta GO acceptance.

## Active-work cancellation

During producer work, the Run batch slot becomes Cancel work; the other three
generation actions remain disabled. Painting, pointer input and semantic controls
share this state. Activation uses the existing controller stop request, preserving
the worker's real terminal outcome and late-publication rules. Completion restores
the Run batch action. This is not rollback of already published material.

Regression checks assert that Cancel work is the only enabled generation control
during asynchronous score inspection, and that Run batch returns after polling
completion. Live cancellation of an active worker through the new control remains
unverified; earlier live dialog-cancel evidence does not qualify this behavior.
Release build and export/core tests passed 2/2 in 25.14 seconds.
