// Application-owned neural selection: a surface's own signed deployment plus an
// installed bundle must produce exactly the source the renderer consumes, and
// every refusal must happen before a bundle is admitted or a runner exists.
//
// The selected helper is the transport fixture probe, which performs no
// inference. Nothing here proves musical output or model quality; it proves
// selection, admission and refusal behaviour.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "test_onnx_fixture.hpp"

#include "seam/authoring/neural_resource_registry.hpp"
#include "seam/authoring/neural_selection.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#ifndef SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE
#error SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE is required for neural selection tests
#endif

namespace {

using namespace seam;

constexpr std::size_t kBundleBytes{1024U * 1024U};
constexpr std::size_t kMaximumResidentBytes{256U * 1024U * 1024U};
constexpr std::uint64_t kMaximumFrames{96'000U};
constexpr std::int64_t kInferenceSteps{10};

#if defined(_WIN32)
constexpr std::string_view kPlatform{"windows-x64"};
#else
constexpr std::string_view kPlatform{"macos-arm64"};
#endif

// The mel feature declaration has a short shape for configuration schema 1 and an
// extended one from schema 2, and the reader accepts exactly one shape per
// configuration version.
std::string features(bool extended) {
  std::string value{R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF",)"
      R"("amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,)"
      R"("maximumHz":16000.0)"};
  if (extended) value += R"(,"fftSize":2048,"windowSize":1024,"melFrequencyScale":"slaney")";
  return value + "}";
}

// Configuration schema 3 declares a steps layout and a vocoder output name.
// Schema 2 declares only a steps layout, and schema 1 declares neither, so a v1
// bundle cannot be executed without assuming defaults that were never reviewed.
// Each shape is exactly what the bundle metadata reader accepts for its version.
std::string configuration(std::uint32_t schemaVersion) {
  std::string value{R"({"formatId":"com.project-seam.neural-bundle-configuration","schemaVersion":)"};
  const bool extended = schemaVersion >= 2U;
  value += std::to_string(schemaVersion) + R"(,"maximumFrames":48000,"acousticFeatures":)" +
      features(extended) + R"(,"vocoderFeatures":)" + features(extended);
  if (schemaVersion >= 2U) value += R"(,"stepsLayout":"scalar")";
  if (schemaVersion >= 3U) value += R"(,"vocoderOutput":"audio")";
  return value + "}";
}

constexpr std::string_view kVocabulary{
    R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a","i"]})"};

struct Bundle final {
  std::filesystem::path directory;
  std::string id, version, contentHash;
};

// One installable neural resource: the verified bundle plus the resource record a
// project saves. The record digest is the manifest digest by construction, exactly
// as the installed-resource scanner requires.
Bundle writeBundle(const std::filesystem::path& root, std::string_view name, std::string id,
    std::string version, std::uint32_t configurationSchema) {
  using namespace synthesis;
  Bundle bundle{};
  bundle.directory = root / name;
  std::filesystem::create_directories(bundle.directory);
  // Real graph bytes: admission reads both files and binds the acoustic mel output to the vocoder
  // mel input, so a placeholder would be refused before the selection behaviour under test.
  const std::string acoustic{test::onnx::onnxAcousticGraph()};
  const std::string vocoder{test::onnx::onnxVocoderGraph(80U,1U,"audio")};
  const std::string declaration{configuration(configurationSchema)};
  const auto asset = [](NeuralAssetRole role, const char* assetName, std::string_view value) {
    return NeuralBundleAssetInput{role, assetName,
        std::as_bytes(std::span{value.data(), value.size()}), core::sha256Hex(value)};
  };
  const std::array assets{asset(NeuralAssetRole::Acoustic, "acoustic", acoustic),
      asset(NeuralAssetRole::Vocoder, "vocoder", vocoder),
      asset(NeuralAssetRole::Vocabulary, "vocabulary", kVocabulary),
      asset(NeuralAssetRole::Configuration, "configuration", declaration)};
  const auto manifest = FrozenNeuralBundle::manifest(assets, kBundleBytes);
  if (!manifest) throw test::Failure{"neural selection fixture manifest failed"};
  bundle.id = std::move(id);
  bundle.version = std::move(version);
  bundle.contentHash = core::sha256Hex(manifest.value());
  const auto frozen = FrozenNeuralBundle::freeze(
      {domain::SingerResourceKind::Neural, bundle.id, bundle.version, bundle.contentHash},
      assets, kBundleBytes);
  if (!frozen) throw test::Failure{"neural selection fixture freeze failed"};
  if (!core::durableAtomicWriteNew(bundle.directory / "manifest.json",
          frozen.value().manifestData().bytes()))
    throw test::Failure{"neural selection manifest write failed"};
  for (const auto& entry : frozen.value().assets())
    if (!core::durableAtomicWriteNew(bundle.directory / entry.name, entry.data->bytes()))
      throw test::Failure{"neural selection asset write failed"};
  const std::string record{
      "{\"formatId\":\"com.project-seam.neural-resource\",\"schemaVersion\":1,\"id\":\"" +
      bundle.id + "\",\"version\":\"" + bundle.version + "\",\"contentHash\":\"" +
      bundle.contentHash + "\"}"};
  if (!core::durableAtomicWriteNew(bundle.directory / "resource.json",
          std::span{reinterpret_cast<const std::byte*>(record.data()), record.size()}))
    throw test::Failure{"neural selection record write failed"};
  return bundle;
}

// The surface's own deployment: a helper manifest beside the loaded module, a
// signed descriptor naming them, and the release key that signed it. Production
// takes all three from the deployment trust owner; this fixture signs with an
// ephemeral key and removes both written files again.
struct SurfaceFixture final {
  std::filesystem::path descriptorPath;
  std::filesystem::path manifestPath;
  authoring::NeuralSelectionSurface surface;
  distribution::SigningKeyPair key{};

  ~SurfaceFixture() {
    std::error_code error;
    std::filesystem::remove(descriptorPath, error);
    std::filesystem::remove(manifestPath, error);
  }
};

std::string helperFile(std::string_view path, std::string_view digest) {
  return R"({"path":")" + std::string{path} + R"(","sha256":")" + std::string{digest} +
      R"(","maximumBytes":268435456})";
}

SurfaceFixture makeSurface(const std::filesystem::path& scratch, std::string buildId,
    std::uint32_t descriptorSchema, std::string_view platformName, std::string_view surfaceName) {
  static const char moduleAnchor{0};
  const auto module = platform::loadedModulePath(&moduleAnchor);
  if (!module) throw test::Failure{"neural selection fixture has no loaded module"};
  const auto moduleDigest = core::sha256File(module.value());
  const auto helper = std::filesystem::canonical(SEAM_NEURAL_BUNDLE_TRANSPORT_PROBE);
  const auto helperDigest = core::sha256File(helper);
  if (!moduleDigest || !helperDigest) throw test::Failure{"neural selection fixture digest failed"};

  SurfaceFixture fixture{};
  fixture.manifestPath = module.value().parent_path() / "seam-neural-selection-fixture.json";
  const std::string manifest = R"({"formatId":"com.project-seam.neural-helper-package","schemaVersion":)" +
      std::to_string(descriptorSchema) + R"(,"buildId":")" + buildId + R"(","protocolVersion":)" +
      std::to_string(descriptorSchema) + R"(,"module":)" +
      helperFile(module.value().filename().string(), moduleDigest.value()) + R"(,"helper":)" +
      helperFile(helper.filename().string(), helperDigest.value()) + R"(,"dependencies":[]})";
  if (!core::durableAtomicWriteText(fixture.manifestPath, manifest))
    throw test::Failure{"neural selection fixture manifest write failed"};
  const std::string descriptor = R"({"formatId":"com.project-seam.neural-deployment","schemaVersion":)" +
      std::to_string(descriptorSchema) + R"(,"buildId":")" + buildId + R"(","platform":")" +
      std::string{platformName} + R"(","surface":")" + std::string{surfaceName} + R"(","modulePath":")" +
      module.value().filename().string() + R"(","manifestPath":")" + fixture.manifestPath.filename().string() +
      R"(","manifestSha256":")" + core::sha256Hex(manifest) + R"(","protocolVersion":)" +
      std::to_string(descriptorSchema) + "}";
  fixture.descriptorPath = scratch / "neural-deployment.json";
  if (!core::durableAtomicWriteText(fixture.descriptorPath, descriptor))
    throw test::Failure{"neural selection fixture descriptor write failed"};
  const auto key = distribution::generateSigningKeyPair();
  if (!key) throw test::Failure{"neural selection fixture key failed"};
  fixture.key = key.value();
  const auto signature = distribution::signEd25519(
      std::as_bytes(std::span{descriptor.data(), descriptor.size()}), fixture.key.privateKey);
  if (!signature) throw test::Failure{"neural selection fixture signature failed"};

  fixture.surface = authoring::NeuralSelectionSurface{};
  fixture.surface.deploymentDescriptor = fixture.descriptorPath;
  fixture.surface.deploymentSignature = signature.value();
  fixture.surface.trustedReleaseKey = fixture.key.publicKey;
  fixture.surface.buildId = std::move(buildId);
  fixture.surface.platform = std::string{platformName};
  fixture.surface.surface = std::string{surfaceName};
  fixture.surface.moduleAnchor = &moduleAnchor;
  fixture.surface.provenance = rendering::NeuralRenderProvenance{
      .workerVersion = "seam.neural-worker.v1", .runtimeVersion = "onnxruntime-1.30.0",
      .provider = "CPUExecutionProvider"};
  fixture.surface.maximumBundleBytes = kBundleBytes;
  fixture.surface.maximumFrames = kMaximumFrames;
  fixture.surface.inferenceSteps = kInferenceSteps;
  fixture.surface.maximumResidentBytes = kMaximumResidentBytes;
  fixture.surface.maximumCpuTime = std::chrono::seconds{5};
  fixture.surface.helperTimeout = std::chrono::seconds{20};
  return fixture;
}

authoring::NeuralResourceRegistry scan(const Bundle& bundle, std::size_t maximumResources = 8U) {
  const auto registry = authoring::NeuralResourceRegistry::scan(
      bundle.directory.parent_path(), maximumResources, kBundleBytes, kBundleBytes);
  if (!registry) throw test::Failure{"neural selection fixture scan failed"};
  return registry.value();
}

domain::NeuralResourceReference reference(const Bundle& bundle) {
  return domain::NeuralResourceReference{{domain::SingerResourceKind::Neural, bundle.id,
      bundle.version, bundle.contentHash}};
}

}  // namespace

namespace {

class StubDialog final : public platform::IFileDialog {
public:
  core::Result<std::optional<std::filesystem::path>> choose(
      const platform::FileDialogRequest&) override {
    return std::optional<std::filesystem::path>{};
  }
};

class StubPrompt final : public platform::IUnsavedChangesPrompt {
public:
  core::Result<platform::UnsavedDecision> choose(std::string_view) override {
    return platform::UnsavedDecision::Discard;
  }
};

}  // namespace

TEST_CASE("neural selection verifies the surface deployment before admitting a bundle") {
  const auto scratch = test::support::temporaryDirectory("neural-selection-surface");
  const auto bundle = writeBundle(scratch / "resources", "voice", "seam-pilot-01", "1.0.0", 3U);
  const auto registry = scan(bundle);
  const auto track = domain::TrackId{};

  auto fixture = makeSurface(scratch, "fixture-build", 3U, kPlatform, "standalone");
  const auto created = authoring::NeuralSelectionService::create(fixture.surface);
  CHECK(created);
  if (!created) return;
  CHECK(created.value().buildId() == "fixture-build");
  CHECK(!created.value().deploymentContentHash().empty());
  CHECK(created.value().worker().maximumResidentBytes == kMaximumResidentBytes);
  CHECK(created.value().worker().protocolVersion == 3U);

  // A descriptor signed by a different key is not this surface's deployment.
  const auto stranger = distribution::generateSigningKeyPair();
  CHECK(stranger);
  auto wrongKey = fixture.surface;
  wrongKey.trustedReleaseKey = stranger.value().publicKey;
  CHECK(!authoring::NeuralSelectionService::create(wrongKey));

  // Earlier CLI contracts cannot select a runner that requires an explicit
  // admitted step count, even when their packages and signatures are valid.
  auto contractSigned = makeSurface(scratch, "fixture-build", 1U, kPlatform, "standalone");
  CHECK(!authoring::NeuralSelectionService::create(contractSigned.surface));
  auto previousContract = makeSurface(scratch, "fixture-build", 2U, kPlatform, "standalone");
  CHECK(!authoring::NeuralSelectionService::create(previousContract.surface));

  // Target mismatch and unmeasured budgets are refused before any file is used.
  auto otherTarget = fixture.surface;
  otherTarget.surface = "clap";
  CHECK(!authoring::NeuralSelectionService::create(otherTarget));
  auto otherPlatform = fixture.surface;
  otherPlatform.platform = kPlatform == "windows-x64" ? "macos-arm64" : "windows-x64";
  CHECK(!authoring::NeuralSelectionService::create(otherPlatform));
  auto unbudgeted = fixture.surface;
  unbudgeted.maximumResidentBytes = 0U;
  CHECK(!authoring::NeuralSelectionService::create(unbudgeted));
  auto unanchored = fixture.surface;
  unanchored.moduleAnchor = nullptr;
  CHECK(!authoring::NeuralSelectionService::create(unanchored));
  auto cancelled = std::stop_source{};
  cancelled.request_stop();
  CHECK(!authoring::NeuralSelectionService::create(fixture.surface, cancelled.get_token()));

  // The saved reference resolves to the admitted bundle, and the prepared source
  // carries the surface's own provenance rather than anything a bank could state.
  const auto selected = created.value().select(track, reference(bundle), registry);
  CHECK(selected);
  if (!selected) return;
  CHECK(selected.value().trackId == track);
  CHECK(selected.value().bundle != nullptr);
  CHECK(selected.value().runner != nullptr);
  CHECK(selected.value().bundle->execution().modelId == bundle.id);
  CHECK(selected.value().bundle->execution().modelVersion == bundle.version);
  CHECK(selected.value().bundle->execution().bundleContentHash == bundle.contentHash);
  CHECK(selected.value().provenance.workerVersion == "seam.neural-worker.v1");
  CHECK(selected.value().provenance.runtimeVersion == "onnxruntime-1.30.0");
  CHECK(selected.value().provenance.provider == "CPUExecutionProvider");

  // A saved selection this installation does not have is refused instead of
  // being replaced by the installed voice.
  auto unknown = reference(bundle);
  unknown.resource.version = "2.0.0";
  CHECK(!created.value().select(track, unknown, registry));
  auto relabelled = reference(bundle);
  relabelled.resource.contentHash = std::string(64U, '0');
  CHECK(!created.value().select(track, relabelled, registry));
  CHECK(!created.value().select(track, reference(bundle), registry, cancelled.get_token()));
}

TEST_CASE("neural selection refuses a bundle that cannot be executed") {
  const auto scratch = test::support::temporaryDirectory("neural-selection-refusal");
  // Schema 1 declares no steps layout and no vocoder output name, so admitting it
  // would silently assume defaults that were never reviewed.
  const auto bundle = writeBundle(scratch / "resources", "voice", "seam-pilot-01", "1.0.0", 1U);
  const auto registry = scan(bundle);
  auto fixture = makeSurface(scratch, "fixture-build", 3U, kPlatform, "standalone");
  const auto created = authoring::NeuralSelectionService::create(fixture.surface);
  CHECK(created);
  if (!created) return;
  const auto selected = created.value().select(domain::TrackId{}, reference(bundle), registry);
  CHECK(!selected);
  if (selected) return;
  CHECK(selected.error().code == core::ErrorCode::Unsupported);
}

TEST_CASE("neural selection refuses a resource root with a duplicate identity") {
  const auto scratch = test::support::temporaryDirectory("neural-selection-duplicate");
  const auto root = scratch / "resources";
  writeBundle(root, "voice-a", "seam-pilot-01", "1.0.0", 3U);
  // A second directory claiming the same identity is ambiguous, not a retry.
  writeBundle(root, "voice-b", "seam-pilot-01", "1.0.0", 3U);
  const auto registry = authoring::NeuralResourceRegistry::scan(root, 8U, kBundleBytes, kBundleBytes);
  CHECK(!registry);
}

TEST_CASE("standalone controller selects and clears an installed neural singer") {
  const auto scratch = test::support::temporaryDirectory("neural-selection-controller");
  const auto bundle = writeBundle(scratch / "resources", "voice", "seam-pilot-01", "1.0.0", 3U);
  auto fixture = makeSurface(scratch, "fixture-build", 3U, kPlatform, "standalone");
  std::filesystem::create_directories(scratch / "banks");
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = scratch / "cache",
      .voicebankRoots = {voicebank::VoicebankSearchRoot{
          .path = scratch / "banks", .kind = voicebank::VoicebankRootKind::Development}},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = true,
  });
  CHECK(session);
  if (!session) return;
  standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = scratch / "autosaves";
  config.recentProjectsPath = scratch / "recent.json";
  config.neuralSelection = fixture.surface;
  config.neuralResourceRoot = bundle.directory.parent_path();
  auto controller = standalone::StandaloneApplicationController::create(*session.value(),
      std::make_unique<StubDialog>(), std::make_unique<StubPrompt>(), config);
  CHECK(controller);
  if (!controller) return;

  const auto listed = controller.value()->neuralResources();
  CHECK(listed.size() == 1U);
  if (listed.size() != 1U) return;
  CHECK(listed.front().id == bundle.id);
  CHECK(listed.front().version == bundle.version);
  CHECK(listed.front().contentHash == bundle.contentHash);
  CHECK(!listed.front().selected);
  // The neural menu states what the singer renders, exactly as the procedural picker does. A creator
  // choosing a neural voice must be able to see that the six timbral channels are not part of it
  // before writing a phrase, rather than discovering that after drawing a curve.
  CHECK(listed.front().capabilitySummary.find("neural singer") != std::string::npos);
  CHECK(listed.front().capabilitySummary.find("pitch") != std::string::npos);
  CHECK(listed.front().capabilitySummary.find("formant") == std::string::npos);

  // A selection this installation cannot run is refused before the project changes.
  const auto& project = session.value()->runtime().document().session().project();
  const auto trackId = session.value()->runtime().selectedTrack();
  const auto* track = project.findVocalTrack(trackId);
  CHECK(track != nullptr);
  CHECK(!controller.value()->selectNeuralResource(bundle.id, "2.0.0", bundle.contentHash));
  CHECK(!controller.value()->clearNeuralResource());
  CHECK(track != nullptr && !track->neuralResource.has_value());

  CHECK(controller.value()->selectNeuralResource(bundle.id, bundle.version, bundle.contentHash));
  const auto* selected = session.value()->runtime().document().session().project()
                             .findVocalTrack(trackId);
  CHECK(selected != nullptr);
  if (selected == nullptr) return;
  CHECK(selected->neuralResource.has_value());
  CHECK(selected->neuralResource->resource.kind == domain::SingerResourceKind::Neural);
  CHECK(selected->neuralResource->resource.id == bundle.id);
  CHECK(selected->neuralResource->resource.version == bundle.version);
  CHECK(selected->neuralResource->resource.contentHash == bundle.contentHash);
  CHECK(controller.value()->neuralResources().front().selected);

  // Undo restores the previous state through the same command the edit used.
  CHECK(session.value()->runtime().undo());
  const auto* undone = session.value()->runtime().document().session().project()
                           .findVocalTrack(trackId);
  CHECK(undone != nullptr && !undone->neuralResource.has_value());

  CHECK(controller.value()->selectNeuralResource(bundle.id, bundle.version, bundle.contentHash));
  CHECK(controller.value()->clearNeuralResource());
  const auto* cleared = session.value()->runtime().document().session().project()
                            .findVocalTrack(trackId);
  CHECK(cleared != nullptr && !cleared->neuralResource.has_value());
}
