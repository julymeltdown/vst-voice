# Selected generated dynamics inspection

U25 now displays selected generated dynamics separately from the staged target. This extends `DYNAMICS_LANE_MODEL_2026-09-08.md`; it does not accept U25 or Beta GO.

## Implementation

`CompiledScorePerformance::inspectAt` shares the canonical evaluator with ordinary `at`, but additionally inspects accepted Dynamics values before manual replacement. The returned `selectedGeneratedDynamicsGain` is inspection-only. Ordinary audio evaluation does not expose it and continues to skip replaced lanes. No manual ownership, selection, target gain, pitch or articulation is changed by inspection.

The source-window offset, tempo mapping, accepted scope and lane interpolation use the existing compiler paths. Generated zero is an engaged value representing silence; no selection/rest is absent. Null Dynamics samples remain invalid under `PerformanceLane::validate`—only pitch permits null samples. The new test explicitly rejects malformed null dynamics rather than relaxing that domain contract.

The editor's cached, per-voice target samples now retain this optional generated value. Orange hollow markers show selected generated values; cyan dots show the staged post-ownership dynamics target. An accepted generated value remains visible when manual Dynamics Replace ownership masks its effect. Matching values can overlap visually, with different marker shapes. Values belong to active notes within accepted scopes, not every point of every stored proposal; unselected proposals and measured audio remain undisplayed.

This remains a bounded sampled score-only overview at 48 kHz. Dots/markers are not connected across rests or discontinuities. It is not a complete continuous envelope, final mixed level, per-voice isolated editor, measured dynamics estimate or listening-quality judgment. Existing target refresh timing and synchronous worst-case latency limitations are unchanged.

## Evidence

- New dedicated compiler regression covers an unselected proposal, accepted source offset, a tempo change, manual replacement, scope exit, generated silence, invalid null rejection and unchanged audio-control output between ordinary and inspection evaluation.
- The native dynamics regression verifies generated 0.8 remains available before ownership even where the staged target becomes 0.25.
- Release native app/core and Release/Debug compiler/dynamics targets build successfully.
- 617 Release core cases pass (13.85 s), including existing dynamics audio/save/reload/export regressions.
- 18 dedicated compiler cases pass Release/Debug (0.89/4.13 s). These are a separate target, not included in the 617 core count.
- Eight dynamics workflow cases pass Release/Debug (0.65/0.54 s).
- `git diff --check` passes.
- The 480×320 raster `/tmp/seam-dynamics-generated.jVEVLW/generated.png` was inspected: the generated-versus-target difference is visible and the legend, rows and actions fit without overlap.

Changes are local/uncommitted. Measured audio, continuous/time-zoom interaction, voice-isolated inspection, worst-case performance and real plugin/OS/host qualification remain open.
