# SING UI-fidelity packet a4630958

Source a46309582b083e8c7e7e73b3bcf3411a86c1e9d4 (dirty: False), binary sha256 8af26e712bb20f098e6c9a222ab6de41858ecf7f3b3b8b9158d771eb42318d36, macOS 26.2 (25C56), backend: AppKit software raster + NSTextInputClient, device scale 2.

Measurements only. Native visual acceptance, FL Studio, VoiceOver and the reviewer and owner verdicts are not decided by this tool.

## Captures

| Capture | Frame state | Logged at exit | State reached | Geometry | Semantics | Images | AppKit | Paint p50/p95 ms | Footprint MB |
|---|---|---|---|---|---|---|---|---|---|
| emo-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 25.2/28.9 | 620 |
| scene-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 23.7/26.0 | 612 |
| emo-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.9/30.4 | 625 |
| scene-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.9/34.5 | 618 |
| emo-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 53.5/58.0 | 851 |
| scene-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 49.6/57.5 | 953 |
| emo-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 26.4/28.0 | 621 |
| scene-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 26.2/30.0 | 614 |
| emo-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.8/30.0 | 623 |
| scene-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.5/29.9 | 617 |
| emo-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 5.5/6.0 | 552 |
| scene-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 6.0/6.6 | 552 |
| emo-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.1/9.6 | 575 |
| scene-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.8/10.5 | 571 |
| emo-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 16.2/17.3 | 573 |
| scene-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 16.7/18.6 | 573 |
| emo-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 20.8/23.9 | 593 |
| scene-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 21.2/23.9 | 587 |
| emo-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 26.0/27.9 | 613 |
| scene-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.9/32.3 | 604 |
| emo-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 13.8/14.5 | 551 |
| scene-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 14.1/14.8 | 555 |
| emo-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.8/19.8 | 572 |
| scene-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 19.4/21.9 | 572 |

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
