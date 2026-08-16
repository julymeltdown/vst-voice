# Phase 2 Evidence

Generated with:

```bash
python3 scripts/generate_phase2_evidence.py --root .
```

## Core artifacts

- `evidence/phase2-editor.png`: note, phoneme, unit, seam, waveform, and validator proof UI.
- `evidence/phase2-raw-phrase.wav`: deterministic raw concatenative phrase.
- `evidence/phase2-raw-waveform.svg`: waveform visualization.
- `evidence/phase2-raw-spectrogram.png`: rendered phrase spectrogram.
- `evidence/phase2-summary.json`: end-to-end counts and audio statistics.
- `evidence/synthetic-voicebank-manifest.json`: data-only test bank.
- `evidence/voicebank-validation.json`: CLI validation output.
- `evidence/cli-analysis-analysis.json`: standalone WAV analysis output.
- `evidence/phase2-benchmark.json`: raw renderer, seam composer, and F0 analysis timing.
- `evidence/test-output.txt`: 33 individually reported test results.
- `evidence/ctest-dev.txt`: Debug/dev CTest matrix.
- `evidence/ctest-release.txt`: Release CTest matrix.
- `evidence/ctest-sanitize.txt`: AddressSanitizer/UndefinedBehaviorSanitizer CTest matrix.
- `evidence/verification-matrix.json`: collected build/runtime status.

## Interpretation

The synthetic bank is designed to expose, rather than conceal, sample joins. One `loop-discontinuity` warning is intentionally retained in the test data so the validator and evidence UI prove that warnings remain visible without preventing a structurally valid bank from rendering. Duplicate alias information demonstrates Variant inventory.

Timing overlap issues are also expected: preutterance places consonants before note-on, and adjacent units therefore overlap. The seam engine receives these overlaps explicitly.
