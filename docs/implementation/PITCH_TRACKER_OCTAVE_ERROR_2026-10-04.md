# The pitch tracker jumps an octave after a voicing gap, and most of the measured pitch error is that

Date: 2026-10-04. This is a measurement, not a fix. Nothing in the product changed.

## What was measured

The retained application comparison
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/application-combined-e9-v512-e1/comparison.json`
reports `status: MISMATCH`, `meanAbsoluteCents: 102.93`, and 447 measurable voiced pairs. Read as a
distribution rather than a mean, it says something different:

| Measure | Value |
| --- | --- |
| Median absolute frame error | **3.54 cents** |
| Mean absolute frame error | 102.93 cents |
| Mean absolute error, excluding 23 frames | **7.17 cents** |
| Share of total absolute error carried by those 23 frames | **93.4 %** |
| Of those 23, how many are exact negative multiples of 1200 cents | **20** |

The multiples are not approximate. Sorted, the 23 are -3693, -3578, -3377, -2539, -2532, -2483,
-2479, -2478, -2477, -2477, -2477, -2463, -1677, -995, -993, -977, -977, -977, -977, -977, 394,
579 and 1893 cents: that is -1200 nine times, -2400 eight times and -3600 three times, plus three
that are not.

Every one of those 23 frames carries `frameStatus: "compared"`. The tracker reported them as
confident measurements, not as uncertain ones. None of them is a frame it flagged low confidence.

## The shape the errors take

Reading the frame series around each error shows one repeated shape. Frames 95-96 read -2477 and
-2478 preceded by two unmeasurable frames. Frames 437-440 read -977 four times running, also after
a gap. Frames 313-315 read -2539, -2532 and -2479 after a gap. Frames 23-24 read -1677 then -3578
after a gap. In every cluster the error begins immediately after the track went unvoiced, and it
persists for as long as the note does.

Between the gaps the tracker is accurate to single-digit cents.

## Reproduced on the real audio, not on a synthetic tone

The mechanism is visible in `libs/seam-voicebank/src/pitch.cpp`. The correlation search finds the
highest-scoring lag (line 152), then a refinement pass takes **the earliest** lag whose correlation
clears `max(voicingThreshold, best * 0.92)` and which is a local maximum (lines 159-170). For a
voice with a strong second harmonic, the correlation at twice the true period is nearly as high as
at the true one, so the earliest qualifying peak is the double period: an octave down. There is
no continuity check against the previously accepted frame, so nothing corrects it while the note
continues.

Running the shipped extractor (`seam_voicebank_cli extract-pitch`) over the retained candidate WAV
after an arithmetic stereo downmix reproduces it:

| Frame | Reported | Held | Cents | Confidence |
| ---: | ---: | ---: | ---: | ---: |
| 98 | 264.22 Hz | 593.21 Hz | -1400.2 | 0.320 |
| 297 | 1200.00 Hz | 250.93 Hz | +2709.2 | 0.558 |
| 315 | 783.34 Hz | 195.76 Hz | +2400.6 | 0.375 |
| 416 | 331.16 Hz | 679.66 Hz | -1244.7 | 0.337 |
| 467 | 329.50 Hz | 655.02 Hz | -1189.5 | 0.497 |
| 490 | 189.52 Hz | 332.80 Hz | -974.8 | 0.325 |
| 497 | 521.47 Hz | 189.61 Hz | +1751.5 | 0.444 |

Seven post-gap octave jumps in 1172 frames, all within a few cents of an exact multiple of 1200.

## Two failed reproductions, recorded because they were informative

**A steady tone with a strong second harmonic does not reproduce it.** A 220 Hz tone with an
equally strong second harmonic tracks correctly across every frame, because with no gap there is
no moment at which the earliest-peak rule is applied to a fresh decision.

**A synthetic note-gap-note does not reproduce it either.** The gap used was long and
digital-silent, which left no energy for the refinement to lock onto at all.

What reproduces it is a real voice across a real voicing boundary, where energy continues but
periodicity does not. That is why the measurement above is on retained audio, and the two
synthetic cases are not offered as evidence.

## What this is and is not

**Established, by the sources and the runs named above.** That 93.4 % of the measured pitch error
in the retained application comparison sits in 23 frames that are exact octave multiples; that all
23 were reported as confident comparisons; that the error begins after a voicing gap and
persists; that the shipped extractor reproduces the same pattern on the same audio; and that the
earliest-peak rule at `pitch.cpp:159-170` has no continuity term.

**Not established, and not claimed.**

- **Nothing is fixed.** No product code changed in this entry.
- **This is not shown to be the whole story.** 23 frames is 5 % of the comparison's frames.
  Removing them drops the mean to 7.17 cents, and whether the remaining frames are inside a
  50-cent tolerance has not been measured.
- **The reference track was not re-extracted**, so this compares the candidate against a stored
  error series rather than re-deriving both sides. The downmix used here is 16-bit and differs
  from the comparison's float32 one, so frame indices do not correspond; the pattern does,
  independently, in both.
- **No threshold is proposed.** A continuity check would change what the tracker reports on real
  voices, and which check is right is a listening question. The listening packet named in the
  readiness register is the input that decision needs, and no one has listened to these frames.
- This does not move SEAM-BETA-P0-08. That blocker is a pitch-accuracy verdict on an application
  render; a measurement explaining most of one number in it is progress toward diagnosis, not a
  qualification.
