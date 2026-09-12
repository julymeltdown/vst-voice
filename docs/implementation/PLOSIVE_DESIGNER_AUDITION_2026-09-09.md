# Dedicated plosive-source audition

Voice Designer now renders a selected plosive independently of its vowel and
frication previews. Command/Control-Space prepares the selected noise source;
when ready, the same shortcut plays it. Selected plosive rows also expose
separate render/play semantic actions with current epoch/revision targeting.

The audition is mono Float32 at 48 kHz for half a second: 50 ms of exact silent
closure, the recipe's nominal finite burst, then silence. The duration and
closure convention are explicit, not inferred phonetic timing. Existing preview
peak protection remains active. Playback is labeled source-only and unapproved.
This is a filter/source audition, not a stop/vowel phrase or a naturalness claim.

## State and verification contract

Plosive PCM has a distinct buffer and source index. The shared asynchronous
noise-preview worker tags results with epoch, recipe revision, pose and pitch;
edits/removal/replacement invalidate buffers and discard stale worker results.
Switching between frication and plosive preview clears the previous noise
preview. Vowel/reference buffers retain their separate meaning. The visible
status line now uses a single priority chain instead of overpainting vowel,
noise and file-operation status on the same row.

The added test covers exact closure and silence bounds, nonzero finite burst,
reproducibility, finite bounded output, invalid index, cancellation, separate
vowel/noise state, duration-change output, stale-result rejection and removal.

The full Release build passed. Four focused CTest suites passed in 26.91 seconds:
Designer, voice design, export and aggregate core. This was not a fresh full-suite
or cross-platform release run.

Live native button/keyboard/accessibility dispatch and audio-device playback
remain unverified. The prior standalone-app attachment issue is not treated as
a source-code blocker or a successful UI test. Full phrase quality, richer
articulation, independent singer qualification and the full Beta GO scope remain
open. No whole roadmap unit or release approval is claimed.

No files were missing relative to `session-preservation-8Wooi5`. Existing dirty
work remains intact; no staging, commit or push was performed. Logs are retained
under `evidence/plosive-audition-2026-09-09/`.
