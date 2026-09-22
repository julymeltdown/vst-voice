# U17 native selection details

Date: 2026-09-22. Baseline: `9484adedebad3e5d51f94926ae13d7c9dc07021e`.

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

## Verification

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
not deduplicated coverage. The focused Debug suites pass; the broad Debug result
and independent review are pending. Logs are `u17-details-{configure,build}.log`
and `u17-details-final-ctest.log` under `build-u4-macos/`, and
`u17-details-{configure,build,final-build,final-ctest}.log` under `build/debug/`.
Source closure and diff whitespace checks pass.

Rendered QA fixtures are generated with
`SEAM_MICROSCOPE_DETAILS_CAPTURE_DIR`. Current local captures are under
`build/debug/u17-details-captures/`: compact waveform, compact details and
desktop details, with PPM originals and PNG previews. They deliberately use
long adversarial IDs. These are native scene rasterizations with the actual
system text engine, not screenshots of an installed AppKit/DAW session.

## Scope retained

No synthesis algorithm, cache identity, project schema or singer resource changes
are made. This closes the truncated-rationale presentation gap only after its
review passes. Scored counterfactual alternatives, natural-voice join/blend
qualification, the full resource matrix, neural singer acceptance, actual
installed-host journeys and the complete U17/Beta contract remain open. Windows
continues as the documented TODO; portable source compilation is not Windows
runtime evidence. No extra complete roadmap unit is claimed.
