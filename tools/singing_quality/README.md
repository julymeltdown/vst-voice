# U1 auditory diagnostic baseline

This packet reproduces a narrow technical baseline. It makes no musical approval or release-readiness decision. The existing public-domain bank intentionally gives eight phoneme labels the same 0.55-second spoken recording. Its original source, provenance, notice, README and exact bytes remain part of the corpus. Listen to the dry vocals with those limitations visible.

The checked-in corpus contains an original 40-second melody with a one-second lead-in, plus a short unequal-note/rest case. Each project is rendered twice: the bank's declared renderer choices, then forced raw rendering. Float32 mono dry audio preserves measured amplitudes. Per-phrase reports retain snapshot/ABI/resource hashes, target timing, actual placement timing, requested and actual renderers, fallback explanations, timing issues and waveform statistics. The existing voicebank CLI adds measured pitch frames, a waveform and a spectrogram. Target MIDI notes and measured pitch are separate records; this baseline does not score them against a musical threshold.

The Python runner rejects missing or modified locked inputs, escaping or symlink asset paths, unlisted manifest audio and all unlisted bank files before starting any renderer process. It copies the verified bytes, preserving relative paths, into a new private packet directory. The C++ driver freezes every snapshot and checks every selected audio digest against the audio lock before its first phrase render. Errors stop the run and retain execution records; a zero process exit without required artifacts also fails. Neither runner nor driver overwrites an existing packet.

## Integration

Root-owned CMake additions, outside any production-shipping target list:

```cmake
add_executable(seam_singing_quality_render
  tools/singing_quality/render_main.cpp
  tools/singing_quality/render_inputs.cpp
  tools/singing_quality/render_packet.cpp)
target_link_libraries(seam_singing_quality_render PRIVATE seam_rendering)
target_compile_definitions(seam_singing_quality_render PRIVATE
  SEAM_SINGING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
  SEAM_SINGING_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
  SEAM_SINGING_CONFIGURATION="$<CONFIG>")
seam_apply_compiler_options(seam_singing_quality_render)
```

Inside the existing test/Python condition:

```cmake
add_test(NAME seam_singing_quality_contract_tests
  COMMAND ${Python3_EXECUTABLE} -m unittest discover
    -s tests/singing_quality -p "test_*.py" -v)
set_tests_properties(seam_singing_quality_contract_tests PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_test(NAME seam_singing_quality_workflow
  COMMAND ${CMAKE_COMMAND} -E env
    "PYTHONDONTWRITEBYTECODE=1"
    "SEAM_SINGING_QUALITY_DRIVER=$<TARGET_FILE:seam_singing_quality_render>"
    "SEAM_SINGING_QUALITY_ANALYZER=$<TARGET_FILE:seam_voicebank_cli>"
    "SEAM_SINGING_QUALITY_BUILD_EVIDENCE=${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt"
    ${Python3_EXECUTABLE} -m unittest discover
      -s tests/singing_quality -p test_driver_workflow.py -v)
set_tests_properties(seam_singing_quality_workflow PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 900)
```

## Collect a retained packet

Prepare a current source evidence file that records source HEAD and the exact working-diff/added-source identity; a current build evidence file should contain the compiler, generator, flags, configuration and relevant configure/build invocation. The runner retains both supplied files verbatim and hashes them. Compiled build values may be local defaults: they are reported as found, never promoted to release identity. A source-only CMake test fixture uses CMakeLists.txt as test source evidence; that tests collection mechanics and does not replace an operator's actual source/diff record.

```sh
python3 -m tools.singing_quality \
  --root . \
  --corpus tests/singing_quality/corpus/corpus.json \
  --output-parent /absolute/existing/private-evidence-directory \
  --driver build/dev/seam_singing_quality_render \
  --analyzer build/dev/seam_voicebank_cli \
  --build-evidence /absolute/current-build-evidence.txt \
  --source-evidence /absolute/current-source-evidence.txt
```

The command prints the fresh packet directory. `input-provenance.json` identifies the inputs, executables and environment; `output-provenance.json` lists resulting artifacts; `commands/` retains exact argument vectors, exit/time records and stdout/stderr. Each case directory contains `dry.wav`, `saved-project.seam`, `diagnostics.json` and the analyzer outputs. An interrupted or failed run can leave an incomplete packet with command/error records; it never emits a completion or qualification flag.

Focused Python tests: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/singing_quality -v`. The four native workflow tests skip unless their explicit driver/analyzer/build-evidence environment is present. The CMake workflow test supplies these values and must execute without skips before claiming the native path verified. Listening and independent quality acceptance remain separate work under the full-scope plan.

## Actual ASR diagnostic screening

The schema-1 ASR reports produced by `compare_listening_packets.py` before the
2026-09-19 correction contain no recognition evidence. Their confidence came from
manifest loudness metadata and their control outcomes were hard-coded. Keep those
historical files unchanged, but do not use `confidence` or `PINNED_HELD` as evidence.

The replacement reads every WAV, checks its retained SHA-256, and actually executes
the optional recognizer on the audio and three generated controls. It fails with
exit 2 and `ASR_TRIAGE=NOT_RUN` when no local model is configured. It never downloads
a model implicitly and does not send audio to an external service.

For an isolated environment, install `requirements-asr.txt`. Obtain the model
separately from the pinned upstream revision (about 145 MB):

```sh
python -c 'from huggingface_hub import snapshot_download; snapshot_download("Systran/faster-whisper-base", revision="ebe41f70d5b6dfa9166e2c581c45c9c0cfc57b66", local_dir="/absolute/model-directory", allow_patterns=["config.json", "model.bin", "tokenizer.json", "vocabulary.txt", "README.md"])'
python scripts/compare_listening_packets.py /absolute/packet/manifest.json \
  --asr-triage --asr-model /absolute/model-directory \
  --report /absolute/new-asr-report.json
```

Use the isolated environment's Python for both commands. An optional
`--asr-expected-text /absolute/expected.json` accepts a JSON object such as
`{"unfamiliar-song": "あさのそらあおいかぜこえがひびくきみとうたうあさのそらこえ"}`.
Supply only actual case IDs whose intended text is known. Nonlexical events and
held-vowel exercises can be left unscored. References are used only after inference;
they are never supplied as a recognizer prompt. `--asr-decoding-settings` accepts
only `{"language": "ja"}`, `en`, or `ko`; the rest of decoding stays fixed.

Schema 2 retains actual transcripts/segments, native model scores (not calibrated
phonetic confidence), model-file digests, runtime versions, runner digest, and every
audio digest. Controls are two-second silence, seeded white noise, and one impulse.
All go through the same recognizer as the packet. Text on a control produces
`triage_control_breach` and exit 3, even if candidate transcripts appear plausible.
The diagnostic report is still saved. Missing/changed/escaping files abort instead
of generating successful controls. Existing reports and source audio are not overwritten.

Optional character error rate uses NFKC, case folding, katakana-to-hiragana conversion,
and edit distance. Kanji readings are not guessed, so equivalent spellings can look
different. No frozen singing-intelligibility threshold has been calibrated for this
model. Empty transcripts, mismatches, and control hallucinations are investigation
signals. Nonempty transcripts do not establish correct lyrics. Negative controls
alone do not establish recognition sensitivity; known intelligible singing and
known unintelligible singing are still needed for calibration. `perceptualStatus`
remains `UNREVIEWED` and `releaseEligible` remains false in every report.

The orchestration tests use explicitly named fake recognizers to exercise hash
checks, control breaches, absent references, and text comparison. They establish
tool behavior only. Real retained-packet runs are recorded separately in the root
`SEAM_AUTOMATED_VERIFICATION_AND_ACCELERATION_2026-09-19.md` report.

## Fixed LF contamination panel (developer-only)

The default-OFF WORLD experiment includes a prospectively frozen 42-input panel.
It does not change normal preview/export or replace the historical speech FAILs.
See `docs/implementation/U16_LF_CONTROL_RESULTS_2026-09-22.md` for its exact
protocol, build identities, failures and interpretation limits.

```sh
python3 -B -m tools.singing_quality.lf_control_diagnostics /absolute/comparison.json --output /absolute/new-diagnostics.json
```

Run on every arm/configuration's `lf-controls-v1/comparison.json`, not a selected
passing subset. The tool checks all 42 retained cases and runs the unchanged
source-frequency diagnostic on every combined source, then compares declared
clean pairs descriptively. Rejected clean reconstructions stay unavailable.
It performs no synthesis, filtering, normalization, automatic selection or quality
approval. Outputs bind source/analysis/output identities, protocol, executable,
source lock and diagnostic code; existing output files are never overwritten.
