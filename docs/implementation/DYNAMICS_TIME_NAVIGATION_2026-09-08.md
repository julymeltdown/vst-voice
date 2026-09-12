# Dynamics plot time navigation

U25 now has local dynamics-plot zoom, pan and Fit. This extends the dynamics model and generated/target inspection work without changing the score or main piano-roll viewport.

## Behavior

- Zoom +, Zoom − and Fit region are visible buttons with matching accessibility actions. The point list now uses two rows per page so navigation fits at 480×320.
- Plus/minus zoom around the visible interval midpoint; left/right pan by one-quarter of the visible span (at least one tick); R fits the full region/curve extent.
- Wheel gestures over the plot pan. Command/Control plus wheel zooms at the pointer. Small deltas accumulate to a 20-unit threshold; navigation remains discrete rather than a smooth pinch gesture.
- `DynamicsPlotViewport` keeps a nonnegative integer tick interval with a minimum span of one tick. It handles the maximum signed 64-bit extent without overflowing integer additions/doublings or converting a rounded out-of-range floating value to an integer. Nonfinite zoom anchors are ignored.
- Native curves are clipped at the visible interval, using the exact domain interpolation value at both boundaries. Out-of-view handles/candidates and generated/target markers are not drawn. The full-region point list remains available for exact numeric edits even when a point is outside the plot viewport.
- Navigation cancels active gain dragging and invalidates old modal accessibility actions. It does not emit a score command, create undo history, change the main piano-roll origin/scale or modify serialized musical intent. Source replacement invalidates further navigation until Refresh.

## Important boundary

Generated and target markers now resample the visible interval using cached compiled voices (see the adaptive-sampling checkpoint below). They remain sampled values rather than continuous coverage. Shift-drag horizontal point editing is implemented in the checkpoint below; voice isolation, measured audio and live platform/host gesture qualification remain open.

## Verification

Two new cases cover integer-limit navigation and the native pointer/keyboard/wheel/accessibility workflow, exact clipped endpoints, hidden out-of-view handles, stale callbacks/document replacement, and unchanged project/history/main timeline. Existing paging tests were updated for two point rows per page.

- Release native app/core and Release/Debug focused builds pass.
- 620 Release core cases pass (14.19 s).
- Eleven focused dynamics cases pass Release/Debug (1.09/3.21 s).
- `git diff --check` passes.
- `/tmp/seam-dynamics-zoom.4TaFrh/zoom.png` was inspected at 480×320: controls, tick-range labels and the clipped curve fit without overlap.

Changes remain local/uncommitted. U25 and Beta GO are not accepted by this checkpoint.

## Viewport-adaptive sampling checkpoint

`DynamicsLaneModel::refreshTargetPreview` accepts an optional inclusive display window (`TargetWindow::first/last`). It validates nonnegative ordered endpoints, clips evaluation to the captured region duration, and produces an empty ready result for a valid window entirely beyond the region. Invalid windows clear old samples and report an error; a subsequent valid request recovers without mutating the draft.

Compiled per-voice score evaluators are cached within the captured draft model. Zoom/pan/Fit resamples those evaluators instead of copying and recompiling the project. Successful native-curve mutations clear both the samples and compiled cache; staging an edited point rebuilds against the new curve and current visible range. Source replacement still requires the existing guarded Refresh path. Neither sampling nor navigation runs in the painter or gain-drag handler.

The uniform grid now spans the visible interval (up to 257 samples per voice), supplemented by source-note anchors inside the window. The grid includes both display endpoints; this does not change the compiler's half-open musical ownership scopes. At narrow spans, every integer tick is inspected. Values are still a score-only 48 kHz sample overview—not continuous gain, final audio amplitude or measured output. Sub-tick detail, metadata-heavy worst-case latency and live host qualification remain open.

Expanded regressions verify denser native output after zoom, the exact 1438–1442 tick window across a manual replacement boundary, selected generated values remaining independent of the changed target, invalid-window recovery, and a window ending at the maximum signed 64-bit tick beyond the region. The 4096-note/16-voice capacity case also measures cached resampling over ticks 500–550: 368 retained samples, 0.193 ms Release / 2.424 ms Debug in the focused run (0.180 ms in the Release core run). These are local simple-fixture measurements, not portable performance guarantees.

Release native app/core and Release/Debug focused builds pass. All 620 Release core cases pass (13.76 s); eleven focused dynamics cases pass Release/Debug (0.90/2.97 s), with existing cases expanded rather than new case-count credit. `git diff --check` passes. Changes remain local/uncommitted; U25/Beta GO remain incomplete.

## Horizontal point dragging and early region validation

Shift held at pointer-down selects horizontal time editing; ordinary dragging remains vertical gain editing. The axis is locked for the whole gesture, so releasing or pressing Shift midway cannot unexpectedly alter the other field. Horizontal mapping uses the captured visible tick interval, rounds to integer ticks (not the note-grid quantization), and clamps the cursor to the visible endpoints. It changes only the unsaved point's tick text and preserves gain. Numeric tick fields remain the exact keyboard/accessibility alternative.

Save to draft uses the existing atomic move operation: occupied destinations reject without merging or overwriting a point. Apply region curve remains the only score publication step. Navigation, geometry change, stale document state, cancellation and lost-button handling continue to retire an active gesture. Nonfinite coordinates reject before modifying the field. The point form and handle descriptions explain Shift-drag time editing.

An extreme-endpoint test exposed a pre-existing layer distinction: `DynamicsAutomationPoint` accepts a nonnegative 64-bit tick, but `VocalRegion::validate` rejects a curve extending beyond the region. The original synthetic project at `INT64_MAX` was therefore correctly inadmissible. Arithmetic extremes are now checked independently through `DynamicsPlotViewport::tickAtFraction`; actual model and numeric-field tests check region-duration bounds. `DynamicsLaneModel::validatePoint` now enforces the project rule at draft upsert/move and field validation, preventing a staged curve that could only fail later at Apply. No project validation rule was relaxed.

Three added regressions cover axis locking, gain preservation, collision recovery, exact whole-project Apply/undo/redo, zoomed endpoint clamping, nonfinite input, maximum-integer conversion, rejected out-of-region drafts and disabled point saving. Release native app/core and Release/Debug focused builds pass. All 623 Release core cases pass (13.81 s); fourteen focused cases pass Release/Debug (0.80/2.91 s); `git diff --check` passes. Live OS/host gesture behavior, measured audio and remaining U25 requirements are still unqualified. Changes remain local/uncommitted; no Beta GO acceptance is claimed.
