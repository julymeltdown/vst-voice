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
