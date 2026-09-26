# SING UI-fidelity packet 209aa9d2

Source 209aa9d2f9b05fddb27ce155d8d6d111eb4bbd83 (dirty: False), binary sha256 922bc235c81b1a5cfb3016147fbecf4a75903e0d63654d6f6c8deaa8d2578583, macOS 26.2 (25C56), backend: AppKit software raster + NSTextInputClient, device scale 2.

Measurements only. Native visual acceptance, FL Studio, VoiceOver and the reviewer and owner verdicts are not decided by this tool.

## Captures

| Capture | Frame state | Logged at exit | State reached | Geometry | Semantics | Images | AppKit | Paint p50/p95 ms | Footprint MB |
|---|---|---|---|---|---|---|---|---|---|
| emo-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 23.3/27.6 | 623 |
| scene-empty-1600x900 | cancelled | cancelled | yes | PASS | PASS | PASS | captured | 24.0/25.5 | 612 |
| emo-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.9/30.2 | 626 |
| scene-ready-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.2/30.9 | 619 |
| emo-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 48.8/52.5 | 1007 |
| scene-rendering-1600x900 | rendering | rendering | yes | PASS | PASS | PASS | captured | 48.0/51.2 | 1008 |
| emo-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 25.4/27.4 | 622 |
| scene-failed-1600x900 | failed | failed | yes | PASS | PASS | PASS | captured | 25.4/26.8 | 613 |
| emo-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 28.4/30.0 | 624 |
| scene-dense-overlap-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.5/29.8 | 618 |
| emo-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 7.3/8.4 | 553 |
| scene-ready-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 7.5/8.7 | 551 |
| emo-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.6/10.8 | 571 |
| scene-ready-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 9.6/10.5 | 572 |
| emo-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 15.9/17.1 | 574 |
| scene-ready-1100x720 | ready | ready | yes | PASS | PASS | PASS | captured | 16.4/17.6 | 572 |
| emo-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 20.3/21.7 | 592 |
| scene-ready-1280x800 | ready | ready | yes | PASS | PASS | PASS | captured | 21.3/22.3 | 589 |
| emo-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 26.9/28.1 | 614 |
| scene-ready-1440x900 | ready | ready | yes | PASS | PASS | PASS | captured | 27.9/29.1 | 604 |
| emo-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 14.8/15.6 | 552 |
| scene-inspector-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 15.2/16.2 | 553 |
| emo-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.6/20.0 | 573 |
| scene-inspector-860x640 | ready | ready | yes | PASS | PASS | PASS | captured | 18.6/19.6 | 572 |
| emo-tune-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 68.7/70.9 | 627 |
| scene-tune-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 69.0/71.4 | 623 |
| emo-tune-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 16.6/17.4 | 553 |
| scene-tune-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 17.1/18.0 | 553 |
| emo-mix-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 46.6/48.5 | 625 |
| scene-mix-1600x900 | ready | ready | yes | PASS | PASS | PASS | captured | 45.4/48.2 | 623 |
| emo-mix-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 13.2/14.3 | 552 |
| scene-mix-720x480 | ready | ready | yes | PASS | PASS | PASS | captured | 13.7/14.5 | 553 |

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
- tune 1600x900: PASS
- tune 720x480: PASS
- mix 1600x900: PASS
- mix 720x480: PASS

## Software vs AppKit per region (pixels over channel delta 8, and max delta)

The initial targets of section 11.1 are uncalibrated. The AppKit frame is taken shortly before the app closes and the software frame at close, so moving content (meters, progress) can differ legitimately.

| Capture | header | portrait | notes | expression | lane | footer | body |
|---|---|---|---|---|---|---|---|
| emo-empty-1600x900 | 0.00% (4) | 0.00% (3) | 0.00% (2) | 0.00% (3) | 0.00% (2) | 0.00% (2) | - |
| scene-empty-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) | - |
| emo-ready-1600x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) | - |
| scene-ready-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (9) | - |
| emo-rendering-1600x900 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) | - |
| scene-rendering-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) | - |
| emo-failed-1600x900 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) | - |
| scene-failed-1600x900 | 0.00% (5) | 0.00% (7) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (2) | - |
| emo-dense-overlap-1600x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) | - |
| scene-dense-overlap-1600x900 | 0.00% (5) | 0.01% (12) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (9) | - |
| emo-ready-720x480 | 0.00% (4) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (4) | - |
| scene-ready-720x480 | 0.00% (5) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (8) | - |
| emo-ready-860x640 | 0.00% (4) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (4) | - |
| scene-ready-860x640 | 0.00% (5) | 0.00% (7) | 0.00% (3) | - | 0.00% (2) | 0.00% (9) | - |
| emo-ready-1100x720 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (4) | 0.00% (2) | 0.00% (4) | - |
| scene-ready-1100x720 | 0.00% (5) | 0.00% (8) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (9) | - |
| emo-ready-1280x800 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (4) | 0.00% (2) | 0.00% (4) | - |
| scene-ready-1280x800 | 0.00% (5) | 0.01% (11) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (8) | - |
| emo-ready-1440x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) | - |
| scene-ready-1440x900 | 0.00% (5) | 0.00% (12) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (9) | - |
| emo-inspector-720x480 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (4) | - |
| scene-inspector-720x480 | 0.00% (5) | 0.00% (6) | 0.00% (2) | 0.00% (2) | 0.00% (2) | 0.00% (8) | - |
| emo-inspector-860x640 | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (4) | - |
| scene-inspector-860x640 | 0.00% (5) | 0.00% (6) | 0.00% (2) | 0.00% (2) | 0.00% (2) | 0.00% (9) | - |
| emo-tune-1600x900 | 0.00% (4) | 0.00% (4) | - | 0.00% (3) | - | 0.00% (3) | 0.00% (2) |
| scene-tune-1600x900 | 0.00% (5) | 0.01% (12) | - | 0.00% (3) | - | 0.00% (9) | 0.00% (8) |
| emo-tune-720x480 | 0.00% (4) | 0.00% (4) | - | - | - | 0.00% (4) | 0.00% (2) |
| scene-tune-720x480 | 0.00% (5) | 0.00% (4) | - | - | - | 0.00% (8) | 0.00% (8) |
| emo-mix-1600x900 | 0.00% (4) | 0.00% (4) | - | 0.00% (3) | - | 0.00% (3) | 0.00% (4) |
| scene-mix-1600x900 | 0.00% (5) | 0.01% (12) | - | 0.00% (3) | - | 0.00% (9) | 0.00% (3) |
| emo-mix-720x480 | 0.00% (4) | 0.00% (4) | - | - | - | 0.00% (4) | 0.00% (4) |
| scene-mix-720x480 | 0.00% (5) | 0.00% (4) | - | - | - | 0.00% (8) | 0.00% (3) |

## NOT_RUN

- emo-flstudio.png, scene-flstudio.png: the embedded FL Studio capture is an owner session (see the FL test package).
- stale state: stale audio exists only after an edit follows a published render; the standalone command line cannot script an edit, so this state is covered by the shell tests, not by a capture.
- VoiceOver and Accessibility Inspector walk of the same frames.
- Concept-to-native comparison (section 11.1 item 1): a reviewer judgement.

## Verdicts

- Reviewer verdict: PENDING
- Owner result: NOT_RUN
