# Native UI raster verification

This runbook implements the macOS software-raster portion of the redesign plan's
section 14.4. It does not establish FL Studio presentation, VoiceOver usability,
owner approval, cross-platform pixel parity, or musical quality.

## Capture scope

The required score matrix contains 160 cells:

- EMO and SCENE.
- Standard and High Contrast.
- 1600×900, 1100×720, 860×640, and 720×480 logical points.
- 1× and 2× software raster density.
- Empty, dense-overlap, selected note, rendering, and failed render.

The dense-overlap case uses the existing eleven-note overlap fixture; it is not
the separate 10,000-note performance workload. Selection selects an actual note
in the editor model and requires a selected accessibility node in the captured
frame. Rendering and failure come from the real renderer, not a painted label.

The default `--full-matrix` run additionally captures VOICE, TUNE, MIX, and EXPORT
at wide and minimum sizes, both contrasts and densities: 224 captures in total.
EXPORT coverage verifies the presented workspace and its action geometry. It
does not claim that an export file or receipt was produced.

`--screenshot-scale` changes the density of the software surface while preserving
logical window geometry. It requires a screenshot path and accepts only 1 or 2.
The packet requires `--no-appkit` for explicit density overrides: rendering a 1×
surface on a Retina display is not evidence of an actual 1× monitor or DAW host.
Ordinary app launches keep their existing monitor-scale behavior.

## Reproduce twice

From the repository root, with the Release build configured for
`-DPython3_EXECUTABLE=/usr/local/bin/python3`:

```sh
/usr/local/bin/python3 scripts/capture_sing_fidelity_packet.py \
  --full-matrix --no-appkit --close-ms 2500 \
  --output build/evidence/ui-fidelity/matrix-run-1
/usr/local/bin/python3 scripts/capture_sing_fidelity_packet.py \
  --full-matrix --no-appkit --close-ms 2500 \
  --output build/evidence/ui-fidelity/matrix-run-2
/usr/local/bin/python3 scripts/compare_fidelity_packets.py \
  build/evidence/ui-fidelity/matrix-run-1 \
  build/evidence/ui-fidelity/matrix-run-2 \
  --require-identical --require-plan-matrix \
  --output build/evidence/ui-fidelity/matrix-comparison.json
```

Use new output directories on subsequent runs. Each native app instance closes
before the next opens. The 2500 ms wait is a tested fixture setting, not a promise
that every machine reaches READY by that deadline. A frame that misses its state
fails the packet; use the default 10000 ms wait if the local machine needs it.
Do not edit the source or rebuild a different candidate between the two runs.

For a small investigation, filters are supported, for example:

```sh
/usr/local/bin/python3 scripts/capture_sing_fidelity_packet.py \
  --states export,selection --canonical-only --contrasts high --scales 1 \
  --no-appkit --output build/evidence/ui-fidelity/export-selection-smoke
```

Matching subset pixels establish repeatability of that subset only.
`--require-plan-matrix` independently checks all 160 cells and rejects a subset,
even if its declared capture count and every supplied hash are internally valid.

## Evidence integrity

Version 2 packets record distinct run IDs, the source and binary identity,
effective font files, selected design/character/font/translation asset roots,
voicebank hashes, observed scale and contrast, and all per-frame check results.
The child process strips inherited `SEAM_*` diagnostic overrides, pins its asset
roots, and bypasses the shared appearance preferences with an English, motion-on
capture profile. It does not persist that profile. Animation time is frozen;
render state and note selection remain real.

Comparison reads the PNG pixels and recomputes their RGBA SHA-256 hashes. It
rejects empty packets, missing or duplicate frame IDs, failed or missing checks,
unreached states, missing PNGs, wrong image sizes, tampered pixels, missing frames,
and comparing a packet with itself or a copy bearing the same run ID. Strict
comparison also requires matching source/binary/resources/environment and a
build immediately before capture.

Older v1 packets remain historical evidence. They lack the run IDs and complete
check results required by this stronger comparison gate and must be recaptured
to meet it. The previous v1 comparison result is not silently upgraded.

The Python contracts run in CTest as `seam_ui_fidelity_packet_contract`. Geometry
and semantics come from the same native frame as the raster, and the new density
configuration is covered by the native UI tests. Paint primitive checks run as
`seam_design_paint_tests`; their glow-energy value is a backend regression
reference, not an independent proof of the blur algorithm.

## Remaining acceptance

The owner and independent reviewer must still score the design rubric. FL Studio
F02–F05 and the VoiceOver walk-through remain separate real-host checks. Windows
and GitHub CI remain deferred per the project scope. The true-cold 14 ms p95
budget is independent of image reproducibility and must pass its own benchmark.
