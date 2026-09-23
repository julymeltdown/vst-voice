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

This is a hypothesis consistent with the measurements above, not yet confirmed by a
patch: the window width is the only quantity in the loop that depends on
periodSource without reference to targetHz, and it is the only quantity whose
behaviour changes at ratio 2.0. Confirming it means sizing the window from the
target period and re-running the sweep.

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

