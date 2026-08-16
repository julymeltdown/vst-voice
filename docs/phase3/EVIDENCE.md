# Phase 3 Evidence Index

The reproducible generator is:

```bash
python3 scripts/generate_phase3_evidence.py --root .
```

Evidence is written to `docs/phase3/evidence/`.

## Main artifacts

| File | Meaning |
|---|---|
| `test-output.txt` | Individually named test cases and aggregate result |
| `demo-output.txt` | End-to-end Phase 3 demo console output |
| `phase3-summary.json` | Structured project, renderer, scheduler, cache, and schema result |
| `phase3-editor.svg/png` | Evidence view generated from real domain/render data |
| `phase3-psola-phrase.wav` | Mixed Raw/Classic-PSOLA phrase render |
| `phase3-raw-reference.wav` | Raw renderer comparison |
| `phase3-waveform.svg` | Rendered phrase waveform |
| `phase3-spectrogram.pgm/png` | Rendered phrase spectrogram |
| `phase3-demo.seam.json` | Project JSON schema 3 round-trip artifact |
| `synthetic-voicebank-manifest.json` | Voicebank manifest schema 2 with pitch marks |
| `voicebank-validation.json` | CLI bank validation output |
| `voicebank-inspection.json` | CLI inventory output |
| `phase3-benchmark.json` | PSOLA, seam, cache, and scheduler benchmark |
| `audio-metadata.json` | WAV container and numeric sample statistics |
| `branch-policy.txt` | Master-only policy verification |
| `license-audit.txt` | Dependency-policy audit |
| `verification-matrix.json` | Machine-readable Phase 3 evidence summary |

The synthetic voicebank and audio are technical evidence only. They are not represented as an official or commercially licensed voice.
