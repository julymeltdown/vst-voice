# Native inference experiment

This optional probe runs two actual ONNX Runtime sessions. Its arithmetic
fixtures multiply a four-element input by two and then by one quarter, and the
native executable checks the resulting samples. They are not learned acoustic
or vocoder models, do not produce singing, and do not qualify Beta GO.

The SDK is application-selected; this probe is not the model-bundle importer or
the signed first-party worker protocol. Do not use it to admit untrusted banks.

## Reproduction on the development Mac

Use the [official ONNX Runtime 1.30.0 release](https://github.com/microsoft/onnxruntime/releases/tag/v1.30.0).
The published SHA-256 for `onnxruntime-osx-arm64-1.30.0.tgz` is
`6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012`.
Verify the complete archive before extracting it to an ignored build directory.

```sh
uv venv --python /opt/homebrew/bin/python3.12 build/neural-runtime/fixture-env
uv pip install --python build/neural-runtime/fixture-env/bin/python -r tools/neural_runtime/requirements.txt
cmake -S . -B build/release -DSEAM_ONNXRUNTIME_ROOT="$PWD/build/neural-runtime/onnxruntime-osx-arm64-1.30.0"
cmake --build build/release --target seam_onnx_runtime_probe
build/neural-runtime/fixture-env/bin/python tools/neural_runtime/check_runtime.py build/release/seam_onnx_runtime_probe
```

The check uses temporary generated graphs, verifies repeated native inference,
and requires a changed vocoder scale to fail the expected-output check. The
ordinary build does not require ONNX Runtime when its root option is empty.
Model feature/schema admission, external tensor restrictions, worker protocol
integration, learned weights and installed distribution remain separate work.
