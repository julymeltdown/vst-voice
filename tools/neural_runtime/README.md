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
The native probe now reads each fixture once into bounded owned bytes (16 MiB
per graph) and constructs sessions from those bytes. Before inference it checks
actual session input/output names, float32 element types and exact `[1,4]`
fixture shapes. The test also rejects swapped graphs, wrong rank/dimensions,
dynamic dimensions, double tensors, empty files and oversized files. These are
fixture-specific contracts, not the eventual DiffSinger tensor contract.

Session introspection happens after ORT parses the graph; it does not provide
the required pre-session protobuf/operator/resource admission. In-memory input
alone does not prove external tensor references are safe or absent. Keep using
only application-generated trusted fixtures with this experiment.
Model feature/schema admission, external tensor restrictions, worker protocol
integration, learned weights and installed distribution remain separate work.

## Offline graph intake

`inspect_graph.py GRAPH.onnx` parses bounded bytes directly, never resolves
external tensor files, rejects external references throughout nested protobuf
messages, and rejects custom domains, unknown operators, local functions and
training graphs. It checks standard ONNX structural validity and reports the
exact byte SHA-256, operator revision and actual tensor interfaces. Initial
limits are 256 MiB input, 200,000 protobuf messages, depth 64, tensor rank 8 and
64 million elements per initializer. Supported IR/opset ranges are explicit in
the tool; unsupported exports require a reviewed extension, not a bypass.

```sh
build/neural-runtime/fixture-env/bin/python tools/neural_runtime/test_inspect_graph.py
build/neural-runtime/fixture-env/bin/python tools/neural_runtime/inspect_graph.py GRAPH.onnx
```

This is an offline intake diagnostic, not an admission certificate. Standard
operators can still be computationally expensive; symbolic interface dimensions
are reported rather than bounded for execution. Model-family contracts,
aggregate execution budgets, parser-process resource supervision, feature
compatibility and production child-side re-admission remain required. The
Python input byte cap is not a resident-memory ceiling. The native probe does
not itself invoke this inspector; the fixture runner inspects its generated
graphs before the positive inference runs.

### Proposed paired export profile

`convert_vocabulary.py EXPORTED.phonemes.json` converts the pinned exporter's
phone-to-ID map to SEAM vocabulary v2 on stdout, preserving every positive ID
and merged alias. Padding is reserved at zero. Gaps, duplicate JSON keys,
invalid token names and invalid IDs are rejected; the converter never renumbers
or modifies the source file. Canonical names within alias groups are chosen
deterministically without changing lookup IDs. This is vocabulary conversion,
not language-ID conditioning, acoustic model admission or rights approval.

Native bank-preparation command (no Python or ONNX Runtime required):

```text
seam_voicebank_cli convert-neural-vocabulary SOURCE_JSON SOURCE_SHA256 NEW_OUTPUT_JSON
```

The command verifies the exact source digest before conversion, publishes a
new output file without overwriting an existing one, and reports source/output
hashes with status `CONVERTED_UNAPPROVED`. It does not install a bank, approve
source rights or claim that a model is executable.

`seam_voicebank_cli inspect-neural-bundle DIRECTORY MODEL_ID MODEL_VERSION MANIFEST_SHA256 MAX_PAYLOAD_BYTES`
loads declared assets and inspects metadata without executing graphs. It reports
`METADATA_INSPECTED_ONLY`, hashes, vocabulary size, clock and configuration
version, with `executionAdmitted` and `releaseEligible` both false. Use an
explicit payload budget no greater than 512 MiB; this is not an RSS limit.

`seam_voicebank_cli prepare-neural-bundle DIRECTORY MODEL_ID MODEL_VERSION MAX_PAYLOAD_BYTES`
prepares a directory containing four regular files named `acoustic`, `vocoder`,
`vocabulary`, `configuration`. It validates frozen metadata, creates a new
canonical `manifest.json`, then reloads declared bytes for verification. It
reports `DATA_BUNDLE_PREPARED_UNAPPROVED`, not executable admission. Existing
manifests are never replaced. Keep assets unchanged during preparation; if the
post-publication reload fails, the new manifest remains for diagnosis. This is
not a multi-file transaction or graph-format/rights approval.

The Python converter matches native `convertDiffSingerVocabulary` serialization
(two-space indentation and final newline). Verify byte parity with:
`build/neural-runtime/fixture-env/bin/python tools/neural_runtime/check_vocabulary_parity.py build/release/seam_neural_worker_probe`.
Earlier compact Python conversions have different hashes; do not reuse their
manifests when regenerating vocabulary bytes.

Configuration v3 adds required `vocoderOutput` (`audio`/`waveform`), binding the
actual graph output name in both offline inspection and native paired execution.
Earlier configurations retain legacy audio semantics. This supersedes the
pending output-name binding described in older source-review notes below.

Exporter source inspected at openvpi/DiffSinger revision
`336cf01b57f2ad44c6b37a79cf33993043291759`: NSF-HiFiGAN exports `waveform`.
Pair inspection accepts explicit `vocoder_output="audio"` (legacy default) or
`"waveform"` and binds the name. Native trusted-fixture execution supports both;
the vector1 dynamic case now uses `waveform`. Bundle output-name configuration
binding remains pending; its wrapper still defaults to `audio`. Neither the
source checkout nor this interface match supplies trained weights or their rights.

Bundle configuration version 2 now binds `stepsLayout` (`scalar`/`vector1`)
and requires `fftSize`, `windowSize`, `melFrequencyScale` (`slaney`/`htk`) in
each feature declaration. Native and offline readers enforce matching values;
offline bundle inspection passes the configured steps rank to pair inspection.
Version 1 is retained as legacy metadata with unspecified spectral fields.
This supersedes the missing-field limitation recorded in the source review
below; declarations alone still do not prove the graph implements them.

Source correction: OpenUtau checkout `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`
uses int64 `[1]` steps for continuous acceleration. `inspect_pair` now takes
explicit `steps_layout="scalar"` (legacy fixture default) or `"vector1"` and
binds the choice in its contract hash. Native fixture execution checks and uses
the actual input rank; both layouts are exercised. This does not yet extend
bundle configuration or support speedup/depth/variance conditioning. FFT size,
window size and mel-frequency scale also remain missing configuration fields
identified by this source review; current compatibility is incomplete.

`inspect_pair.inspect_pair` consumes actual acoustic/vocoder bytes and explicit
`bins`, `layout`, `hop_size`, and `maximum_sample_frames` parameters. It invokes
graph inspection itself; caller-supplied inspection reports are not trusted.
The proposed `seam-acoustic-vocoder-v1` profile requires:

| Graph | Inputs | Output |
|---|---|---|
| Acoustic | int64 tokens/durations `[1,N]`; float32 f0 `[1,T]`; int64 scalar steps | float32 mel `[1,T,F]` or `[1,F,T]` |
| Vocoder | float32 mel in the same layout; float32 f0 `[1,T]` | float32 audio `[1,S]` |

Axis symbols must agree within each graph; their spellings need not match
across files. Extra conditions are rejected rather than silently dropped.
The result binds both graph hashes and supplied profile parameters, including
the maximum mel-frame/element budget, but sets `executionAdmitted` to false.
This is a proposed export adapter target, not a discovered universal DiffSinger
interface. No export adapter or learned-model compatibility is claimed yet.

Run `test_inspect_pair.py` with the pinned fixture interpreter. Its constant
graphs are structural fixtures only. Actual graph outputs, hop-to-sample
relationship, nonzero conditioning response, intermediate allocations, mel
feature scale/range, vocabulary, speaker/style mappings and immutable bundle
configuration binding still require production validation. Static declarations
alone cannot establish these properties.

### Native paired-profile execution experiment

The complete trusted-fixture disk handoff can be run with:

```sh
build/neural-runtime/fixture-env/bin/python tools/neural_runtime/check_bundle_runtime.py build/release/seam_voicebank_cli build/release/seam_onnx_runtime_probe
```

This uses CLI vocabulary conversion and manifest preparation, offline bundle
inspection, then `--paired-bundle DIRECTORY MODEL_ID VERSION MANIFEST_SHA256`
in a separate native process. That process reloads and freezes actual bytes
before constructing sessions. A same-length vocoder mutation after inspection
is rejected on child loading. The mode is restricted by documentation to
trusted arithmetic fixtures; it is not the signed production worker protocol,
pre-session graph admission or an arbitrary-bank execution command.

The native paired experiment now authors v2 metadata. Scalar steps are the
default; `--steps-vector1` explicitly declares `[1]`. The actual graph rank must
match that frozen declaration before `Run`. The paired test executes both
layouts and rejects the opposite declaration for the same graph. This replaces
the earlier rank-selection experiment and v1 fixture configuration; it is not
production admission or proof of FFT/mel numerical semantics in learned graphs.

Offline `inspect_bundle.inspect_bundle` accepts manifest bytes, a name-to-bytes
asset map and the expected manifest digest. It verifies immutable asset lengths
and hashes, exact closure and the four required roles, validates configuration
and vocabulary, and derives pair inspection parameters from those configuration
bytes. Extra variance/tensor roles are explicitly unsupported in this initial
paired profile, not silently ignored. No filesystem or executable is selected.
Its result remains `executionAdmitted: false`: production C++/Python parity,
bounded parser supervision and child-side enforcement are not established.
JSON input is byte-capped, not a hard process-memory bound. Run all offline
inspection tests with `python -m unittest discover -s tools/neural_runtime -p
'test_inspect*.py'` using the pinned fixture interpreter.

```sh
cmake --build build/release --target seam_onnx_runtime_probe
build/neural-runtime/fixture-env/bin/python tools/neural_runtime/check_paired_runtime.py build/release/seam_onnx_runtime_probe
```

The `--paired-profile` native mode runs four acoustic inputs through a real ORT
session using graph bytes owned by an actual `FrozenNeuralBundle`. The probe
freezes both graphs and its application-authored fixture configuration/vocabulary;
native metadata inspection supplies the model/vocabulary and real manifest
digest used in requests. This replaces the former placeholder model identity.
It then transfers tensors between sessions as described below. This is still
trusted-fixture execution, not pre-session admission of arbitrary banks.

The acoustic
session transfers its float32 `[1,T,80]` mel tensor to a second session alongside
f0, and verifies finite audio of shape `[1,T*256]`. Two sequence lengths (3 and
5 mel frames) run through the same sessions. Generated arithmetic graphs make
output values depend on tokens, durations, steps, acoustic f0 and vocoder f0;
the native expected-value check verifies this controlled computation.

A negative fixture declares the same valid interfaces but expands by 128
instead of the claimed 256 samples per frame. Static pair inspection passes;
native output-length verification rejects it. This proves the runtime check
for these fixtures, not general graph semantic correctness. The probe now
constructs a sample-domain SEAM request, round-trips the actual request codec,
and uses `prepareDiffSingerAcousticInputs` for tensor conditioning. Both cases
have a 37-sample partial-hop tail; shared `finalizeDiffSingerAudio` validates
all padded samples, trims to the requested count and applies dynamics once.
The finalizer rejects nonfinite/out-of-range raw or gained PCM rather than
clipping; even invalid samples in the discarded tail cause rejection.
`finalizeDiffSingerResponse` wraps that final audio in a response bound to the
canonical request hash. The native paired experiment also round-trips the
response codec and verifies that this preserves the once-applied gain.
This same path is now executed by the production worker for an admitted bundle;
it is not yet connected to the normal song backend.
The graph weights are arithmetic constants, not
learned voice weights. Cancellation through the render coordinator, bounded
runtime allocations beyond the process ceilings, real model inference and
musical evaluation remain open.
## Production worker

`check_production_render.py` runs as the `seam_neural_production_render` CTest. It
prepares a real ONNX bundle with the CLI, collects the phones one deterministic phrase
actually needs, and then drives the authoring render path with the shipped worker
instead of the transport probe. The check passes only when the published render names
`seam.neural-worker.v1`, reports the admitted bundle identity and contains non-silent
finite audio, so routing without execution can no longer pass as coverage. The graphs
are arithmetic constants: this proves that the worker executed inside the render path,
not that a voice was produced.

`apps/seam-neural-worker/main.cpp` builds as `seam_neural_worker` only when
`SEAM_ONNXRUNTIME_ROOT` and `SEAM_NATIVE_ONNX_SCHEMA` are both set, because it
must refuse any bundle it cannot inspect structurally. The application selects it
through the bundle launch contract:

    seam_neural_worker --seam-neural-worker-v3 BUNDLE_DIR MODEL_ID MODEL_VERSION \
        BUNDLE_CONTENT_HASH MAXIMUM_BUNDLE_BYTES INFERENCE_STEPS

One SNW1 request frame arrives on stdin and exactly one SNW1 response frame leaves
on stdout. `INFERENCE_STEPS` is the trusted admitted render snapshot's 1..1000
selection; protocol-3 launches without it fail. It reaches the acoustic ONNX
`steps` tensor, and the response backend identity records the executed value.
Protocol 3 deliberately supersedes the six-field v2 launch. Existing v2
packages remain parseable for diagnostics but cannot be selected for a render;
restage the new worker and bind its package digest in a v3 signed deployment.
The child reads the bounded frame, re-loads the bundle directory itself
so changed bytes fail their manifest or asset digest, requires the request's
bundle, model and vocabulary identity to match what it loaded, parses both graphs
with the native inspector and the frozen pair contract, and only then creates
sessions. Loaded interfaces are cross-checked against the admitted declaration
before any tensor runs. It never serves `--seam-neural-worker-v1`, so the
transport fixture cannot satisfy the production launch by accident.

Exit codes are distinct: 2 wrong contract or usage, 3 invalid byte budget,
4 bundle load or digest failure, 5 metadata inspection failure, 6 unreadable
request frame, 7 malformed or mismatched request identity, 8 admission or
inference failure (including nonfinite and out-of-range PCM), 9 response encoding
failure, 10 stdout write failure. Diagnostics go to stderr and never to stdout.

`check_production_worker.py` runs as the `seam_neural_production_worker` CTest. It
prepares a real ONNX bundle with the CLI, drives the worker through the contract,
and asserts exact response binding, sample count, once-applied dynamics and
arithmetic output. It also asserts empty stdout for the legacy contract, an
invalid budget, a wrong manifest digest, a wrong launch identity, changed bytes,
an inadmissible graph bundle, wrong model/vocabulary/sample-rate/frame identity,
malformed frames and an out-of-range gain. The fixture graphs remain arithmetic
constants, so none of this qualifies a learned singer. Diffusion steps are a
pinned constant in the worker until the admitted configuration schema carries
them, and packaging the worker with its runtime is still open.

The optional pinned-model command described in
`tools/voice_model_training/README.md` goes beyond these arithmetic CTests: it
composes actual exported DiffSinger acoustic and locally trained vocoder graphs,
then runs the conditioned request through two fresh production-worker processes.
Because diffusion sampling uses random noise, it checks each response's identity,
shape, finite range and non-silence, and reports inter-run PCM differences; it
does not require byte-identical renders. It also routes the composed bundle through
the native authoring renderer using a saved project and requires an exported WAV.
Its training corpus is synthetic and the result remains explicitly unqualified.
# External request experiment

For repeatable optional integration checks, configure `SEAM_NATIVE_ONNX_PYTHON`
with the absolute path to the existing fixture environment's Python executable.
Configuration verifies ONNX 1.19.1 is importable; it never installs dependencies.
Then run `cmake --build build/release --target seam_neural_native_checks -j 4`.
This builds required binaries and runs the five `neural-native-experiment` CTests:
owned bytes, native structural/pair inspection, paired runtime, bundle runtime and
the production worker.
Default builds without native inspection retain their existing test set.

Set root CMake `SEAM_NATIVE_ONNX_SCHEMA` to the pinned local `onnx.proto` path
alongside `SEAM_ONNXRUNTIME_ROOT` to enable native checks in the bundle runtime
probe. Both frozen graph byte buffers are inspected and pair-checked before
either ORT session is created; those same buffers are then passed to ORT.
The default empty option adds no parser dependency. With it enabled, pass
`--native-inspection` to `check_bundle_runtime.py` and `check_paired_runtime.py`.
The former verifies a correctly hashed bundle containing an unknown operator
is rejected before inference. This is still an optional fixture experiment,
not a production worker, execution-family policy or release admission.

Offline graph reports include a sorted operator/count inventory and combined
declared tensor storage (`declaredTensorBytes`). Intake caps that storage at
512 MiB across initializers and attribute tensors, checks static interface
dimension products, and accepts only the declared numeric/bool tensor types.
These bounds do not account for runtime intermediates, dynamic dimensions,
allocator overhead, or parser RSS. The operator inventory is not an allowlist
and does not admit a graph for production execution.

The fixture probe accepts legacy conditioned metadata v2 and bundle-conditioned
v3. V3 explicitly binds `bundleContentHash` to the frozen manifest/model identity
and returns response v3 with both bundle and canonical request hashes. SNW1 binary
framing stays version 1. The ordinary legacy worker launcher deliberately refuses
v3 until the distinct production v2 launch/deployment contract is implemented.

`seam_onnx_runtime_probe --paired-request DIRECTORY MODEL_ID VERSION MANIFEST_SHA256`
accepts one bounded SNW1 request on stdin (EOF required) and emits a binary
request-bound response on stdout. It reloads the frozen bundle and validates
request identity and vocabulary/timing preparation before creating graph sessions.
This remains a trusted arithmetic-fixture experiment, not production graph
admission, a signed worker, or evidence of learned singing quality.
`check_bundle_runtime.py` exercises pitch/dynamics changes, exact output length,
PCM values, canonical request hashes, malformed frames, wrong model identity,
and changed bundle bytes. Runtime cancellation and production isolation remain open.
