# Integrated Singer Execution

## A neural singer can be chosen, listed and cleared

The project schema has stored `neuralResource` for a while, the encoder and
decoder round-trip it, and the renderer compares a prepared bundle against it —
but nothing could *set* it. The controller had no way to choose an installed
neural singer, so the selection the previous change made renderable could only
appear in a project a test wrote by hand.

`application::SetTrackNeuralResourceCommand` now mirrors the procedural-recipe
command: it records only an identity a surface already resolved, refuses to apply
or undo over a changed selection, validates a replacement before swapping it, and
reports project-audio impact scoped to its track. Resolution and bundle admission
stay where they belong, in the surface's own deployment and the installed-resource
index.

The standalone controller exposes the chooser the native surface needs:
`neuralResources()` lists what this installation can actually run — in registry
order, from the same verified index the renderer resolves through, and empty when
the surface ships no verified deployment rather than listing bundles nothing could
execute. `selectNeuralResource(id, version, contentHash)` resolves the identity
through that index *before* editing, so a selection this installation cannot admit
is never saved into a project, then executes the command and refreshes the
document and browser. `clearNeuralResource()` records the same edit with no
replacement. `platform::NeuralResourceMenuItem` carries the identity, a display
name and the selected flag, matching the shape of the existing voicebank menu
item.

Verification. `tests/test_performance_commands.cpp` adds a command case: a stale
chooser result is refused without touching the revision, a reference whose kind is
not neural is rejected, clearing and undo restore exactly the previous project, and
an unknown track is a clean failure. `tests/test_neural_selection.cpp` adds a
controller case that runs against a real installed bundle fixture: the list shows
the installed singer as unselected, an identity the index cannot resolve is
refused *before* the project changes, selection records the exact identity, undo
restores the cleared state, and clearing works through the public surface.

Not claimed. There is still no menu entry, so a user cannot yet reach selection
from the running application: the dispatcher and native menu list are the next
step, and the Windows menu implementation cannot be compiled or verified on this
machine. No real deployment is signed, no model exists, and none of this is a
listening result.

## The application selects its own neural helper

`TrackNeuralSource` was already consumed by the renderer, the render cache and the
export owner, but nothing outside tests ever constructed one: the deployment
descriptor was read only by tests and the packaging writer, and no product surface
consulted its own signed deployment. A project could therefore save a neural
selection that the product could not run, with no surface even looking for the
helper it ships.

`libs/seam-authoring-runtime` now owns `NeuralSelectionService`. One service is
created per surface. It validates the surface's declared budgets and provenance,
reads the descriptor with the same 16 KiB bound the verifier enforces, verifies the
signature against the surface's release key under the bundle launch contract
(schema 2 only), and loads the helper through the loaded-module anchor, so a bank
cannot redirect execution even if it could name a path. The package supplies the
helper path and its expected digest; the surface supplies the process budgets,
because the package format deliberately carries none; the surface also declares
the worker/runtime/provider provenance that is hashed into the render cache
identity and published. Nothing comes from a project or a voicebank.

`select()` then resolves one saved `NeuralResourceReference` through the installed
resource index, freezes and admits the bundle, cross-checks the admitted execution
identity against the saved identity, and creates the runner. It refuses an unknown
or relabelled identity, a duplicated resource root, a bundle whose configuration is
not executable (schema 1 declares no steps layout and no vocoder output name), a
cancelled request, and a non-zero anchor or descriptor that fails verification. A
saved selection this installation cannot resolve is a failure; another voice is
never substituted for it.

The standalone surface is wired to it. `StandaloneApplicationControllerConfig`
gains the optional deployment surface and an installed-resource root, so a build
that ships a signed helper selects it and a build that ships none behaves exactly
as before. Construction verifies the deployment and indexes the resource root,
which means a surface whose declared helper cannot be verified refuses to start
instead of failing later on the first note. Both export paths (`makeExportRequest`
and `exportAudio`) resolve a neural track before any bank source for that track and
fail the export with the selection error rather than exporting a different singer.

Verification: `tests/test_neural_selection.cpp` adds three cases to the new
`seam_neural_selection_tests` target — the full guarded selection path with the
refusals for a stranger's signature, a schema-1 deployment, a mismatched target or
platform, unmeasured budgets, a missing anchor and a cancelled token; the
non-executable bundle refusal; and a resource root whose duplicate identity is
ambiguous. `seam_u2_tests` adds one controller case: a surface whose deployment is
signed by nobody is refused at construction, while a controller without a neural
deployment still starts. The executing helper in these tests is the transport
fixture probe, which performs no inference.

Not claimed. No real deployment is signed and none can be: the repository's trust
roots are still marked `testOnly`, so a production surface has no release key to
verify with yet. No surface can yet *choose* a neural resource — the project schema
stores the reference, but no command or editor action sets it, and no preview or
stop/retry surface exists. No model was trained or executed, and no listening
result follows from any of this. M6 still owns the audible exit.

## The shipped helper no longer links the build machine's Protobuf

`build/release/seam_neural_worker` recorded 81 absolute `/opt/homebrew` load
paths (78 Abseil, 2 Protobuf, 1 OpenSSL), so the packaging owner refused the only
helper SEAM can actually ship. The development SDK publishes Protobuf and Abseil
as shared libraries only, and a payload that links them can never resolve on a
user's machine.

`tools/neural_runtime/build_static_protobuf.py` now builds the pinned Protobuf
33.4 release with its vendored Abseil 20250512.1 as static archives, verifies that
the install tree contains no shared library at all, and links a probe
(`static_protobuf_probe.cpp`) that reads and re-parses a `FileDescriptorProto`.
The repository's own closure reader then proves the probe resolves every
non-system reference from beside itself. A distribution build points at that
prefix with `-DSEAM_STATIC_PROTOBUF_ROOT` and at the pinned code generator with
`-DSEAM_PROTOC_EXECUTABLE`, because the runtime-only archive deliberately ships no
`protoc`; `tools/phase13a/static_protobuf.py` is the payload-facing entry point.

Three defects were found and fixed while making this work, and the first one is
worth recording because it fails in a way that looks like a broken archive:

* Abseil installs an ABI-pinned `absl/base/options.h` derived from the C++ standard
  known at configure time, while its own sources compile from the unpinned header.
  Leaving the standard to the compiler default pinned `ABSL_OPTION_USE_STD_STRING_VIEW`
  to 0 (Abseil's own `string_view` class) while the archives were built against
  `std::string_view`, so every Protobuf reference to `absl::string_view`, `CEscape`,
  `ByChar::Find` and friends was unresolved even though the archive defined those
  exact symbols. Linker flags cannot repair an ABI disagreement; both builds now fix
  `CMAKE_CXX_STANDARD=17`, and `verify_static_install()` re-reads the installed
  header and refuses a pin that disagrees with the archives.
* The probe's flattened Abseil link list was previously diagnosed as mis-ordered.
  It was not: the same undefines survived `-Wl,-all_load`, which is what pointed at
  a symbol-level ABI mismatch rather than a link-order problem.
* The builder is importable rather than only a CLI, and `--probe-only` is hermetic:
  it re-verifies an installed prefix and the probe without the pinned tarballs, the
  network or a code generator, so a configured build can re-check the SDK cheaply.

Evidence. Configuring a distribution build with the static Protobuf/Abseil SDK, the
pinned static OpenSSL 3.5.7 (`libcrypto.a`, commit
`8cf17aaeb4599f8af87fefd810b5b5fee90fe69e`) and the 1.30.0 ONNX Runtime takes
`seam_neural_worker` from **81 host load paths to zero**: `otool -L` lists only the
ONNX runtime (`@rpath`) and operating-system libraries/frameworks, and
`derive_runtime_closure()` reports **1 entry, 0 unresolved** — the one entry being
the ONNX runtime the payload ships beside the helper. The staging invariant that
previously exercised only its refusal branch now stages the real worker, which is
the end-to-end statement that matters: the helper a payload would ship is one the
packaging owner accepts.

Verification. `tests/phase13a/test_static_protobuf.py` adds 13 hermetic cases
(exact pins and digests, the pin parser, the refusals for a shared library, an ABI
pin mismatch, a missing archive, a missing CMake package and a missing prefix, and
the payload entry point's placement). CTest gains
`seam_static_protobuf_contract_tests` unconditionally and `seam_static_protobuf_closure`
whenever `SEAM_STATIC_PROTOBUF_ROOT` is set; both pass in this checkout, and the
closure test passes in a build configured with the static SDK. `THIRD_PARTY_NOTICES.md`
now records the statically linked Protobuf/Abseil pins.

Not claimed. No payload was assembled, signed or installed, the payload driver does
not yet build the neural worker at all, and no Windows closure was produced. This
removes a hard blocker for the installed payload; it is not installed-execution
evidence, and M5 still owns the nine host tuples.

## The Windows payload's runtime closure is derived from its own imports

Packaging could derive a macOS helper's runtime from its load commands but refused
the same for Windows, so a Windows payload could only be staged with a
hand-written dependency list. That asymmetry is gone.

Added `tools/phase13a/pe_linkage.py`, which reads a PE image's import directory:
the DOS header, the optional header's data directory, the real section table for
RVA-to-offset translation, and the import descriptor array up to its declared
size. Import names are returned in descriptor order, and a descriptor that names
a path instead of a module is distinguishable.

`derive_runtime_closure()` now lives in `tools/phase13a/runtime_closure.py` and
dispatches on the image container, with `macho_linkage` reduced to Mach-O parsing
and resolution. The two platforms need different rules and the module states
both: macOS resolves `@rpath` through LC_RPATH entries, so an absolute
build-directory rpath is reported unresolved; Windows records only module names
and its loader searches the running image's directory first, so a bare module name
resolves beside the helper while a path-naming descriptor does not. Resolution is
case-insensitive on Windows and follows symbolic links, so a plain-name copy of a
versioned runtime is staged under the name the import asks for.

One deliberate rule: the VC++ runtime is not treated as host-provided. Assuming it
exists is exactly the failure mode this work exists to catch, so `vcruntime140.dll`
and friends are reported unless the release owner ships them or supplies them in a
search path. Only the operating system's own modules (including `api-ms-win-*` API
sets) are skipped.

Staging and the assembly CLI use the same owner for both platforms:
`--runtime-search-path` now derives a Windows closure too, and the previous
"requires explicit dependencies" refusal for Windows is removed.

Verification: `tests/phase13a/test_neural_helper_staging.py` grows to 17 cases with
three new ones — reading a synthetic PE import directory (including the truncated
and non-PE refusals), deriving a Windows closure that stages the package's own
modules while reporting a path-referencing descriptor and a missing module, and
staging a Windows payload end-to-end from a derived closure so
`neural_package_inventory()` reports `VERIFIED_FILES`. The staging group runs 46
tests. The PE fixtures are synthetic: the Windows worker has still never been
built or executed on Windows, so this proves the analysis and the refusals, not a
running Windows helper.

## The Studio controller can plan, run, cancel and resume a generation campaign

Closed the M1.P3 item that was still open in this document: the generation
campaign existed as authoring services and CLI commands, but the native Studio
controller had no plan/run/cancel/resume path, so a singer-scale campaign could
not be driven from the product surface.

`libs/seam-native-ui/src/voicebank_studio_campaign.cpp` adds
`beginGenerationCampaignPlan()`, `beginGenerationCampaignAdvance()`,
`beginGenerationCampaignResume()`, `cancelGenerationCampaign()` and
`generationCampaignProgress()` to `VoicebankStudioController`, implemented
entirely on the same services the CLI calls (`planGenerationCampaign`,
`VerifiedGenerationCampaign::admit`, `advanceGenerationCampaign`, the production
repository). The controller never edits campaign JSON and never writes producer
state itself: planning publishes one immutable definition into a new directory,
and advancement is a loop of one-bounded-batch service calls. A second plan into
an existing directory is refused, so a stored campaign is resumed or advanced,
never replaced. Planning also refuses when the durable producer no longer matches
the `initialProducerSha256` the definition binds, which is what made the CLI's
stale-state check necessary in the first place.

Cancellation keeps the contract the plan requires. The stop is honoured before
every batch that would render or commit, committed batches stay in the
repository, the recorded campaign identity is unchanged, and the controller
reports `Cancelled` with the real campaign size so `beginGenerationCampaignResume()`
continues from the campaign's own receipts. The batch count is read from the
admitted definition before the first batch, so a cancel that lands while the
worker is starting still reports "0 of 2" rather than "0 of 0" — the first
version of this code reported the latter and the new test caught it.

`tests/test_voicebank_studio_campaign.cpp` with CTest `seam_studio_campaign_tests`
covers the three outcomes against a real producer workspace and a real recipe:
planning writes the definition and refuses to replace it (including the empty
recipe and empty take-id refusals); advancement commits two one-job batches,
adopts the recovered producer into the controller, leaves every take in
`MarkerReview` with the durable generation advanced by exactly two, and is
idempotent when repeated; and a cancelled run keeps its retained batches and
completes on resume, while a wrong campaign digest fails with `Conflict` and
`Failed` progress instead of advancing a different definition. The fixture is a
synthetic producer, recipe and audio: it proves the orchestration and its
refusals, not a useful singer or a qualified resource.

The native surface now exposes it. `studioGenerationControls()` adds a "Plan
campaign" control and a "Run campaign" / "Resume campaign" control below the
single-job controls: planning is offered whenever the producer has planned take
ids, running only once this controller recorded a campaign identity, and either
recording or a busy worker disables both. `apps/seam-voicebank-studio-native`
wires them to the platform dialogs and to `Cmd/Ctrl-Shift-C` and
`Cmd/Ctrl-Shift-Y`, with a new `FileDialogPurpose::PlanGenerationCampaign` that
names a *new* folder (save mode with directory creation on AppKit and Win32, since
the definition must live in its own directory and an existing one is never
reused). ESC already cancelled a run because campaign work shares the controller's
stop source, and the status line already reports batch progress, so no separate
progress or cancel path was needed.

Verification for the surface: `seam_studio_campaign_tests` checks the control set
(present only with a producer workspace, planning enabled for planned take ids,
running disabled until an identity exists and relabelled to "Resume campaign"
afterwards, every control disabled while recording, and non-colliding bounds down
to the 720px minimum width), and `seam_export_tests` now finds the batch and cancel
controls by id instead of by position, because the panel legitimately grew a row.

## Neural tracks can render, cache and publish through the authoring coordinator

Added the plan's `tests/test_neural_render_workflow.cpp` with CTest
`seam_neural_render_workflow_tests`, and it immediately found that the
coordinator could not render a neural track at all.

`AuthoringRenderCoordinator::preflight()` resolved a track's source and then fell
through to the sample-bank requirement for every source that was neither a recipe
file nor a procedural resource. A neural source has no voicebank reference, so an
audible neural track was refused with "Voicebank ID and version are missing"
before any render started. Preflight now handles `TrackNeuralSource` explicitly:
it requires an admitted, valid bundle and a selected runner, and it does not ask a
neural track for a sample bank. Whether the project's saved selection agrees with
the admitted bundle stays with the project renderer, which sees both. The new
failure is a typed `RenderFailureKind::NeuralSourceMissing`, mapped to the CLAP
editor's failed preview status and the `RENDER_FAILED` diagnostic code so a neural
problem is never reported as a missing voicebank.

The same workflow test then found that the neural branch of
`ProductionProjectRenderer::renderWithSources()` never consulted the PCM cache:
every neural phrase re-ran the worker and `cacheHits` stayed zero. The neural
branch now loads and stores cache entries under the prepared content identity,
which already binds the admitted bundle digest, the feature and control identity,
the provider, runtime and worker versions, the quality setting and the owned
window. A second identical submission is a cache hit, and changing the worker or
runtime is a miss, so one execution's audio can never be served for another. The
renderer identity recorded in the cache entry is `seam.neural-worker.v1`, now a
shared constant (`rendering::kNeuralRendererIdentity`) so a cold render and a
cache hit disclose the same renderer.

Publication now names the execution instead of guessing it. `PublishedProjectAudio`
carries `neuralIdentities`, one entry per audible neural track with the track id,
model id, model version, bundle content hash, configuration version, inference
step count and the worker/runtime/provider provenance — for the failure path too,
so a failed neural render still says which model was attempted. `activeRenderer`
reports `seam.neural-worker.v1` for an active neural track instead of the
source-filter label that a unit-plan-derived name produced for every non-voicebank
source.

The three workflow cases are: two simultaneous neural tracks whose published
identities and phrase content hashes stay distinct; a cancelled neural preview that
publishes nothing (idle retained slot, no identity) and then completes on retry;
and cache provenance that hits for an identical submission and misses when only
the worker or runtime changed. None of them proves musical output: the executing
runner is the transport fixture probe, which returns silence and performs no
inference.

Two more cases close the rest of M2.P2 item 7. A neural project now exports a
committed master and stems through `ExportService::exportSetWithSources()`, with
the exported WAV frame count equal to the neural render and a reproducible master
digest across two exports of the same execution. A source with no runner is a
typed `NeuralSourceMissing` failure with the diagnostic text naming the missing
admitted bundle: the previously published audio keeps its revision and samples,
and a repaired request publishes again at a newer revision. So an older
successful phrase cannot be published over a failed current neural request.

Verification: the workflow target now passes 5 cases.

Verification: the new target passes 3/3, the neural label group 7/7 and the
neighbouring `seam_neural_phrase_runner_tests`, `seam_neural_render_tests` and
`seam_authoring_render_coordinator_tests` all pass unchanged.

## The packaged helper's runtime closure is read from its own load commands

Staging could copy a library, but it could not tell whether the helper would find
it after installation. Added `tools/phase13a/macho_linkage.py`, which reads the
Mach-O load commands (`LC_LOAD_DYLIB` and its weak, re-export, upward and lazy
forms, `LC_ID_DYLIB`, `LC_RPATH`) and answers the question that matters: which
images the host provides, and which references resolve from the directory the
helper is launched out of. It parses thin 32/64-bit and universal images, selects
the slice the payload will actually run, and refuses a truncated or oversized load
command table instead of guessing.

`derive_runtime_closure()` walks that linkage recursively through supplied search
paths. A reference that the host provides is skipped. A reference that exists on
the build machine but is reached through an absolute path or an rpath that will
not exist in the package is reported as unresolved rather than copied, because
copying the file would not repair the load command. Runtime SDKs publish a
versioned file behind the load-name symlink, so search paths resolve symlinks and
the real file is staged under the name the load command asks for.

Staging now accepts `runtime_search_paths` and refuses the whole payload when any
entry is unresolved, and `scripts/assemble_release_payload.py` exposes it as
`--runtime-search-path`. Deriving a closure is implemented for macOS arm64; a
Windows payload must pass explicit dependencies until PE import analysis lands.

The first run of this analysis against the real development worker produced a
concrete defect: the worker linked `@rpath/libonnxruntime.1.dylib` through the
absolute build-directory rpath, so the packaged helper would have failed to load
its runtime on any user machine while working here. `seam_neural_worker` now
carries `@executable_path` and `@loader_path` ahead of the development SDK
directory, so the ONNX Runtime library resolves beside the staged helper and the
development run still finds it. After the rebuild the analysis reports
`libonnxruntime.1.30.0.dylib` staged as `libonnxruntime.1.dylib`, which is exactly
the name the load command asks for.

What the same analysis still reports as unresolvable is real remaining work, not
a packaging bug: 81 absolute Homebrew references (78 abseil, 2 protobuf, 1
OpenSSL). The shipped worker must link the vendored static OpenSSL and a static
or staged protobuf/abseil closure before a payload can be assembled with a
derived closure. Until then the staging path refuses that worker by design, and a
hand-written `--neural-dependency` list would have to ship 81 libraries whose
reference paths do not exist outside this machine, which is why the check refuses
rather than papers over it.

Verification: `tests/phase13a/test_neural_helper_staging.py` grows to 14 cases
covering load-command parsing, absolute-rpath refusal, symlinked SDK publication,
derived staging and closure refusal; `seam_neural_package_materialization_tests`
passes, and the new `seam_neural_worker_relocatability` target checks the invariant
against the real built worker on every run of the neural label group (7/7 passing).

## The neural helper is staged from finalized bytes into every payload surface

Completed the packaging half of M2.P2 item 9 for the two declared targets. No
packaging path could previously place the neural worker and its inference runtime
into a payload, so `neuralPackages` could only ever report `MISSING` and a
released payload had nothing to launch.

Added `tools/phase13a/neural_helper_staging.py`. `stage_neural_helper()` takes the
finalized worker, its runtime dependencies, the payload platform and the build
identity, then stages one helper package beside every declared surface and seals
it through the existing `build_neural_package_manifest()`. Staging is two-phase:
every surface is validated before the first byte is written, so a payload can
never be left half-staged by a later failure. An artifact that is already staged
with identical bytes is left untouched, and one staged with different bytes is
refused instead of silently replaced.

The platform check reads the image itself, not the file name. `image_identity()`
parses thin and universal Mach-O headers and PE headers, and
`require_platform_image()` refuses an artifact whose container or machine does not
match the target: a Mach-O cannot be sealed into a Windows payload, a PE cannot be
sealed into a macOS payload, an arm64 target refuses an x86_64-only Mach-O, and a
universal image is accepted only when it declares the required machine. A file
that is not a Mach-O or PE image is refused rather than treated as a dependency.
The helper name (`neural-helper`/`neural-helper.exe`), the surface layout, the
manifest location and the module path all come from one place:
`neural_package_layout()` now owns the per-surface layout that
`neural_package_inventory()` used to restate.

The assembly entry point can now stage before sealing:
`scripts/assemble_release_payload.py --neural-worker <path> [--neural-dependency
<path>]... [--neural-surface <id>] [--neural-protocol-version 1|2]` stages the
helper, binds it to the payload's own `RELEASE_IDENTITY.json` build id, and then
assembles and seals the payload. Without those flags the previous behavior is
unchanged and `neuralPackages` still records explicit absence.

Verification: `tests/phase13a/test_neural_helper_staging.py` adds 8 cases over
container parsing, macOS and Windows staging, cross-platform refusal, universal
binaries, name conflicts, replaced bytes and the CLI; the payload assembly suite
adds an end-to-end case where the CLI stages a helper and the sealed manifest
reports `VERIFIED_FILES` for every Windows surface, plus a refusal case for a
macOS image. The 40 tests of `seam_neural_package_materialization_tests` pass
(3 probe-dependent cases skip when the group is run without the CTest-provided
native probe). The fixture images used by these
tests are labelled fixtures: they prove the packaging path and its refusals, not
that an inference-qualified worker was produced.

Still open in this package: an actual built worker plus a verified ONNX Runtime
dependency closure per platform (the macOS closure must be derived from the
image's own load commands rather than a hand-written list), the Windows process
backend execution, and M2.P2 item 7's native preview/stop/retry, multi-voice
scheduling and cache provenance on the neural path.

## Platform identity is one explicit table, not string coincidence

Advanced the naming half of M2.P2 item 9. The product names its two supported
targets in three namespaces whose Windows spelling differs on purpose: host and
install evidence carry a platform name plus a separate architecture
(`macos`/`arm64`, `windows`/`x86_64`), payload, update and neural-deployment
descriptors carry one identifier (`macos-arm64`, `windows-x64`), and the
full-product contract carries `macos-arm64`/`windows-x86_64`. Nothing previously
owned the translation between them, so a caller that compared
`windows-x64` with `windows-x86_64` would either pass by coincidence or fail by
accident.

Added `tools/platform_identity.py` as that owner. One frozen table holds a row
per target with its host name, architecture, deployment identifier and optional
contract identifier; `linux-x64` is deployment-only and therefore has no
contract identity. The accessors are `host_platforms()`, `deployment_platforms()`,
`product_contract_platforms()`, `product_contract_platform()`,
`deployment_platform()`, `deployment_platform_for_host()` and
`identity_for_deployment()`. Every accessor refuses an unknown value, a wrong
namespace (the contract spelling is not a deployment platform and the reverse is
also true) and a malformed or empty string with `PlatformIdentityError`, so an
unlisted platform cannot pass through unexamined.

Four duplicated tables now read from that owner instead of restating the strings:
`tools/phase13a/update_contract.py`, `tools/external_beta/host_evidence.py`,
`tools/external_beta/install_evidence.py` and
`tools/external_beta/product_soak.py` take the host or deployment view, and
`tools/external_beta/full_product_contract_registry.py` takes the contract view.
`host_platforms()` deliberately returns only the two certifiable host targets, so
the install and soak evidence contracts keep exactly the platform pairs their
matrix documents declare.

The mapping also became load-bearing rather than descriptive. The sealed release
payload manifest now records `productContractPlatform` through
`product_contract_platform()`, and `verify_release_payload_manifest()` re-derives
that value, so a payload whose contract identity was rewritten after assembly is
refused as a manifest identity failure instead of being trusted. On the native
side, `tests/test_neural_worker_protocol.cpp` now proves the two namespaces are
distinct: a correctly signed descriptor declaring `windows-x64` loads, and an
equally signed descriptor declaring the contract spelling `windows-x86_64` is
refused. Translation may only happen through the mapping owner.

Verification: `seam_platform_identity_tests` is registered (9 tests),
`seam_neural_package_materialization_tests` passes 30 tests including the new
payload-manifest binding case, the external-beta Python contract suite passes
179 tests, and `seam_neural_worker_protocol_tests` passes with the new native
guard. Packaging the actual worker and runtime into a payload is still open: no
packaging script stages the neural worker or its ONNX Runtime dependencies yet,
and the Windows process backend still has no executed evidence. (The packaging
half of that sentence is superseded by the entry above: staging now exists.)

## Production neural worker executes admitted bundles

Added the prepared admission handle `AdmittedNeuralBundle`
(`libs/seam-neural-synthesis/{include/seam/neural_synthesis/model_bundle.hpp,src/model_bundle.cpp}`).
`admit()` verifies the frozen manifest, both graph payloads, the vocabulary and
the declared execution identity once, then retains those immutable payloads as
shared pointers. Copying a prepared handle therefore shares one model instead of
duplicating graph bytes per phrase snapshot, which is what the render path needs
before it can prepare work outside the audio callback. The handle carries
`ModelContract`, `NeuralBundleVocabulary`, `MelFeatureSpec`, steps layout,
vocoder output name and a `NeuralExecutionIdentity` that includes the bundle
content digest and the inference-step count, because steps change the result for
identical input.

Admission refuses four cases the metadata inspection alone would allow: a
configuration schema v1 bundle, which declares neither a steps layout nor a
vocoder output name and would otherwise execute assumed defaults; a bundle whose
declared frame bound exceeds the caller's prepared budget; an out-of-range or
zero step count and frame budget; and a vocabulary that does not decode or does
not match the declared model vocabulary hash. A moved-from handle is refused as
well. `tests/test_neural_model_bundle.cpp` covers acceptance, identity binding,
asset sharing across handle copies, a distinct identity for a different step
count, and every refusal above. This handle is not operator-level graph admission
and not an OS sandbox: the child still parses the exact bytes it executes.

Verification note: the complete Release suite passed 132/132 serially in 275.42
seconds. An earlier `ctest -j 4` run on the loaded machine reported nine failures
(demo smokes, two neural runtime checks, dependency direction, production staging
and import outcome); every one passed in `--rerun-failed` and in the serial run.
Those parallel failures were contention, not regressions. Prefer a serial run or a
quiet machine when interpreting this suite.

### Next unit: the neural render path, with two inspected constraints

Implemented `RenderSnapshotFactory::createNeural()`
(`libs/seam-rendering/src/render_snapshot.cpp`). It binds one admitted model
bundle to one vocal region and refuses to invent any part of the identity: the
bundle owns model identity, the project owns music and pronunciation, and the
caller supplies `NeuralRenderProvenance` (worker build, runtime version, selected
provider) from the signed deployment descriptor. A `RenderSnapshot` now carries
`neuralExecution`, the authoritative prepared bundle, while `resource` holds an
inert `NeuralSingerResource` tag; nothing may read resource bytes as a model.

`buildNeuralIdentity` hashes the bundle digest, configuration version, inference
steps, declared frame bound, mel geometry, amplitude scale, vocabulary hash, steps
layout, vocoder output name, `kDiffSingerInputRevision`, the execution provenance,
the frozen project JSON, pronunciation identity, style, ABI, quality and owned
window. Two runs that differ in any of those cannot share cached audio. The
factory refuses a bundle that cannot be prepared, unbounded or non-printable
provenance, a region with no notes, an unknown track or region, a snapshot rate
that differs from the admitted model rate, an owned window outside the score
context, and a track already bound to a procedural recipe.

`renderResourceFamily()` now reports Sample, Procedural or Neural from one place,
and `findSample()`/`findProcedural()` give checked carriers. This was not cosmetic:
the first draft of the render test called `sample()` on a neural snapshot and
trapped with `bad_variant_access`, which is exactly the failure mode the plan
warned about for sample-only accesses. Neural subdivision and neural pipeline
execution remain explicit `Unsupported` refusals with named messages, so no path
can silently treat a prepared bundle as a bank.

Verified: `seam_neural_render_tests` passes, and the complete Release suite passed
132 of 133 with the only failure being source closure for these then-untracked
files.

Added the pipeline half of that package. `PhraseRenderPipeline` now takes an
optional `std::shared_ptr<const NeuralPhraseRunner>`; the abstract
`NeuralPhraseRunner` is what the application implements, so helper selection,
expected digest and process budgets stay out of the bank and out of the audio
callback. A pipeline without a selected runner refuses a neural snapshot with a
named `Unsupported` error instead of falling back to sample material. With a
runner, the branch verifies the snapshot is complete, that its rate matches the
admitted model, and that the returned audio covers the declared owned window with
exactly the declared frame count. It rejects a runner that returns sample
placement metadata as an invariant violation, forwards the compiled phonemes,
reports `SingerResourceKind::Neural`, and leaves the unit plan and procedural
markers empty. `PhraseAudio` carries no sample rate, so rate agreement remains the
runner's contract and is stated in the interface.

`seam_neural_render_tests` covers both halves with a test-only runner: refusal
without a selected runner, successful execution, exact owned-window enforcement, a
one-sample overlap rejected as a conflict, fabricated placements rejected as an
invariant violation, and cancellation reaching the runner. The production runner
itself is still to be written in the authoring/application layer, where the
deployment descriptor and `runNeuralBundleWorker` are available.

Implemented that production runner as
`libs/seam-authoring-runtime/{include/seam/authoring/neural_phrase_runner.hpp,src/neural_phrase_runner.cpp}`.
`NeuralPhraseRunnerOptions` carries the canonical bundle directory, its payload
budget, the already-resolved helper identity and process budgets, and the
explicit silence symbol; `validate()` refuses a relative path, an unbounded
bundle, a non-bundle launch contract, an unbudgeted process, a relative helper or
a missing helper digest. `create()` also reads the selected directory's manifest
and binds its digest, so a snapshot admitted from one bundle cannot be rendered
through a different one. `render()` builds the request with
`prepareNeuralScoreRequest`, sets `bundleContentHash` so the launch satisfies
metadata v3, re-validates against the admitted model contract, runs
`runNeuralBundleWorker`, and returns window-exact mono audio with no placements.
A vocabulary without the declared silence symbol is refused instead of guessed.

Two real constraints surfaced while implementing this. First, the neural path has
no bank source, so consonant timing uses the existing source-independent in-note
policy; `createNeural` now selects it explicitly and the timing-policy revision
participates in the render identity. Second, request preparation refuses any
phoneme span outside the requested output window rather than clipping it, so a
partial neural window that excludes part of a syllable is refused with
`InvalidArgument` today. Windowed neural rendering therefore needs a package that
defines how out-of-window phonemes are represented before it can ship; the test
records this refusal instead of hiding it.

`seam_neural_phrase_runner_tests` drives the real parent transport with the
existing bundle transport fixture as the selected helper and covers: a bound
request and window-exact result over the full phrase, refusal of every unsafe
execution option, a foreign bundle rejected as a conflict, cancellation, and a
vocabulary without silence refused as unsupported. The helper returns silence by
contract, so this proves admission, request binding and response validation, not
synthesis. The real inference path is covered separately by
`seam_neural_production_worker`, which executes actual ONNX graphs.

Closing the model chain, `tools/voice_model_training/prepare_bundle.py` composes an
admitted bundle directly from real acoustic and vocoder export directories. The
declaration is read from the receipts rather than typed by hand: it re-verifies
each graph against its recorded digest and byte count, requires both exports to
declare the same supported 48 kHz/80-bin logarithmic-mel profile with a matching
`profileSha256`, requires the acoustic runtime smoke result, takes the ordered
vocabulary from the acoustic receipt and converts it through the native-compatible
converter, and writes the configuration and vocabulary with canonical bytes. The
manifest is published last, so a directory without it is not a bundle.

Two representation details were settled by inspection rather than assumption. The
stored target matrix is `[T,F]` while the admitted graph consumes `[1,T,F]`, so the
declaration maps the profile layout to `BTF`. Native `JsonValue` objects are
`std::map`, and the frozen manifest is published pretty-printed with sorted keys
and a trailing newline; the tool reproduces those exact bytes, which
`check_bundle_preparation.py` verifies by preparing the same assets through the
native CLI and comparing digests and manifest bytes.

`seam_neural_bundle_preparation` then runs the composed bundle through the
production worker with a real SNW1 v3 request and asserts the v3 response, bundle
binding, request hash and expected PCM. Refusals cover tampered graph bytes, a
profile mismatch between exports, an unsupported profile, a vocabulary whose
padding token repeats, a missing runtime smoke result, an existing output
directory and a nonpositive frame bound. The graphs are deterministic arithmetic
fixtures, so the chain is proven while the singer remains unqualified.

Added the persisted product selection. `domain::NeuralResourceReference` stores
only a singer resource identity, and `VocalTrack` now holds an optional
`neuralResource` beside `proceduralRecipe`; `Project::validate()` refuses a track
that selects both families and refuses a reference whose kind is not Neural.
Project JSON is schema 10: the track member is written explicitly (including
`null`), and it is required when the schema is 10 or newer so an older build
refuses a newer project instead of silently dropping the selected voice. A schema
9 file without the member still migrates to "no neural selection", while a file
that claims schema 9 and carries a neural reference is refused rather than
half-understood. No path, helper command or resolved execution state is stored in
a project file.

The render path now honours that selection in both directions. A track that saved
a neural selection cannot produce a sample-bank snapshot, mirroring the existing
saved-procedural-recipe refusal, and `createNeural` refuses a bundle whose model
id, version or bundle digest differs from the saved selection. An unbound track
still previews, so trying a voice before selecting it stays possible.

Verified: `seam_tests` (including the new schema round-trip, migration, smuggling
and family-conflict cases) and `seam_neural_render_tests` (including the
match/mismatch selection cases) pass.

Closed the remaining link between a saved selection and installed bytes.
`prepare_bundle.py` now accepts `--resource-id` and `--resource-version` and, when
both are given, publishes a `resource.json` record after the manifest with
exactly that identity and the manifest digest as its content hash. The record is
never guessed from the directory and the two values must be supplied together.

`NeuralResourceRegistry` (`libs/seam-authoring-runtime/{include,src}/seam/authoring/neural_resource_registry.*`)
scans an installation root under explicit resource, per-asset and total byte
budgets, and verifies each candidate before indexing it: the record schema and
printable identity, that the manifest digest equals the recorded content hash,
that every declared asset exists as a regular file with exactly its recorded
length and SHA-256, and that the four required roles each appear once. A
directory without a record, a tampered asset, a duplicate identity, a relative
root, an out-of-budget asset or a cancelled scan is refused; an empty root is a
valid empty catalog. `resolve()` matches id, version and digest exactly, so a
saved selection can never be satisfied by a different voice, and a reference
whose kind is not Neural is refused before lookup.

Verified: `seam_neural_phrase_runner_tests` now also covers registry scanning and
resolution, and `seam_neural_bundle_preparation` asserts the record's identity
against the manifest digest and refuses a half-specified identity.

Reached the product render path. `rendering::TrackSingerSource` gained a
`TrackNeuralSource` carrying the admitted bundle, the execution provenance and the
selected runner; the source carries no helper path because the runner already
holds the resolved first-party selection and its process budgets.
`ProductionProjectRenderer::renderWithSources` dispatches a neural track to
`createNeural` plus `PhraseRenderPipeline{runner}`, produces mono model audio at
the phrase's absolute start frame, routes it through the same track route and
gain as other families, and publishes the snapshot content hash. The previous
fall-through to the sample branch is now unreachable for a neural source, so a
prepared model can no longer trap or be treated as a bank.

The saved-selection rule is enforced in two places: the coordinator refuses a
resolved singer whose model id, version or bundle digest differs from the saved
selection, and `createNeural` refuses the same mismatch at snapshot level. A
missing runner is an explicit invalid-argument refusal rather than a silent
substitution. `seam_neural_phrase_runner_tests` covers a full neural project
render (one track, one region, one phrase, stereo routing with silence on the
right channel, empty unit plan and no invented diagnostics) plus the mismatch and
missing-runner refusals.

Measured the worker invocation cost that decides whether the pilot needs a
bounded session owner. `seam_neural_production_worker` now times its first
accepted invocation and three identical repeats on the development Mac and prints
them as JSON. Final recorded run: cold 0.0521 s, warm 0.0484/0.0498/0.0493 s. The
fixture bundle is about one kilobyte of arithmetic graph, so these numbers bound
process creation plus admission and deliberately exclude real model-load time; a
production-sized bundle will be larger and slower to admit. The decision stays
`pending real-model measurement`: with roughly 50 ms of fixed per-invocation cost
on this machine, a per-phrase process remains acceptable for a pilot, and the
question only becomes pressing once a real bundle's admission and reload time are
measured against the agreed budget.

Advanced the primary lane's installation handoff (M1.P3 item 6). The installed-song
regression in `tests/test_standalone_voicebank_workflow.cpp` now goes past its
one-unit fixture: it derives the phone symbols the engine will actually request
for the phrase from the phonemizer instead of guessing unit names, packs a
multi-unit bank for those symbols, installs it through the trusted installer, and
selects it. The new song then reports complete coverage, the producer's generation
inputs are removed from disk, and the song must still export master plus stems
from the installed bank. It saves through `ApplicationCommand::SaveProjectAs` and
reopens through `ApplicationCommand::OpenProject` on the same controller, verifies
the reopened track still binds the same bank id, version and content hash with
complete coverage, and re-exports to prove the master hash is identical. That
closes the "producer workspace unavailable to the new song" requirement with
evidence rather than intent.

M1.P3 remains open: the full source → generation campaign → edit → review →
candidate → package chain still needs its own connected regression, the plan's new
`tests/test_original_singer_workflow.cpp` and `seam_original_singer_workflow_tests`
do not exist yet, and the human/reviewer and real-input journeys remain external
evidence that this work does not claim.

Added that regression. `tests/test_original_singer_workflow.cpp` now runs as CTest
`seam_original_singer_workflow_tests` and covers the connected lifecycle the plan
names: a synthetic source is imported into a producer workspace, edited by a
committed normalize operation, prepared for review, accepted by a supplied
reviewer decision, published as a candidate bank, packaged as a signed
`.seambank`, and installed through the standalone controller. The installation
first attempts a byte-tampered package and requires that attempt to fail with no
installed card, then retries the same action successfully, so a genuine refusal
and retry is part of the flow rather than a separate unit fixture.

After installing, the new song must report complete coverage, the producer
workspace is renamed away, and the song still exports master plus stems from the
installed bank. It saves through `SaveProjectAs` and reopens through `OpenProject`
on the same controller, re-exports with an identical master hash, and verifies the
installed bank's content hash against the card. Finally it mutates the producer
draft (unit queue state and last durable generation) and proves the saved song
bytes and the installed bank manifest are unchanged, which is the plan's
immutable-old-song requirement.

Still open under M1.P3: the generation-campaign orchestration (plan/run/cancel/
resume with collected takes) is exercised by its own unit suites rather than this
connected regression, the native Studio actions for review and packaging are not
yet the path this test drives, and the reviewed-by-a-real-person and real-input
journeys remain external evidence. This test uses a synthetic source and test
identities only; it does not qualify an original singer.

Storage note: the machine reached 124 MiB free, which caused eleven unrelated
suite failures (demo smokes, contract tests, neural runtime checks). Those were
not regressions; the same tests pass with storage restored. Four regenerable
ONNX Runtime build artifacts were removed to recover space: `onnxruntime-source`
(954 MiB), `onnxruntime-telemetry-free-pinned-build` (686 MiB),
`onnxruntime-telemetry-free-build` (260 MiB) and the redundant
`onnxruntime-osx-arm64-1.30.0.tgz` (40 MiB). The telemetry-free SDK, the release
SDK, every Python environment, both source checkouts and every evidence directory
were retained. Rebuild the removed trees with `tools/neural_runtime/build_telemetry_free.py`.

`RenderSnapshotFactory::createNeural()` and the phrase-pipeline neural branch are
the next package. Two constraints were inspected rather than assumed.

`cmake/NeuralDependencyDirection.cmake` walks the transitive dependencies of
`seam_neural_synthesis` and fails only if that closure reaches
`seam_authoring_runtime` or `seam_rendering`. It does not forbid
`seam_rendering` from linking `seam_neural_synthesis`, so the factory and the
snapshot carrier may live in `seam-rendering` as the plan intends. The prepared
handle must be carried in a new snapshot member; the legacy opaque
`NeuralSingerResource::model` payload must not be reinterpreted as an admitted
bundle.

Worker run options carry the helper path, expected digest and process budgets,
and those belong to the deployment descriptor owned above rendering. The pipeline
therefore cannot build them itself: the neural branch needs an injected runner or
launch contract supplied by the authoring/application layer, keeping helper
selection out of a bank and out of the audio callback. Today the scheduler and
pipeline already reject a neural resource with `Unsupported`
(`libs/seam-rendering/src/render_scheduler.cpp`, `.../render_pipeline.cpp`), so no
existing path silently treats a neural resource as sample material.

Added `apps/seam-neural-worker/main.cpp`, the first executable that performs real
acoustic-then-vocoder inference for an admitted model bundle. The application
selects it through the existing launch contract,
`--seam-neural-worker-v2 BUNDLE_DIR MODEL_ID MODEL_VERSION BUNDLE_CONTENT_HASH
MAXIMUM_BUNDLE_BYTES`, with one SNW1 request frame on stdin and exactly one SNW1
response frame on stdout. The child never receives a command, library path or
script from a bank.

Admission order is deliberate. The worker reads the bounded request frame, then
re-loads the bundle directory itself through `loadNeuralBundleDirectory`, so a
file changed after parent inspection fails its manifest or asset digest. It then
inspects the bundle metadata, requires the request's bundle, model and vocabulary
identity to match the bytes it loaded, and only then parses both graphs with the
native ONNX inspector and the frozen pair contract. Session creation happens after
that admission; loaded interfaces are cross-checked against the admitted
declaration before any tensor is executed. A build without native graph admission
cannot execute a bundle, and the CMake target now exists only where the pinned
schema is available.

Inference reuses the shared DiffSinger path: `prepareDiffSingerAcousticInputs`
produces tokens, durations, padded F0 and the step tensor; the acoustic graph
produces mel; the admitted vocoder produces audio; `finalizeDiffSingerResponse`
validates the complete padded buffer, trims the final partial hop to the exact
requested sample range and applies sample-domain dynamics once. Rejections cover
nonfinite mel or PCM, wrong mel or audio geometry, an out-of-range gained sample
and any request/response identity mismatch.

`tools/neural_runtime/check_production_worker.py` builds a real ONNX bundle with
the production CLI, drives the worker through that contract, and asserts exact
response binding, sample count and arithmetic output. It also asserts that the
worker refuses the v1 transport contract, an invalid byte budget, a wrong manifest
digest, a wrong launch identity, bytes changed after preparation, a bundle whose
graphs fail admission, wrong model/vocabulary/sample-rate/frame identities, malformed
frames and an out-of-range dynamics result, always with empty stdout. The v1
transport fixture remains a separate executable that accepts an uninspected-graph
bundle the production worker rejects.

Verified in two builds: `build/release` with the local ONNX Runtime 1.30.0 SDK and
`build/neural-runtime/seam-telemetry-free` with the telemetry-free local SDK. All
five `neural-native-experiment` tests pass in both, including the new worker test.
These are deterministic arithmetic fixture graphs executed with real runtime and
real admission; no learned singer, voice identity, listening result or Beta claim
follows. The worker's diffusion step count is a pinned diagnostic constant because
the admitted configuration schema does not yet carry it, and the production
bundle still needs its model-configuration and packaging revision.

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Actual DiffSinger architecture checkpoint

Recovery verification: the complete Release CTest suite passed 130/130 in 86.02
seconds after the native export-path changes. This includes native paired/bundle
checks and source closure, but does not erase the separately reproduced Python ORT
teardown SIGABRT. Telemetry-free source retrieval remains active; replacement build
and qualification have not yet occurred. Existing runtime installations are intact.

Inspected ONNX Runtime v1.30.0 source at
`f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`: runtime telemetry disabling only flips an
atomic flag; constructor initialization and SDK shutdown remain active. The source
supports `onnxruntime_USE_TELEMETRY=OFF` at compile time. Added a pinned CPU
wheel/shared-library build helper and `tools/neural_runtime/TELEMETRY_FREE_RUNTIME.md`
with the required replacement qualification. Source expansion/download is underway;
no replacement build or runtime installation has yet been claimed or performed.

Connected actual checkpoint exports to the native C++ ORT probe through
`--acoustic-export GRAPH SHA256`. Native hash binding, structural inspection,
dynamic BTF output checks and finite-value checks execute before reporting three
successful learned-weight cases (3/16/23 frames); wrong hashes reject. Existing
paired-fixture regression passes with `--native-inspection`. Native telemetry is
explicitly disabled. This remains a probe, not the production worker/render path.

The first real graph revealed native rejection code 14: the exported encoder
stores a negative-infinity attention mask, disallowed by native finite-tensor
intake. Export now rewrites only recognized scalar float32 negative-infinity
constants consumed as Where data operands to the finite float32 floor. Other
nonfinite constants reject. Valid token IDs are positive; all-padding input is
outside this contract. The real native subprocess subsequently completed all
cases and the wrong-hash rejection test.

IMPORTANT: the complete parent Python diagnostic again aborted at shutdown with
recursive_mutex failure (SIGABRT) despite disabling ORT telemetry, after native
subprocess success. Disabling telemetry has NOT proven a root-cause repair. Native
inference evidence is valid for that subprocess, but the overall export/runtime
environment remains unqualified. Do not rerun until green and discard this finding;
shipping runtime lifecycle/telemetry implementation needs further investigation.

Added the persistent acoustic export command from a captured trusted local
checkpoint. It verifies architecture settings, vocabulary and acoustic profile,
strictly restores weights, exports/inspects the merged graph, executes an ORT smoke
case and publishes acoustic.onnx followed by export.json. The actual upstream
diagnostic now invokes this command in a separate process and verifies published
graph hashes and checkpoint receipt provenance; the complete run exited zero with
checkpointExportVerified=true. These are temporary engineering fixtures in the
diagnostic; no qualified singer asset or redistribution authorization is created.
The command itself preserves its selected output for later native integration.

Added real-weight denoiser ONNX/PyTorch parity using identical supplied tensors:
eight cases over 3/16/23/257 frames and timesteps 0/7 met 1e-5 absolute/relative
tolerance; maximum observed error 7.450581e-8. This isolates learned-model arithmetic
from cross-runtime RNG differences and does not close stochastic sampler parity.
One diagnostic run exited 134 after emitting passing results. The macOS report
`Python-2026-09-13-195108.ips` identifies an ORT Microsoft telemetry worker in
`DebugEventSource::DispatchEvent` / HTTP response handling, with recursive_mutex
failure during teardown. An unchanged repeat exited zero. ORT telemetry is now
disabled before diagnostic session creation; broader shutdown reliability remains
unqualified and JSON `passed` alone must never substitute for a zero process exit.
The first complete rerun after telemetry disabling exited zero and retained all
eight denoiser parity passes; this is a smoke result, not a stability soak.

Actual encoder+diffusion ONNX merge now passes SEAM's data-only graph inspector
and ONNX Runtime 1.30.0 with scalar runtime steps (1,4,8) and dynamic frame lengths
(16,3,23). All outputs have finite [1,T,80] geometry. Merged graph is 845063 bytes,
SHA-256 `f9e0862d596fddfb88f2e4f7c17db2c962224541681a190e660dc5052f6282fb`.
Root-cause fixes: a typed non-shallow entry avoids upstream Optional-Tensor JIT
inference failure while reusing upstream samplers and requiring seeded exact Torch
parity; seeded outer initializer name mappings preserve If/Loop captures during
graph prefixing/merge. This is real trained-weight graph execution, not arithmetic
fixture substitution. Stochastic numerical parity across runtimes, vocoder/model
bundle publication and native production rendering remain outstanding.

Serialized the actual trained duration encoder to ONNX opset 17 and executed its
owned bytes with ONNX Runtime 1.30.0. Five shape cases passed (including zero-duration
phone and 1025-token sequence), maximum absolute error 9.536743e-7. Materializing a
4096-token positional table before tracing prevents the upstream lazy-growth branch
from freezing the initial 1024-entry capacity. Intermediate graph: 644485 bytes,
SHA-256 `cbb7c6ac9069526cb7b70697cb6ca97461516ac0495024a8e50d70890a2b61de`.
Verified with NumPy 1.26.4, ONNX 1.19.1, ml-dtypes 0.5.3 after repairing an unintended
NumPy upgrade from unconstrained dependency resolution; explicit export-check pins
are now provided. This encoder is not the complete SEAM acoustic graph. Diffusion
serialization, graph merge, vocoder and native production rendering remain open.

Inspected the actual upstream deployment modules and SEAM acoustic/vocoder tensor
profile. Added a strict training-weight → deployment-model bridge with explicit
natural-log scale binding: upstream deployment defaults to converting log10 mel,
whereas SEAM targets already use ln amplitude. Setting `mel_base=e` avoids that
incorrect extra scaling. The actual model passed strict state loading and exact
conditioning parity at 3, 16 and 23 frames (including a zero-duration phone), and
deployment diffusion returned finite [1,23,80] mel. This is Torch-side deployment
verification, not serialized ONNX parity, acoustic quality or native rendering.

Added bounded multi-epoch command execution, retaining one parent-linked checkpoint
per completed epoch and publishing a run completion record only after all requested
epochs finish. Source/label/shard admission refreshes each epoch with stable dataset
identity; live optimizer and CPU RNG state continue without reinitialization. Run
time and cumulative binary checkpoint bytes are bounded cooperatively. Earlier
complete checkpoints remain recoverable if a later epoch fails.
Actual upstream-model verification passed: two continuous epochs match separate
first-epoch/resumed-second-epoch execution in loss, model tensors and CPU RNG.

Implemented strict local-checkpoint continuation in the training command. Resume
requires captured receipt identity, matching configuration/environment/profile and
fresh matching dataset admission before optimization. It restores model, optimizer
and CPU RNG state and records completed epochs plus parent receipt hash. Review
renewal, dataset migration and quality-based scheduling remain open.
Verified with two separate upstream-model subprocess continuations from the same
first-epoch checkpoint: identical second-epoch loss, model tensors and CPU RNG,
with completedEpochs=2 and matching parent receipt identities in both outputs.

Added the runnable `tools.voice_model_training.train` CPU entry point: exact
configuration hashes, shared dataset input capture, bounded model settings, flat
target inventory, trusted pinned upstream checkout and reviewed-epoch publication.
The real architecture diagnostic invokes it in a fresh subprocess and reloads the
result: one complete 16-frame training phrase, loss 0.9917272, matching checkpoint
receipt and coverage. Configurable continuous epochs are now supported; quality-based scheduling,
real corpus training, export and native singer rendering remain open. See the
training tool's `TRAINING_COMMAND.md` for the runnable contract.

The next connected diagnostic has now passed against clean pinned upstream
DiffSinger revision `336cf01b57f2ad44c6b37a79cf33993043291759` (Torch 2.8.0,
NumPy 1.26.4, CPU). Three distinct temporary oscillator sources traverse actual
file capture, fixture-only dual signatures, sharded admission and the reviewed
training service. One train phrase covers all 16 frames, changes 43 model tensors,
and restores checkpoint parameters exactly. A separate validation phrase executes
the evaluation path; the third fixture remains in the test partition. No mock
replaces admission, target loading or optimization in this diagnostic. Reported
training loss 0.9709514 and validation loss 1.0373794 describe synthetic mechanics,
not lyric intelligibility or held-out singer quality. Real corpus production,
multi-epoch training, export and native rendering remain open.

Connected `train_reviewed_epoch` now orchestrates fresh admission, paired targets,
whole-phrase optimization, complete train-frame coverage and revalidated checkpoint
publication. Preflight rejects preparation issues and phrases above 4096 frames.
Cancellation/expiry interrupt between updates; source/label/shard admission is
repeated after the epoch and before the final receipt. Contract tests cover early
rejection, late identity changes and failed training without publication. These
mocked boundary tests do not establish real-corpus training or singer quality.
The signed-fixture integration is now exercised as described above; production
multi-epoch execution and actual model export/render integration remain next.

Added read-only conditioning reuse during fresh dataset assembly. Existing shards
must match features reconstructed from newly admitted source/label inputs; they
are never rewritten. CLI tests preserve dataset identity and shard bytes on a
successful refresh, reject missing reuse directories, altered shard content and
changed source audio, and publish no snapshot on rejection. This enables fresh
admission checks at training boundaries without regenerating feature caches.

Connected checkpoint restoration to captured receipt identity and verified owned
bytes, replacing direct path loading. The local-producer loader rejects altered
binary content before Torch deserialization and verifies embedded metadata against
the completion record. The actual model still restores inference and continuation
exactly. This does not extend trust to arbitrary downloaded checkpoints or bypass
fresh source/review admission for a resumed training run.

Added reusable checkpoint publication with new-directory/no-overwrite semantics,
bounded binary writes, file fsync, metadata/binary hashes and a final authority
recheck callback before receipt publication. Tests cover byte limits, absent
coverage and late expiration without a completion record. The actual architecture
round trip now exercises this owner. It remains a local CPU checkpoint writer,
not an untrusted importer or completed authorized production-training workflow.

Recovery checkpoint verification: all 39 training-tool tests passed in the
isolated real-model environment (13.995 seconds), including optional Torch tests
executed rather than skipped. Native pitch CLI and tracked-source checks were
also rerun separately. The preceding actual upstream architecture run passed
audio-backed optimization, isolated evaluation and exact checkpoint continuation.
This checkpoint preserves connected training mechanics; it does not complete
production training, lawful corpus acquisition, model export or Beta qualification.

Added isolated CPU objective evaluation without optimizer updates, preserving
Torch RNG, existing gradients and mixed module modes. Tests cover repeatable
seeded loss and state restoration on rejected input. The real architecture check
also exercises evaluation, explicitly reusing its synthetic training fixture only
for mechanics; genuine held-out corpus evaluation remains outstanding.

Replaced constant mel targets with actual 80-bin extraction from a byte-verified
original oscillator WAV. The real upstream model now has 58,704 parameters and
its fixed-noise loss fell 0.9959463 → 0.9717840. Finite [1,16,80] inference, exact
checkpoint restoration and the next resumed update all passed. Dataset/checkpoint
engineering identity binds actual source/target/profile records and synthetic
conditioning. This supersedes prior constant-target measurements; oscillator
token labels are not linguistic supervision or a qualified singing dataset.

Added explicit epoch source/core coverage accounting with halo-mask verification.
Duplicate, missing, out-of-order and unknown-source cores reject. The architecture
experiment now treats its eight repeated updates as eight single-phrase epochs,
each with verified coverage, preserving the synthetic nature of the experiment.
Generic iterator-only runs explicitly report coverageVerified=false.

Connected the real architecture experiment to a bounded epoch runner. Eight
synthetic updates completed with sample-weighted mean loss 0.9427672, followed by
the same exact restore/resume checks. Unit tests cover weighted aggregation,
empty input, budget exhaustion, cancellation, changed identities and late shard
failure. No incomplete epoch returns a success record or writes a checkpoint;
actual sampler/coverage policy and source admission remain caller responsibilities.

Extended the actual model check with temporary checkpoint serialization of model,
optimizer and CPU RNG plus configuration/revision/step identity. Strict restoration
reproduced inference bit-for-bit, then the original and restored optimizers produced
identical next loss (0.9727515) and model state. The self-produced 712,539-byte
checkpoint was not retained. This proves the tested CPU round trip only; durable
production checkpoint handling, other-device resume and real-data training remain open.

Ran the SEAM optimization/DDPM adapter against the clean pinned upstream
DiffSingerAcoustic class, not an interface stand-in. A deliberately small random
54,024-parameter WaveNet DDPM changed 43 parameter tensors across eight synthetic
fixed-noise updates; loss decreased 0.9528179 → 0.9277189. Upstream DDIM inference
returned finite [1,16,8] mel. No learned singer checkpoint or audio was published.
The isolated model environment needed setuptools 75.8.0 for the upstream legacy
librosa pkg_resources import; no upstream code patch or runtime bypass was used.
This proves executable architecture integration, not lawful data training,
vocoder compatibility, production-size behavior or musical qualification.

## Acoustic optimization primitive

Implemented the inspected non-shallow DDPM training-call adapter and noise-loss
layout conversion. Its interface fixture reaches a real optimizer update while
checking original tokens/mel2ph and rejecting incomplete phrases or unsupported
speaker conditioning before the model call. Source inspection exposed duration
derivation from mel2ph; whole-phrase-only enforcement prevents falsely treating
halo chunks as equivalent upstream training. Actual model construction and full
conditioning/objective support remain unfinished.

Added full source token sequences and one-based mel2ph to batches and the Torch
adapter interface. Sequence construction uses original labeled phonemes rather
than collapsing frame token runs, preserving repeated and unsampled short phones.
Tests retain three identical tokens with frame ownership [1,3], exercise contextual
batch mapping and reject out-of-range alignment. Actual upstream task wiring remains open.

Cross-checked the pinned upstream acoustic task: DDPM noise and reflow velocity
objectives are distinct from direct mel regression. Extended the update primitive
with a named, model-owned per-element objective callback while retaining SEAM's
mask/weight reduction and gradient checks. Tests distinguish custom squared-noise
fixture loss from mel L1 and reject unnamed or already reduced objectives. Actual
upstream architecture, noise scheduler and training-task adaptation remain open.

Added source-local context halos to conditioning and paired target batches, with
separate core offsets and an explicit loss mask. Optimization counts only core
frames while retaining their partial-hop sample weights. Tests reconstruct all
eight owned target frames exactly once from overlapping 4/5/3-frame inputs and
verify exclusion of halo loss. Model-specific receptive-field sizing and temporal
chunk-equivalence qualification are still required.

Added a real Torch update step for supplied acoustic adapters, using paired
conditioning/targets, sample-weighted L1 and finite gradient clipping. It refuses
held-out batches and mismatched optimizer parameter ownership. Optional Torch
tests exercise decreasing loss and changed parameters in a clearly labeled
constant-output fixture, partial-tail weighting, invalid targets and a finite
forward/nonfinite backward that must not call the optimizer. This is optimizer
mechanics, not an original singer or completed M2.P3 training workflow.

## Integrated training preparation regression checkpoint

Full Release CTest run: 129/130 passed in 112.91 seconds. The source-closure test
caught two newly created comparison files not yet indexed during the run; after
staging them, its focused rerun passed in 0.26 seconds. Thus every registered
test has current passing evidence across the full run and that corrected rerun;
this was not a single clean 130/130 invocation. No runtime regression failed.

An isolated Python 3.11 / Torch 2.8.0 / librosa 0.10.2 / NumPy 2.2.6 environment
passed dependency checking and all 24 frontend numerical comparison cases.
Maximum absolute errors: 4.7401e-7 (float64 reference), 0.0016051 (float32).
The comparison explicitly prepads to SEAM's full-hop policy. Trained-model
compatibility, lawful production data, optimization, learned checkpoints,
production worker integration and musical qualification remain unfinished.

## Frame conditioning implementation

Connected acoustic target binaries to source-local conditioning batches through
`iter_supervised_batches`, requiring an explicit profile hash and matching source,
PCM, sample rate, hop and frame geometry. It verifies exact binary content and
finite float32 values, then supplies owned target slices. End-to-end tests use
the actual extraction CLI and compare batches of 3/3/2 frames to the full API
output, with source-identity and single-byte corruption rejection. Training
optimization, upstream numerical parity and qualified data remain outstanding.

Exposed acoustic extraction through `acoustic-targets CONFIG HASH WAV NEW_DIR`.
It captures bounded regular source bytes, verifies the expected digest, computes
targets, writes little-endian float32 data, and publishes hash-bound metadata
last. Subprocess tests verify binary equivalence to the API, hashes, no overwrite
and altered-source rejection without output. This closes standalone extraction,
not dataset/optimizer integration, upstream parity or real singer qualification.

Connected mel extraction to owned, digest-verified WAV bytes through the existing
PCM inspector. Added signed 16/24/32-bit decoding and target/profile/source hashes
without resampling or source writes. Cross-width exact-signal tests passed,
including negative full scale and 24-bit sign extension; wrong digest/rate reject.
This yields acoustic data, but does not yet publish a target cache or train a model.

Added actual log-mel target computation as an optional NumPy API after inspecting
pinned DiffSinger and librosa source. Its explicit full-hop tail policy differs
from unpadded upstream extraction; no learned-model compatibility is claimed.
Local NumPy 2.4.4 tests ran (not skipped), covering silence, partial-hop geometry,
amplitude scaling and invalid input. Upstream numerical comparison, authenticated
audio intake, acoustic shard integration and optimizer consumption remain open.

Implemented source-local, partition-specific column batch consumption of schema-3
shards. The reader recomputes split geometry, checks binding/reference identities,
and compares each shard with conditioning reconstructed from captured labels
before yielding it. Integration tests cover held-out exclusion, altered bytes,
invalid batch limits, and a four-frame phrase split into batches of three and one.
No optimizer or acoustic target loading is implied; callers still own fresh
source/review admission and must discard a training attempt if later I/O fails.

Added optional phrase-sharded dataset assembly through the public CLI. Schema 3
retains only one expanded phrase while writing bounded, hash-referenced feature
files, and publishes the dataset manifest last. Total limits are 1M analysis
frames and 256 MiB per attempt, with 65,536 frames per phrase. Existing output
directories reject; incomplete attempts are retained without a final manifest.
Subprocess checks compare sharded features exactly with inline schema-2 output,
verify hashes/sizes, and ensure retries and snapshot/directory collisions reject.
Trainer-side shard consumption and checkpoint production remain open.

Connected the transform to actual `assemble-dataset` execution. Schema-2
snapshots carry source-sorted conditioning, its canonical digest in dataset
bindings, and the aggregate analysis frame count. API and subprocess coverage
inspect the generated phone/note/pitch/tail values and digest. Expansion is
preflight-bounded to 65,536 frames per compact snapshot; full-corpus sharding and
trainer consumption are explicitly unfinished, not bypassed by raising limits.

Added a bounded pure training feature transform mapping validated phoneme and
score intervals onto the existing F0 analysis clock. Preserved independent
phone/note timing, explicit rest versus MIDI-zero distinction, slur ownership,
expressive F0, and partial-tail sample counts. Unit coverage checks exact
boundaries, unchanged inputs, budget rejection and unresolved confidence issues.
No training run or learned singer is claimed; trainer consumption remains open.

## Reviewed dataset assembly CLI checkpoint

Connected `assemble-dataset` to fresh source-rights and schema-3 label admission,
captured configuration/review references, independently pinned policy anchors,
and deterministic leakage-aware splitting. The output retains configuration
identity and the earliest review expiry. Missing partitions or duplicate-selection
work publish an issue-bearing snapshot with exit 3; invalid inputs reject, and
existing snapshots cannot be overwritten. No training or release approval follows.

Verified the actual subprocess command against currently valid fixture-only
signatures: issue publication, exact configuration binding, no overwrite, changed
review digest rejection, and parent-path rejection. Focused CTest groups
`seam_voice_model_training_tests` and `seam_training_pitch_cli` both passed
(12.89 seconds total). These checks use test material, not a lawful production
corpus or independent musical approval. Real data acquisition, derived-clip
admission joining, training, export, and held-out singer qualification remain open.

## Neural bundle metadata compatibility follow-up

CLI-to-native child handoff: added `check_bundle_runtime.py` and trusted native
`--paired-bundle` experiment mode. The check converts exporter-style vocabulary
with the real CLI, prepares its manifest, inspects the byte-bound bundle offline,
then launches a separate native process that reloads/freeze-verifies the actual
directory before paired inference. Rebuilt runtime/CLI targets and the end-to-end
check passed. A same-length mutation of the vocoder after parent inspection is
rejected by child loading. This advances disk-byte revalidation but is not the
production worker protocol or production pre-session ONNX admission; all graphs
remain application-generated arithmetic fixtures, not learned singer weights.

CLI bundle preparation: added `prepare-neural-bundle DIRECTORY MODEL_ID
MODEL_VERSION MAX_PAYLOAD_BYTES` for four named regular assets. It reads bounded
bytes, builds/freeze-verifies a canonical manifest, validates metadata, creates
the manifest without overwriting, and reloads it before reporting
`DATA_BUNDLE_PREPARED_UNAPPROVED`. CLI integration CTest passed (1/1, 2.47 seconds),
including invalid-metadata/no-publication, exact manifest identity, prepare→
inspect and repeat/no-overwrite. Graph validity remains unclaimed. Assets must
remain stable during preparation; a failed post-publication reload retains the
manifest for diagnosis, not an automatic rollback or multi-file transaction.

CLI directory inspection: exposed `inspect-neural-bundle DIRECTORY MODEL_ID
MODEL_VERSION MANIFEST_SHA256 MAX_PAYLOAD_BYTES`. It uses native directory
loading and metadata inspection, reports verified identity/clock/vocabulary,
and explicitly returns `METADATA_INSPECTED_ONLY` with execution/release flags
false. Rebuilt CLI integration CTest passed (1/1, 3.65 seconds). Tests cover
invalid limits, changed asset rejection and successful metadata-only inspection
of deliberately non-ONNX graph placeholders, preventing a metadata PASS from
being described as executable admission. It performs no installation or writes.

Native directory intake: added `loadNeuralBundleDirectory`, reading bounded
`manifest.json`, validating all declared role/name/size/hash fields and aggregate
payload limits before asset reads, and freezing the exact loaded asset bytes.
Canonical manifest identity and payload hashes are rechecked by the freeze
factory. Initial rebuilt native protocol CTest passed (1/1, 1.12 seconds),
covering successful metadata inspection, wrong identity, payload limits,
changed-file rejection, cancellation and retained immutable bytes after disk
changes. Regular-file/non-symlink checks are point-in-time checks, not race-free
filesystem isolation; byte hashes remain authoritative and graph execution is
not admitted. Peak memory includes read buffers plus frozen copies, so payload
limits are not process-RSS limits. General importer UI and child-side invocation
are still open.

Native CLI preparation integration: added `convert-neural-vocabulary SOURCE_JSON
SOURCE_SHA256 NEW_OUTPUT_JSON` to the existing voicebank CLI. It reads bounded
source bytes, verifies the caller's expected digest, uses the native converter
and durably creates a new output without overwriting. Its report binds both
hashes and remains `CONVERTED_UNAPPROVED`/releaseEligible false. The rebuilt CLI
integration CTest passed (1/1, 2.90 seconds), including wrong-hash/no-output,
alias preservation, output hash, source preservation and no-overwrite cases.
This is a usable preparation action, not a full neural bank importer or release
approval. No Python or ONNX Runtime dependency was added to the CLI.

Converter differential verification: added a bounded native test-helper entry
and `check_vocabulary_parity.py`. The first run exposed differing canonical
bytes: native JSON is two-space-indented with a final newline; Python had emitted
compact JSON. Aligned Python output to the native writer. Sixteen byte-for-byte
comparisons now pass across shuffled maps, aliases, Unicode/escaped Unicode and
larger inventories; twelve invalid-source cases are rejected by both paths.
All 40 offline unit tests also passed. Regenerated converted vocabularies have
new content hashes compared with the earlier compact Python output; existing
serialized vocabulary decoding is unchanged, and bundle manifests must always
bind the actual bytes. This is converter parity, not complete bank admission.

Native vocabulary conversion: added `convertDiffSingerVocabulary` to the neural
library using bounded native JSON parsing. It preserves positive source IDs,
canonicalizes merged alias groups deterministically and rejects gaps, padding
collisions, malformed IDs/names and duplicate keys. Native protocol CTest passed
(1/1, 1.37 seconds). The paired native runtime fixture now uses this converter
before freezing its vocabulary, replacing its hand-authored SEAM vocabulary.
This provides the native conversion service; general bank-import UI, persisted
source provenance and actual learned-bank qualification remain unfinished.

Exporter vocabulary conversion: added `convert_vocabulary.py`, consuming bounded
phone-to-ID JSON and emitting canonical SEAM vocabulary v2 without reassigning
any positive trained ID. Merged aliases are retained, padding is reserved at
zero, sparse IDs are rejected rather than compacted, and output is rechecked
against the metadata parser envelope. Tests reconstruct the complete original
mapping, verify order-independent output, reject malformed mappings, and feed
converted bytes into hash-bound bundle inspection. This closes the offline
mapping conversion gap, not native importer wiring or learned model execution.

Exporter vocabulary compatibility: pinned DiffSinger `PhonemeDictionary.dump`
exports phone-to-ID JSON and merged groups can assign multiple phone names to
one positive ID (`utils/phoneme_utils.py:107-137,187-189`). Added SEAM vocabulary
v2 with canonical `tokens` plus `aliases` mapping names to existing nonpadding
IDs. Native vocabulary size now counts embedding tokens, not alias names.
Native protocol CTest passed (1/1, 1.12 seconds); all 36 offline tests passed,
including shared cross-language IDs and invalid alias collisions/targets.
Legacy vocabulary v1 remains unchanged. Exported-map conversion, language-ID
conditioning and actual learned-bank inference are still open.

Configuration v3 output binding: native/offline readers now require explicit
`vocoderOutput` (`audio` or `waveform`) in v3, retaining v1/v2 legacy semantics.
Offline bundle inspection passes this declaration to actual pair inspection;
native paired fixtures author v3 and reject a graph output name that differs
before inference. Native protocol CTest passed (1/1, 2.44 seconds), dynamic
paired runtime passed and all 35 offline tests passed. This supersedes the
unbound output-name limitation below. The fixture CLI deliberately selects two
fixed profiles (scalar/audio and vector1/waveform); it is not a general bank
importer or production admission interface, and does not execute learned vocals.

Exporter-source intake: cloned `openvpi/DiffSinger` into ignored
`build/neural-runtime/DiffSinger-source` and pinned inspection to
`336cf01b57f2ad44c6b37a79cf33993043291759`. No exporter/dependency/training code
or pretrained weights were executed. Its NSF-HiFiGAN exporter declares output
`waveform`, not our initial fixture's `audio`. Pair inspection now takes an
explicit allowed output name and binds it in the digest; native trusted-fixture
execution handles either declared output. Dynamic paired tests now exercise
`waveform` with vector1 steps. Bundle configuration still has no output-name
field, so its offline wrapper retains the legacy audio default pending a
versioned binding; no arbitrary bank execution is authorized.

Source: `deployment/exporters/nsf_hifigan_exporter.py` at the pinned revision,
input/output names and opset 17 in `_torch_export_model`. The acoustic exporter
also has optional language, speaker, variance, gender, velocity and depth paths.
These are not automatically covered by the minimal paired profile. The source
checkout includes an Apache-2.0 license; this is not evidence of permissions
for separately obtained voice weights, recordings or training datasets.

Native v2 execution binding: the paired runtime fixture now freezes explicit v2
spectral declarations and application-selected scalar/vector1 steps layout.
The actual graph steps rank must match that metadata before inference. Release
runtime target rebuilt and paired check passed: both layouts execute at both
sequence lengths, opposite declared layouts fail before Run, and wrong-hop
output fails afterward. This supersedes the v1 fixture limitation below.
It does not prove FFT/window/mel semantics from actual learned model behavior
or establish production graph admission and worker execution.

Configuration v2 follow-up: native and offline metadata readers now require
`fftSize`, `windowSize`, `melFrequencyScale` on both acoustic/vocoder feature
objects and root `stepsLayout`. FFT bounds are 2..32768; window must be positive,
no larger than FFT, and at least the hop. Frequency scale is explicitly Slaney
or HTK; steps layout is scalar or vector1. Pair declarations must match exactly.
Version 1 remains readable with unspecified spectral fields and a legacy scalar
layout; no missing values are inferred. Native protocol CTest passed (1/1,
2.96 seconds); all 33 offline tests passed, including actual vector1 graph
inspection through v2 bundle configuration. This supersedes the missing-field
and scalar-only bundle limitations below, but not graph/runtime compatibility
or execution admission. The native arithmetic fixture still authors v1 metadata.

Source-backed exporter correction: inspected the existing OpenUtau checkout at
`8c0dc4007e6e8c8181f3a12c10205671800eeb8b`. Its
`OpenUtau.Core/DiffSinger/DiffSingerRenderer.cs:275` constructs continuous
acceleration `steps` as int64 `[1]`, not the scalar used in our initial fixture.
Offline pair inspection now accepts an explicit `steps_layout` choice, binds
it in the contract digest and rejects mismatched ranks. Native fixture execution
inspects the actual steps input and supports exactly scalar or `[1]`. Both
layouts passed dynamic paired execution; wrong-hop output still fails. All 31
offline tests passed. Bundle configuration currently has no steps-layout field,
so its existing inspector retains scalar behavior; production schema binding
must be completed rather than inferring rank silently.

The same source review found missing metadata dimensions in our configuration:
FFT size, analysis-window size and mel-frequency scale (Slaney versus HTK).
OpenUtau checks these in addition to sample rate, hop, mel bins and frequency
range. Our current metadata cannot establish their compatibility. This is an
open admission requirement, not justification to accept an arbitrary exported
pair. No learned model assets were found in the checked project paths.

Offline JSON-bound follow-up: added a quote/escape-aware container-depth check
before recursive decoding and post-decode node, collection, finite-number and
128-byte UTF-8 string checks. Configuration uses native 128-node/16-entry
limits; vocabulary uses its separate larger limits. All 30 offline inspection
tests passed, including 4000-level nesting rejection, braces within strings,
numeric overflow and collection/node limits. Post-decode limits do not bound
peak allocation, and no hard parser-process memory ceiling is claimed.

Neural process-budget propagation: source inspection found the neural runner
did not forward the platform helper's resident-memory/CPU limits. Added
application-owned run options and forwarded both fields. Actual child probes
verify resident-memory and CPU-time termination with their specific diagnostics;
negative CPU limits are rejected. Rebuilt neural protocol CTest passed (1/1,
2.63 seconds). Zero defaults retain legacy v1 behavior; production admission
still must choose measured nonzero budgets. Sampling remains best-effort and
is not an OS sandbox or Windows/installed-host qualification.

Frozen-identity runtime integration: the paired native experiment now freezes
its two loaded graphs plus fixture configuration/vocabulary through the actual
`FrozenNeuralBundle` factory, inspects native metadata, and constructs ORT
sessions from the frozen graph spans. Requests use the real manifest-derived
model digest and frozen vocabulary, replacing the placeholder model hash.
Release runtime target rebuilt and dynamic paired execution passed, including
the wrong-hop rejection case. Fixture configuration remains application-authored
and fixed to this experiment; this does not implement arbitrary bank import,
production pre-session graph admission, signed worker launch or learned vocals.

Vocabulary policy reconciliation: source comparison found offline intake had
allowed 256-byte/control-character tokens while native decoding limits tokens
to 128 UTF-8 bytes and excludes C0/DEL. Offline intake also incorrectly capped
vocabulary entries at 4096 rather than native decoding's 65536 (phone-span limits
are separate). Corrected those policies. All 26 offline tests passed, including
UTF-8 byte boundaries and a 4097-entry vocabulary. Mirrored native boundary and
control-character regression cases passed in the rebuilt neural protocol CTest
(1/1, 7.54 seconds). This is targeted vocabulary-policy reconciliation, not a
claim of complete differential parity for all metadata or graph admission.

Offline bundle-binding follow-up: `inspect_bundle.py` accepts only immutable
manifest/asset bytes and an expected manifest digest, verifies asset hash/size
closure and required roles, then derives graph-pair parameters from the actual
configuration bytes and checks the vocabulary. Six new tests cover success,
changed bytes, wrong manifest identity, unlisted assets, duplicate JSON and a
rehashed configuration that contradicts graph mel bins. All 23 offline
inspection tests passed. This closes the caller-supplied-parameter gap in the
offline tool only; runtime enforcement, cross-language parity and executable
admission remain open. Optional variance/tensor roles are explicitly unsupported
by this initial paired execution profile, not removed from the full plan.

Response-path follow-up after the full-suite checkpoint below: shared
`finalizeDiffSingerResponse` performs worker-side trim/gain finalization,
constructs the normalized response and binds it to the canonical request hash.
The native paired experiment now round-trips the actual response codec too,
checking request binding and exactly-once gain. Tests verify changed dynamics
change both returned PCM and the request digest, and reject an empty backend
identity. Release neural/runtime targets built; neural protocol CTest passed
(1/1, 0.94 seconds), followed by the dynamic native paired check. The existing
receiving backend was inspected and does not apply another dynamics pass.
No fresh full-suite claim for this follow-up, and no actual production helper
launch, admitted learned model or normal song integration is established.

Integrated verification checkpoint: complete Release build passed, followed by
a fresh full CTest run: 124/124 passed in 293.51 seconds. Separately, all 17
offline graph/pair inspection tests and both native runtime experiments passed
again against the rebuilt binary. This supersedes the earlier focused-only
regression boundary for this accumulated change set. It does not qualify a
learned singer, production graph admission, Windows execution, actual installed
host matrix, human listening or full Beta GO. Linker duplicate-library warnings
were present; the build completed successfully.

Request-conditioning integration follow-up: the native paired probe now links
the real neural library, serializes/deserializes a sample-domain request and
uses `prepareDiffSingerAcousticInputs` rather than handwritten tensors. Both
runtime cases exercise a 37-sample partial-hop tail. New shared
`finalizeDiffSingerAudio` validates the entire padded mono buffer, trims to
the request count and applies sample-domain dynamics once, rejecting invalid
raw/tail PCM or gain overflow instead of clipping. Release targets built;
neural protocol CTest passed (1/1, 0.89 seconds), including finalizer shape,
tail, gain, identity and cancellation cases; dynamic native paired check
passed. This supersedes the earlier manual-tensor limitation, not the open
production-worker, real model, graph admission or normal song integration.

Native paired execution follow-up: the optional runtime probe now executes
tokens/durations/f0/scalar-steps acoustic inputs, checks finite `[1,T,80]` mel,
passes mel plus f0 into a second native session, and checks finite `[1,T*256]`
audio with expected values. The generated dynamic arithmetic pair passed at
T=3 and T=5 using the same sessions. A pair with identical declared interfaces
but an actual 128-sample hop passed offline inspection and was correctly
rejected at runtime for output shape. `check_paired_runtime.py` passed after
the Release probe rebuild. This remains a controlled integration experiment:
no learned singer, normal song request bridge or production worker is claimed.

Paired-interface follow-up: `inspect_pair.py` directly inspects both graph byte
strings against a proposed SEAM export profile matching the existing prepared
acoustic inputs (tokens/durations/f0/steps) and a pitch-conditioned vocoder.
It checks names, dtypes, ranks, intra-graph axis relationships, mel layout/bins
and bounded maximum mel-buffer sizing; its contract digest binds graph hashes
and supplied parameters. Eight structural tests passed. This is not universal
DiffSinger compatibility or production execution admission. Constant graph
fixtures do not prove actual output shapes, hop timing, learned conditioning,
musical quality, or that the supplied parameters match frozen configuration.

Offline pre-runtime intake now exists in `tools/neural_runtime/inspect_graph.py`.
It parses bounded bytes without external-data resolution, recursively rejects
external tensors/custom operators (including nested graphs and attributes),
checks standard ONNX structure, and reports hash-bound actual interfaces.
Nine focused Python tests passed, covering the trusted graph report, nested
rejections, oversized dimension product, custom imports, empty input and
truthful reporting of unresolved dynamic dimensions. This is not production
admission: model-family bounds, execution budgets and child-side enforcement
remain open. The positive arithmetic fixture runner now invokes intake before
native inference; direct native invocation remains trusted-fixture-only.

Native-runtime follow-up: the optional arithmetic probe now reads bounded owned
graph bytes once (16 MiB each), creates sessions from memory and checks actual
session tensor names, float32 dtype and exact fixture rank/dimensions before
inference. The manual Python/native check passed with repeated correct output,
wrong-scale rejection, swapped graph rejection, wrong shape/rank/dtype and
dynamic-dimension rejection, plus empty/oversized input rejection. This is
post-parse runtime introspection of trusted generated fixtures, not pre-session
operator admission, external-tensor safety, DiffSinger execution or singing.
The production pre-session graph inspection requirement remains open.

Added `inspectNeuralBundleMetadata` over immutable frozen bundle bytes. It
binds the vocabulary digest and model identity, parses bounded configuration,
and requires acoustic/vocoder agreement on sample rate, hop, mel bins, layout,
amplitude encoding, multiplier, offset and frequency range. Unknown fields and
invalid numeric domains are rejected even when both declarations match.

The configuration schema is `com.project-seam.neural-bundle-configuration`
version 1. Its exact root fields are `formatId`, `schemaVersion`,
`maximumFrames`, `acousticFeatures` and `vocoderFeatures`. Both feature objects
contain `sampleRate`, `hopSize`, `bins`, `layout`, `amplitudeScale`,
`multiplier`, `offset`, `minimumHz` and `maximumHz`. This internal declaration
schema is not yet the complete production model/export contract.

Verification: Release neural protocol target built; its CTest passed (1/1,
3.39 seconds), including independent feature mismatches, matching invalid
declarations, frame bounds, unknown executable field, invalid vocabulary and
cancellation. No full-suite or graph admission result is claimed.

This API deliberately returns metadata, not executable admission. The test
uses non-ONNX graph placeholders: passing it proves no graph compatibility.
Next: bounded actual graph inspection, immutable executable admission,
application-selected bundle transport and child-side byte re-admission, then
production acoustic/vocoder execution. Windows supervision, model training,
rights and musical qualification remain open under the original plan.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | In progress: inventory v2→producer v4; language-bound generation; multi-style draft/review/publication; legacy readers retained | Two-style initialization, generation collection, native-controller draft creation, review and candidate reopen regressions | Explicit legacy migration and populated-history parity remain | Complete migration, then campaign/articulation |
| M1.P2/P3 | Not completed by this increment | Existing procedural/producer foundation retained | Connected articulation, campaign, actual bank and unfamiliar-song evidence | Continue after the necessary M1.P1 producer bindings |
| M2–M6 | Remaining full scope retained | No new milestone qualification | As specified by the implementation plan | Independent neural process/data work remains available |

## Verification checkpoint — September 13, 2026

- Nine draft tests plus eight legacy inventory tests passed (17 total), including
  real CLI generation, exact numeric types, bounded IDs and legacy-writer rejection.
- CMake configure succeeded and registered `seam_draft_inventory_tests` passed.
- The focused CTest target was rerun after the bounded-ID case was added.
- The producer now defines `ProductionUnitIdentity` with exact language/style/
  coverage/layer equality and a canonical inventory-v2 SHA256. Python-generated
  rows and C++ agree on ASCII and quoted Japanese-label golden vectors; distinct
  style slugs cannot merge assignments.
- Producer schema 4 now persists a workspace language and per-assignment/take
  style, checks four-axis duplicate/retake ownership, and retains legacy 1–3
  serialization. Raw and generated import matching use style; source assessment
  retains v4 and includes language/style in its material identity. Review and
  single-style manifest paths reject relabeling. The later publication checkpoint
  below admits complete schema-4 style matrices through the canonical publisher.
- Tests import identical PCM into two distinct styles, recover the durable
  workspace, and reject cross-style retakes, missing styles, language changes,
  and relabeling existing takes without mutating saved state. Generic save cannot
  masquerade as legacy migration; the explicit migration operation is pending.
- At checkpoint `5291e652`, rebuilt the complete configured Release tree and ran all **122 CTest targets:
  122 passed, zero failed** (86.13 seconds). The producer target now has 49 cases.
- Following that checkpoint, score-job preparation and CLI collection now carry
  assignment style. Expectation v2 carries explicit language; legacy expectation
  v1 retains its format. Tests prepare/load/render a schema-4 job, collect it via
  the actual CLI, and repeat collection without creating another generation.
  Wrong-style preparation and wrong-language collection fail without changing
  producer state; malformed v2 language/version fields are rejected.
- Rebuilt the complete Release tree after the generation integration. The three
  focused CTest targets (export workflow, draft inventory, producer) passed.
  The 122-target run above predates this latest integration, not a fresh claim.
- Version-aware Python definition preparation now maps inventory v2 to producer
  v4 without repeating workspace language in assignment rows. C++ init-production
  admits empty v4 drafts and still rejects preapproved/imported material. Python
  verification checks style keys, retake binding, immutable language/take identity
  and schema-4 source-quality material hashes. Legacy generic inventory readers
  remain unchanged.
- New parity tests execute both preparation and initialization CLIs, then verify
  the resulting two-style workspace in Python. Missing language/style, duplicate
  assignments and schema relabeling are rejected by both readers. All 33 focused
  Python tests passed; registered draft inventory, external-beta Python contract
  and sample-review CLI tests also passed. This is not exhaustive full-product
  or populated multi-style publication qualification.
- No generated voice, musical review, qualified range, installed bank or Beta GO
  is claimed. The complete implementation goal remains active.

## Multi-style review integration

- Schema-4 workspaces can now prepare and apply a multi-style review packet
  through the shared review service. Legacy style-free workspaces remain rejected.
- A synthetic regression uses two takes with identical PCM, phone coverage and
  pitch but different styles. Reviewing the first does not review the second;
  the second requires a fresh packet and its own explicit decision. Stale packets
  and relabeling into the other style fail, and durable recovery retains both
  separate review records.
- Producer, native Studio review and sample-review CLI targets rebuilt and all
  three focused CTest targets passed. The producer target has 50 cases.
- This review checkpoint alone did not admit publication. See the subsequent
  publication integration below. No musical approval outside the explicitly
  synthetic tests was created.

## Multi-style candidate publication integration

- The canonical publisher now admits schema-4 multi-style workspaces only when
  declared styles exactly match assignment ownership, all styles have the same
  required phone/pitch matrix, and every current assignment has its own valid
  take and retained independent review. Legacy workspaces keep the single-style
  restriction. No approval is inferred from shared PCM.
- Regression coverage publishes and reopens a two-style candidate, verifies its
  manifest and content hash, and rejects omitted styles, reused review evidence,
  extra declared styles and asymmetric phone/pitch requirements.
- Schema-4 source qualification now requires an explicit current source-quality
  assessment. C++ and Python no longer allow the historical no-assessment fallback
  for these workspaces. Source execution remains separate and does not need a
  musical PASS. Test evidence is expressly synthetic, not a real evaluation.
- Scope clarification from code inspection: Python `_production_candidate.py`
  validates a separate legacy `READY`/`unitBindings` export contract, not the
  canonical C++ `com.project-seam.resource-candidate` descriptor. Its pair-based
  legacy contract is not being reinterpreted as authority for these candidates.
- This remains an engineering candidate with `releaseEligible: false`. Native
  multi-style draft authoring, explicit legacy migration, generation campaigns,
  real singer quality and the remaining M1–M6 obligations are still unfinished.
- Verification after this integration: complete configured Release build passed;
  **122/122 registered CTest targets passed**, zero failures (82.22 seconds).

## Native multi-style draft creation

- The shared draft builder now derives all schema-4 styles from assignments and
  generates style-distinct unit IDs. Missing takes retain style-qualified labels.
  The selected identity style must belong to the workspace; it cannot relabel or
  filter its other assignments. Legacy workspaces retain explicit single-style
  behavior. CLI help and Studio's progress status explain the all-styles behavior.
- Tests create a partial and complete two-style draft, prepare it for review,
  reject an unknown style, and prove that selecting either existing style retains
  identical manifest content. A native-controller test asynchronously creates and
  opens both styles without changing producer state or creating reviews.
- Shared manifest, Studio manifest and sample-review CLI targets passed after
  rebuilding affected targets. The prior 122-target run predates this increment;
  no fresh desktop visual QA or musical qualification is claimed.

## Legacy migration preparation

- Added `python3 -m tools.external_beta.voicebank_production prepare-style-migration
  --workspace WORKSPACE --inventory LEGACY_INVENTORY --output NEW_PLAN_JSON`.
  It verifies durable history and matching inventory, captures the source bytes'
  SHA256/generation and inventory evidence, and writes only a new plan outside
  the workspace. Existing output paths are not overwritten.
- A singleton style in the validated legacy inventory can resolve ownership.
  Multi-style legacy inventories produce `UNRESOLVED` with per-assignment reasons;
  the planner does not guess a selected style. A resolved proposal retains old
  reviews/source bindings but clears active marker/pitch approval and requires
  source-quality reassessment. Its generation is not advanced by the planner.
- All 18 production-draft parity tests passed, including actual CLI invocation,
  deterministic proposal content, ambiguous styles, unchanged workspace bytes,
  preservation of historical evidence and rejection of in-workspace/overwrite
  destinations. The proposed state is validated against the target schema.
- **Not yet applied:** the C++ durable migration operation, retained migration
  receipt and history-transition verification remain to implement. Generic save
  continues to reject a schema upgrade; this plan cannot bypass that boundary.

## M1.P2 audible pilot started

- Added a reproducible `seam_singer_pilot` executable using the existing production
  export path, not a separate DSP implementation. It creates saved scores, editable
  recipes, master WAVs and unapproved baked candidates for a six-note Japanese
  vowel/fricative ladder and three phonation/formant variants.
- Retained complete local outputs in `build/release/seam-pilot-listening-02/`.
  Master peaks are about 0.0645; RMS spans 0.0126–0.0221. Distinct variant hashes
  establish different PCM, not perceived improvement or female identity.
- The first run exposed a harness metadata-as-WAV measurement error; fixed it
  and preserved the partial directory. Registered a real-CLI test for repeated
  identical audio hashes, finite/nonzero unclipped diagnostic output, distinct
  recipe identities and refusal to overwrite an existing destination. It passed.
- No listening verdict, complete articulation coverage, installed pilot bank or
  unfamiliar-song acceptance is claimed. Next: analyze the retained phrase timing,
  pitch and transitions, expand consonant/context probes, and connect inventory
  campaign generation. Explicit legacy migration remains unfinished but does not
  prevent the new-workspace pilot work.

### Pilot steady-pitch measurement

- The pilot now emits per-variant pitch diagnostics tied to the dry candidate's
  SHA256. It uses the existing broad-range FFT pitch analyzer, with fixed central
  half-note windows derived from the project tempo map. Full analysis windows
  must fit inside those intervals. Unvoiced frames remain in the denominator;
  missing voiced estimates produce null medians, not zero error.
- Retained run `build/release/seam-pilot-listening-03/`: the baseline has 92
  analyzed windows, all voiced and within 50 cents; per-note median absolute
  errors range from 0.052 to 0.350 cents. This supports steady-pitch behavior for
  this six-note fixture only, not transitions, timing-edit accuracy, language
  intelligibility, singer identity or Beta qualification.
- Rebuilt the pilot and passed its real-CLI regression (1/1), now checking
  diagnostic hash binding, denominators and baseline pitch. No full-suite rerun
  is claimed. Next synthesis investigation should prioritize consonant/context
  transitions and articulation coverage over steady-pitch changes.

### Expanded articulation listening fixture

- Added an explicit `articulation` pilot mode: fourteen Japanese CV notes,
  `ma mi mu me mo na ni nu ne no pa ta ka sa`, rendered through ordinary export
  in the same three variants. Recipes explicitly bind nasal resonance and
  antiresonance for m/n, separate released-stop bursts for p/t/k, and s noise.
  This extends the diagnostic recipe, not the renderer's supported source types.
- Complete local audio/scores/recipes/markers/pitch diagnostics are retained at
  `build/release/seam-pilot-articulation-01/`. No auditory verdict is asserted.
- The real CLI regression now repeats both fixtures and verifies exact 28-phone
  coverage, four gesture classes, ordered contiguous planned boundaries, complete
  candidate span, actual SHA256 binding and unapproved state. Rebuilt executable
  and focused CTest passed (1/1, 2.85 seconds); no full-suite run claimed.
- M1.P2 remains open: context/transition semantics, remaining consonant families,
  held-out linguistic phrases and resumable inventory generation are not supplied
  by this diagnostic. Prioritize those gaps rather than treating marker coverage
  or steady vowel pitch as proof of an intelligible singer.

### Inventory assignment to real generation job

- Added shared `inventory_generation.hpp/.cpp`: deterministic template-v1 score
  construction from a unique schema-4 Japanese producer assignment. The score
  retains canonical coverage phones as an explicit phonetic hint, assignment
  pitch/style, stable IDs and a template hash. It uses one 960-tick note at the
  default 120 BPM; this is an initial timing template, not complete context design.
- `prepareInventoryGenerationJob` writes a create-new score and delegates to
  `prepareGenerationJobFromScore` with its exact hash and selected recipe. Job IDs
  include take identity; expectations capture producer state at preparation time.
  No producer mutation, automatic approval or alternate renderer is introduced.
- The integration test uses a durably initialized synthetic producer, prepares
  `cv:s:a`, renders the actual job, verifies ownership and unchanged producer
  bytes, and rejects duplicate output, wrong style, duplicate assignment identity,
  unsupported language and `release:a:R` (unsupported adapter phone). The initial
  test accidentally used generation zero; its rejection was retained as a fixture
  correction, not bypassed in production. Export CTest passed, 1/1 (4.45 seconds).
- Still required: inventory-file admission/CLI, coverage-wide unsupported-context
  reporting, additional timing templates and resumable campaign prepare/render/
  collect receipts. Do not prepare an entire campaign's expectations up front.
- Inspection also found `generation_batch.cpp` still deduplicates assignments by
  coverage/pitch without language/style. Repair and test this before admitting
  a multi-style campaign; this increment does not claim that path complete.

### Multi-style generation batch repair

- Repaired the preceding batch-admission gap: version-2 expectations now use
  `ProductionUnitIdentity` (language/style/coverage/pitch). Legacy expectations
  still use their original style-free identity; recipe style cannot create a
  second legacy assignment. Duplicate job/take and frame-budget checks remain.
- A real integration fixture initializes two same-phone/same-pitch assignments,
  prepares each through the inventory score builder, admits/renders the batch,
  saves its manifest, collects both atomically and reopens the durable producer.
  Both styles survive; neither assignment gains marker/pitch approval. Repeated
  job references and a budget one frame below the required total are rejected.
- Focused export CTest passed (1/1, 3.96 seconds). Complete configured Release
  build also passed. This closes the batch identity mismatch, not the campaign
  scheduler, restart receipts or real singer qualification.
- Full configured regression after this repair: **123/123 CTest targets passed**,
  zero failures, 87.81 seconds. This also covers the intervening inventory-score
  and listening-pilot increments; it does not stand in for installed-host or
  independent musical acceptance.

### Bounded immutable campaign planning

- Added shared `generation_campaign.hpp/.cpp`. `planGenerationCampaign` accepts
  explicit planned take IDs, validates the producer and frozen recipe, constructs
  each inventory template and preflights it through the normal procedural snapshot
  compiler. It keeps only one temporary compiled snapshot at a time. Unsupported
  takes fail with the take ID; there is no truncation or substitute silence.
- Definitions capture exact initial producer JSON/hash (including inventory and
  source-policy evidence), recipe JSON/hash, score JSON/template identity, ordered
  take/style/coverage/pitch rows, frame totals and deterministic batch membership.
  Input order does not affect the canonical definition. Every job is UNPREPARED;
  no generation expectation is captured or producer/filesystem state changed.
- Admission retains the existing 64-job/32M-frame batch ceiling and applies
  aggregate job/frame/estimated-byte limits. Disk numbers are conservative planning
  allowances, not measured filesystem quotas. Definition serialization is bounded
  to 32 MiB. Cancellation is checked before and between template compilation.
- `verifyGenerationCampaign` requires the supplied digest, decodes frozen inputs,
  reconstructs the canonical plan and compares exact bytes. Changed totals,
  batch layouts, hidden expectation fields and numeric type spoofing fail even
  with a recomputed outer digest. This proves internal consistency against the
  selected digest, not authority to replace that digest or source approvals.
- Focused Release build and export CTest passed (1/1, 4.44 seconds), covering
  deterministic two-batch planning, resource mismatch, budgets, cancellation and
  adversarial definitions alongside real two-style generation/collection. The
  prior 123-target run predates this increment; no new full-suite run claimed.
- Still open: create/inspect CLI publication, filesystem quota enforcement during
  execution, just-in-time batch preparation, durable advancement/commit receipts,
  external-edit detection and crash/restart integration. A valid plan is not a
  completed or resumable campaign yet.

### Campaign CLI publication and inspection

- Added separate CLI command module, using shared planning/verification services:
  `draft-generation-campaign WORKSPACE RECIPE NEW_PLAN TAKE...`,
  `plan-generation-campaign WORKSPACE PLAN HASH NEW_DIRECTORY`, and
  `inspect-generation-campaign CAMPAIGN HASH`. Drafting uses default limits and
  explicit selected take IDs; publication retains the exact caller-selected
  plan bytes/hash, not regenerated expectations or a changed recipe.
- Plan publication recovers the current producer and rejects a changed initial
  state before creating output. The new directory's atomic `campaign.json` is the
  publication boundary; partial directories are retained and never overwritten.
  This is a point-in-time state check, not a workspace lock or authorization to
  run later without rechecking. Inspection proves frozen-plan consistency only.
- Real CLI tests draft, inspect and publish, reject repeated destinations and a
  wrong hash, verify unchanged producer bytes, then collect real two-style output
  and confirm stale publication fails before directory creation.
- Advancement remains unimplemented: just-in-time preparation and durable
  completion/recovery receipts must precede any resumable-execution claim.
- Verification: focused Release build and export CTest passed (1/1, 3.92 seconds).
  No fresh full-suite result or musical acceptance is claimed.

### Recoverable batch collection checkpoint

- Added `collectGenerationBatchWithReceipt`, a shared transaction building block
  for campaign advancement. It requires the original producer snapshot and frozen
  job references, uses a persistent exclusive receipt lock, validates ownership
  and budgets, then delegates atomic collection to the existing repository.
- Retry recognizes every original expectation through retained producer lineage.
  Partial recognition, changed initial state, an extra producer generation, or a
  conflicting receipt fails. Exact receipt retries neither collect nor generate
  again. Receipt bytes bind original/committed producer hashes, generation, take
  audio hashes and expectation hashes; they are create-new and never overwritten.
- Tests inject an interruption after the real two-style commit but before receipt
  publication, hide both temporary output directories, then recover the receipt
  from producer-owned assets without regenerating audio or advancing generation.
  Repeated recovery preserves receipt bytes; false receipts and an unrelated
  later producer save are rejected.
- Important remaining durability boundary: repository read-only recovery does not
  re-fsync its mutable current pointer. Recovered results therefore deliberately
  return `durabilityConfirmed=false` with a diagnostic, even though the receipt
  file itself is durably written. Add locked exact-generation pointer
  reconciliation before permitting the next campaign batch. Do not interpret
  successful recognition as permission to skip this boundary.
- This is not yet the complete campaign runner: batch preparation/resume, pointer
  reconciliation, advancement CLI and broader process-crash tests remain open.
- Verification: affected Release targets rebuilt and export CTest passed (1/1,
  4.30 seconds). No new complete-suite run claimed.

### Exact current-pointer reconciliation

- Added repository `reconcileCurrentPointer(expectedGeneration, expectedHash)`.
  It takes the existing workspace writer lock, refuses newer occupied generation
  or journal records, verifies recovered state against both supplied identities,
  and durably republishes `project.json`. It does not append generations, alter
  immutable record contents or change reviews/source policy. Cancellation is
  checked before locking and at the last pre-publication boundary.
- Batch receipt recovery now invokes this operation after recognizing the exact
  committed requests. This supersedes the preceding temporary unconfirmed-pointer
  result: recovery returns confirmed only after exact locked pointer publication
  succeeds. An error still leaves the committed take recoverable, never reimports.
- The integration fixture damages the pointer after the injected post-commit
  interruption. Wrong hash, cancellation and a competing writer are rejected;
  normal retry restores the exact pointer hash without adding a generation or
  regenerating hidden output. The existing later-external-change rejection stays.
- This verifies the pointer repair path, not arbitrary power-loss behavior across
  filesystems/platforms. Campaign batch preparation and advancement are still open.
- Affected Release targets rebuilt and focused export CTest passed (1/1, 3.89
  seconds). No fresh complete-suite or installed-host result claimed.

### Explicit generation preparation recovery

- Campaign integration exposed a prerequisite: existing job preparation required
  a brand-new directory and could not resume partially written inputs. New job
  preparation now publishes `preparation.json` first, containing the exact future
  manifest and its score/recipe/expectation digests. It then publishes inputs,
  reference and final `job.json` in that order under a preparation lock.
- Added explicit `resumeGenerationJobPreparation` using the original snapshot,
  producer and take. Recomputed intent must match exactly. All existing named
  files are checked for regular-file status and exact bytes before any missing
  file is written. Changed producer expectations, conflicting content, symlinks
  and directories without intent are not adopted. No files are overwritten.
- Existing prepare APIs retain create-new behavior. Existing complete jobs remain
  readable without the new intent. An old partial job or interruption between
  directory creation and intent publication remains unowned and is not silently
  repaired; campaign-level recovery must preserve that artifact explicitly.
- Tests retain a complete job, hide score/expectation/final manifest, reject a
  changed producer and tampered recipe before filling any gaps, then restore the
  exact inputs and recover the original manifest/expectation hashes. Repeating
  explicit resume succeeds while ordinary prepare and unowned-directory resume
  remain rejected. These are interruption-state fixtures, not OS process-kill tests.
- This is the job-preparation building block, not yet campaign batch advancement.
- Verification: affected Release build and focused export CTest passed (1/1,
  4.36 seconds). No fresh complete-suite run claimed.

### Just-in-time campaign batch preparation integration

- Added shared `prepareGenerationCampaignBatch`. It verifies the immutable plan,
  bounds the batch index, matches batch 0 to the exact initial producer, and
  requires a confirmed predecessor receipt/current-producer hash and expected
  generation offset for every later batch. This is an internal service: callers
  must supply a verified collection result, not trust arbitrary receipt JSON.
- Each batch retains its original producer JSON and campaign/index identity under
  a preparation lock. Selected score templates must still exactly match frozen
  plan bytes. Job creation/resume uses the retained-intent preparation service;
  batch manifests are create-new or verified against the exact prepared job list.
  No rendering, collection or approval happens during batch preparation.
- A real two-batch integration now plans two styles, prepares/retries batch 0,
  renders and collects it, recovers the producer, prepares batch 1 using the first
  confirmed receipt, renders and collects batch 1, and reopens both takes. The
  second job's expectation binds the first commit's hash, explicitly not the
  initial hash. Premature batch 1 and stale batch 0 are rejected before creating
  their directories. This demonstrates sequential preparation, not only planning.
- Focused Release build and export CTest passed (1/1, 3.87 seconds). No new full
  suite run is claimed. The persisted advancement controller/CLI, authoritative
  receipt-chain loading, runtime disk quotas, crash-window handling before intent
  publication, and OS process-kill tests remain open. Do not label this a complete
  resumable campaign runner yet.

### Repository-backed historical receipt verification

- Added exact-hash `recoverGeneration` and optional historical generation/hash
  arguments to `findCollectedGeneration`. Historical reads validate the requested
  immutable generation and its normal repository evidence; they never move the
  current pointer or silently substitute a newer/older recoverable snapshot.
- Added `loadVerifiedGenerationBatchReceipt`. It verifies the original producer
  against stored history, checks frozen job expectations, resolves the exact next
  generation, validates take lineage/audio identities in that historical state,
  and reconstructs canonical receipt bytes. Unknown fields, altered values or
  a valid hash belonging to the wrong generation are rejected.
- The two-batch test verifies batch 0's saved receipt after batch 1 has committed:
  the returned historical state contains one take, while the current pointer and
  two-take workspace remain untouched. Negative cases replace the committed state
  hash with the latest generation hash, forge an audio hash, insert an unknown
  field, or request a missing/wrong-hash historical generation.
- This supplies authoritative persisted-receipt loading for the forthcoming
  advancement loop. Historical verification alone does not assert currentness or
  restore a pointer; the loop must compare its final state with the current
  producer and use exact reconciliation before further mutations.
- Verification: affected Release build and focused export CTest passed (1/1,
  4.22 seconds). No fresh complete-suite run claimed.

### Persisted campaign advancement loop and CLI

- Added `advanceGenerationCampaign` and the planned CLI form:
  `advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC`.
  The immutable campaign and repository history are authoritative; there is no
  mutable unchecked progress counter. A campaign lock serializes invocations.
- The loop reconstructs completed batch state using verified historical receipts
  and exact frozen templates. For the first incomplete batch it requires the
  expected producer state, prepares/resumes original jobs, renders only if the
  requests have not already been collected, and atomically collects with a durable
  receipt. Each invocation advances at most one incomplete batch.
- A producer one generation ahead is considered only when retained batch inputs
  exist, and original-request recognition must prove every take before skipping
  rendering. This handles commit-before-receipt interruption without rebinding
  expectations. Partial collection and unrelated changes fail. A completed retry
  verifies the whole chain and requires current producer equality.
- CLI reports `BATCH_COLLECTED` or `COLLECTED_UNREVIEWED`, always with
  `releaseEligible:false`. It does not manufacture source rights, independent
  reviews, qualified singer resources or release approval.
- Integration tests inject a post-commit/pre-receipt interruption in batch 0,
  recover it with one take still present, advance batch 1 through the real CLI,
  verify two takes, repeat without a generation change, then reject an external
  producer save. Focused export CTest passed (1/1, 4.20 seconds).
- Remaining hardening: directory-created/intent-not-yet-published recovery,
  hard runtime disk quota accounting, process-kill/cancellation coverage and
  large-campaign performance. Current completed-batch traversal revalidates plans
  repeatedly; measure and eliminate redundant compilation before singer-scale
  campaigns. This pilot-scale working loop does not close all M1.P2 obligations.
- Full configured Release build passed. Full CTest run: 122 passed, one failed
  (84.18 seconds); source closure correctly reported the new advancement source
  was not yet indexed. After staging that exact file, the source-closure target
  passed (1/1, 0.22 seconds). No code changed between those runs; all 123 targets
  now have passing evidence, but no second all-green full invocation is claimed.

### Reuse immutable campaign admission during traversal

- Added `VerifiedGenerationCampaign`: callers cannot construct it from unchecked
  JSON. Admission performs the existing digest/canonical reconstruction checks and
  owns an immutable parsed plan. Copies share that plan; changing the source text
  after admission cannot change its content or digest. Moved-from handles are
  rejected by preparation before filesystem writes.
- Advancement now admits once and passes the handle through each visited batch.
  Previously it performed a full-plan verification initially and again for every
  batch; each verification compiled every campaign template. This change removes
  those repeated whole-plan preflights without removing per-batch producer,
  predecessor, frozen-score, prepared-job or repository-history checks. Existing
  string-based preparation remains a wrapper that fully admits its input.
- Tests exercise shared immutable ownership, source-buffer replacement, wrong
  digest and moved-from rejection, then run the existing two-batch/CLI recovery
  flow through the admitted-handle path. This is a structural reduction in repeated
  work; no singer-scale wall-clock speedup is claimed without a benchmark.
- Remaining scaling work includes indexing selected rows and reducing repeated
  historical job loads. Runtime disk bounds and OS process-crash qualification
  also remain open; this change does not complete M1.P2.
- Verification: affected Release build and export CTest passed (1/1, 4.55 seconds).
  No new complete-suite run claimed.

### Real process termination at campaign commit boundary

- Extended the existing generation test helper with a campaign mode that raises
  SIGKILL after the producer commit and before collection-receipt publication.
  The parent uses actual process wait status and requires termination by SIGKILL,
  rather than accepting a generic failure or an injected Result error. The signal
  path is enabled on macOS/Linux; this run was on macOS.
- The fixture confirms one committed take and no receipt, moves the temporary
  output directory aside, then retries through the normal advancement CLI. Retry
  succeeds with exactly unchanged producer bytes and batch manifest hash and does
  not recreate output. It then advances the second batch and retains the existing
  completed-retry and external-edit rejection checks. This also exercises release
  of campaign/receipt OS locks when destructors cannot run.
- Focused Release build and export CTest passed (1/1, 4.40 seconds). This is real
  process-death evidence for the post-commit/pre-receipt window only, not a machine
  power-loss test or complete campaign crash matrix. Preparation-intent windows,
  cancellation during rendering, storage bounds and Windows qualification remain.

### Bounded retained-storage inspection and phase guards

- Added shared `inspectCampaignStorage`: counts logical file bytes (including
  sparse files and duplicate/hard-linked paths conservatively), bounds entries
  and nesting depth, checks cancellation, and rejects symbolic links, special
  files or enumeration errors. It does not follow links, delete artifacts or
  treat unreadable paths as empty. Only the campaign directory is counted.
- Advancement applies the admitted byte allowance before its lock/write work and
  checks retained storage after preparation, rendering and collection. Exceeding
  a boundary returns an error and preserves evidence. The producer-owned asset
  repository outside the campaign directory is not included by this scan.
- Tests count a five-byte/two-file fixture exactly, reject four-byte and one-entry
  allowances, cancellation and a symlink, then place an over-limit sparse canary
  in a real campaign directory. Advancement rejects it before preparing batch 0
  or collecting any take. Moving the canary aside allows the existing SIGKILL/
  CLI-recovery scenario to proceed.
- This is phase-boundary enforcement, not a hard per-write disk quota: a phase
  can overshoot before its post-check, and external writers can race a scan.
  Reservation/accounted writers and combined campaign/producer growth remain
  necessary before declaring aggregate storage control complete.
- Verification: affected Release build, export CTest (1/1, 4.41 seconds), and
  staged-source closure (1/1, 0.25 seconds) passed. No new full-suite run claimed.

Next concrete implementation owners: explicit evidence-backed legacy migration,
then complete populated-workspace parity and candidate
review/publication parity and the resumable inventory campaign. The generation
test uses synthetic diagnostic material, not a qualified singer. No M1 completion
is claimed.

## Voiced reattack boundary repair

### M2.P1 process-ownership extraction

Native inference experiment: added an optional `seam_onnx_runtime_probe` C++
target selected by an application-owned SDK root, and an isolated ONNX fixture
generator/check. The official macOS-arm64 ONNX Runtime 1.30.0 archive was
downloaded and its SHA-256 matched the release digest
`6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012` before
extraction. The probe compiled and ran two real CPU inference sessions against
generated arithmetic graphs. Repeated outputs passed; changed scale returned
the exact output-mismatch exit code and swapped graph schemas returned the
runtime-error code. SDK and fixture environment remain ignored under
`build/neural-runtime`; requirements and reproduction steps are tracked under
`tools/neural_runtime`. No global Python packages were installed.

This is an actual runtime experiment, not neural singing: graphs contain no
learned voice weights, model admission is not implemented, and the executable
is not the first-party framed worker or an untrusted-bank sandbox. Signed
deployment, bounded model loading, feature matching, worker integration and
trained acoustic/vocoder assets remain required. No full-suite rerun claimed.

Data-bundle follow-up: added a separate `FrozenNeuralBundle` in synthesis, not a
reinterpretation of legacy NeuralSingerResource. It requires exactly one each
of acoustic/vocoder/vocabulary/configuration, permits bounded variance/tensor
assets, and owns deep-frozen hash-verified bytes behind shared immutable backing.
The deterministic name-sorted manifest binds roles, names, lengths and digests.
There are no executable/path/runtime-library fields or ONNX Runtime dependencies.
Asset count is 4–32, payload total at most 512 MiB, each asset at most 256 MiB,
vocabulary/configuration at most 4 MiB each, and manifest at most 32 KiB.
Tests cover reorder-stable identity, shared backing, caller-buffer mutation,
digest mismatch, aggregate overflow, invalid/duplicate names and roles, and
cancellation. Targeted synthesis build and performance-snapshot CTest passed
(1/1, 2.13 seconds). These arbitrary-byte fixtures do not prove graph validity:
manifest import, acoustic/vocoder feature compatibility, vocabulary admission,
render-resource integration and real inference remain required.

Follow-up: moved the dependency check into a reusable CMake module and added
six configure fixtures: allowed, harmless cycle, forbidden direct/transitive,
LINK_ONLY and alias links. The guard resolves ALIASED_TARGET before checking
forbidden owners so aliases cannot bypass the rule. Negative fixtures require
the specific dependency diagnostic, not merely any configure failure. The new
CTest passed (1/1, 0.37 seconds); total registered tests are now 124. This does
not assert support for arbitrary nested generator expressions.

Moved the bounded helper request/output API and sole process implementation
to `libs/seam-platform`. Japanese reading and neural execution now call the
platform API directly. The old authoring header contains only using-declaration
aliases for source compatibility. A normalized source comparison confirmed
that process implementation logic is unchanged apart from include/namespace;
timeouts, bounded I/O, cancellation, process-group cleanup and existing POSIX
limitations are preserved. Helper tests now link platform without authoring.

Removed neural -> authoring-runtime and explicitly declared neural -> synthesis.
A configure-time transitive target-link check rejects paths from neural to
authoring or rendering (including LINK_ONLY-wrapped dependencies). Fresh CMake
Graphviz output in `build/release/seam-dependencies.dot` shows neural's direct
dependencies as core, distribution, formats, platform and synthesis. The fresh
Ninja graph assigns helper_process.cpp to seam_platform only. Targeted helper,
neural-worker and Japanese-reading CTests passed (3/3, 2.69 seconds).
This completes the source-owner extraction, not M2.P1: Windows supervision,
host qualification, data-only neural bundle admission and real inference remain
open. The POSIX runner remains best-effort supervision, not a security sandbox.
Full verification after extraction: complete Release build passed; fresh CTest
passed 123/123 in 85.20 seconds, including source closure and compatibility-header
callers. No Windows execution or installed-host supervision result is implied.

### Rhythmic custom phrase authoring

Custom pilot input now accepts `LYRIC:MIDI[:TICKS]`, defaults to 480 ticks and
preserves explicit note durations in the normal saved score. Per-note durations
are bounded to 1–3840 ticks and total duration to 61440 ticks (32 seconds at
120 BPM). Syntax/count/range failures occur before output-directory creation;
phonetic timing still rejects a gesture that cannot fit its note. The custom
recipe now includes explicit b/d/g models, with unsupported liquids still
rejected rather than substituted. A regression exports ba/melisma/N/a with
960/240/720/480 ticks and verifies all five marker spans and the 60000-frame
candidate. Invalid, empty, extra-field and aggregate-overflow duration inputs
are rejected. Targeted pilot build and CTest passed (1/1, 6.35 seconds).
No full-suite rerun or naturalness qualification is claimed for this CLI change.
The maximum-duration diagnostic also rendered successfully in all three variants
to `build/release/seam-pilot-rhythmic-16bar-01`: 64 quarter notes, 16 bars at
120 BPM. Baseline candidate metadata confirms 1536000 frames at 48 kHz, 104
markers and unapproved status. The repeated kana phrase with varied melody is
an engineering render exercise, not independent creator/new-song acceptance.

### Normal voiced-stop rendering and candidate integration

Schema-six recipes are now admitted through normal resource decoding, compiled
articulation and rendering. Candidate schema six adds `voicedPlosiveRevision`
and `voiced-plosive` markers. Loading binds the marker to the exact recipe's
closure model and rejects unvoiced relabeling, missing/incorrect revision,
insufficient closure space, schema downgrades and approval claims. Closure
parameters remain in hash-bound recipe bytes rather than duplicated editable
metadata. The voiced source revision participates in schema-six snapshot hashes.
Older candidate schemas retain their shapes; mixed schema-six recipes may
export older gesture subsets without claiming a voiced stop they did not render.

Added the ordinary `stops` pilot: pa/ba/ta/da/ka/ga with matched pair pitches and
three variants. The exported Float32 regression checks exactly silent unvoiced
closures, nonzero voiced closures, precise marker identities and repeated hashes.
The repository-import/Studio-reopen fixture now includes ba so collection must
retain the new marker kind and unreviewed state. Listening WAVs are retained in
`build/release/seam-pilot-voiced-stops-01`. The fixture isolates closure voicing
using paired release spectra; this is not proof of natural b/d/g pronunciation.
This section supersedes the earlier default-admission gates, not their remaining
acoustic-quality limitations. Advanced timing/coarticulation and other phone
classes remain open under M1.P2.
Verification: full Release build passed; fresh full CTest passed 123/123 in
92.61 seconds, including voiced-stop export/import/Studio reopen. No listening
qualification, installed-host qualification or M1 completion is claimed.

### Opt-in articulated voiced-stop rendering

ArticulatedStream revision ten renders admitted VoicedPlosive gestures using
the continuous score-driven PhonationSource as closure excitation. Its stateful
VoicedPlosiveSource owns closure filtering and the release burst; the separate
noise lane explicitly delegates that gesture instead of rendering the burst
twice. Ordinary vowel-tract excitation is muted during the stop, and the vowel
re-entry uses a bounded taper. Compiled dynamics/articulation gain applies after
mixing, as for other sources. Stream copies/reset include the voiced-stop state.
Preparation compares closure gain/cutoff and release configuration with the
frozen recipe. Default schema-six admission remains disabled pending candidate
metadata/export integration.

The real Japanese ba timing fixture now exercises opt-in audio rendering,
nonzero closure, exact release/vowel boundary silence, whole/chunk equality,
checkpoint replay, cancellation rollback, reset replay and changed-recipe
rejection. Targeted Release build and voice-design CTest passed (1/1, 8.01
seconds). This is not yet normal song/export support or acoustic qualification.

### Voiced-stop compiled timing integration

ArticulationPlan revision nine carries an explicit VoicedPlosive gesture with
the closure source configuration, preserved PhonemeKey and ordered interval.
An explicit experimental admission flag permits schema-six planning only;
default resource decoding and song rendering remain closed. Recipe-selected
stop bindings now include voiced phones and validate closure parameters and
token voicing. A real Japanese `ば` score compiles a 2,880-frame onset into
2,400 closure frames plus 480 burst frames at 48 kHz, with no timing invention
outside the owning note. The following vowel retains its compiled nucleus.
Noise-only rendering rejects the new gesture rather than omitting its voicing.
Tests cover the actual resolved score, source parameters, default rejection,
wrong-style rejection and downstream renderer gates. The source/filter mixer
and candidate ABI remain required before normal rendering admission.
Verification: targeted Release build and voice-design CTest passed (1/1,
7.40 seconds). No full-suite or musical-quality result claimed.

### Explicit voiced-closure recipe contract

Schema six adds optional `plosives[].voicedClosure` with explicit `gain` in
(0, 0.5] and `lowpassHz` in [40, 2000]. Its presence admits b/d/g design-time
bindings; its absence preserves p/t/k. Existing same-style source requirements
and duplicate frication/plosive rejection remain. A schema-six mixed recipe
encodes null closure fields for unvoiced entries. Strict decoding rejects
missing/extra fields, invalid models and semantic downgrades; schemas 1–5 retain
their prior canonical representation. Tests verify round trip, schema identity,
downgrade/missing-field rejection, invalid gain/cutoff/phone combinations and
byte/hash-identical legacy stop encoding after removing the new opt-in pose.

Targeted Release build and voice-design CTest passed (1/1, 7.56 seconds).
`decodeVoiceRecipeResource` deliberately still rejects schema six in rendering:
normal song rendering must not silently ignore closure voicing. Timing, mixed
source rendering and candidate marker integration are the next required work.
No production voiced-stop support or acoustic quality qualification is claimed.

### Experimental voiced-closure source primitive

Added `VoicedPlosiveSource` beside the existing unvoiced primitive. It accepts
caller-supplied excitation (no independent pitch oscillator), applies bounded
gain and a stateful one-pole low-pass during the closure, tapers the closure
edges, and retains the existing release burst exactly. Configuration rejects
nonfinite/out-of-range gain and cutoff and closures too short to voice. Invalid
excitation and cancellation roll back both the filter and release-source state.
Tests at 22.05/48/96 kHz verify nonzero closure, exact unvoiced-burst equivalence,
whole/chunk identity, reset and failed-render rollback. Targeted Release build
and voice-design CTest passed (1/1, 9.31 seconds).

This is a source primitive only, not shipped voiced-stop support. Next required
integration: explicit versioned recipe fields and same-phone model binding,
compiled closure/release timing and score-derived excitation in the articulated
renderer, marker/candidate ABI propagation, and voiced-versus-unvoiced exported
phrase tests. Production still rejects unsupported voiced stops. No recipe
schema or existing renderer behavior changed in this increment, and no acoustic
quality claim is made for the experimental closure parameters.

### Actionable recipe coverage errors

Recipe articulation failures now retain the original error code and identify
the recipe ID and selected style alongside the existing phone/note diagnostic.
Missing VocalTract poses identify phone, style and recipe rather than only
reporting a missing pose. Tests verify missing phone/style and the stderr from
an actual unsupported `ば:60` production pilot export. No source substitution,
DSP change, approval or expanded phonetic support is implied. Targeted Release
build and voice-design/pilot CTests passed (2/2, 7.78 seconds).

### Bounded user-authored pilot phrases

The pilot CLI now accepts `phrase LYRIC:MIDI ...` rather than only fixed
fixtures. It validates 1–64 UTF-8 lyric/pitch pairs, MIDI 24–96, and retains
ordinary editable `.seam` projects plus recipes, WAVs and unapproved candidates.
Notes currently use 480 ticks at 120 BPM; richer editing belongs to the saved
score/native editor, not a second CLI score engine. The combined explicit pilot
recipe supports the existing phone models and leaves unsupported phones as
render errors. A custom m/a/t/a/continuation-a/N/a phrase exported successfully
to `build/release/seam-pilot-custom-01`. Tests cover actual marker output,
invalid syntax/pitch/count rejection before directory creation and unsupported
voiced-stop rejection without a successful report. This is not complete phone
coverage, a populated bank or the full new-song acceptance journey.
Verification: Release pilot target built; updated pilot CTest passed (1/1,
4.92 seconds). No full-suite rerun or listening acceptance claimed.

### Standalone nasal production fixture

Added `seam_singer_pilot NEW_DIRECTORY nasals`: six alternating standalone
Japanese `N` and oral vowel notes through normal ExportService. Inspection
confirmed the existing syllabic-N fallback timing path; no new timing or DSP
semantics were needed. A dedicated explicit nasal recipe pose supplies full
nasal coupling with resonance/antiresonance instead of inserting an oral vowel.
The CLI regression checks exact N/a/N/i/N/u markers, full-note spans, unapproved
metadata, voiced analysis windows in every note, audio hashes and deterministic
repeat exports across three variants. Release pilot build and updated pilot
CTest passed (1/1, 4.67 seconds); no fresh full-suite run claimed. Listening
artifacts are retained in `build/release/seam-pilot-syllabic-nasal-01`.
This exercises the required standalone-N path but does not qualify its sound,
close all phonetic classes, or complete M1.P2.

### Vowel-only production-path follow-up

The new `seam_singer_pilot NEW_DIRECTORY boundaries` fixture renders the same
four-note melody as separate vowels and as a melisma, across three recipe
variants. Its real exported dry-PCM assertion initially failed: the baseline
first reattack boundary had summed adjacent absolute amplitude 0.01975246.
Pure-vowel phrases select SustainedPoseStream, so the mixed renderer repair
alone did not fix this production path. Sustained vowel scheduling now applies
the existing 5 ms taper at compiled reattacks without tapering continuations.
Its renderer revision increased from 12 to 13 to invalidate prior identities.

The CLI regression checks eight vowel markers, hash-bound Float32 mono audio,
zero boundary samples for separate attacks, nonzero continuation boundaries,
and exact repeat hashes for all three variants. Targeted Release build and
pilot/voice-design/export CTests passed (3/3, 8.38 seconds). No fresh full-suite
run is claimed for this follow-up. Listening artifacts are retained in
`build/release/seam-pilot-boundaries-02`; `-01` retains the pre-fix comparison.
Neither fixture is a listening-quality acceptance result.

- Source inspection found that PhonationSource restarts phase on compiled score
  reattacks, but ArticulatedStream previously joined all adjacent voiced gestures
  without an envelope taper. Optional authored attack/release controls do not
  supply a default taper. A new adjacent-vowel regression failed before repair
  at the final sample preceding the reattack.
- The mixed renderer now applies its existing bounded 5 ms smoothstep taper at
  gesture boundaries that coincide with a compiled note reattack. Intra-note
  phone transitions and compiled melisma continuations remain connected. Filter
  and source state are retained; this is not a filter reset or score mutation.
- ArticulatedStream revision increased from 8 to 9. Existing snapshot/cache and
  candidate metadata paths consume that constant. Recipe schemas are unchanged;
  affected old render identities must not be treated as newly rendered evidence.
- Regression covers separate Japanese vowel notes, a continuation vowel,
  exact boundary silence only for reattack, and sample-identical whole/chunked
  rendering with an owned-window split one frame before the boundary. The
  expanded matrix exercises 22.05/44.1/48/96 kHz, starting MIDI pitches 36/61/84,
  both reattack and continuation (24 combinations), cancellation rollback,
  checkpoint replay and reset replay.
- Verification: affected Release targets built; voice-design and export CTest
  targets passed (2/2, 7.57 seconds). The new regression was observed failing
  before the implementation change. No full-suite or listening-quality pass is
  claimed. Voiced stops, expanded phonetic context, qualified singer assets and
  the remaining six-milestone plan are still open.
- Regenerated three articulation variants through the production ExportService
  in `build/release/seam-pilot-articulation-reattack-01`. These are retained
  unqualified listening artifacts, not phonetic or identity acceptance evidence.
- Follow-up verification: complete Release build passed, followed by a fresh
  full CTest invocation: 123/123 passed in 84.76 seconds, including the expanded
  boundary matrix and production pilot CLI. This supersedes the earlier narrow
  test boundary for this repair, but does not establish musical qualification.
# Training label consistency checkpoint

Added phoneme/F0/voicing label validation and a correction queue. Checks cover
phrase bounds, complete contiguous phone spans, vocabulary membership, explicitly
configured confidence threshold and analysis-frame geometry. Low-confidence,
unknown or unreviewed labels are not silently accepted. Supplied review revision
text is not authenticated; all outputs retain trainingAdmitted=false.

Verification: fourteen training-tool tests passed, including valid consistency
without approval, combined correction reasons and invalid alignment/feature
geometry. Source digest, note/slur/lyric binding, reviewed revisions and the
label-report CLI remain open, as do permission admission and actual training.

# Captured source-preparation CLI checkpoint

Added the `prepare CONFIG SHA256 SOURCE_ROOT NEW_REPORT` command, sharing bounded
configuration loading and no-replace publication with `split`. It binds the exact
configuration identity to file-inspection results. Exit 3 preserves per-source
rejection diagnostics without a split-ready inventory; exit 0 means inspected
only, not rights/training approval. Invalid config/publication returns exit 2.

Verification: eleven training-tool tests passed, including subprocess preparation
success, rejected-source diagnostics and existing-output preservation. Audio
transforms, segmentation, permission admission, labels and training remain open.

# Source-file preparation integration checkpoint

Added read-only `prepare_sources`: explicit root, contained source paths,
captured digests, per-file/aggregate byte budgets, regular-file checks and
per-item inspection diagnostics. Successful sources produce actual PCM-derived
split identities; any failure suppresses the complete split-ready inventory
instead of silently omitting failed records. Original files remain unchanged.
Path checks do not claim race-free filesystem isolation.

Verification: ten training-tool tests passed, including actual source reads,
missing/escaping/linked paths, changed digest, no partial split publication and
source preservation. Captured-config prepare CLI, permissions and transforms
remain unfinished; these results do not qualify training data or a singer.

# Training source PCM inspection checkpoint

Added bounded inspection of actual captured mono integer PCM WAV bytes: expected
source digest, requested sample rate, supported widths, duration/frame limits and
exact payload length. It returns both source-container identity and a separate
geometry-plus-PCM identity, without converting or modifying audio. Unsupported
formats require a future explicit preparation transform. No rights are admitted.

Verification: nine training-tool tests passed. Metadata-only WAV variations have
different source hashes but the same PCM identity and therefore stay in one split
group; wrong digests, stereo/unsupported width, wrong clock and truncated payloads
reject. Permission admission, reviewed labels and training remain unfinished.

# Exact-audio duplicate dossier checkpoint

Split output schema 2 now reports duplicate-audio source groups, unique-audio
counts by partition and redundant-source totals, while retaining every source
reference. This prevents raw record counts from masquerading as independent
recording counts. Selection/removal is explicitly review-required; no label or
permission conflict is resolved automatically. Input configuration stays v1.

Verification: six training-tool tests passed, including three duplicate records
counted as one unique recording in the held-out partition, stable output order
and preservation of all source records. Hashes remain caller-supplied pending
the separate source-audio admission stage.

# Captured dataset split command checkpoint

Added `python3 -m tools.voice_model_training split CONFIG SHA256 NEW_OUTPUT`.
It reads bounded captured configuration, rejects duplicate keys/schema changes,
binds configuration and source-inventory hashes, and publishes canonical output
without replacing an existing destination. Opened input must be a regular file.
Temporary output is fsynced before same-directory no-replace link publication;
directory durability and Windows qualification are not claimed.

Verification: five training-tool tests passed, including subprocess command
execution, bad digest/no output, deterministic repeated output, source preservation,
overwrite refusal and temporary cleanup. No source permissions or audio are
admitted by this split command; the rest of the training pipeline remains open.

# Original-model dataset split implementation checkpoint

Started the planned `tools/voice_model_training` owner with deterministic source
splitting. Song/session/lineage/exact-audio relationships are unioned transitively;
groups touching explicitly held-out songs are assigned wholly to test. Remaining
groups use seeded hashing; missing partitions are reported rather than repaired
by leaking related recordings. A canonical source-inventory digest is retained.

Verification: four tests passed, covering order independence, transitive leakage,
explicit holdout propagation, invalid identities and input preservation. This is
the split algorithm only, not the full command or source-admission pipeline. No
audio, permissions, labels, learned checkpoint or singing qualification was created.

# Native graph text-budget checkpoint

Native inspection bounds each protobuf string field to 4096 bytes and total
model-tree text to 8 MiB before upstream checking. Raw tensor storage is excluded
from this text policy and retains its own bounds. The policy includes descriptive
metadata; actual export compatibility is still to be qualified. Checks are
post-parse and do not prevent all parser allocation.

Verification: all four native integration tests passed in 8.36 s. Tests accept
the per-field boundary, reject an oversized name and reject aggregate metadata
overflow. The diagnostic privacy test now uses a bounded forged-log name so it
still reaches the upstream checker rather than failing the new text budget first.

# Native checker exception diagnostic checkpoint

The reusable native inspection API no longer writes upstream checker exception
text to stderr. The CLI emits a stable numeric failure message; the runtime
caller retains its existing bounded generic failure message. This avoids echoing
model-controlled tensor names or graph fragments from caught checker exceptions.
It does not intercept every third-party library's internal logging mechanism.

Verification: all four native integration tests passed in 6.34 s. A regression
uses a 64 KiB input name containing a forged log line; rejection emits only the
fixed code-15 diagnostic and no stdout.

# Cooperative native-inspection cancellation checkpoint

`inspectBytes` now accepts a stop token, checks cancellation before/after parsing
and upstream checking, per traversed message and periodically during numeric
tensor scans. Observed cancellation returns code 18 without replacing outputs.
The ownership test covers pre-cancelled inspection and preservation of prior
results. Third-party parser/checker calls remain noninterruptible internally;
process supervision is still required for hard deadlines. CLI behavior is unchanged.

Verification: native build-and-test target passed all four integration tests in
4.87 s. In-flight cooperative cancellation latency is not measured by these tests.

# Full native-enabled regression checkpoint

The complete enabled Release build passed, followed by all 128 CTest targets
(`ctest --test-dir build/release --output-on-failure -j 4`, 113.11 s).
All 43 offline Python neural tests also passed. This includes the four optional
native inspection/runtime tests and the original product regression suite.
It supersedes the focused-only test boundary for the accumulated native changes.
The parser/runtime remain development dependencies; full production execution
policy, trained original singer, installed-host evidence and Beta GO are not
established by this regression result.

# Optional native integration test target checkpoint

Registered four native experiment CTests and a build-and-test target,
`seam_neural_native_checks`, with explicit binary dependencies. Enabling the
Python-driven checks requires an application-configured existing ONNX 1.19.1
environment; configuration performs no installation or download. Default builds
do not acquire these dependencies. The enabled checkout now has 128 CTest entries,
not 128 newly verified tests in this checkpoint.

Verification: the new target built its dependencies and passed all four tests
in 9.25 s. The last complete 124-test run predates this optional registration;
no new full-suite claim is made. Tests remain fixture/structural evidence, not
production singer qualification or Beta GO approval.

# Frozen native inspection-to-runtime checkpoint

The root build can optionally link native structural inspection into the existing
arithmetic runtime probe with `SEAM_NATIVE_ONNX_SCHEMA`. Bundle metadata and both
frozen graphs are checked, including native pair compatibility, before either
ORT session is constructed. Parsed validation models are discarded; ORT receives
the same immutable bytes that were inspected. No path is reopened in between.

Verification: rebuilt the enabled probe; CLI-prepared bundle/request inference
and both pair profiles passed. A newly prepared, correctly hashed bundle with
an unknown operator rejects with the native inspection error before session
construction. Mismatched pair declarations now reject earlier in this mode.
Default builds do not gain a parser dependency. This integration is still a
development experiment: production worker packaging, execution-family resource
policy and an admitted handle remain open; no learned singing is claimed.

# Reusable in-memory native inspection checkpoint

Extracted the native checker into a reusable object-library target with an
`inspectBytes` API. The pathname command is now an adapter. Parsing operates
only on supplied bytes; model/report outputs are replaced transactionally after
all checks pass. Failed inspection preserves previous outputs. This enables
future frozen-bundle/worker integration without reopening graph paths.

Verification: native build and owned-byte test passed; results survive source
buffer changes and malformed/empty input leaves prior outputs unchanged. The
tensor and pair regression script also passed. No production worker currently
consumes this API, and a prepared admission handle remains required.

# Native acoustic/vocoder pair checkpoint

Added a native pair contract operating directly on parsed/checked ModelProto
objects. It requires exact tensor names/types/ranks, batch one, declared mel
layout/bins, steps scalar or vector-one, vocabulary-duration axis agreement,
per-graph frame-axis consistency and a distinct output-sample axis. Hop/frame
declarations bound mel element count; actual computed shapes remain runtime checks.

Verification: native experiment rebuilt; scalar/audio and vector1/waveform
profiles passed; 12 mismatched configurations rejected, alongside the existing
tensor regressions. This is structural pair validation, not a prepared execution
admission handle or proof of acoustic feature semantics/learned singing.

# Native graph interface reporting checkpoint

Native inspection now returns verified top-level input/output names, element
types, static/symbolic dimensions, IR and opset versions using proper JSON
serialization. Differential checks compare both acoustic/vocoder interfaces
against Python and cover a name containing quotes, backslash, newline and Unicode.
The rebuilt checker passed the existing 16 representation/28 rejection cases.
These interfaces support the next native pair-contract step; reports still do
not authorize execution or establish dynamic runtime bounds.

# Upstream native graph-checker integration checkpoint

The isolated native inspection build now includes the locally installed ONNX
1.19.1 checker and standard operator schemas, plus their source dependencies.
It calls the in-memory checker only after external-reference and local bounds
checks. Unknown standard operators, undeclared inputs and unsupported attributes
are rejected. Full shape inference is disabled; an execution-family allowlist,
resource-cost policy, pair validation and production packaging remain open.

Verification: native target built successfully; 16 valid tensor representations
and 28 rejection cases passed, including three upstream checker regressions.
Status is `NATIVE_STRUCTURE_CHECKED`, not execution admission. Dependencies remain
local development inputs and are not shipping/release-qualified artifacts.

# Native tensor numeric-value checkpoint

Native tensor inspection rejects nonfinite float32/64/16, noncanonical Boolean
values and out-of-range typed int8/uint8/float16 storage values. Raw storage uses
explicit little-endian order. This policy also rejects intentional infinite mask
constants; real-model compatibility is not established. Operator attributes and
runtime outputs need separate validation.

Verification: rebuilt parser; 16 valid raw/typed representations and 25 rejection
cases passed, including eleven invalid-value cases. Operator semantics and
production inference admission remain unfinished.

# Native tensor payload consistency checkpoint

Native tensor inspection now enforces exact raw byte lengths or exact typed
element counts according to the declared shape/type. Mixed raw/typed storage,
wrong typed fields and segmented tensors are rejected. Numeric ranges and
finiteness remain separate unfinished checks, as do operator/schema validation
and production graph admission.

Verification: rebuilt parser; both graph inventories still match Python;
14 invalid cases reject; all eight supported types pass in both raw and typed
form with matching declared byte counts (16 representation cases). The earlier
large missing-data fixture now rejects missing payload before reaching the
second tensor's aggregate-storage check, as intended.

# Native tensor bounds checkpoint

The native parser experiment now checks numeric/bool element types, rank,
overflow-safe dimension products, aggregate declared tensor storage and all
encountered ValueInfo shapes. It rejects unnamed dynamic dimensions but does
not yet bound named dynamic dimensions at runtime. Reports include declared
tensor bytes. Generated parser payloads are not execution-admitted; tensor
payload consistency and operator/schema validation remain missing.

Verification: rebuilt native experiment; two graph storage inventories matched
Python; ten invalid cases were rejected, including excessive tensor/interface
products, negative dimensions, unsupported tensor types and aggregate storage.

# Native schema parser experiment checkpoint

Added an isolated native ONNX parser target using locally available Protobuf
33.4.0 and hash-checked ONNX 1.19.1 schema bytes. Reflection code is generated
only in the build directory. No shipping target links this experiment.
Native input-byte and recursion limits precede parsing; a bounded reflective
walk rejects external tensors, unknown fields, custom domains, training graphs
and local functions. Message-count checks occur after parsing, not before
allocation. This is parser groundwork, not a completed graph admission factory.

Verification: configured/built on macOS arm64; two arithmetic graph node/tensor
inventories matched the Python inspector; empty/truncated bytes, unknown fields,
custom domains and nested external tensor metadata were rejected. Operator,
shape and pair validation and production worker integration remain open.

# Parent bundle-loading lifetime checkpoint

Bundle launch now rejects process budgets beyond the platform runner's limits
before filesystem lookup/model loading. Regression cases use a missing directory
to prove invalid-budget rejection precedes path admission. Parent graph payload
ownership is scoped to metadata inspection and released before launching the
child, which independently loads its own verified bytes. Metadata/vocabulary
remain owned values. This removes parent graph retention during inference; it
does not establish a measured peak-memory ceiling or eliminate loading copies.

Verification: rebuilt native worker protocol target and passed its CTest in
3.58 s, including bundle transport, cancellation and deadline coverage.

# In-flight bundle-worker termination checkpoint

The dedicated transport fixture now supplies a test-only readiness/PID marker
after child-side bundle and request validation. Native tests wait for this
handshake before cancellation, verify the terminated PID no longer exists, and
exercise a separate deadline-terminated child. A fresh request must succeed
afterward. This closes the earlier pre-launch-only cancellation evidence gap for
the macOS bundle transport fixture, not installed hosts or Windows supervision.

Verification: rebuilt fixture/protocol test; initial protocol run passed (2.19 s)
and three consecutive repeat-until-fail runs passed (4.23 s total). No production
graph runtime or learned audio is involved in these lifecycle tests.

# Graph intake storage and interface checkpoint

The offline ONNX inspector now limits aggregate declared tensor storage to
512 MiB, including nested tensor attributes, rather than applying only per-tensor
element limits. Numeric/bool interface types and static dimension products are
checked explicitly. Reports expose sorted operator counts and declared storage
for subsequent graph-family policy work; neither field grants admission.
Runtime intermediates, dynamic dimensions, parser memory and production child
admission remain outside this check.

Verification: all 43 offline neural tests passed, including combined-storage,
interface-product and unsupported-interface regressions. Paired arithmetic and
CLI-prepared bundle runtime checks passed. No new full CTest run was needed for
this Python-only inspector change; the preceding native checkpoint remains below.

# Integrated regression checkpoint

The accumulated bundle metadata/vocabulary conversion, CLI preparation, request
v3, launch v2, deployment materialization and transport changes passed a complete
Release build and all 124 CTest targets (`ctest --test-dir build/release
--output-on-failure -j 4`, 102.83 s). Separately, 40 offline neural-runtime tests,
16 native/Python vocabulary byte comparisons plus 12 rejection cases, paired
arithmetic runtime inference and CLI-prepared bundle request inference passed.
This supersedes the earlier focused-only verification boundaries for these code
changes. It does not satisfy learned singing, independent listening, production
graph admission, Windows/installed-host qualification or full Beta GO acceptance.

# Deployment descriptor materialization checkpoint

Added `build_neural_deployment_descriptor` in the release-package tooling. It
reconstructs the canonical helper manifest from finalized files, checks the
application-specified build/module/protocol target, and emits unsigned canonical
descriptor bytes plus their digest for the release signing owner. Schema 1
remains eight fields; schema 2 includes explicit launch protocol 2. Unsupported
platform/surface combinations, unsafe manifest paths, changed payloads and
cross-version packages are rejected. It neither supplies keys nor signs releases.

The native package materialization suite passed in 6.12 s. New tests generate
descriptors in Python, sign them with ephemeral test keys in a copied native
module, and exercise signature verification plus module-anchored package loading
for both versions. A correctly signed descriptor targeting the wrong loaded
package version is rejected. Test helper bytes are not execution-qualified;
these results establish deployment plumbing only, not production inference.

# Bundle-aware process transport checkpoint

`runNeuralBundleWorker` now requires launch protocol 2, request metadata v3,
an application-selected absolute canonical directory, a bounded aggregate bundle
size, and nonzero CPU/resident-memory budgets. It reloads and verifies metadata,
uses the bundle's vocabulary, and launches the selected helper with the directory,
model ID/version, manifest hash and payload budget as separate arguments.
Shared transport code retains helper hashing, deadlines, bounded pipes, request
hash checks and exact response identity checks, including the bundle hash.

The separate `seam_neural_bundle_transport_probe` is a zero-PCM transport fixture,
not the v1 probe and not a production graph worker. It reloads actual bundle bytes
in the child; no ONNX session is constructed. Tests cover successful binding,
missing response bundle identity, missing budgets, payload limits, wrong launch
version, the legacy helper, cancellation before launch and changed bundle bytes.
Production child graph admission remains mandatory and unimplemented in this
transport layer. Windows and installed-host execution evidence remain pending.

# Versioned neural deployment checkpoint

Helper manifests now admit only the explicit schema/protocol pairs 1/1 and 2/2.
The resolved options retain that launch version; the legacy runner rejects 2.
Signed deployment schema 2 requires an explicit protocolVersion 2, an application
target expecting 2, and a loaded package with the same launch version. Schema 1
retains its original eight-field representation and expected legacy protocol.
Python package materialization can seal either version without changing its
default; payload inventory reconstructs the declared version rather than silently
rewriting it to 1. File integrity is not proof of a helper's implementation.

Verification: native protocol test passed (1.05 s), including signed version
selection and cross-version rejection. Python/native package materialization
exercises generated manifests for both versions. Production bundle launch and
child graph admission are still pending; no new singing capability is claimed.

# Bundle-conditioned protocol v3 checkpoint

Implemented the M2.P1 metadata version boundary: request/response v3 carry an
explicit `bundleContentHash`, matching the current frozen bundle's model content
identity. V3 requests require phonetic conditioning; v3 responses require the
canonical request hash. Binary framing remains SNW1/version 1, and metadata v1/v2
retain their existing representations. Unknown fields prevent silent downgrades.
The legacy `runNeuralWorker` launcher refuses bundle-conditioned requests rather
than letting its v1 helper satisfy a production contract accidentally.

Focused verification: native protocol CTest passed (1.11 s); native arithmetic
bundle runtime test passed for both v2 and v3, including wrong bundle identity
and attempted downgrade. This implements the wire contract, not the remaining
production v2 launcher, admitted graph handle, packaging, or real singer model.

# Request-driven runtime experiment checkpoint

The native optional ORT probe now accepts an external framed request through
`--paired-request`, validates it against child-loaded bundle metadata and vocabulary
before session creation, and returns binary request-bound PCM. The end-to-end
CLI preparation/reload test verifies two pitch/dynamics combinations, 731-sample
output with padded-tail removal, exact canonical request hashes, and rejection of
truncation, trailing bytes, wrong model identity, and changed graph bytes.

Verification: optional native probe rebuilt successfully; bundle runtime test
passed; all 40 offline neural-runtime unit tests passed. No full Release-suite
rerun is claimed for this checkpoint. Fixtures are arithmetic graphs, not learned
voices. Production worker packaging, model admission, cancellation, lawful learned
assets, singing qualification, and the remaining full implementation plan stay open.
# Source-bound training label command

Reviewed dataset assembly API: joined fresh source-rights admission, annotation
admission and deterministic splitting with exact source/hash/clock/lineage checks.
Snapshot binds both configurations/reviews, labels, vocabulary and split evidence;
missing partitions/duplicate selection remain explicit issues and training stays
unadmitted. Fixture integration passed for deterministic assembly, missing train/
validation partitions and signed conflicting lineage rejection. No real dataset
was admitted or trained. CLI and derived-source dataset assembly remain open.

Annotation admission CLI: added `admit-labels` with captured label/review/policy
inputs, independent trust anchor, fresh source inspection and current-clock
expiry checks before publication. Existing output and signed unresolved label
errors reject without overwriting anything. Fixture subprocess tests cover valid
admission, immutable prior output and invalid-label nonpublication. Source rights
and full training readiness remain explicitly separate.

Source-bound annotation admission: refactored label inspection for reuse and
joined it with label-specific signed review. Admission requires schema 3 score/
silence ownership and rejects unresolved phone/confidence/voicing corrections.
The absent legacy review string may be superseded by the verified signature;
source permissions and training execution remain separate. All 30 training
tests passed, including fixture admission and signed low-confidence rejection.
No real label approval or whole-dataset training admission was issued.

Annotation authority separation: refactored shared review checks behind explicit
rights/label verification entry points. Label reviews require a distinct policy,
role, format and decision; tests reject rights-as-label and label-as-rights replay
while valid fixture signatures still verify. All 30 training tests passed. No
real annotation review was created; source-bound label admission remains open.

Extractor lifecycle repair: timeout cleanup previously called poll(), which could
reap an exited leader and skip killing descendants retaining output pipes. It
now checks unreaped returncode state and terminates the process group before
waiting, preserving PID ownership through that operation. A forking fixture
exits the leader while its descendant holds pipes; regression verifies targeted
group termination on timeout. The first test draft had unsafe redundant cleanup
after PID reaping and an overly short startup allowance; removed that redundant
kill and increased the test deadline. Focused lifecycle tests pass.

Correction publication: `correct-labels` now re-inspects the source, checks label
identity/geometry, applies stale-checked corrections and publishes new label/edit
evidence with parent/child label hashes. Review remains invalidated; consistency
diagnostics remain visible. All 29 training tests passed, including subprocess
corrected publication, hash lineage, no-overwrite and stale-edit rejection.
Native correction UI and authenticated musical review remain unfinished.

Label correction API: added transactional F0/voicing and phoneme-object edits
with expected-old-value checks. Shared boundaries can be changed in one batch;
stale targets, duplicate targets, invalid timing and inconsistent voicing reject
without changing the original. Derived labels clear review revision. All 28
training tests passed. CLI/editor integration and authenticated label approval
remain unfinished; a corrected label is not automatically training truth.

Pitch correction queue: fresh extraction now publishes low-confidence voiced
frame and zero-padded-window diagnostics, with source frame indices and explicit
reviewRequired. Refreshed reports use schema 2; fresh-pitch segments use schema 5.
Old artifacts are not overwritten/migrated during resume. Training and native CLI
integration tests passed, including uncertainty/tail diagnostics. This exposes
known estimate uncertainty; it does not establish a validated acoustic threshold
or independent label approval.

Fresh-feature workflow integration: propagated the extractor option through
batch preparation and reviewed derived-source admission. Native tests execute
off-grid batch extraction, exact resumed extraction and fixture-signed parent
admission followed by real native feature extraction. Training and native feature
groups passed. This closes option plumbing, not real dataset permission, musical
label review, Windows supervision or actual model training.

Off-grid crop integration: `segment --fresh-pitch-extractor` now extracts fresh
native pitch on the exact child WAV and publishes schema 4 evidence-bound labels.
It supports starts between parent hops without shifting/reusing parent F0 frames;
phonemes are rebased and review stays invalidated. The native integration test
executes a one-sample-offset crop and verifies child feature hash and geometry.
Training and native feature CTest groups passed. Batch/admission option propagation
and real-voice boundary quality remain unfinished.

Automatic refresh CLI: `refresh-pitch` now joins captured configuration, bounded
source inspection, supervised native extraction and feature-to-label conversion,
then publishes a new report containing full feature evidence and cleared-review
labels. Integration exercises the actual native binary through the Python CLI
and verifies no-overwrite. Training and native feature CTest groups passed. This
does not create phoneme alignments, approve labels or train a model.

Native extraction supervisor: added POSIX first-party extractor invocation with
deadline, bounded stdout/stderr, concurrent pipe draining, failure termination and
strict JSON parsing. Integration compares actual native output to supervised
output; fixture tests cover timeout, duplicate fields, stderr overflow and a
subsequent successful invocation. All 27 training tests passed. This does not
establish OS sandboxing, Windows support or executable authenticity; refreshed
label publication is still unfinished.

Voiced feature verification: native CLI tests now extract deterministic PCM tones
at 8 kHz/110 Hz, 48 kHz/220 and 880 Hz, and 192 kHz/440 Hz, then consume their
features in the training adapter. Complete-window estimates pass a 10-cent test
tolerance; no tail-accuracy or real-voice claim follows. Adapter regressions reject
wrong settings/grid, nonfinite values and inconsistent voicing without modifying
parent labels. All 26 training tests and the native feature CLI integration passed.

Native-to-training feature adapter: `apply_pitch_features` now validates exact
native feature fields, source hash, clock, window/hop settings and full frame grid,
then copies fresh F0/voicing into labels and clears review revision. The native
CLI test consumes actual generated feature JSON and rejects a wrong source hash;
it passed. This does not yet orchestrate extraction automatically or establish
real-voice pitch accuracy. Existing phonemes remain supplied annotations.

Native feature command: `seam_voicebank_cli extract-pitch WAV` now emits full-hop
FFT autocorrelation features as JSON stdout, binding the exact captured WAV hash
and declaring window/hop/range/threshold/algorithm/coverage. Input is bounded to
64 MiB, mono decoded samples to 16 million, output frames to 65536 and FFT work
to 512 million butterflies. Rate-scaled power-of-two windows preserve the existing
60..1200 Hz diagnostic range. No automatic training approval. Added native CLI
integration cases for full/partial-hop silence, hash binding, source preservation
and malformed input. Python training adapter and real-voice quality remain open.

Fresh-feature preparation: inspected native `analyzePitch` and found its default
complete-window output does not match training's ceil(samples/hop) label geometry.
Added explicit `PitchFrameCoverage::FullHopGrid` with zero-padded tail windows,
preserving default CompleteWindows behavior. Work/frame budgets include every
added tail frame. Regression cases cover single-sample, exact-hop, partial-hop
and longer tails, silence, invalid coverage and frame-budget rejection. This is
the native prerequisite for fresh crop feature extraction; CLI integration and
acoustic quality qualification are not yet claimed.

Derived admission CLI: `admit` now accepts a captured crop configuration/source
and separate clip destination, checks all four options together, and publishes
the derived-source admission outside the clip directory. Exact clip resume is
explicit and always creates a new time-bound admission report after parent
review verification. All 25 training tests passed, including CLI derived hash
binding and resumed re-admission. No real approvals or training were performed.

Checkpoint `4e05891b` preserves signed source admission and the shared signature
repair locally (not pushed). Follow-up `admit_segment` now revalidates parent
review/source/evidence, checks parent hash and lineage, then derives an exact clip
and binds its identities to the reviewed singer. Tests cover derived admission
and unreviewed lineage rejection before output. All 25 training tests passed.
Only fixture approvals were exercised; no real source authorization or training
was performed. Derived admission CLI/dataset assembly remain open.

Signature repair interoperability checks: fetched RFC 8032 section 7.1 from
https://www.rfc-editor.org/rfc/rfc8032.txt and added its first two public known-answer
vectors. Public-key derivation, exact deterministic signatures and verification
match; S+L malleability rejects. Four update-CLI tests passed. Added a training
review regression showing that an identity-key forged review rejects even under
an explicitly supplied policy containing that weak key; training CTest passed.
These vectors and regression tests are targeted evidence, not a full independent
cryptographic implementation audit.

Shared signature repair: reproduced identity-key universal forgery acceptance in
the Python Ed25519 verifier used by release and training review verification.
Added on-curve/canonical point decoding and rejection of torsion-only public keys
and nonce points. Tests cover the forged identity signature, noncanonical zero-x
sign encoding, out-of-field encoding, valid signatures and altered messages.
Three update-CLI tests and all 25 training tests passed. This is a targeted
security repair, not a claim of complete cryptographic audit or subgroup-policy
qualification. No real signed approvals were issued.

Admission CLI follow-up: connected `admit` to captured configuration/review/policy
files, independent canonical policy hash and fresh audio/evidence inspection.
Uses the system clock and rechecks expiry before publication. New reports only;
no signing, trust provisioning or training execution. All 25 training tests passed,
including subprocess fixture admission, no-overwrite and wrong-anchor rejection.
Only fixture signatures were used; no real source admission was performed.

Source-admission join: extracted read-only permission configuration inspection
and connected it to trusted signed-review verification in `admit_sources`.
A valid signature cannot bypass missing scopes or changed audio/evidence.
Successful fixture admission records sourcePermissionsAdmitted with policy,
review, configuration identities and expiry; trainingAdmitted remains false
because label/split/execution prerequisites are separate. All 25 training tests
passed, including valid signed source admission and signed-incomplete/changed
source rejection. No real source permission approval was issued.

Signed-review verification: added closed training review/policy contracts using
existing role-bound Ed25519 verification. Requires an independent policy hash,
exact captured configuration digest, explicit current time, training reviewer
role and matching signer. Expired/future, wrong-configuration, altered-signature
and wrong-anchor reviews reject. All 25 training tests passed using a test-only
signer. No real trust anchor or review was issued. Successful verification is
not execution admission; fresh inspected permissions must still be joined.

Complete integrated checkpoint: all 129 registered CTests passed in 85.50 seconds
with `-j 4`, including the 24-case training-tool group and native ONNX checks.
This supersedes the earlier split 128-plus-one test invocation. Reviewed the next
authorization boundary: existing `tools/public_release/crypto_validation.py`
provides role-bound signed-record verification. Training admission must bind a
distinct trusted reviewer policy and exact captured dataset/evidence identities;
a key embedded in an assertion cannot authenticate itself. No real reviewer
policy, signed training approval or trained model was created in this checkpoint.

Permission CLI follow-up: `permission-report` now captures bounded regular-file
evidence and actual audio from a captured configuration, publishes source-bound
scope assertions and retains explicit false training-admission/review-authentication
flags. Exit 3 reports missing asserted scopes; wrong evidence fails without new
output; existing reports reject. All 24 training tests passed, including subprocess
CLI success, missing modelTraining, changed evidence and no-overwrite cases.
This does not complete the plan's authenticated `admit` operation.

Permission/audio join follow-up: added `inspect_permission_sources`, which
reuses the bounded preparation reader and requires exact source-set and hash
agreement with permission assertions. Schema 2 reports now bind actual PCM
identity/geometry plus song/session/lineage. Source-byte verification is distinct
from review authentication and training admission; both latter flags stay false.
All 24 training tests passed, including mismatched permission digest, unmatched
source ID and altered recording rejection. CLI evidence capture and authenticated
execution admission remain open.

Training-permission capture: inspected existing external-beta source admission;
its four bank/render scopes do not include model training. Added a separate
training manifest checker reusing those names and requiring modelTraining,
modelRedistribution and commercialModels independently. Evidence bytes are
bounded and hash-checked; supplied reviews are not authenticated and no execution
authority is granted. All 23 training tests passed, including bank-only scope
rejection for complete assertions and missing/changed evidence. Actual source
joins and authenticated training admission remain unfinished.

Batch-to-split integration: batch report schema 2 now binds successful child
source IDs, WAV hashes and exact segment-record hashes and exposes `splitSources`
only when every entry succeeds and child IDs are unique. Duplicate IDs are
reported without deleting clips and cause exit 3 with no split inventory.
The integration test consumes the verified inventory in the existing splitter,
checks held-out grouping and exact-audio duplicate accounting, and verifies
duplicate-ID rejection. All 22 training tests passed. Permissions, reviewed
labels and trained-model qualification remain separate unfinished requirements.

Batch preparation follow-up: added `segment-batch` for 1..64 captured phrase
configurations, sequential bounded source reads, unique flat output names,
per-entry rejection reports and explicit resume through existing byte-verified
segment publication. Every attempt requires a new report. The added recovery
test preserves a successful clip and old report while a missing input is supplied
and completed on resume. All 22 training-tool tests passed. Dataset-wide identity
admission, rights, reviewed labels, actual training and musical qualification
are still incomplete; batch success is PREPARED_UNAPPROVED only.

Publication recovery follow-up: `segment --resume` verifies existing output
against re-derived source/configuration-bound bytes. Complete artifacts return
without writes; exact audio lacking a record receives only the final record.
Truncated audio, conflicting records, directory symlinks and unexpected entries
reject without overwrite. Existing no-resume behavior is preserved. This handles
the audio-complete/manifest-missing interruption boundary, not arbitrary partial
audio repair or concurrent hostile directory mutation.

Integrated checkpoint verification: Release build succeeded; the existing full
128-test CTest registry passed with zero failures in 85.53 seconds (`-j 4`).
Registered `seam_voice_model_training_tests` in the root CMake test suite with
a 60-second timeout and repository working directory. After CMake regeneration,
the new test group passed all 21 internal Python tests in 1.13 seconds. The new
registry contains 129 tests; this was 128 full-suite tests followed by the new
group, not a single post-registration 129-test invocation. No learned weights,
independent listening, installed nine-host qualification or Beta GO follows
from this engineering checkpoint.

Score-segmentation follow-up: segment schema 3 now crops explicit-silence score
supervision alongside acoustic labels. Notes/rests are clipped and rebased,
syllables/phone ranges/silence indices remapped, and an initial retained melisma
note becomes a local onset with review still invalidated. Inconsistent ownership
requires relabeling instead of invented supervision. All 21 training tests passed,
including CLI publication and melisma/silence reindexing. Silence-only score crops,
fresh feature extraction, source authorization and actual training remain open.

Label-segmentation follow-up: added copied/rebased acoustic labels to segment
config/output schema 2. Parent source identity, geometry and container/PCM hashes
are checked before output creation. Child labels bind the new clip hashes;
phoneme spans and hop-aligned F0/voicing slices are rebased without modifying the
parent, and review revision is always cleared. Off-grid starts explicitly require
fresh extraction. All 20 training tests passed, including CLI labeled publication
and wrong-parent rejection. Score cropping and feature re-extraction remain open.

Segmentation publication follow-up: the `segment` CLI now consumes a captured
configuration and source WAV and creates a new directory with sample-exact
`audio.wav` plus final hash-bound `segment.json`. Validation precedes directory
creation; existing/partial directories reject without overwrite. Interrupted
output is retained, not silently resumed. Tests verify CLI content identity,
frame count, lineage, invalid interval, existing/partial output and immutable
original bytes. All 19 training-tool tests passed. This is single-phrase
publication, not batch recovery, label rebasing, permission admission or training.

Phrase preparation follow-up: implemented `segment_source` to extract exact
half-open PCM frame intervals from captured mono 16/24/32-bit sources. Returns
new WAV bytes plus parent/child content hashes, transform revision/range and
inherited song/session/lineage. Tests verify exact nonzero sample slices,
repeatability, invalid intervals/identity rejection, and descendant grouping in
held-out splits. All 18 training-tool tests passed. No source is modified; no
rights admission, model training, label rebasing or batch publication is claimed.

Schema 3 follow-up adds explicit non-lyric silence phone ownership. Ordered
syllable ranges and silence indices must partition every phone exactly once;
duplicate, overlapping, missing, unordered and out-of-range ownership reject.
Schema 1/2 interpretation is preserved. Also closed the correction-report
budget check for diagnostics produced only by phonemes and missing review.
Verification: 17 training-tool tests passed, including schema 3 CLI publication
and leading/internal/trailing silence ownership. Supplied silence classification
is not acoustic evidence or authenticated review.

Follow-up: label configuration/report schema 2 adds explicit lyric syllables,
phone ranges, MIDI notes, rests and slur continuation checks. Schema 1 remains
unchanged. Canonical `label-report` command now matches the implementation plan,
with `labels` preserved as an alias. All 16 training-tool tests passed, including
source-bound schema 2 subprocess reporting and invalid slur rejection. These
are structural supervision checks, not acoustic alignment, silence-phone
ownership, authenticated review, training execution or musical acceptance.

Added the `tools.voice_model_training labels` CLI to connect label consistency
checks to actual inspected PCM sources. It requires one label per captured
source, verifies container/PCM hashes and frame geometry, and publishes a
configuration-bound report without overwriting earlier output. Inconsistent
labels produce exit 3 and a correction queue; invalid identities produce exit 2
without publication. Source data stays unchanged. This does not authenticate
reviews, admit training rights, train weights, or establish musical quality.

Verification: all 15 training-tool unit tests passed, including the subprocess
label CLI success/correction/mismatched-source/no-overwrite paths. Full native
regression suite was not rerun for this Python-only addition. Next training work
must connect lyrics, note/slur supervision and source/permission admission to
the prepared data; no milestone is claimed complete here.
