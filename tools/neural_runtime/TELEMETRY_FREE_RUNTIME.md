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

Vocoder integration, production worker/render integration, singing quality and
the rest of Full-Scope Beta GO remain separate obligations.
