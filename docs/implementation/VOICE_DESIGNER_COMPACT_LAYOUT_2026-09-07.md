# Designer compact layout

The Designer now shares a height-dependent layout calculation between painting, control hit testing and accessibility. It uses 1–6 visible rows, keeps the selected control inside the viewport, and places summary/status/help below those rows. Under 600 logical pixels, secondary footer details collapse into a shorter command hint; keyboard and semantic actions remain available. Collapsed semantic commands have empty visual bounds rather than pretending to occupy offscreen controls. In compact mode, an error replaces normal status text instead of drawing over it.

The layout helper supports heights from 320 upward; this does **not** lower Studio's enforced 720×520 minimum. A 720×320 launch was rejected as expected (exit 4). No window-contract relaxation was made.

Resize cancels an active Designer drag through its existing rollback operation before geometry changes. Page navigation uses the current visible-row count, and pointer hit testing uses the same top/row-height/count values as rendering.

## Verification

- Property-style checks cover heights 320, 340, 400, 520, 599, 600, 700 and 900 with up to 224 controls, requiring in-window status/error/help and visible selection. Non-finite height falls back to bounded geometry.
- All 22 focused Designer tests pass in Debug/Release (2.71/1.10 seconds), and both Studio builds pass.
- A live Release run at the supported minimum **720×520** opened the saved synthetic recipe. The six-row list, summary and compact hint fit visibly. Setting open quotient to invalid value 2 exposed its validation error within the window without changing the saved 0.62 value or SAVED state.
- Clean close exited 0 with the intentional validation error recorded, producer generation 3, approved 0 and zero physical-input/recording activity. Temporary native capture: `/private/tmp/seam-studio-job-qa.gvOic0/designer-compact.ppm`.
- The compact shortcut hint was subsequently clarified to spell out Cmd/Ctrl and rebuilt; that wording-only refinement was not separately captured.

Long-text variants, arbitrary scaling, live resize-during-drag and full screen-reader traversal remain unverified. This is focused minimum-size evidence, not complete design/accessibility acceptance. Full U22/Beta GO remains incomplete.
