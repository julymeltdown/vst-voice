# Telemetry-free runtime investigation and build

Status: source-backed repair path, not yet a qualified replacement runtime.

Observed macOS ORT 1.30.0 Python teardown failures reached the telemetry SDK's
`Microsoft::Applications::Events::DebugEventSource::DispatchEvent`, HTTP response
handling and recursive-mutex lock after inference had completed. Calling
`disable_telemetry_events()` did not eliminate the failure. Do not accept a JSON
`passed` field when the process subsequently aborts.

The inspected upstream v1.30.0 revision is
`f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`. Relevant source locations:

- `onnxruntime/core/platform/posix/telemetry.cc:378`: the provider constructor
  initializes the manager on first registration.
- `onnxruntime/core/platform/posix/telemetry.cc:562`: shutdown flushes, tears down
  and releases the manager.
- `onnxruntime/core/platform/posix/telemetry.cc:763`: DisableTelemetryEvents only
  stores false into an atomic event-enable flag. It does not destroy/prevent the
  manager's worker lifecycle.
- `cmake/CMakeLists.txt:172`: `onnxruntime_USE_TELEMETRY` is a build-time option,
  default OFF in source.
- `cmake/onnxruntime_common.cmake`: the POSIX telemetry implementation and SDK
  are conditional on that option.

This explains why runtime event disabling is not equivalent to removing the
crashing subsystem. It does not prove the exact SDK race or that every crash has
the same cause. The next remediation is a same-revision CPU build with telemetry
compiled out, retaining normal shutdown and all inference validation.

## Build

The source checkout must be complete, trusted and clean at the pinned revision.
The helper executes upstream code and downloads upstream dependencies. It does
not install a wheel, change the active native runtime or approve a release.

```sh
build/neural-runtime/diffsinger-model-env/bin/python -m venv build/neural-runtime/ort-build-env
build/neural-runtime/ort-build-env/bin/python -m pip install -r tools/neural_runtime/requirements-runtime-build.txt
build/neural-runtime/ort-build-env/bin/python tools/neural_runtime/build_telemetry_free.py \
  build/neural-runtime/onnxruntime-source \
  build/neural-runtime/onnxruntime-telemetry-free-build --jobs 4
```

The build directory must be new. The helper requests Release CPU shared library
and Python wheel, disables telemetry and upstream unit-test compilation, and checks
the resulting CMake cache. Skipping upstream tests is a build step, not qualification.
Do not reuse an incomplete directory without inspecting the failed build and
choosing an explicit recovery command. Never delete other runtime environments.

## Required qualification before replacement

The pinned source fetch completed and the first telemetry-free build was launched
on September 13. Launching the build is not evidence of successful compilation or
a repaired runtime; retain its terminal result before proceeding.

That initial build reached compilation with `onnxruntime_USE_TELEMETRY:BOOL=OFF`,
but CMake selected Homebrew Protobuf 33.4.0 while the upstream dependency list
pins Protobuf/protoc 33.6. Its `Protobuf_DIR` is `/opt/homebrew/lib/cmake/protobuf`.
Treat this first build as diagnostic, not a reproducible replacement candidate.
The build helper now sets `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER` so subsequent
new builds use FetchContent's pinned sources rather than opportunistic installed
packages. It also supplies `Python3_EXECUTABLE` alongside the upstream builder's
`Python_EXECUTABLE` to keep dependency-generation scripts in the selected Python
environment. These changes do not retroactively alter the running initial build.

The initial build subsequently exited 1 compiling `onnx-ml.pb.cc`; the generated
header explicitly rejected incompatible Protobuf C++ headers/runtime. A serial
incremental build reproduced the same error. The original directory is retained.
The corrected attempt uses the new sibling directory
`build/neural-runtime/onnxruntime-telemetry-free-pinned-build`; its completion and
runtime qualification remain pending.

## Isolated comparison environment

`build/neural-runtime/diffsinger-telemetry-free-env` was created without installing
ONNX Runtime. `pip check` passed, and a complete `pip list --format=json`
comparison against `diffsinger-model-env` found only the intentionally absent
`onnxruntime==1.30.0`; all other installed package versions matched. Install the
successfully built candidate wheel here, not over the baseline environment.

The following pre-runtime diagnostic completed with exit 0 in this environment:

```sh
build/neural-runtime/diffsinger-telemetry-free-env/bin/python \
  -m tools.voice_model_training.check_diffsinger_model \
  build/neural-runtime/diffsinger-source
```

It verified 43 changed parameter tensors, exact checkpoint-restored inference,
exact repeated resume and continuous-versus-resumed training, and zero condition
error in the three deployment-bridge cases. It used synthetic fixtures and did
not request `--check-onnx`: this establishes the comparison environment's baseline,
not exported-runtime inference, a trained singer, or a teardown repair.

1. Retain the exact source revision, CMake telemetry flag, compiler/build settings
   and SHA-256 identities of the resulting wheel and native library.
2. Install into a separate diagnostic environment and point a separate native
   test build at its library; preserve currently working artifacts for comparison.
3. Run trained checkpoint → export → Python inference → native inference and all
   existing shape/hash/invalid-input regressions. Require clean process exits.
4. Repeat full process startup/shutdown with retained per-attempt exit statuses;
   do not hide failures through retries, `os._exit`, swallowed exceptions or sleeps.
5. Check the built artifact for the expected absence of the telemetry SDK, then
   perform the appropriate platform lifecycle/privacy checks. A cache flag alone
   is not binary or network-behavior proof.
6. Only after that evidence select the replacement runtime for ordinary builds.

Use the checked-in lifecycle runner to retain repeated direct-process evidence:

```sh
build/neural-runtime/ort-build-env/bin/python tools/neural_runtime/check_process_lifecycle.py \
  --output build/neural-runtime/lifecycle-candidate-01 --attempts 10 --timeout 300 \
  -- /absolute/path/to/qualified-environment/bin/python /absolute/path/to/inference-check.py
```

The command above is a template, not a completed qualification run. Select a
diagnostic that validates numerical outputs itself, then repeat it with the exact
candidate runtime. Every attempt retains stdout/stderr files, hashes, exit status,
timeout/launch failures and elapsed time. All requested attempts must exit zero;
successful JSON followed by a failing process exit remains a failure. Existing
output directories are refused. `releaseEligible` is always false.

This runner executes trusted local commands without a shell. Logs are streamed to
disk but not size-limited; select bounded diagnostics and sufficient disk space.
Timeout terminates the direct child, not an arbitrary descendant tree. Use direct
inference checks for lifecycle evidence; production worker supervision remains a
separate product obligation. An interrupted runner is not a completed study.

Vocoder integration, production worker/render integration, singing quality and
the rest of Full-Scope Beta GO remain separate obligations.
