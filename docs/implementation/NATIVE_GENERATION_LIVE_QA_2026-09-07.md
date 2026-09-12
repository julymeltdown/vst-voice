# Native preparation-to-candidate live verification

## Outcome

A synthetic saved score was selected, inspected, prepared into a new package, generated and collected through the actual AppKit Studio dialogs. Re-selecting that same package reported `GENERATION ALREADY COLLECTED` without another producer generation. No physical microphone, playback, listening acceptance or approval was exercised.

This verifies a single-region procedural fixture, not the full U21/U22 or Beta GO lifecycle. Multi-region selection, batch interaction, complete Designer controls, review/install/sing, other platforms and accessibility coverage remain open.

## Environment and fixture

- Current locally rebuilt Release Studio in temporary development bundle `com.project-seam.studio-job-qa`; configured window 1000×700 and forced synthetic input.
- macOS selected input mode observed read-only: `com.apple.inputmethod.Korean.2SetKorean`. No input-source settings were changed.
- Test root: `/var/folders/j4/41h_5mjj7j9d8f2j2bzcsngw0000gn/T/project-seam-articulated-export-76491-0`.
- Producer: `studio-preparation`, initially durable generation 2 with one missing assignment and planned take `changed-take`.
- Saved source: `job-source.seam`; region shown by the native popup as `Singer / Phrase [0000000000014052]`.
- UI-selected destination: `native-live-prepared.seamjobdir`.
- Prepared manifest SHA-256: `03a5fc0ee005fe7096cf3a974d8d68d02c1c300d21e95d7f92a632708a5f20f4`.

All fixture and capture paths are temporary verification evidence, not production assets or release provenance.

## Observed interaction

1. Opened the score picker, used its native Go-to-path field and selected the saved score. Studio reported `SCORE READY / SHIFT-P PREPARE`.
2. Opened the real region popup. Its label and description were visible and its `Generation score region` accessibility label was exposed. Chose Destination, supplied the new folder name and saved.
3. Studio reported `JOB PREPARED / NOT GENERATED`. Producer remained at generation 2 with no imported audio. The new directory contained project, recipe, expectation, manifest and `.seamjob` reference files.
4. After repairing the non-Latin shortcut issue below and restarting the test app, focused Studio and used Cmd+Shift+I. Selected the new `job.seamjob` through the actual native picker.
5. Studio reported `GENERATED CANDIDATE: MARKER REVIEW`; generation became 3, missing count became 0, marker-review count became 1 and approved count remained 0. The UI displayed `changed-take` and planned s/a gesture spans.
6. Repeated the native job selection. Studio reported `GENERATION ALREADY COLLECTED` and remained at generation 3.
7. Focused Studio, used Shift+P under Korean input and cancelled the score picker. Closed the app. Process exit was 0; final diagnostics confirmed generation 3, one MarkerReview take, approved 0, physical input false and zero input callbacks/frames/recorded frames.
8. Read the durable producer file: generation 3 and procedural lineage for `changed-take` were present with `approval: unapproved`. Raw audio digest was `f143d7272f5bc0bacc200193c4970aa7fd1cd25547903f87aee9ab7fefa05530`.

Screenshots were inspected inline during preparation, region selection, collection and retry. The final native capture is `/private/tmp/seam-studio-job-qa.gvOic0/generation-live.ppm`.

## Defect found and repaired

AppKit's existing letter-key mapping depended entirely on Latin `charactersIgnoringModifiers`. With Korean input active, focused Shift+P and Cmd+Shift+I did not invoke their actions; synthetic direct Latin text delivery could invoke preparation. Added a physical-key fallback only for non-ASCII characters, using the SDK's HIToolbox ANSI key codes for the supported command keys. ASCII layouts retain their existing character-based mapping. Active text entry still goes through `interpretKeyEvents` before this shortcut path, so IME composition is not rerouted through command handling.

After rebuilding, both advertised shortcuts worked in the live Korean-input session. Regression cases cover Korean P/I/B/S/Z, preservation of ASCII-layout handling, empty character input and unknown key codes. The rebuilt Release core/native suite passed **490 tests, 0 failures** (11.36 seconds), and `git diff --check` passed. This is a standalone AppKit-window repair; it does not claim embedded plugin-host input parity.

## Observation limits

Early automation attempts needed explicit canvas focus; clipboard paste also failed, while setting the native path field worked. One timed run ended after score selection and was confirmed terminal before a fresh run. Observation timeouts during close were resolved by polling the same process handle, which confirmed exit 0. These transport/focus issues are not counted as successful actions without a subsequent visible result.
