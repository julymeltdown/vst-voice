# U25 dynamics lane editing model

Status: implemented and locally tested model, native point inspector and graphical gain handles; complete expression-lane and U25 acceptance remain open.

## Contract

### Capacity and latency audit (latest checkpoint)

The voice allocator's existing region admission limit is **4,096 notes**, lower than the native editor's 10,000-note limit. It is now exposed as `kMaximumScoreVoiceAllocationNotes` and used by the target-preview preflight, before copying a large captured project for compilation. No renderer admission limit was increased. The visible inspector summary now shows the actual target diagnostic, including the note limit, rather than a generic unavailable message. This identifies a remaining implementation boundary, not a reduction of the approved Beta GO scope.

The new capacity regression constructs 4,096 notes across 16 simultaneous voices. Its 12,432 retained samples represent every note and all voices, remain within the derived candidate-evaluation bound, and leave the project/history unchanged. With the actual allocator limit, the general candidate bound is 16×257 + 3×4,096 = 16,400 before deduplication/filtering; the earlier 34,112 estimate below used the larger editor admission limit and was conservative, not the compiler's supported capacity.

The same case then constructs a 10,000-note region: model and native inspector explicitly report the target limit without partial samples or silent truncation, while native point editing, Apply and exact undo remain operational. Supporting target evaluation above 4,096 notes requires further deliberate compiler/render-budget work; it is not qualified here.

Local timings from the final focused run: Release capture 0.419 ms / preview compilation 8.093 ms; Debug capture 2.447 ms / compilation 85.579 ms. The core run observed Release capture 0.480 ms / compilation 7.021 ms. These are direct intervals around model capture and target refresh, not whole-window frame times. The fixture has no generated takes or retained unit/seam edits, so it is a supported note/voice ceiling check, not metadata-heavy worst-case qualification or a portable timing guarantee. It does not establish a need for asynchronous refresh for this simple case; broader latency qualification remains open.

Verification: 618 Release core cases pass (13.81 s); nine focused dynamics cases pass Release/Debug (0.88/2.97 s); Release native app/core and Release/Debug focused builds and `git diff --check` pass. Changes remain local/uncommitted. U25 and Beta GO remain incomplete.

`DynamicsLaneModel` captures the active vocal region through the application's immutable performance-job context. It maintains a separate native dynamics curve until explicit Apply. Note selection is deliberately not its scope: the continuous curve belongs to the region, and selection changes alone do not invalidate it.

- Add/update, delete, move and reset operate only on the draft. Reset restores the captured curve; it is not Clear Region Dynamics.
- Moving onto an occupied tick rejects, except updating the source point in place. Invalid destinations, missing sources and capacity failures preserve the previous draft exactly.
- Domain rules remain authoritative: nonnegative region-local ticks, finite linear gain from silence through +12 dB, at most 16,384 unique ordered points. The vocal region additionally requires point ticks not to exceed its duration. Draft upsert/move and numeric point fields now enforce that region constraint before staging, rather than waiting for canonical Apply to reject the curve. Typed invalid times reject; they are not silently clipped to a note or region boundary.
- Sampling retains linear interpolation, constant endpoint extension and unity for an empty curve. Deleting an endpoint can therefore affect time outside the adjacent note. A future native surface must explain this region scope rather than imply note-local isolation.
- Apply validates active region, document revision, document generation and captured musical/resource inputs, then uses one canonical `EditPerformanceCommand` with only `RegionDynamicsEdit`. A no-op adds no history. Applied/cancelled drafts reject further editing and publication.
- This path does not claim manual ownership or remove accepted generated dynamics. The displayed native curve must not be presented as the final composite audible curve when generated performance participates.
- Capture accepts regions with at most 10,000 notes. Cancellation is cooperative at capture/publication boundaries; synchronous copying and validation are not interruptible worker jobs.

## Files and verification

- `libs/seam-editor-ui/include/seam/ui/dynamics_lane_model.hpp`
- `libs/seam-editor-ui/src/dynamics_lane_model.cpp`
- `tests/test_creator_batch_edits.cpp`
- CMake registers the model in `seam_editor_ui`.

Two new regression cases cover interpolation/endpoints; draft-only operation; invalid/colliding/missing-point rejection; selection-independent region scope; wrong-region rejection; exact whole-project Apply/undo/redo while preserving another region, pitch, vibrato, hint and ownership; capacity admission including a move at capacity; reset/no-op; same-content document replacement; and cancellation/closed-state rejection.

Verified on the current local dirty worktree:

- Release builds: `seam_creator_batch_tests`, `seam_tests`.
- Debug build: `seam_creator_batch_tests`.
- Release creator suite: 30 cases, 1.24 seconds.
- Debug creator suite: 30 cases, 6.84 seconds.
- Release core suite: 607 cases, 25.93 seconds; ran alongside Debug build/test work, not an isolated performance benchmark.
- `git diff --check` passes.

## Integration work identified at the model checkpoint

Connect the model to visible point handles and keyboard/semantic editing, including explicit Apply/Cancel and region-scope wording. Keep native, generated and measured dynamics distinguishable. Then verify this editing path through compiled performance, audible rendering, persistence, plugin state and supported host workflows. These tests do not replace the separate existing clear-region dynamics or vibrato audio evidence, nor qualify the new model's native UI, listening quality or release readiness.

Changes remain local and uncommitted. No additional roadmap unit or Beta GO gate is accepted by this checkpoint.

## Native point inspector integration

The native controller now exposes `openDynamicsInspector()`, using the same compact review/inspector surface rather than another window. Entry is available through macOS Edit → Edit Region Dynamics and the shared track-inspector Dynamics button. The existing track-inspector layout only appears with character display Off and enough dock space; this change does not resolve character/inspector coexistence or crowded-dock reachability. The macOS menu does not depend on that dock visibility.

The curve list shows six points per page. Opening a point exposes separate integer region-tick and linear-gain fields. Add creates a new unsaved point, Delete removes an existing point from the draft, Save to draft performs an atomic insert/move, Back discards the current point form, and Apply region curve publishes all saved draft changes as one document command. Cancel discards the whole draft. Refresh/reset explicitly recaptures the curve. A destination collision is reported without merging or replacing another point.

Native text composition captures document generation/revision and region before opening. Invalid numeric input is retained up to 64 bytes and disables point saving; longer input rejects without replacing the prior field. Editing/canceling a field returns to the point form. Stale document input returns to a non-applicable form with Back/Refresh/Cancel paths. Interaction IDs invalidate old point/list/button callbacks. Semantic field names specify tick and linear gain units; pointer, keyboard focus/Enter and accessibility actions share the controller. Score Delete is isolated while the draft form is open.

`tests/test_dynamics_lane_workflow.cpp` now runs three dedicated cases both in the core suite and in `seam_dynamics_lane_workflow_tests`. These cover native entry via semantics and pointer, field editing via pointer and keyboard activation, invalid and oversized values, occupied-tick rejection, deletion, exact Apply/undo/redo, no draft document notifications, cancellation, generation replacement, refresh, stale callbacks, final-page deletion and narrow semantic bounds. The standalone lifecycle test also verifies the new menu-command dispatcher callback. The first entry-point test run correctly rejected a hidden inspector with default character display; fixtures now explicitly select the existing Off layout, without changing production visibility rules.

The 480×320 native raster capture was inspected at `/tmp/seam-dynamics-inspector.sU9C87/inspector.png`: point rows and six actions fit within the compact right-anchored panel without overlap. This is a rendered/controller-tested UI, not live OS menu, IME, VoiceOver or installed-host qualification.

Release native app/core and Release/Debug focused builds pass. Before the final gain-label wording rebuild, all 610 Release core cases passed (23.40 s), the three dynamics workflow cases passed Release/Debug (1.22/0.70 s), and 33 lifecycle cases passed Release/Debug (0.30/1.22 s). Final rerun evidence is recorded in the execution ledger.

At the point-form checkpoint, remaining work included graphical active-point handles and lane visualization, score/target/generated/measured distinctions, native-curve versus generated-performance feedback, this connected editing path's audio/save/reload/plugin-state proof, and live input/host qualification. The following checkpoint advances the graphical work without claiming U25 or Beta GO acceptance.

## Graphical gain handles and source/draft visualization

The inspector now reserves a curve plot below three point rows per page. Applied score dynamics are drawn gray, the staged native curve pink and a valid unsaved point yellow. Score means the captured application score, not necessarily a document saved to disk. These are stages of the same native curve, not substitutes for neural/generated dynamics, measured audio or compiled target-performance lanes. The accessibility description explicitly says generated and measured curves are not shown.

The horizontal scale spans region-local tick zero through the maximum of region duration and source/draft/candidate point times. The vertical scale is native linear gain from silence to +12 dB, with a unity reference line. All bounded curve vertices are connected directly, including endpoint extension; narrow peaks are not dropped by time sampling. At most 16,384 points per curve are admitted by the domain. Worst-case interactive plotting latency remains unqualified. Zoom/pan and direct horizontal dragging are not implemented here.

Each visible point row has a corresponding graph handle. Pointer hit testing chooses the nearest handle within seven pixels, with stable order for exact ties; semantic handles expose the exact row values for dense or coincident pixel positions. Activating a handle through keyboard/accessibility opens the same exact tick/gain fields. Vertical pointer dragging changes only the unsaved point gain, clamped to the domain range, before Save to draft and Apply region curve. The previous applied and staged curves stay visible until their respective explicit commit steps.

Drag publication rejects stale document inputs and nonfinite vertical coordinates. Geometry changes during a drag cancel it; a pointer move with no left button held retires a lost gesture without changing gain. Model/controller tests cover exact field equivalence, dirty-notification isolation, endpoint clamping, nearest dense-point selection, stale document replacement, resize and lost-button handling. The embedded editor's prior modal guard discarded move/up events; it now forwards them to the review controller while retaining background technical-edit isolation. A dedicated core regression exercises embedded down/move/up and verifies only the final region curve is changed on Apply.

The 480×320 raster `/tmp/seam-dynamics-curve.kCDugV/curve.png` was inspected: point rows, legend, plot and six actions fit without overlap. Six focused workflow cases pass Release/Debug (0.40/0.50 s). The full Release core result and native build evidence are recorded in the latest execution-ledger checkpoint.

Remaining: time zoom/horizontal handles; actual target/generated/measured performance curves; capability and ownership feedback; audio, persistence, plugin-state and live OS/host qualification. The embedded regression is an in-process runtime test, not a live plugin-host sign-off. Changes remain local/uncommitted and U25/Beta GO remain incomplete.

## Native dynamics through Final audio and persistence

`tests/test_export_service.cpp` adds a native graphical drag → exact gain field → Save to draft → Apply region curve regression using a procedural Japanese vowel fixture with vibrato enabled. The singer recipe is saved as `singer.json` and resolved from each score's persisted typed recipe reference. This does not rely solely on separately injecting a frozen singer resource.

The regression establishes:

- Saving a point to the draft does not alter the project, emit a document-change notification or change Final PCM.
- Apply changes exactly the native curve and emits one notification/revision. Existing vibrato and the complete remaining project are preserved.
- At gain 0.25, output length stays unchanged, phrase content hashes change, every finite sample matches baseline ×0.25 within absolute tolerance 0.00001, and energy is 0.0625 of baseline within tolerance 0.0001. This is a constant-curve signal-path check, not a listening-quality score or qualification of every curve shape.
- Project save/reload reproduces the expected complete project and exact edited Final PCM.
- Committed Float32 master and stem WAVs both reproduce the edited PCM exactly.
- Undo/redo reproduce the original/edited project and PCM exactly.

The embedded dynamics regression in `tests/test_native_ui.cpp` also checks the actual editor state codec used by the CLAP state callbacks: staged drafts leave serialized project bytes unchanged; applied dynamics changes the state; decoding and constructing a fresh embedded runtime reproduce the exact applied project and visible gain. This proves the codec/runtime boundary, not invocation of host state streams or a live DAW restart. The state test uses an explicitly absent-bank fixture, so it does not establish rendered audio after a real plugin-host restore.

Release/Debug export builds pass, with all 22 export cases passing (5.01/23.50 s). The full Release core result, including the embedded codec/runtime checks, is recorded in the execution ledger. Live playback, listening acceptance, classical/neural renderer parity, multi-point acoustic qualification and the complete plugin/OS/host matrix remain open. No U25 or Beta GO acceptance is claimed; changes are local/uncommitted.

## Generated-performance influence warning

The captured dynamics model now counts accepted Dynamics selections and manual Dynamics Replace scopes once at preparation. The inspector displays these counts when generated dynamics is selected, alongside “Generated dynamics may override this curve.” Its accessible curve description explains the compiler's precedence: non-null generated values replace the native curve inside accepted scopes unless manual Dynamics Replace ownership applies. A native curve edit neither claims ownership nor deletes accepted selections. Counts represent stored scopes, not rendered coverage, affected-note percentages or measured audibility. Pitch ownership is excluded from the dynamics count. Articulation, track gain and renderer processing remain additional level influences.

The new native workflow regression checks this explanation against `compileScorePerformance`, not a UI-only approximation. One selected generated Dynamics lane supplies gain 0.8 over a note; a manual Dynamics Replace range covers its latter portion. Native gain starts at 1.0 and is edited through the inspector to 0.25. At ticks 1200 and 1680, the compiled post-edit gains are respectively 0.8 (generated still controls) and 0.25 (manual ownership exposes native gain). Whole-project equality proves the edit retained the original takes, selections, ownership and unrelated intent, and undo/redo remain exact.

All 616 Release core cases pass (13.95 s); seven focused dynamics cases pass Release/Debug (0.77/0.61 s). Release native app/core and Release/Debug focused builds and diff checks pass. The 480×320 capture `/tmp/seam-dynamics-influence.sWQc2C/influence.png` was inspected: counts and override warning fit above the native-only plot without overlap.

This advances ownership feedback, not a generated/target/measured curve implementation or ownership-editing control. The plot remains explicitly native-only; an accepted lane's scope may differ from a manual replacement scope. Live playback, musical listening and the full host matrix are still unqualified. Changes remain local/uncommitted; U25 and Beta GO remain incomplete.

## Compiler-backed staged target samples

Subsequent selected-generated inspection is documented in `SELECTED_GENERATED_DYNAMICS_2026-09-08.md`; it adds a distinct pre-ownership generated overlay to the target overview below.

The plot now additionally shows cyan target samples evaluated through `compileScoreVoices` on a private copy of the captured project with the staged native curve. The native source/draft lines remain separate. This target incorporates the existing compiler's accepted/generated/manual Dynamics precedence and voice allocation; it does not reimplement ownership rules in the UI.

Target sampling runs explicitly when opening/refeshing the inspector or saving/deleting a staged point, not during paint or drag. Successful native mutations invalidate old samples immediately. Compilation failure clears the target, retains its diagnostic, and leaves native editing available; the renderer's capacity/dependency guards are not bypassed to manufacture a preview. Compilation is currently synchronous and bounded by existing score/voice limits; worst-case UI latency and asynchronous preview scheduling remain open.

Each allocated voice is sampled at up to 257 evenly spaced region ticks, supplemented by each source note's start, midpoint and final tick. Integer quotient/remainder arithmetic avoids overflow when constructing the uniform grid. Note lookup is indexed rather than scanning the region per note. At most 34,112 candidate evaluations arise from 16 voices and 10,000 notes before deduplication/filtering. The retained records identify region tick, actual active note, voice index and compiled dynamics gain; rests are omitted. Dots are deliberately not connected: this sampled overview does not claim continuous coverage, interpolate over rests/discontinuities or fully represent every short generated transition. Source-note sample supplementation improves short-note visibility but is not a complete retiming/coverage guarantee.

The sampling clock is 48 kHz and the compiler is invoked in score-only mode. Cyan values are staged score dynamics—not articulation gain, final mixed amplitude, a raw proposal lane or measured audio. Per-voice values are overlaid, not summed. Unsaved yellow point edits are excluded until Save to draft. Separate raw generated and measured curves, voice-isolated inspection, full time zoom and live rendering/host qualification remain open.

Regression evidence compares target samples to actual compiler results, verifies generated-controlled versus manually replaced values after staging, tests invalidation, preserves polyphonic note/voice identities, and exercises the 17-overlapping-voice rejection while confirming native model edits still apply. All 617 Release core cases pass (14.48 s); eight focused dynamics cases pass Release/Debug (0.82/0.67 s). Release native app/core and Release/Debug focused builds and diff checks pass. The inspected 480×320 capture `/tmp/seam-dynamics-target.iJLgRH/target.png` shows the target samples separately from the native curve and keeps the warning, rows and actions within the panel. Changes remain local/uncommitted; no U25 or Beta GO acceptance is claimed.
