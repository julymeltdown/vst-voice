# Native Studio runtime check — 2026-09-07

Result: native startup, recovery, rendering and timed shutdown passed for a copied synthetic producer workspace. Interactive acceptance remains unverified.

## Executed scope

Launched the current Release Studio executable with a copied test workspace, exact inventory digest, producer operator, forced synthetic input, 1000×700 logical window, final PPM capture and 600,000-ms automatic close. A second run used identical executable bytes inside a temporary macOS app bundle to make the process discoverable to the UI-control service. Neither run used physical microphone capture or audition.

Both process handles were polled to terminal exit code 0. Each reported:

- Project `candidate-import-test`, durable generation 18.
- One MarkerReview assignment; zero approved assignments.
- Zero recorded frames, input callbacks or input failures.
- No staged recovery candidates.

The bundled run's actual 2000×1400 AppKit capture was converted to PNG and visually inspected. It shows recovered manual bounds, stable gesture keys, queue/generation information and complete not-measured/not-approved notices. This uses the native text renderer, not the test canvas's fallback glyphs. It verifies only the recovered idle state: the waveform was not loaded and no editing actions were executed.

## Interaction limit

The UI-control service could not identify the raw CLI executable as an app. Attaching to the temporary bundle then returned `timeoutReached` after approximately 709 seconds. Both launched processes had completed their configured timed shutdown when their handles were subsequently polled. No process was restarted merely because observation timed out.

This is a tool-attachment failure, not evidence of an application crash or successful interaction. Keyboard shortcuts, mouse dragging, zoom, file dialogs, focus/capture behavior and physical-device audition remain open. No approval, recording or deployment was performed. The full implementation goal remains active; this limitation does not prevent further source work.

## Evidence identity

- Git base: `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`, with substantial uncommitted continuation changes. This is not release provenance.
- Release executable and temporary bundle executable SHA-256: `cd1fec564875cc2ae41c6333cf9958ca0725bde2a9c2d1f491cb267d574ee9f2`.
- Final bundled PPM SHA-256: `ea1cbb8a9c5807868023de647dee0b2f0d99783786ea383f55a0d14136a000fe`.
- Copied final producer pointer SHA-256: `b284194dd5aa8f935e04d64318a482338c68d1cc16ee22bf17800e4a6caf6964`.
- Local evidence directory: `/private/tmp/seam-native-review.YzxPBc`; `bundled-final.png` is the inspected conversion. Temporary files may be removed by the OS and are not a durable release packet.

No production source was changed in this verification turn. Both app processes terminated normally. The isolated copied workspace and temporary bundle are retained for inspection, not installed as the user's application.
