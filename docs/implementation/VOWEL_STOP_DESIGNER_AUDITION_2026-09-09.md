# Vowel-stop context audition in Designer

Designer now distinguishes three plosive preview modes: isolated source,
stop-vowel and vowel-stop. Vowel-stop uses a one-second scratch score with the
selected vowel first, followed by a nominal 50 ms closure and the authored burst
at phrase end. It uses the existing production articulation renderer and normal
note/lyric ownership and timing validation. Audition peak/edge protection remains
active. It does not edit a song or publish candidate material.

The mode is typed in the asynchronous result and ready-buffer state. Mode changes
require a matching new render rather than playing the previous ordering's PCM.
The original bool source/phrase entry remains a compatibility wrapper for Source
and StopVowel. Invalid typed modes reject without replacing ready audio.

With a plosive selected, Command/Control-Space selects isolated source, Shift
selects stop-vowel, and Alt selects vowel-stop. A separate Render selected vowel
then plosive semantic action is also wired. Ready/playback labels identify the
current ordering and retain the unapproved distinction. Native shortcut/action
behavior was not newly exercised in this increment.

## Verification

Full Release build passed. Designer, voice design, export and aggregate-core
suites passed in 25.84 seconds. Tests verify the one-second final-stop preview,
initial vowel energy, exact closure [45120,47520) at the fixture's 48 kHz/10 ms
burst settings, final burst, terminal zero, reproducibility and distinction from
stop-vowel PCM. Session tests cover mode adoption, invalid-mode retention and
switching back to the original phrase ordering. Existing tests continue to cover
selection changes, cancellation and separate isolated-source buffers.

Logs are retained under `evidence/vowel-stop-audition-2026-09-09/`. This was not a
full-suite run, a new native playback test or independent pronunciation review.
The complete Full-Scope Beta GO requirements remain open; no whole roadmap unit
or singer-quality acceptance is claimed.

No files were missing relative to `session-preservation-2emetl`. Existing dirty
work was retained without staging, committing or pushing.
