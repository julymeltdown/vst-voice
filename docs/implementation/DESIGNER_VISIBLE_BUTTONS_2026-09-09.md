# Visible Designer action buttons

Compact source actions have since been implemented and pointer-tested at 720x520;
see `DESIGNER_COMPACT_SOURCE_ACTIONS_2026-09-09.md`. Statements below about hidden
compact source bars describe the initial implementation, not the current layout.

Follow-up: the pointer Render path was subsequently exercised successfully in
the rebuilt app after repairing a stale AppKit accessibility snapshot. See
`SESSION_CONTINUITY_AND_AX_COMPLETION_2026-09-09.md`. The historical attempt below
remains accurate for that earlier run; other button paths remain unqualified.

Designer action bars now paint real button backgrounds and concise labels from
the existing semantic tree. Header commands, audition/A-B controls, generation
preparation and source actions no longer rely only on text listing shortcuts.
Source preview modes are labeled Src, CV and VC while retaining full descriptive
accessibility names. Disabled actions have distinct muted styling.

Pointer dispatch uses the same enabled semantic nodes and rectangles as painting.
The target ID is copied before dispatch, because the action may rebuild the tree.
Existing epoch/revision validation remains authoritative; there is no separate
mouse-only mutation route. Numeric control handling continues after button
hit-testing. Existing compact-layout hidden rows remain hidden: the source action
bar still requires the expanded layout, with shortcuts/accessibility retained in
compact mode. Compact source-action presentation remains further UI work.

## Verification boundary

Full Release build and Designer/core suites passed in 21.81 seconds. These tests
do not qualify the new painted-button mouse path.

The live verification attempt could not reach a newly created draft. The UI tool
first returned elementHasNoFrame, then an empty accessibility tree and
noWindowsAvailable, while the launched process handle remained live. The same
process was inspected without restarting it. A one-second process sample showed
the main thread in normal AppKit event waiting for all 90 samples, with no busy
loop or renderer blockage observed. This does not establish the cause of the
automation failure or prove general responsiveness.

No workaround input technology or permission expansion was used. The test process
was stopped after diagnostic collection. The new button layout/click behavior
remains live-unverified and must not be reported as a visual PASS. Build/test logs
and the diagnostic sample are retained under `evidence/designer-buttons-2026-09-09/`.

No files were missing relative to `session-preservation-Zu2ybU`. Existing dirty
work was preserved without staging, commit or push. This is not whole-unit or
full Beta GO acceptance; the complete objective remains active.
