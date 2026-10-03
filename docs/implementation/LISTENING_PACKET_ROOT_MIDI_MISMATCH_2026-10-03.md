# The demo voicebank's declared root pitch does not match its recording, and the
auditory packet's intonation numbers are therefore not about the renderer

Date: 2026-10-03.

## What was done

The first M2.1 listening packet was collected on this machine from the current
`master`, using the repository's own tooling and the Release binaries already in
`build/release`:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m tools.singing_quality \
  --root . --corpus tests/singing_quality/corpus/corpus.json \
  --output-parent /tmp/seam_listening_2026-10-03 \
  --driver build/release/seam_singing_quality_render \
  --analyzer build/release/seam_voicebank_cli \
  --build-evidence ... --source-evidence ...
```

It produced a complete packet with four rendered cases: a 41-second original
melody and a 9.1-second unequal-note/rest case, each rendered through the bank's
declared renderer and through forced raw rendering. Audio is float32 mono dry at
48 kHz, with no clipped samples (measured peak 0.5026) and real signal (RMS
0.069 on the bank-rendered melody).

## The finding

Measuring each note's rendered pitch against the score's `target_midi`, using
the project's own frozen criteria from `tools/singing_quality/acoustic_metrics.py`
(confidence at least 0.5, frames at or above the 1200 Hz analyzer ceiling
excluded and counted as saturated, sub-600-cent gaps counted as octave errors):

| case | scored frames | median error | p95 | within 50 cents | octave-sized |
| --- | --- | --- | --- | --- | --- |
| original-melody-bank | 3318 | 1411.7 c | 2550.1 c | 4.2 % | 3124 |
| original-melody-raw | 3042 | 1618.2 c | 2356.0 c | 3.3 % | 2785 |
| unequal-rests-bank | 613 | 1463.4 c | 2346.0 c | 3.1 % | 581 |
| unequal-rests-raw | 566 | 1597.8 c | 2207.8 c | 3.9 % | 505 |

These are not small errors. They are near-exact multiples of 1200 cents, which is
the signature of an octave-tracking problem rather than poor singing.

**The renderer is not the cause.** A direct Goertzel scan of the rendered
`dry.wav` over note one's steady span finds almost no energy where the analyzer
claimed the pitch was:

```
   98 Hz  E=0.0022      784 Hz  E=0.0002
  196 Hz  E=0.0020      997 Hz  E=0.0000   <- analyzer reported 996.7 Hz here
  392 Hz  E=0.0010     1200 Hz  E=0.0004
```

The analyzer reported a median of 996.7 Hz for a note whose target is 392 Hz,
and the audio contains nothing at 996.7 Hz. Whatever is misreporting, it is not
the audio.

**The bank is mislabeled.** `assets/demo-human-voicebank-public-domain/production-bank/manifest.json`
declares the single unit at `"rootMidi": 67`, which is 392.0 Hz. The recording
that unit points at, `audio/human-vowel-demo.wav`, is PCM16 at 44100 Hz and its
strongest partial across the declared loop span (frames 5292 to 21609) is:

```
strongest partials: [(146, 231.2), (144, 207.5), (148, 204.2), (116, 202.1)]
peak partial 146 Hz -> MIDI 49.90
cents from the declared 392 Hz: -1710
```

The declared root is **1710 cents** away from the audio it names. The provenance
record explains why: the file is `talking.wav` from the upstream `sonic`
repository, described as "the repository author's father talking", a 0.55-second
spoken recording reused under several phoneme labels. A spoken recording is not a
sustained sung vowel, and its pitch is not the pitch the manifest claims.

## Why this matters beyond the fixture

- Every `target_midi` in the corpus is an absolute score value, and every
  rendered note is expected to reach it. With the source unit a fifth-plus below
  its declared root, the pipeline correctly renders the bank and the result
  cannot match the score. The measurement is comparing a correct render against
  an unreachable target.
- Any intonation or pitch-accuracy figure derived from this corpus is a property
  of the mislabeled fixture, not of SEAM. This entry's numbers must not be quoted
  as a renderer result in either direction.
- The packet's own tooling anticipated part of this. `ANALYZER_CEILING_HZ` and the
  octave-error accounting exist precisely because analyzer misreads were seen
  before. What was missing was a check that the bank's declared root matches its
  own audio, which is the defect that would have made those guards legible.

## What this does and does not establish

**Established.** A real, reproducible listening packet renders on this machine
from `master`, with real audio and full provenance. The demo bank's declared
`rootMidi` does not match its recording, by 1710 cents. Intonation figures taken
from this corpus describe that mismatch, not the renderer.

**Not established, and explicitly not claimed.**

- No listening judgment. Nobody has heard this audio. This entry is a
  measurement finding, not a listening observation, and it does not advance M2.1's
  listening requirement.
- No claim that the renderer is correct. The evidence here says the measurement
  was invalid, not that the audio is right. The renderer's intonation remains
  unmeasured against a trustworthy target.
- No change to any manifest, corpus, or source file was made. Correcting the
  fixture's `rootMidi` would make a diagnostic corpus agree with itself by
  rewriting the target, which is the wrong direction: the honest repair is a
  voicebank whose units are actually at their declared pitch, which is the
  external asset U42 already blocks on.

## What unblocks real intonation evidence

The procedural evidence added over the past several units measures the renderer
against a score it defines itself, which is why it is trustworthy. A listening
packet cannot do that with a sample bank, because the bank supplies the pitch.
Until a voicebank's units are verified against their own audio, sample-route
intonation is not measurable here, and M2.1 still needs a human ear. U42 remains
externally blocked on exactly that.
