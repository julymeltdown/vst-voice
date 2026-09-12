# Designer drag and audition interaction QA

## Observed result

The current Release Studio was exercised through native UI interaction at 1000×700 under Korean input, using the existing saved synthetic recipe. Mouse editing, one-step undo, the full formant viewport, preview rendering, pinning and current/reference device startup were observed. No listening-quality, intelligibility, speaker identity or production approval is claimed.

## Sequence

1. Opened Designer and selected the saved `designer-live.json` through the real recipe picker. The view showed SAVED, open quotient 0.62, and control 1/14.
2. Dragged the open-quotient row horizontally by 80 logical pixels. The draft became UNSAVED with increased open quotient. One Cmd+Z returned the draft to SAVED.
3. Up wrapped selection to control 14/14. The six-row viewport displayed the later F2/F3 controls, including F3 gain −6 dB, without overlapping rows at this window size.
4. Space rendered the selected a/neutral pose at MIDI 69 and reached `POSE READY / SPACE PLAY`.
5. Cmd+B pinned A, displaying a/neutral, MIDI 69 and recipe-hash prefix `60fb24fdc0bf`.
6. Space entered `CURRENT B / NOT APPROVED`. A later screenshot showed return to ready without an error. This build links the CoreAudio device factory, not the unavailable/silent-output factory. The observation supports successful device-path execution, not a claim that the audio was heard or acoustically qualified.
7. Right changed selected F3 gain from −6 to −5.5 dB. Space rendered current B, while the A label remained unchanged. Shift+Space entered `REFERENCE A / NOT APPROVED`.
8. The 180-second test deadline ended the run before the planned pitch-mismatch interaction. The existing automated-close path uses programmatic closure and bypasses ordinary dirty-close confirmation; the transient F3 edit was not saved. Process exit was 0.

The saved recipe was read after exit and still contained open quotient 0.62 and gains 0, −3, −6 dB. Producer diagnostics remained generation 3, one MarkerReview take, approved 0, physical input false, and zero input callbacks/frames/recorded frames. The microphone was never used.

## Evidence locations and limits

- Temporary saved recipe: `/var/folders/j4/41h_5mjj7j9d8f2j2bzcsngw0000gn/T/project-seam-articulated-export-76491-0/native-live-prepared.seamjobdir/designer-live.json`.
- Native test capture: `/private/tmp/seam-studio-job-qa.gvOic0/designer-drag.ppm`. Intermediate UI screenshots were inspected inline.
- Source check: `native_window_appkit.mm` automated deadline uses `programmaticClose_`; this run is not new evidence for ordinary dirty-close behavior.
- This check did not finish the live pitch-mismatch rejection, reference playback completion, drag-Escape cancellation or current/reference acoustic comparison. Those remain distinct from existing unit-test coverage.
- Dense typography, smaller-window layout and semantic accessibility still need broader review. The screenshot proves this fixture's viewport did not overlap at 1000×700, not universal layout acceptance.

Full U22/Beta GO remains incomplete. These synthetic artifacts and temporary paths are not release evidence or distributable voicebank material.

Broader regression checkpoint: the current Release core/native executable was rebuilt and passed **504 tests, 0 failures** (11.59 seconds). `git diff --check` passed.
