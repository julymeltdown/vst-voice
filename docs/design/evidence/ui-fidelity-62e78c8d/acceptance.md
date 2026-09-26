# SING UI-fidelity packet 62e78c8d

Source 62e78c8dc13ab5260714fbe36aa1182262106808 (dirty: False), binary sha256 f4a3bcf8154413714975cc1749bd80a7b5ae3c2c9580cbc545f8bfc8347832ad, macOS 26.2 (25C56), backend: AppKit software raster + NSTextInputClient, device scale 2.

Measurements only. Native visual acceptance, FL Studio, VoiceOver and the reviewer and owner verdicts are not decided by this tool.

## Captures

| Capture | Frame state | Logged at exit | State reached | Geometry | Semantics | Images | AppKit | Paint p50/p95 ms | Footprint MB |
|---|---|---|---|---|---|---|---|---|---|
| emo-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 22.5/24.8 | 623 |
| scene-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 23.3/24.8 | 616 |
| emo-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.8/29.9 | 629 |
| scene-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.3/30.4 | 621 |
| emo-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 50.0/54.4 | 1015 |
| scene-rendering-1600x900 | rendering | ready | yes | PASS | PASS | PASS | captured | 47.8/51.7 | 1010 |
| emo-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 25.8/27.2 | 624 |
| scene-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 25.9/27.1 | 616 |
| emo-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 30.0/31.4 | 629 |
| scene-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.6/32.3 | 622 |
| emo-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 7.5/8.5 | 555 |
| scene-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 7.0/7.8 | 554 |
| emo-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.6/10.5 | 575 |
| scene-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 10.1/10.8 | 577 |
| emo-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 16.5/17.7 | 578 |
| scene-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 17.2/18.5 | 577 |
| emo-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 20.3/21.1 | 599 |
| scene-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 20.8/22.3 | 590 |
| emo-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.1/28.7 | 617 |
| scene-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 25.6/27.7 | 607 |
| emo-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 14.4/15.5 | 556 |
| scene-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 15.0/15.9 | 556 |
| emo-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.2/19.6 | 577 |
| scene-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.5/19.9 | 575 |

## Geometry failures

- none

## Semantic failures

- none

## Image failures

- none

## Rack width against the responsive rule (section 3.4)

Section 3.4 asks for a 44-point inspector drawer button below 860 points, a 56-point rail below 1100 and the full rack above. A mismatch is a geometry failure.

| Capture | Presentation | Spec | Width | Spec width |
|---|---|---|---|---|
| emo-ready-1600x900 | full | full | 440 | 440 |
| scene-ready-1600x900 | full | full | 440 | 440 |
| emo-ready-720x480 | drawer | drawer | 44 | 44 |
| scene-ready-720x480 | drawer | drawer | 44 | 44 |
| emo-ready-860x640 | rail | rail | 56 | 56 |
| scene-ready-860x640 | rail | rail | 56 | 56 |
| emo-ready-1100x720 | full | full | 320 | 320 |
| scene-ready-1100x720 | full | full | 320 | 320 |
| emo-ready-1280x800 | full | full | 352 | 352 |
| scene-ready-1280x800 | full | full | 352 | 352 |
| emo-ready-1440x900 | full | full | 396 | 396 |
| scene-ready-1440x900 | full | full | 396 | 396 |

## Mode parity (same state and viewport, EMO vs SCENE geometry)

- empty 1600x900: PASS
- ready 1600x900: PASS
- rendering 1600x900: PASS
- failed 1600x900: PASS
- dense-overlap 1600x900: PASS
- ready 720x480: PASS
- ready 860x640: PASS
- ready 1100x720: PASS
- ready 1280x800: PASS
- ready 1440x900: PASS
- inspector 720x480: PASS
- inspector 860x640: PASS

## Software vs AppKit per region (pixels over channel delta 8, and max delta)

The initial targets of section 11.1 are uncalibrated. The AppKit frame is taken shortly before the app closes and the software frame at close, so moving content (meters, progress) can differ legitimately.

| Capture | header | portrait | notes | expression | lane | footer |
|---|---|---|---|---|---|---|
| emo-empty-1600x900 | 0.00% (4) | 0.00% (3) | 0.00% (2) | 0.00% (3) | 0.00% (2) | 0.00% (2) |
| scene-empty-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) |
| emo-ready-1600x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) |
| scene-ready-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (9) |
| emo-rendering-1600x900 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) |
| scene-rendering-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) |
| emo-failed-1600x900 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) |
| scene-failed-1600x900 | 0.00% (5) | 0.00% (7) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) |
| emo-dense-overlap-1600x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) |
| scene-dense-overlap-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (9) |
| emo-ready-720x480 | 0.00% (4) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (4) |
| scene-ready-720x480 | 0.00% (5) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (8) |
| emo-ready-860x640 | 0.00% (4) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (4) |
| scene-ready-860x640 | 0.00% (5) | 0.00% (7) | 0.00% (3) | - | 0.00% (2) | 0.00% (9) |
| emo-ready-1100x720 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (4) | 0.00% (2) | 0.00% (4) |
| scene-ready-1100x720 | 0.00% (5) | 0.00% (8) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (9) |
| emo-ready-1280x800 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (4) | 0.00% (2) | 0.00% (4) |
| scene-ready-1280x800 | 0.00% (5) | 0.01% (11) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (8) |
| emo-ready-1440x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) |
| scene-ready-1440x900 | 0.00% (5) | 0.00% (12) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (9) |
| emo-inspector-720x480 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (4) |
| scene-inspector-720x480 | 0.00% (5) | 0.00% (6) | 0.00% (2) | 0.00% (2) | 0.00% (2) | 0.00% (8) |
| emo-inspector-860x640 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (4) |
| scene-inspector-860x640 | 0.00% (5) | 0.00% (6) | 0.00% (2) | 0.00% (2) | 0.00% (2) | 0.00% (9) |

## NOT_RUN

- emo-flstudio.png, scene-flstudio.png: the embedded FL Studio capture is an owner session (see the FL test package).
- stale state: stale audio exists only after an edit follows a published render; the standalone command line cannot script an edit, so this state is covered by the shell tests, not by a capture.
- VoiceOver and Accessibility Inspector walk of the same frames.
- Concept-to-native comparison (section 11.1 item 1): a reviewer judgement.

## Verdicts

- Reviewer verdict: PENDING
- Owner result: NOT_RUN
