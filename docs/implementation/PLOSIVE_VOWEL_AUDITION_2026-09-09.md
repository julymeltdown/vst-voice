# Stop plus selected vowel audition

Designer now supports a one-second plosive/vowel preview through the existing
production ArticulatedStream, alongside the isolated half-second burst preview.
It uses the selected plosive, selected vowel pose, exact shared style and current
audition pitch. The scratch score carries valid note/lyric ownership; explicit
phoneme tokens define the pair rather than phonemizing the placeholder lyric.

The preview allocates a nominal 50 ms closure plus the authored burst duration,
then sustains the selected vowel to one second. Time is quantized through the
normal compiled-score timing path. The selected pose must be a supported vowel
in the source's style; no substitute pose or style is silently selected.

## Controls and state

With a plosive row selected, Command/Control-Space prepares or plays its isolated
source. Adding Shift prepares or plays the stop/vowel phrase. A separate Render
plosive with selected vowel accessibility action also prepares the phrase.
The current plosive playback action plays the ready mode and labels it explicitly.
Existing vowel A/B previews retain their separate buffers and meaning.

Async results retain epoch/revision/pose/pitch identity plus preview mode.
Switching mode never reuses the other mode's PCM; changing selection while a
worker runs cancels/discards its old result. Both modes remain unapproved audition
material and do not mutate a song, bank, source-rights decision or producer take.

## Verification

Full Release build passed. Designer, export and aggregate-core CTest suites all
passed in 33.04 seconds. New checks cover deterministic ka/ki previews, different
pose/pitch output, closure/burst/vowel presence, one-second bounds, invalid source
or pose indices, pitch bounds, style mismatch, cancellation, mode separation and
stale selection completion. An initial missing lyric-ownership link was caught
by the normal score validator and fixed without bypassing that validator.

Logs are retained under `evidence/plosive-vowel-audition-2026-09-09/`. This was not
a new full-suite run. Native phrase-action dispatch and acoustic playback were
not newly exercised in this increment; previous isolated-source UI evidence does
not qualify the new phrase controls. Musical/listener qualification, richer
articulation and the complete Full-Scope Beta GO plan remain unfinished.

No files were missing relative to `session-preservation-VYPbt8`. Existing dirty
work was preserved without staging, commit or push. No whole roadmap unit or
release acceptance is claimed.

## Subsequent live macOS verification

Opened the retained `plosive-live.json` engineering recipe through the native
file dialog in source-free Designer. Selected k without changing its spectrum.
The Render plosive with selected vowel action reached Stop and vowel ready;
Play dispatched successfully and showed STOP + SELECTED VOWEL / NOT APPROVED.
Stopped playback and invoked the isolated-source render action: the mode changed
to Plosive source ready, without presenting the phrase buffer as an isolated burst.

Changed burst duration from 22 to 23 ms through the native value action. Playback
became disabled and state returned to Not rendered. Undo restored 22 ms and Saved
state; it did not revive the invalidated PCM. Closed the session normally.
The process reported input backend unopened, physical input false, zero callbacks
and no recorded frames. A byte comparison confirmed the recipe file was unchanged.

This closes the focused native semantic-action/mode/invalidation verification
gap described above. It does not establish keyboard-shortcut behavior, captured
device-loopback fidelity, perceived phonetic accuracy or independent listening
approval. No source-code changes or regression rerun were required for this
live-only follow-up; the earlier test results retain their stated scope.
