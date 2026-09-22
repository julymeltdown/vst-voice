# U17 native selection details

Date: 2026-09-22. Baseline: `9484adedebad3e5d51f94926ae13d7c9dc07021e`.

Initial implementation: `83dcaa1927365205a5ef92efe09e002cb5574c32`.
Developer 2 returned **REQUEST CHANGES** on this commit, then **APPROVED** the
repair at `afb655f2f38bfec1b9320a9cf3480b3030c97794`. The initial passing tests
did not establish CLAP interaction or complete modal isolation; all three
findings are now closed for this bounded increment.

## Delivered behavior

The shared native sample microscope now has a **Details / D** control. It shows
the complete captured unit identity and destination/selection description in a
read-only, UTF-8-aware, paged view. Long IDs, predecessor IDs and evidence hashes
are split across lines, not discarded or replaced by an ellipsis in this view.
The small header remains a summary; it is no longer the only visible way to
read the decision. The existing standalone/CLAP producers still supply the
actual selected occurrence's description; this increment does not recompute a
different selection or invent an explanation from catalog metadata.

The details explicitly say **Captured at open; reopen after edits or bank
refresh**. They are historical inspection data, not a new current-audio claim.
Opening the microscope again replaces the capture; navigation alone never
reloads it, alters a note, accepts a unit, modifies PCM or creates an undo entry.
Source-boundary proxy and Metadata-only labels are preserved verbatim.

The combined source unit/context payload is bounded to 64 KiB before composing
the explanatory prefix. Invalid UTF-8 or oversized input returns a clear error
without replacing an already-open valid capture. Wrapping is cached on open or
resize, not repeated for every rendered frame. Pagination retains every source
byte, including line endings, and reflow chooses the page containing the former
first-visible byte. Only line-ending bytes are omitted when painting a row.

## Interaction and layout

- Details / D toggles between read-only text and the original waveform view.
- Previous/Next buttons and Left/Right arrows navigate pages; end controls are
  disabled and out-of-range actions are rejected.
- Tab/Shift-Tab stays inside the modal semantic tree. Enter activates a focused
  control. Escape returns from details to waveform, then closes the microscope.
- The visible Close control now closes on one click, before the historical
  double-click playback behavior can intercept it. Right-click close is retained.
- Details hide waveform playback/edit targets from accessibility and pointer
  routing. Background accessibility commands, editing keys and timeline scroll
  are not routed through an open microscope.
- Opening inspection during a text edit, drag or another conflicting modal is
  refused. Its initial keyboard focus is the Details control; stale background
  or closed-microscope focus rectangles are not painted through it.
- Panel/plot geometry adapts to 480x320 through desktop sizes. Neither plot has
  a minimum dimension larger than its available area. Header controls, details,
  page controls and plots use the same geometry for painting and input/semantics.
- Modal painting now occurs after the background Time Map control. Visual QA
  exposed the previous paint-order overlap; a pixel regression retains it.
- Body text uses 12 px fonts in 22 px rows. Actual system-font rendering exposed
  19 px bitmap height even for a Latin line; the original proposed 18 px row was
  corrected rather than weakening the glyph-fit assertion.

## Initial implementation verification

`tests/test_sample_microscope_details.cpp` is included in the native-enabled
monolithic suite and the new `seam_sample_microscope_tests` target. Four cases
cover compact/intermediate/desktop geometry, lossless traversal of all pages,
Unicode/long identities, source-proxy descriptions, current reading reflow,
system-font glyph fit, pointer/keyboard/semantic navigation, modal isolation,
capture retention/reopen, no project/undo mutation, exact/over-limit payloads,
invalid UTF-8 and empty destination text.

The focused Debug suite passes all four cases. Initial failures exposed a
fixture initialization error and a temporary-lifetime error in the new test;
these were corrected. Glyph height and the Time Map paint order were actual UI
defects and were fixed, with retained assertions. No synthesis defect is claimed.

The fresh warnings-as-errors Release build and eight selected CTest targets
pass: 838 monolithic, four microscope, ten style-coverage, two vibrato-inspector,
one measured-dynamics, five standalone, 21 coordinator and four original-singer
journey cases (885 case executions, 27.54 seconds). These are suite executions,
not deduplicated coverage. The broader Debug run subsequently passed six targets
and 989 case executions (967 monolithic) in 180.92 seconds. Independent review
nevertheless found the three missing paths below. Logs are `u17-details-{configure,build}.log`
and `u17-details-final-ctest.log` under `build-u4-macos/`, and
`u17-details-{configure,build,final-build,final-ctest}.log` under `build/debug/`.
Source closure and diff whitespace checks pass.

Rendered QA fixtures are generated with
`SEAM_MICROSCOPE_DETAILS_CAPTURE_DIR`. Current local captures are under
`build/debug/u17-details-captures/`: compact waveform, compact details and
desktop details, with PPM originals and PNG previews. They deliberately use
long adversarial IDs. These are native scene rasterizations with the actual
system text engine, not screenshots of an installed AppKit/DAW session.

## Independent review and connected repair

Developer 2 found three P2 defects in the initial commit:

1. CLAP's separate microscope state supplied the shared painter but did not
   implement its new Details/Close controls or modal keyboard/AX routing. The
   initial tests instantiated only the native controller, not `EditorRuntime`.
2. The accessibility root was modal, but virtual note enumeration and focus
   traversal still exposed background notes. A retained regression reproduced
   this against the initial commit: three cases passed and one failed on the
   requirement that an open microscope have zero virtual notes.
3. `setAccessibilityValue` bypassed the modal command guard. Its valid note path
   could select and edit a background lyric; retained tempo/meter IDs were also
   unguarded. This finding was source-traced, not an observed end-user mutation.

The repair removes CLAP's duplicate microscope model, audio, identity and focus
state. Its loader supplies the actual rendered selection to the same
`NativeEditorController` used by standalone; scene state, pointer/key handling,
details pages and accessibility now use that controller. Active CLAP technical
gestures refuse inspection, and open inspection intercepts CLAP's S/R unit
shortcuts before they can change the score. The loader does not create new
selection or listening evidence. Existing callback capability checks keep
unsupported sample editing/playback unavailable.

`AccessibilityTree` now suppresses both materialized and virtual background
notes while inspection is open. `setAccessibilityValue` rejects at entry,
before parsing, selection, composition or project edits. Regressions cover
forward/reverse focus cycles, actual/stale note IDs, valid tempo/meter values,
unchanged score/selection/undo state and restored note enumeration after close.

The new `tests/test_clap_microscope_details.cpp` uses the real `EditorRuntime`
and checked-in development voicebank, waits for current rendered PCM and checks
the selected occurrence's actual rationale. Two cases cover pointer/key/AX
controls, lossless details pagination, resize, single-click Close, retained
SetValue rejection, background shortcut isolation and Escape transitions. They
run in a focused target and the CLAP-enabled monolithic suite. This is connected
runtime evidence, not an installed DAW session or a qualified singer resource.

The first repair build failed because the new test included the pronunciation
resolver rather than `language_resolver.hpp`, which declares the inspection
entry point. The include is corrected. All four focused Debug targets pass in
11.25 seconds: four native microscope cases, two connected CLAP cases, and the
existing Phase 11 and Phase 12B integration executables.

The repaired-tree broad runs also pass:

- Release, warnings-as-errors: eight targets / 885 case executions, including
  838 monolithic cases, in 21.04 seconds.
- Debug: nine targets in 164.99 seconds; seven test-framework suites account
  for 993 case executions (969 monolithic), plus the two separate Phase 11/12B
  integration executables. These counts overlap across suites and are not
  deduplicated coverage.

Release currently has CLAP disabled; the connected CLAP coverage is Debug only,
and Debug warnings-as-errors is disabled. Source closure and staged whitespace
checks pass. No failed build or initial red regression is counted as passing.

Repair logs: `build/debug/u17-details-repair-{build,focused-ctest}.log`;
the red native regression is retained in
`build/debug/u17-details-focus-red-{build,ctest}.log`.
Broad repair logs are `build/debug/u17-details-repair-broad-{build,ctest}.log`
and `build-u4-macos/u17-details-repair-{configure,build,ctest}.log`.

## Independent repair approval

Developer 2 APPROVED `afb655f2f38bfec1b9320a9cf3480b3030c97794`, closing all
three original P2 findings with no additional blocking regression established
in this bounded repair. The reviewer checked callback wiring, recursive-lock
safety, controller ownership after project replacement, read-only CLAP
capabilities, modal virtual-note isolation and the early SetValue rejection.

Independent existing-binary reruns passed 20 case executions: Debug CLAP
microscope 2/2 and native microscope 4/4, Release native microscope 4/4 and style
coverage 10/10. The reviewer verified a clean worktree at the exact hash and
`git diff --check`. They did not rebuild or rerun the producer's broad,
Phase 11/12B, monolithic or source-closure checks. CLAP runtime evidence remains
Debug only. This approval does not establish installed AppKit/DAW/Windows,
live assistive-technology operation, perceptual quality or a full U17/Beta gate.

## Scope retained

No synthesis algorithm, cache identity, project schema or singer resource changes
are made. This closes the truncated-rationale presentation gap only after its
review passes. Scored counterfactual alternatives, natural-voice join/blend
qualification, the full resource matrix, neural singer acceptance, actual
installed-host journeys and the complete U17/Beta contract remain open. Windows
continues as the documented TODO; portable source compilation is not Windows
runtime evidence. No extra complete roadmap unit is claimed.
