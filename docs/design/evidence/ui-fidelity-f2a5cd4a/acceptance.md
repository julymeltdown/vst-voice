# SING UI-fidelity packet f2a5cd4a

Source f2a5cd4ab0de2341293c3daebd099c39bc93011d (dirty: False), binary sha256 37d045a8ba9becd3b5121b27b628a9f078967d9fc0fdc74b527a6e646e52fc8d, macOS 26.2 (25C56), backend: AppKit software raster + NSTextInputClient, device scale 2.

Measurements only. Native visual acceptance, FL Studio, VoiceOver and the reviewer and owner verdicts are not decided by this tool.

## Captures

| Capture | Frame state | Logged at exit | State reached | Geometry | Semantics | Images | AppKit | Paint p50/p95 ms | Footprint MB |
|---|---|---|---|---|---|---|---|---|---|
| emo-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 22.9/25.4 | 620 |
| scene-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 23.3/24.4 | 612 |
| emo-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.2/29.7 | 625 |
| scene-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.0/29.6 | 615 |
| emo-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 48.3/50.7 | 961 |
| scene-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 48.4/56.1 | 953 |
| emo-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 25.7/27.5 | 622 |
| scene-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 27.6/30.5 | 615 |
| emo-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.0/29.9 | 626 |
| scene-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.4/30.2 | 617 |
| emo-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 5.6/6.3 | 550 |
| scene-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 6.1/6.9 | 552 |
| emo-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.5/10.1 | 574 |
| scene-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.9/10.6 | 572 |
| emo-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 16.0/17.0 | 573 |
| scene-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 16.6/17.7 | 572 |
| emo-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 20.3/21.4 | 593 |
| scene-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 21.0/22.0 | 585 |
| emo-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 25.3/26.5 | 615 |
| scene-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.1/30.1 | 605 |
| emo-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 13.3/14.2 | 556 |
| scene-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 13.8/15.5 | 553 |
| emo-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.1/19.2 | 572 |
| scene-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.6/19.3 | 571 |

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
| emo-ready-720x480 | 0.00% (4) | 0.00% (4) | 0.00% (4) | - | 0.00% (2) | 0.00% (4) |
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
