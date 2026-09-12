# Designer readability and viewport density

Removed the fixed six-row ceiling. Visible controls now use available window
height while reserving the existing compact/full footers. Rows are 32 logical
pixels high; body text is 15 rather than 11, and the heading is 20. The selected
row has a full-width contrast background. Painting, pointer lookup and semantic
bounds use the same shared row geometry instead of duplicated 27-pixel offsets.

## Evidence

The geometry test retains footer and selection-in-view assertions across heights
320–900 and varied parameter counts. New assertions require eight rows at 520,
sixteen at 900, and additional rows at 1200 rather than another fixed ceiling.

Live macOS Studio verification used the bundled application and an isolated
synthetic-bank draft. The restored tall window showed sixteen rows; manually
resizing to the 720-by-520 client minimum showed eight rows with readable labels
and no overlap in the inspected header/control/footer content. Clicking the last
visible modulation row and pressing Right changed exactly that value from 0 to
0.1, verifying the new pointer-to-row mapping. Screenshots were inspected during
the session. The temporary in-memory QA draft was not saved; its test process
was stopped. No user recipe was replaced and no microphone recording was started.

The first launch restored the previous saved window dimensions despite explicit
initial-size arguments; the minimum-size check therefore used actual window
resizing, not an assumption about those arguments.

This is a focused readability/geometry repair, not a complete visual redesign.
Horizontal composition, real labeled visual action controls, all content-length
cases, Windows/Linux appearance and the full Beta GO acceptance remain open.
No whole roadmap unit is accepted by this change.

Full Release build and both focused CTest suites (Designer and aggregate core)
passed in 22.22 seconds. Logs are retained in `evidence/designer-density-2026-09-09/`.
This was not a new full-suite run. Existing dirty work was preserved without
staging, committing or pushing.
