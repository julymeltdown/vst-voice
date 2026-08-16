# Project SEAM — Phase 2 + Raw Concatenative Vertical Slice

Project SEAM is a C++20 sample-concatenative singing-voice editor. Its deliberate product constraint is that phoneme and sample boundaries are editable musical material rather than defects that must always be hidden.

This repository contains Phase 1 plus the completed Phase 2 phoneme/voicebank foundation and an additional end-to-end **raw sample-concatenation vertical slice**. Development remains on the **`master` branch only**.

## Implemented

### Editor and canonical state

- Integer musical time, tempo/meter maps, and sample-frame conversion.
- Project, track, region, note, lyric, voicebank, and character references.
- Strong typed IDs and post-load ID synchronization.
- Command-based editing with undo/redo and monotonic revision.
- Piano-roll geometry, selection, snapping, zoom/pan, hit testing, and 10,000-note virtualization.
- `PhonemeKey`, generated phoneme tokens, explicit symbol/timing/lock overrides.
- Reversible lyric and phoneme-override commands.
- Note deletion that removes and restores attached overrides.
- Project JSON schema 2 with schema 1 migration.
- Backend-independent IME composition state and Phoneme Lane geometry.

### Language front end

- `IPhonemizer` abstraction.
- Japanese Hiragana/Katakana normalization.
- Contracted mora, sokuon, moraic nasal, long-vowel continuation, punctuation, and warnings.
- Override application without storing derived phoneme plans as canonical state.

### Voicebank foundation

- Data-only manifest schema 1.
- Relative-path and metadata validation.
- Bounded RIFF/WAVE reader for common PCM/float formats.
- Mono PCM16 writer.
- Peak, RMS, clipping, and DC statistics.
- Multilevel waveform summaries and SVG output.
- First-party radix-2 FFT spectrogram output.
- Autocorrelation F0 analysis with octave-error-resistant peak selection.
- Acoustic marker normalization/editing.
- Full-bank validator for files, markers, pitch, clipping, DC, loops, and inventory.
- `seam_voicebank_cli` for validation, inspection, and WAV analysis.

### Raw synthesis vertical slice

- Exact-phone unit candidate generation.
- Deterministic dynamic-programming complete-cover selector.
- Pitch distance, unit length, priority, and take scoring.
- Vowel-onset-to-note-on timing.
- Explicit timing issue reporting.
- `RawLoopRenderer` with pitch/time resampling, stable-vowel loop, release, gain, DC removal, and de-click.
- `SeamComposer` with a controllable smooth-to-hard overlap character.
- End-to-end synthetic bank → lyrics → phonemes → units → timing → PCM WAV.

### Verification and product work

- 33 individually reported tests covering domain, commands, UI models, codecs, phonemizer, bank analysis, and synthesis.
- Debug/release warnings-as-errors and sanitizer presets.
- Phase 1/2 benchmarks and reproducible evidence generators.
- Master-only hooks and branch verifier.
- License allowlist audit and SBOM/notice scaffolding.
- First-party low-poly emo character direction assets remain included from Phase 1.

## Honest current boundary

This repository does **not** yet contain the production iPlug2 + Skia native window, native Windows/macOS IME overlay, PSOLA, spectral synthesis, background render scheduler/cache, official human-recorded voicebank, or plugin targets.

The Phase 2 editor image is an SVG proof generated from real note and phoneme view models. The WAV is genuinely rendered by the implemented raw concatenative pipeline. The synthetic voicebank is test data, not a commercial voice.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release and sanitizers:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

## Run the Phase 2 vertical slice

```bash
./build/dev/seam_phase2_demo --output out/phase2
```

Key outputs:

```text
out/phase2/phase2-editor.svg
out/phase2/phase2-raw-phrase.wav
out/phase2/phase2-raw-waveform.svg
out/phase2/phase2-raw-spectrogram.pgm
out/phase2/phase2-demo.seam.json
out/phase2/phase2-summary.json
out/phase2/synthetic-voicebank/manifest.json
```

## Voicebank CLI

```bash
./build/dev/seam_voicebank_cli validate out/phase2/synthetic-voicebank/manifest.json
./build/dev/seam_voicebank_cli inspect out/phase2/synthetic-voicebank/manifest.json
./build/dev/seam_voicebank_cli analyze \
  out/phase2/phase2-raw-phrase.wav out/phase2/analysis
```

## Evidence

```bash
python3 scripts/generate_phase2_evidence.py --root .
```

This runs the test suite, demo, CLI, benchmark, branch policy, license audit, and generates PNG/audio metadata when supporting tools are installed.

## Benchmarks

```bash
./build/dev/seam_phase1_benchmark
./build/dev/seam_phase2_benchmark
```

Benchmark values are machine-specific evidence, not a universal performance guarantee.

## Repository policy

Only `master` is permitted. Hooks are configured through:

```bash
git config core.hooksPath .githooks
```

## Documentation

- [`PHASE2_IMPLEMENTATION_REPORT.md`](PHASE2_IMPLEMENTATION_REPORT.md)
- [`docs/architecture/PHASE2_PIPELINE.md`](docs/architecture/PHASE2_PIPELINE.md)
- [`docs/phase2/ACCEPTANCE.md`](docs/phase2/ACCEPTANCE.md)
- [`docs/formats/PROJECT_JSON_V2.md`](docs/formats/PROJECT_JSON_V2.md)
- [`docs/formats/VOICEBANK_MANIFEST_V1.md`](docs/formats/VOICEBANK_MANIFEST_V1.md)
- [`docs/licensing/DEPENDENCY_POLICY.md`](docs/licensing/DEPENDENCY_POLICY.md)

## Ownership and licensing

Project source and first-party concept assets are currently proprietary and all rights are reserved. No production third-party source is vendored. Reference projects are documented for behavioral study only.

See [`LICENSE`](LICENSE), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and [`third_party/manifest.yml`](third_party/manifest.yml).
