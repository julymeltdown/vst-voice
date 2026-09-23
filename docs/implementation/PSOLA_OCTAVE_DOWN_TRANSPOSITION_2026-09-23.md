# Classic PSOLA loses transposition at an exact octave down

Date: 2026-09-23
Plan unit: U16, scenario 1 ("Annotated CV/VC material transposes without
source-pitched voiced edges")
Base commit: 26e15fff
Status: **reproduced, not fixed.** Recorded because the evidence is complete and
the repair needs care.

## The finding

Rendering a sustained unit to a target exactly one octave below its source leaves
the output at the **source** pitch. Measured output is 440.00 Hz for a 220.00 Hz
target: the transposition does not happen at all, an error of +1200.0 cents.

The decisive experiment holds the target fixed and varies only the source, so the
target itself cannot be the explanation:

| source | source Hz | source / target | measured | error |
|---|---:|---:|---:|---:|
| A4 (69) | 440.00 | **2.000** | 440.00 | **+1200.0 cents** |
| E4 (64) | 329.63 | 1.498 | 220.05 | +0.4 cents |
| C4 (60) | 261.63 | 1.189 | 220.09 | +0.7 cents |
| B3 (59) | 246.94 | 1.122 | 220.07 | +0.5 cents |
| A#3 (58) | 233.08 | 1.059 | 220.11 | +0.9 cents |
| A3 (57) | 220.00 | 1.000 | 220.00 | +0.0 cents |

Same target, same unit shape, same analyser. Every source within a musical
distance transposes correctly to within 1 cent; the one exactly an octave above
does not. The failure therefore depends on the ratio between source and target
period, not on the target pitch or the material.

## Measurement

Two independent instruments agree, so this is not an analyser artefact:

- The product own analyzePitch reports 440.00 Hz.
- A plain Hann-windowed FFT of the rendered WAV peaks at 440.00 Hz, with
  |X(440)| = 893.7 against |X(220)| = 10.2 -- a ratio of 87.9. The 220 Hz
  partial is 39 dB below the fundamental that should not be there.

A sweep of all 31 semitone targets from one source puts the failure at exactly the
octave: 30 of 31 targets land within 1 cent, and 220.00 Hz (ratio 2.000) reports
440.00 Hz. The neighbouring target at ratio 1.888 is correct to +0.6 cents.

The harness was validated before being believed. It reproduces the existing
passing test in tests/test_synthesis.cpp exactly -- source A4 440 Hz, target midi
72, expected 523.25 Hz, measured 524.00 Hz by FFT -- so it measures the same thing
that test measures.

## Mechanism

libs/seam-synthesis/src/classic_psola.cpp sizes the overlap-add window from the
**source** period, not the target period:

    const auto periodSource = std::clamp(
        localPeriod(stableMarks, stableIndex, sourceMedianPeriod),
        sourceMedianPeriod * 0.55, sourceMedianPeriod * 1.8);
    const auto halfOutput = std::max<time::SampleFrame>(
        2, static_cast<time::SampleFrame>(std::llround(periodSource * sampleRateRatio)));

For a 109-sample source period the window is 219 samples wide, and output marks are
placed one target period apart. At ratio 2.000 the target period is 218.18 samples,
so the window is wider than the distance between marks and each grain source
periodicity lands in phase with its neighbours instead of being replaced. The window
is derived from the thing being replaced rather than the thing being produced,
which is why the artifact appears exactly where the two periods have the simplest
relationship.

**This window hypothesis was tested and refuted.** Sizing the window from the
target period (so grains overlap 50% at ratio 2.0 instead of merely touching) was
implemented and re-measured: the octave-down case still reported 440.00 Hz, and
the integer-ratio cases still failed. The change was reverted rather than left in
as an unproven edit.

What the refutation leaves is a better explanation. The loop reads each grain with
a source position of sourceMark.frame + relative * sourcePerOutput, where
sourcePerOutput is only the **sample-rate** ratio and carries no target-pitch
scaling. A grain is therefore a 1:1 copy of a source region, containing the source
periodicity at its original sample spacing. Pitch change comes only from where
grains are placed. Two consequences follow, and both are observed:

- Consecutive grains are placed targetPeriod apart while reading source marks
  sourcePeriod apart, so corresponding samples of adjacent grains land
  sourcePeriod apart in the output. Where those copies reinforce in phase, the
  output carries energy at the source period -- the pitch that should have been
  removed.
- That reinforcement is strongest when the target period is a whole multiple of
  the source period, because then the copies line up exactly. Measured integer
  ratios all fail (2.000, 4.000) and the nearby non-integers pass (1.888 at
  +0.6 cents, 2.520 at +0.7 cents), which is the shape this explanation predicts
  and the window hypothesis did not.

So the defect is not the window width but that grain placement and grain content
are scaled by different quantities. Stating that as a repair is not yet justified
by a measurement, so no fix is included.

## Why this was not caught

tests/test_synthesis.cpp has one PSOLA transposition case, and it transposes **up**:
unit rootMidi 69 rendered to target midi 72. Down-transposition is uncovered, and in
particular the octave-down case is uncovered. The demo bank cannot cover it either:
its stored pitch marks are stale (see PITCH_MARK_STALENESS_2026-09-23.md), so
rendering it measures the stale-mark defect rather than this one.

## Consequences

An octave-down transposition is not an exotic request: it is how a voicebank at the
top of its range covers a lower melody line, and it is a single step in any
octave-correction workflow. A renderer that silently returns the source pitch
produces confident audio at the wrong note rather than an error, which is the
failure mode the plan asks U16 to eliminate.

## What this does not do

No fix is included. Changing the overlap-add window sizing affects every classical
render, so it needs its own before/after across the sweep and the existing suite
rather than being folded in here.

## Resolution (fixed in `4d79e06c`, pinned by `19945613`)

The defect is fixed. The grain read used

    sourcePosition = sourceMark.frame + relative * sourcePerOutput

where `sourcePerOutput` is only the sample-rate ratio. A grain was therefore a
1:1 copy holding the source periodicity at its original spacing, and the output
only lost that periodicity where neighbouring grains overlapped enough to cancel.
At integer down-ratios the grains barely overlap, so the source pitch survived.
That is why the failure tracked the *ratio* rather than the target pitch, and why
the earlier window-only hypothesis was wrong: the window was not causing the
failure, it was the symptom of the same missing scaling.

Grains are now resampled by `periodSource * sampleRateRatio / targetPeriod`, so
one source period occupies one target period, and the overlap-add window is sized
from the target period to match the new read step. Sizing the window from the
source period was the wrong unit once the step changed: at ratio 2.52 a
219-sample window read only 0.6 of a source period and the truncation left a
subharmonic at 87.56 Hz for a 174.61 Hz target.

`SEAM_PSOLA_RENDERER_REVISION` went from 10 to 11. That revision is already part
of the render identity, and without the bump cached PCM from the old renderer
would have been reused against a changed renderer -- which is what
`seam_phase12a_tests` caught when the code change was made without it. The
mechanism worked as designed; I had simply not used it.

Verified across all 46 semitone targets from one source. The 42 targets below the
analyser 1200 Hz ceiling land within about 3 cents, including both previously
failing integer ratios (2.000 and 4.000). The four above the ceiling were
confirmed correct by FFT, within 2 cents, since the product analyser cannot report
them.

The decisive table after the fix:

| source | source Hz | source / target | measured | error |
|---|---:|---:|---:|---:|
| A4 (69) | 440.00 | 2.000 | 220.11 | +0.9 cents |
| E4 (64) | 329.63 | 1.498 | 220.06 | +0.4 cents |
| C4 (60) | 261.63 | 1.189 | 220.10 | +0.8 cents |
| B3 (59) | 246.94 | 1.122 | 220.06 | +0.5 cents |
| A#3 (58) | 233.08 | 1.059 | 220.11 | +0.9 cents |
| A3 (57) | 220.00 | 1.000 | 220.00 | +0.0 cents |
