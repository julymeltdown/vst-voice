# Phase 4 Evidence Index

Generate evidence with:

```bash
python3 scripts/generate_phase4_evidence.py --root .
```

Evidence is written to `docs/phase4/evidence/`.

For constrained CI runners, verification can be split into separate commands and
the logs reused during collection:

```bash
./build/dev/seam_tests > docs/phase4/evidence/test-output.txt
ctest --preset dev --output-on-failure > docs/phase4/evidence/ctest-dev.txt
ctest --preset release --output-on-failure > docs/phase4/evidence/ctest-release.txt
ctest --preset sanitize --output-on-failure > docs/phase4/evidence/ctest-sanitize.txt

python3 scripts/generate_phase4_evidence.py \
  --root . --skip-build --reuse-verification --skip-image-conversion
```

`--skip-image-conversion` preserves the canonical SVG and PGM evidence. PNG
derivatives are optional presentation artifacts and can be produced independently.

| Artifact | Meaning |
|---|---|
| `test-output.txt` | Individually named test cases and aggregate result |
| `ctest-*.txt` | CTest results for configured build presets |
| `demo-output.txt` | End-to-end Phase 4 execution log |
| `phase4-summary.json` | Structured renderer, microscope, playback, cache, and scheduler result |
| `phase4-editor.svg/png` | Unit Lane and render-state view generated from actual domain models |
| `phase4-microscope.svg/png` | Waveform, spectrogram, markers, and pitch marks generated from a real Unit model |
| `phase4-mixed-render.wav` | Phrase containing Raw, PSOLA, SpectralClassic, and Stretch Units |
| `phase4-raw-reference.wav` | Raw-only comparison Phrase |
| `phase4-playback-mix.wav` | Vocal plus backing timeline mix |
| `phase4-callback-preview.wav` | PCM captured from feeder → ring → callback processor |
| `phase4-benchmark.json` | Spectral, Stretch, callback, and cache benchmark |
| `audio-metadata.json` | Container and numeric WAV statistics |
| `voicebank-validation.json` | Validator output for the synthetic technical bank |
| `verification-matrix.json` | Machine-readable acceptance summary |
| `branch-policy.txt` | Master-only policy result |
| `license-audit.txt` | Dependency-policy result |

The synthetic voicebank is technical evidence only. It is not represented as a licensed commercial voice or as Official Voicebank 01.
