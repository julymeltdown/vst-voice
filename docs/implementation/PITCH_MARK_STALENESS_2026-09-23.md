# Stored pitch marks were never bound to the audio they measured

Date: 2026-09-23
Plan unit: U15, scenario 3 ("Replaced audio invalidates analysis")
Base commit: `cb719d89`

## The finding

A unit's `pitchMarks` are a *measurement*. They record where the glottal pulses
were, in one specific take. Nothing in the manifest recorded which take that was.

Every rule the manifest applied to a mark was structural and take-independent:

- ascending and unique `frame`
- inside `[markers.audioOffset, markers.audioEnd)`
- `confidence` finite and within `[0, 1]`
- at least three marks when the renderer is `classic-psola`

Replacing a unit's WAV while keeping its file name and frame count therefore left
marks that satisfied all of the above while describing audio that no longer
existed. The bank's own identity did change — `computeVoicebankContentHash`
hashes the audio bytes — so a *cache* rebuilt correctly. What did not change is
any statement that the stored analysis had become false.

That matters because the marks are load-bearing, not decorative:

- `classic_psola.cpp` cuts the source on those marks to estimate the source
  period, and refuses to render below three marks in the sustain region.
- The persisted marker description (`loopStart`, `loopEnd`, `releaseStart`)
  is derived from the same marks by the production draft path.

## How it was measured

The comparison uses SEAM's own analyser, not an external estimator, so the two
sides of the argument are the same algorithm.

The shipped fixture `assets/demo-human-voicebank-public-domain/production-bank`
was inspected with the production draft configuration
(2048/256, FFT correlation, 60–1200 Hz, voicing 0.32):

| | value |
|---|---|
| Stored marks | 148, every gap exactly 113 samples → **390.3 Hz** at 44100 Hz |
| `generatePitchMarks` over the same bytes | 367 marks, median gap 45 samples → **980.0 Hz** |
| Disagreement | **1594 cents** |

The unit declares `rootMidi` 67 (392.0 Hz) and the analyser independently
reports the audio at 980 Hz, so the stored marks and the declared root agree with
each other while both disagree with the audio that is actually present. That is
the signature of marks carried over from a different take.

Reproducing the edit directly confirmed the blind spot: replacing a take with a
different waveform of the same name and length produced **zero** validator
findings beyond the generic root-pitch warning, and the stale marks were carried
forward untouched.

## The change

`libs/seam-voicebank/src/validator.cpp` now re-runs the producer's own analysis
over the audio that is present and compares the median mark spacing against the
stored marks' median spacing. A disagreement beyond 300 cents raises
`pitch-marks-stale` naming both frequencies and instructing re-analysis.

Threshold reasoning: the check exists to catch *different takes*, not to police
pitch accuracy. 300 cents is three semitones — far wider than the variation a
legitimate re-analysis of the same take produces, and far narrower than an octave
error or a take swap. `root-pitch-mismatch` continues to own absolute pitch
accuracy at its existing 80-cent threshold; the two findings answer different
questions and both can fire.

The check runs only where marks are actually consumed:
`renderer == classic-psola` and at least six stored marks. Raw units, and units
that cannot support a period estimate, are untouched.

## Negative control

The pre-fix `validator.cpp` was taken from `git HEAD`, compiled into the same
probe, and run against the same bank:

- HEAD: no `pitch-marks-stale` finding (the check does not exist there).
- Fixed: two findings, 1594 cents each.

The new test was also checked for teeth in the other direction: marks generated
from the bytes that are present do **not** raise the finding, and clearing the
marks resolves it.

## Test

`tests/test_voicebank.cpp` → "replacing unit audio invalidates the pitch marks
measured from it". The fixture generates real marks from a real WAV, confirms no
finding, overwrites the WAV with a different take of identical length, asserts
that the unit still passes structural validation (the reason this was invisible),
and asserts the finding now appears.

## What this does not do

This is a *detector*, not a repair. It does not re-analyse the bank, does not
rewrite marks, and does not decide which take is correct. The shipped demo
fixture is deliberately left as-is: it is a public-domain technical fixture
explicitly documented as "not suitable for release-quality singing", its
provenance records the adaptation, and silently rewriting its stored analysis
would destroy the evidence that this class of error can occur at all. The
finding now surfaces wherever the bank is validated.

No release decision changes. `matrixStatus`, `evidenceStatus` and the release
approval records are unaffected.

## Verification

- Release build: clean.
- `ctest --test-dir build/release`: **183/183 passed**.
- `tests/external_beta`: **189 tests, OK**.
- `scripts/verify_phase12b_contracts.py`: PASS.
- `scripts/verify_phase11_contracts.py`: PASS.
- Negative control against `git HEAD`'s validator: confirmed as described above.
