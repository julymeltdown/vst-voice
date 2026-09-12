# Neural phonetic conditioning — contract groundwork

The current worker request v1 transmits F0/dynamics and a pronunciation hash,
but no phonetic sequence. A hash cannot condition a model's pronunciation. This
is a substantive U37 integration gap, separate from the absent qualified U36 model.

`PhoneticConditioning` now represents a vocabulary hash and bounded token spans.
Each span contains a uint32 token ID and uint64 start/end in output PCM frames
(not feature hops). The sequence must cover the entire output in order, without
gaps, overlap or empty spans. Silence must be represented using the caller's
explicit vocabulary token; validation never guesses missing phones.

Limits: at most 4,096 spans, vocabulary size 1–65,536, token IDs strictly below
that declared size, and frame count within both protocol and model budgets.
The vocabulary hash must be canonical lowercase SHA-256. Model validation binds
it to the expected model vocabulary hash. The declared vocabulary size is not
proof that vocabulary/model files were actually loaded or authenticated.

Tests cover valid explicit spans, gaps, overlap, empty/oversized spans, incomplete
coverage, invalid token IDs, invalid vocabulary dimensions and model hash/budget
mismatch. The subsequent wire integration is described below. A score-to-vocabulary
adapter, real inference and qualified model remain required.

No model training, download, source-rights approval or neural singing completion
is claimed. Full Beta GO continues to require the complete original scope.

Verification: Release build and neural-protocol/core suites passed 2/2 in
22.73 seconds. Changes remain local/uncommitted with existing work preserved.

## Request metadata v2

Requests with phonetic conditioning use kind `seam-neural-request-v2` and feature
kind `f0-dynamics-phonemes`. They add `vocabularyHash`, `vocabularySize` and
`phonemes` to the strict v1 field set. Each phoneme row has exactly `tokenId`,
`startFrame`, `endFrame`. Existing little-endian framing version 1 and the F0 then
dynamics Float32 payload layout are retained; framing and request-metadata
versions are separate. Unconditioned fixtures retain exact v1 field semantics.

The decoder bounds metadata bytes, nesting, nodes and span count, validates the
conditioning before allocating float feature arrays, and rejects mismatched
kind/feature fields or v2-to-v1 relabeling. A stray vocabulary size without
conditioning is invalid. Model request validation now checks the conditioning's
vocabulary hash and output budget automatically.

The subprocess regression sends a conditioned request through the existing
first-party probe helper. This proves framing and helper transport, not phonetic
inference: the probe is still synthetic test code, not a model/vocoder. Response
binding to the complete conditioning identity and actual model execution remain
further work. No neural-singer readiness is implied.
Final Release build and neural-protocol/core suites passed 2/2 in 24.17 seconds,
including v2 round-trip, malformed/downgraded frame rejection and subprocess
transport. Existing work remains uncommitted and preserved.

## Complete request binding in responses

Response metadata v2 adds `requestContentHash`, the lowercase SHA-256 digest of
the complete canonical request frame, including its header, metadata and both
Float32 feature arrays. Existing unconditioned v1 response shape remains supported.
The runner requires a bound response for a conditioned request and compares any
supplied binding against the exact bytes it sent. Request ID, model hash, clock
and output-shape checks remain independently enforced.

The probe helper echoes the received-frame digest for conditioned requests.
Fixture-only request IDs 44/45 deliberately emit a wrong/missing binding to test
runner rejection. Tests cover response-v2 round-trip, invalid digest rejection,
changed phoneme/F0 request bytes and real subprocess rejection of both faulty
responses. A matching digest proves correlation, not that a neural model actually
used the phonetic features; the helper still produces synthetic fixture output.

Release build and neural-protocol/core suites passed 2/2 in 21.97 seconds. The
score adapter, verified model/vocabulary loading, actual neural inference and
qualified model delivery remain unfinished. This completes the response-binding
gap noted above, not U37 or Beta GO as a whole.

## Verified vocabulary loading

`NeuralVocabulary` decodes bounded JSON with exactly `formatId`, `schemaVersion`
and `tokens`. Format is `com.project-seam.neural-vocabulary`, schema 1. Token
array order defines IDs starting at zero; no sorting, case folding, normalization
or unknown-phone substitution is performed. Tokens must be nonempty bounded UTF-8
without controls and must be unique. Vocabulary size is 1–65,536; the JSON byte
ceiling is 4 MiB, with bounded nesting/nodes/collections.

The loader hashes the exact bytes and requires agreement with the model contract
before building the immutable lookup. Even whitespace changes require a new
expected digest. Conditioned helper execution now requires this verified object
and checks the request's declared token count against its actual size. Missing
vocabulary and false size claims fail before helper launch. This supersedes the
earlier caller-declared-size-only boundary; it still does not verify model weights
or prove that a real model consumes this vocabulary.

Tests cover token ordering, Japanese tokens, exact-byte changes, duplicate/empty/
non-string/control tokens, unknown lookups, oversize input, missing verified
vocabulary and wrong request counts. A valid vocabulary-backed request still
passes the probe subprocess and complete-response-binding checks.
Final Release build and neural-protocol/core suites passed 2/2 in 22.25 seconds.
Score conditioning, model/vocoder loading and real neural singing remain unfinished.

## Compiled-score phonetic adapter

The verified vocabulary now maps domain phoneme tokens and compiled timing
anchors into request-relative token spans. Nuclei use their owned nucleus anchor;
consonants require explicit or compiler-inferred starts. Implicit onset ends
require a valid same-note nucleus binding. Key duplication, voicing mismatch,
unknown vocabulary entries, unresolved starts, overlap and output-window clipping
are rejected rather than silently repaired.

The caller supplies the exact output origin/end and a vocabulary token for
silence. Uncovered intervals are filled only with that explicitly selected token;
no implicit silence ID or unknown-phone substitution exists. Final contiguous
coverage and span budgets are checked again.

A real compiled `z a` timing fixture verifies exact mapped spans, explicit leading
and trailing silence, and rejection of unknown phones, unresolved timing,
overlap, missing silence tokens and clipped ownership. Release build and
neural-protocol/core suites passed 2/2 in 24.17 seconds. This connects phonetic
conditioning to score timing; complete request feature construction, model/vocoder
loading and actual qualified inference remain unfinished.

## Compiled-performance request construction

`prepareNeuralScoreRequest` combines the phonetic adapter, verified vocabulary,
model identity and compiled performance into a complete request-v2 object. It
checks clock/output budgets before feature allocation and polls cancellation
during frame evaluation. Compiled frequency already includes pitch/vibrato;
the builder does not add vibrato twice. Dynamics include compiled articulation
gain. Unvoiced phone frames have zero F0 but retain their gain; explicitly padded
silence has zero F0 and gain. Final request/model validation remains authoritative.

The regression builds a real compiled score with leading/trailing silence,
checks voiced and unvoiced feature values, round-trips the encoded request, and
sends it through the verified-vocabulary probe subprocess with response binding.
The probe does not synthesize speech or singing. Production model/vocoder loading,
actual inference, complete backend capability handling and singer qualification
remain unfinished; this request builder is not U37 or Beta GO acceptance.
Final Release build and neural-protocol/core suites passed 2/2 in 22.15 seconds.

## Syllable timing order

Conditioning now rejects an onset ending after its associated nucleus and a
coda starting before it, even when the resulting intervals would be non-overlapping
after sorting. The regression uses deliberately inconsistent anchors to exercise
both failures and a genuinely compiled vowel–coda sequence to preserve valid
behavior. This validates conditioning order, not learned neural timing quality.
Release build and neural-protocol/core suites passed 2/2 in 42.11 seconds.

## Owning-note context outside note boundaries

Explicit preutterance and release phones now sample their owning note's boundary
pitch and dynamics when their resolved span extends beyond that note. They do
not silently become zero features or inherit an adjacent note's controls. The
builder clamps the feature-sampling position to the owning note, verifies note
identity, and omits the closed articulation envelope only outside that note.
Inside-note articulation is unchanged. Unvoiced phones still have zero F0;
actual padding silence still has zero F0 and dynamics. Extensions exceeding two
seconds on either side are rejected as unsupported. This is an explicit bounded
conditioning policy, not a claim of acoustically qualified transitions.

The compiled regression covers a delayed note, preutterance, post-note coda,
boundary dynamics, and leading/trailing silence. Its region must contain the
extended coda; the fixture uses 1,920 ticks. The final neural-protocol/core rerun
passed 2/2 in 21.28 seconds (9 neural and 865 core cases). An earlier disk-full
run is invalid verification evidence. The next run passed neural tests but
failed the Japanese-reading job's Ready assertion; the final unchanged-source
rerun passed. That intermittent helper failure is not established as fixed.
No current whole-suite or actual neural-inference acceptance is claimed.

## Helper executable identity preflight

`NeuralWorkerRunOptions` now requires a canonical lowercase SHA-256 expected
helper digest. The deployment owner must supply it; a bank-selected digest does
not establish trust. The runner validates the request, encodes it, hashes the
selected absolute executable, and rejects a mismatch before process launch.
The configurable executable size ceiling must be positive and at most 256 MiB.
The shared file-hashing routine rejects final-component symlinks and non-regular
files and checks size before reading. Cancellation is checked before preflight
and again after hashing; hashing itself is synchronous off the audio callback.

Tests exercise correct digest/probe execution, absent and noncanonical digests,
wrong identity, a one-byte size ceiling, zero ceiling, and pre-cancellation.
Existing conditioned-response binding failures still run with a valid helper
digest so they continue testing response validation rather than failing early.
Release build and neural-protocol/core suites passed 2/2 in 21.75 seconds.

Remaining U37 work includes trusted module-anchored package manifest resolution,
helper build/protocol and runtime dependency closure, model/vocoder loading and
real inference integration. Preflight hashing alone does not eliminate a
concurrent replacement between file inspection and executable launch, nor a
concurrent file-growth race during hashing. It is not a sandbox, signature check,
or installed-surface qualification. Tests compute the local probe's digest only
as fixture setup; production must not self-authorize an arbitrary executable by
hashing it and treating that result as a trusted expected identity.

## Typed package-relative resolution

`resolveNeuralHelperPackage` accepts an absolute package root, the absolute loaded
SEAM module path, a typed `NeuralHelperPackage` deployment entry and the expected
surface build ID. Build IDs must match, protocol version must be 1 (the SNW1
transport), and at most 64 runtime dependency entries are allowed. The module,
helper and every declared dependency have bounded file sizes and exact digests.

All entry paths are relative, without dot/traversal components; resolved paths
must equal their canonical package-relative location. Redirected/symlink paths,
missing files and duplicate paths fail. The declared module must resolve to the
supplied loaded module. The resolver neither reads PATH nor derives a location
from the process working directory or host executable. Success returns runner
options containing the verified helper path, expected digest and size budget.

Filesystem fixtures exercise module/build/protocol mismatch, absolute/traversal/
missing paths, duplicate and excessive dependencies, dependency digest mismatch,
changed dependency bytes, cancellation, and POSIX symlink redirection. Release
build and neural-protocol/core suites pass 2/2 in 21.98 seconds.

This API does not yet discover the loaded module or parse/authenticate the
release manifest; native surface callers and packaging still need integration.
The supplied dependency list is checked, not proven complete against binary
imports. Dependencies are checked at resolution time, not atomically with launch;
replacement races and dynamic-loader search control remain unresolved. A fake
module/runtime file in this fixture proves validation mechanics, not a loadable
or installed neural runtime. No U37 completion or Beta GO qualification is claimed.

## Digest-bound package manifest decoding

`NeuralHelperPackage::decode(json, expectedContentHash)` now bridges serialized
deployment metadata to the typed resolver. It checks the exact raw-byte SHA-256
against the supplied expected digest before parsing. The expected digest must
come from a trusted deployment identity, not from the manifest itself or a bank.

Schema 1 has exactly seven root fields: `formatId` (the literal
`com.project-seam.neural-helper-package`), `schemaVersion`, `buildId`,
`protocolVersion`, `module`, `helper`, and `dependencies`. Each file object has
exactly `path`, `sha256`, and `maximumBytes`. Protocol version 1 denotes SNW1
transport, not request metadata v1. Paths are portable UTF-8 slash-separated
relative names: empty, control-bearing, drive/ADS, backslash, dot/traversal,
duplicate, and doubled-separator paths are rejected. Canonical filesystem
containment and actual file digests are subsequently checked by the resolver.

Parsing is limited to 256 KiB, depth 4, 512 nodes, strings of 4,096 bytes, and
collections of 64 entries. Build IDs are nonempty valid UTF-8 of at most 256 bytes;
file digests are canonical lowercase SHA-256 and file ceilings are 1 byte–256 MiB.
Unknown fields and noninteger versions/sizes are rejected.

The filesystem fixture now decodes its manifest before resolving package files.
It also rejects changed raw bytes, wrong versions, invalid paths including NUL,
duplicate entries, invalid size ceilings, unknown fields, and oversized input.
Release build and neural-protocol/core suites passed 2/2 in 22.69 seconds after
correcting initial C++ API compatibility errors, without disabling warnings.

This is digest binding, not signature authentication. Trusted expected-digest
delivery, actual loaded-module discovery, release materializer output and native
surface integration remain unfinished, as do the previously documented runtime
loading and replacement-race boundaries. No neural singer or Beta GO acceptance
is inferred from manifest tests.

## Loaded-module address discovery

`platform::loadedModulePath` resolves a stable static-data address using `dladdr`
on POSIX or address-based `GetModuleHandleExW` plus `GetModuleFileNameW` on Windows.
It requires an absolute loader path and a canonical existing regular file; it
does not reinterpret relative loader names using the current directory.
`resolveNeuralHelperForModule` derives the package root from that module and the
manifest's module-relative path, then applies the existing package checks.
The surface must provide an anchor defined in its own binary, not in a host or
shared utility DLL. The module must remain loaded during resolution.

A real-binary test resolves the current test executable, validates it and the
sibling helper against fixture digests, then launches the resolved probe through
the normal runner. Wrong module entries and null addresses fail. Release build
and neural-protocol/core suites pass 2/2 in 22.90 seconds. Additionally, the
standalone neural test executable passed all 11 cases from `/private/tmp` with
`PATH=/nonexistent`, including address-based resolution and subprocess exchange.

This is current macOS executable evidence. Windows code is not runtime-verified;
installed CLAP/VST3/AUv2 surface anchors, materialized trusted manifests, runtime
dependency loading and real model inference remain unfinished. Fixture-computed
digests are not production trust roots, and the probe is not a neural singer.

## Payload-side manifest generation and native compatibility

`tools.phase13a.neural_package.build_neural_package_manifest` now generates
deterministic schema-1 bytes and their SHA-256 from actual module/helper/declared
dependency files. It is read-only: it returns bytes without publishing a manifest
or modifying a release identity. Relative path, build, file-size and dependency
limits mirror the native contract. Hashing streams bounded chunks, rejects growth
beyond the limit, and compares file identity/size/timestamps before and after each
read. This detects ordinary concurrent mutation, not an atomic package snapshot.

The tests verify deterministic output, dependency-byte changes, missing/unsafe/
duplicate paths, directories, build limits and symlink rejection where supported.
An explicitly supplied native probe accepts Python-generated bytes through the
C++ decoder and rejects a raw-byte modification against the original digest.
The new `seam_neural_package_materialization_tests` CTest supplies that probe, so
the cross-language check is not skipped in this target. All six Python tests
passed; neural/core CTest suites passed 2/2 in 21.30 seconds, and the registered
packaging CTest passed 1/1 in 0.09 seconds after CMake regeneration.

Release sealing does not yet call this builder. It must do so for each required
surface at the correct finalized-byte/signing stage and bind the resulting
manifest digest into the trusted external release identity without a circular
binary/manifest hash dependency. Declared dependencies still require independent
binary-closure verification. No fake helper is substituted into production and
no release eligibility is granted by the builder or fixture.

## Sealed release inventory integration

Release assembly and verification now call `neural_package_inventory` for every
singing surface (excluding the installer verifier). The sealed payload manifest
contains `neuralPackages` with surface, manifest path, and either `MISSING` or
`VERIFIED_FILES`; verified rows also carry the exact manifest digest. A present
manifest must match canonical builder output for the current file bytes, the
surface module path, and the release build ID. Unknown/stale/malformed metadata
cannot be sealed merely because its enclosing payload tree has a valid hash.
The verifier recomputes the inventory and compares it to the sealed rows.

Bundle manifests reside at `Contents/Resources/neural-helper-package.json` under
the surface bundle. Windows flat standalone and CLAP surfaces use `Resources`
and `ProjectSEAMEditor.resources`, respectively, beside their surface binaries.
These are packaging conventions; installed native manifest retrieval still needs
to be wired to the same paths and a trusted external manifest digest.

Legacy development payloads can explicitly report missing neural packages and
remain `releaseEligible: false`. Neither inventory state means qualified neural
inference, complete dependency closure, or Beta GO. Actual full-scope release
acceptance must require qualified neural deployment on all required surfaces;
this change does not waive that requirement. Older sealed payload manifests
without the inventory require reassembly rather than silent acceptance.

Tests cover explicit absence, status forgery, a verified standalone fixture, and
changed helper bytes rejected by both verification and reassembly. All 138
phase13a tests passed in 8.201 seconds with the native probe enabled (no skips).
Release build/CMake regeneration passed; the expanded packaging CTest passed
1/1 in 1.98 seconds. The prior native neural/core result is unchanged, not rerun
or presented as a new whole-suite qualification in this Python-only integration.

## Native manifest loading from a surface descriptor

`loadNeuralHelperForModule` now joins address-based module discovery, bounded
manifest reading, exact manifest digest validation and package file verification.
The caller supplies module-relative and manifest-relative paths, expected build
ID and expected manifest digest from its trusted deployment descriptor. The
loader derives the root from the loaded module, rejects redirected manifest
paths, reads at most 256 KiB and requires the decoded module entry to match the
descriptor before resolving helper/dependency files. Cancellation is checked
before loading, after reading and through package validation.

The subprocess fixture copies the native probe into a temporary bundle layout
with a sibling helper and generated manifest. From an unrelated directory with
PATH unavailable, the running module successfully loads its own manifest; wrong
module paths, traversal, manifest digest/build mismatch, modified manifest bytes
and changed dependency bytes are rejected. The interrupted verification run was
recovered from its completed CTest log: neural, packaging and core suites passed,
including 11 neural cases, 28 Python packaging cases and 867 core cases.

This completes a callable native loader path, not the delivery of authenticated
surface descriptors or actual neural inference. Fixture executables and runtime
bytes do not qualify installed plugin surfaces or a model/vocoder. Windows runtime
verification, library-loader control and replacement-race boundaries remain open.
