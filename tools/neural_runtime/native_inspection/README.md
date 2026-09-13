# Native ONNX parser experiment

This standalone development target uses local Protobuf **33.4.0** and the
ONNX **1.19.1** sources from the fixture environment. CMake verifies the core schema
SHA-256 and generates build-local core/data/operator schemas with `LITE_RUNTIME`
changed to `SPEED` for reflection. The data schema's ML import is redirected to
the non-ML core schema consistently with this build. Original files are untouched.
Generated files and the resulting executable are not shipping dependencies.

`inspection.hpp` exposes `inspectBytes` through a reusable object-library target.
It consumes only supplied memory and replaces model/report outputs on success.
Failed inspection leaves previous outputs intact; parsed results own their data.
The command-line reader is an adapter around that API. Run
`build/neural-runtime/native-inspection/seam_native_onnx_owned_bytes_test` to
verify source-buffer independence and failure atomicity. This is not yet the
immutable prepared execution handle required by the production roadmap.

The API accepts an optional stop token and returns code 18 on observed
cancellation, without replacing outputs. Checks surround third-party parsing
and checking, occur per traversed message, and periodically during tensor scans.
Protobuf parsing and the upstream checker are not internally interruptible;
this is cooperative cancellation, not a wall-clock bound. The child supervisor
is still needed for hard deadlines. The CLI passes an empty token.

Declared protobuf text fields are capped at 4096 bytes each and 8 MiB combined
across the model tree (code 19 on violation). This includes metadata/doc strings;
raw tensor bytes follow their separate tensor policy. Reports expose `textBytes`.
These checks happen after protobuf parsing, before upstream structural checking;
they do not establish a preallocation memory limit.

From the project root:

```sh
cmake -S tools/neural_runtime/native_inspection -B build/neural-runtime/native-inspection -G Ninja -DSEAM_ONNX_SCHEMA="$PWD/build/neural-runtime/fixture-env/lib/python3.12/site-packages/onnx/onnx.proto" -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/neural-runtime/native-inspection -j 4
build/neural-runtime/fixture-env/bin/python tools/neural_runtime/check_native_inspection.py build/neural-runtime/native-inspection/seam_native_onnx_inspect
```

The parser enforces 256 MiB input and recursion depth 64 during decoding,
then traverses at most 200,000 messages. It rejects unknown protobuf fields,
custom domains/overloads, external tensor references including nested attributes,
local functions and training graphs. It does not open tensor paths or execute
operators. The post-parse message cap is **not** a preallocation/RSS limit.

Numeric/bool tensor types, rank <= 8, checked dimension products <= 64 Mi
elements, and aggregate declared tensor storage <= 512 MiB are now checked.
Every encountered ValueInfo declaration (including intermediates and nested
graphs) must declare a supported tensor type and bounded shape; named dynamic
dimensions are reported as unresolved rather than assigned runtime bounds.
This is stricter than the Python inspector's top-level interface-only check.
Raw payload lengths and typed-field element counts must match the declared
shape/type. Mixed representations and segmented tensors are rejected. Float32/64/16
tensor values must be finite; Boolean values must be 0/1; typed int8/uint8 and
float16 storage values must fit their declared widths. Raw storage uses explicit
little-endian interpretation. This policy also rejects intentional infinite mask
constants; real-model compatibility is not established. Runtime intermediate
allocations and operator attribute values are not yet checked.
Declared storage is not measured memory usage.

The upstream native ONNX checker now runs on the already-parsed in-memory model
after local rejection checks, with `full_check=false`. It rejects unknown standard
operators, unsupported attributes and invalid graph references. Its filename
loading overload is never called. This is structural checking, not an execution
operator allowlist, resource-cost proof, or full shape-inference validation.

Reports include top-level input/output names, numeric ONNX element types,
static/symbolic dimensions, IR and opset versions. JSON serialization uses
Protobuf's serializer, including escaping arbitrary valid tensor names. The
native/Python differential checks compare these interfaces, not just counts.

Pair inspection is available as:

```sh
seam_native_onnx_inspect --pair ACOUSTIC VOCODER 80 BTF vector1 waveform 256 48000
```

Arguments after graph paths are mel bins, layout, steps layout, vocoder output
name, hop size and maximum output sample frames. Both models pass native checks
before exact I/O contract validation. Token/duration, acoustic time, and vocoder
time axes must agree within their graphs; symbol spellings need not agree across
files. The mel allocation declaration is bounded. Actual operator behavior,
computed waveform length and feature scale/sample-rate agreement need further
runtime/bundle validation. Success remains `executionAdmitted=false`.

Missing: execution-family operator/attribute policy,
bundle feature compatibility, dynamic allocation limits, a prepared admission handle,
worker integration, dependency distribution/signing and Windows validation.
`NATIVE_STRUCTURE_CHECKED` is not production graph admission. The linked Homebrew
Protobuf/Abseil libraries are local experiment dependencies, not pinned shipping
artifacts; no claim of portable packaging is made.
