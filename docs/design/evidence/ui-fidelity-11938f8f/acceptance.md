# SING UI-fidelity packet 11938f8f

Source 11938f8ffac7f57b642c4668db5bf71f6f4516e1 (dirty: False), binary sha256 930af02c6280f58f09c312f0a66b615d80db886a0a3c1eec8dfcea9f4f6578e5, macOS 26.2 (25C56), backend: AppKit software raster + NSTextInputClient, device scale 2.

Measurements only. Native visual acceptance, FL Studio, VoiceOver and the reviewer and owner verdicts are not decided by this tool.

## Captures

| Capture | Render state | State reached | Geometry | Semantics | AppKit | Paint p50/p95 ms | Footprint MB |
|---|---|---|---|---|---|---|---|
| emo-empty-1600x900 | cancelled | yes | PASS | PASS | captured | 23.0/24.1 | 621 |
| scene-empty-1600x900 | cancelled | yes | PASS | PASS | captured | 23.7/25.9 | 613 |
| emo-ready-1600x900 | ready | yes | PASS | PASS | captured | 27.3/28.9 | 626 |
| scene-ready-1600x900 | ready | yes | PASS | PASS | captured | 27.7/28.9 | 618 |
| emo-rendering-1600x900 | rendering | yes | PASS | PASS | captured | 48.0/50.5 | 963 |
| scene-rendering-1600x900 | rendering | yes | PASS | PASS | captured | 47.3/52.1 | 960 |
| emo-failed-1600x900 | failed | yes | PASS | PASS | captured | 25.8/27.2 | 620 |
| scene-failed-1600x900 | failed | yes | PASS | PASS | captured | 25.7/26.8 | 613 |
| emo-dense-overlap-1600x900 | ready | yes | PASS | PASS | captured | 27.8/29.0 | 625 |
| scene-dense-overlap-1600x900 | ready | yes | PASS | PASS | captured | 28.4/29.6 | 619 |
| emo-ready-720x480 | ready | yes | PASS | PASS | captured | 5.8/6.7 | 552 |
| scene-ready-720x480 | ready | yes | PASS | PASS | captured | 6.3/7.0 | 552 |
| emo-ready-860x640 | ready | yes | PASS | PASS | captured | 10.9/11.5 | 573 |
| scene-ready-860x640 | ready | yes | PASS | PASS | captured | 11.2/11.9 | 573 |
| emo-ready-1100x720 | ready | yes | PASS | PASS | captured | 16.2/17.0 | 575 |
| scene-ready-1100x720 | ready | yes | PASS | PASS | captured | 16.7/17.7 | 572 |
| emo-ready-1280x800 | ready | yes | PASS | PASS | captured | 20.4/21.6 | 596 |
| scene-ready-1280x800 | ready | yes | PASS | PASS | captured | 21.2/22.8 | 586 |
| emo-ready-1440x900 | ready | yes | PASS | PASS | captured | 25.1/26.4 | 614 |
| scene-ready-1440x900 | ready | yes | PASS | PASS | captured | 26.4/29.2 | 604 |

## Geometry failures

- none

## Semantic failures

- none

## Rack width against the responsive rule (section 3.4)

Section 3.4 asks for a 44-point drawer button below 860 points and a 56-point rail below 1100. A mismatch is reported here for the reviewer; it is not a contract geometry failure.

| Capture | Presentation | Width | Spec width | Matches |
|---|---|---|---|---|
| emo-ready-1600x900 | full | 440 | 440 | yes |
| scene-ready-1600x900 | full | 440 | 440 | yes |
| emo-ready-720x480 | rail | 56 | 44 | DEVIATION |
| scene-ready-720x480 | rail | 56 | 44 | DEVIATION |
| emo-ready-860x640 | rail | 56 | 56 | yes |
| scene-ready-860x640 | rail | 56 | 56 | yes |
| emo-ready-1100x720 | full | 320 | 320 | yes |
| scene-ready-1100x720 | full | 320 | 320 | yes |
| emo-ready-1280x800 | full | 352 | 352 | yes |
| scene-ready-1280x800 | full | 352 | 352 | yes |
| emo-ready-1440x900 | full | 396 | 396 | yes |
| scene-ready-1440x900 | full | 396 | 396 | yes |

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
| scene-ready-720x480 | 0.00% (5) | 0.00% (6) | 0.00% (3) | - | 0.00% (2) | 0.00% (8) |
| emo-ready-860x640 | 0.00% (4) | 0.00% (4) | 0.00% (3) | - | 0.00% (2) | 0.00% (4) |
| scene-ready-860x640 | 0.00% (5) | 0.00% (7) | 0.00% (3) | - | 0.00% (2) | 0.00% (9) |
| emo-ready-1100x720 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (4) | 0.00% (2) | 0.00% (4) |
| scene-ready-1100x720 | 0.00% (5) | 0.00% (8) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (9) |
| emo-ready-1280x800 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (4) | 0.00% (2) | 0.00% (4) |
| scene-ready-1280x800 | 0.00% (5) | 0.01% (11) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (8) |
| emo-ready-1440x900 | 0.00% (4) | 0.00% (4) | 0.00% (3) | 0.00% (3) | 0.00% (2) | 0.00% (3) |
| scene-ready-1440x900 | 0.00% (5) | 0.00% (12) | 0.00% (3) | 0.00% (2) | 0.00% (2) | 0.00% (9) |

## NOT_RUN

- emo-flstudio.png, scene-flstudio.png: the embedded FL Studio capture is an owner session (see the FL test package).
- stale state: stale audio exists only after an edit follows a published render; the standalone command line cannot script an edit, so this state is covered by the shell tests, not by a capture.
- VoiceOver and Accessibility Inspector walk of the same frames.
- Concept-to-native comparison (section 11.1 item 1): a reviewer judgement.

## Verdicts

- Reviewer verdict: PENDING
- Owner result: NOT_RUN
