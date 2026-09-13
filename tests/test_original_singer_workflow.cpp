#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

class FakeDialog final : public seam::platform::IFileDialog {
public:
  seam::core::Result<std::optional<bool>> chooseRecipePackaging() override {
    return std::optional<bool>{};
  }
  seam::core::Result<std::optional<std::string>> chooseRecipeStyle(
      const std::vector<std::string>&) override { return std::optional<std::string>{}; }
  seam::core::Result<std::optional<std::filesystem::path>> choose(
      const seam::platform::FileDialogRequest& request) override {
    requests.push_back(request);
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto result = responses.front();
    responses.erase(responses.begin());
    return result;
  }
  std::vector<seam::platform::FileDialogRequest> requests;
  std::vector<std::optional<std::filesystem::path>> responses;
};

class FakePrompt final : public seam::platform::IUnsavedChangesPrompt {
public:
  seam::core::Result<seam::platform::UnsavedDecision> choose(std::string_view) override {
    return seam::platform::UnsavedDecision::Discard;
  }
};

// One reviewed, accepted producer candidate: the same deterministic synthetic
// source used by the production-project regressions. Every identity below is a
// test fixture, never a qualified singer or a real reviewer decision.
struct ProducedCandidate final {
  std::filesystem::path root;
  seam::voicebank_production::VoicebankProductionProject project;
  seam::voicebank_production::SampleCandidateRequest request;
  std::filesystem::path workspace;
};

ProducedCandidate produceReviewedCandidate() {
  namespace production = seam::voicebank_production;
  ProducedCandidate produced{};
  produced.root = seam::test::support::temporaryDirectory("original-singer-workflow");
  produced.workspace = produced.root / "workspace";
  const auto license = produced.root / "source-notice.txt";
  CHECK(seam::core::durableAtomicWriteText(license,
      "GENERATED_TEST_FIXTURE_ONLY: no production singer qualification"));
  const auto digest = seam::core::sha256File(license);
  CHECK(digest);
  produced.project = {
      .projectId = "original-singer", .inventoryId = "test-inventory",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "test-synthesis",
      .licenseLocator = license.string(), .licenseSha256 = digest.value(),
      .immutableAssetRoot = "assets",
  };
  auto& project = produced.project;
  project.sourceStrategies.push_back({
      .id = "test-synthesis", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true,
                     .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = digest.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY",
  });
  project.operators = {{.operatorId = "producer", .role = "PRODUCER"},
                       {.operatorId = "reviewer", .role = "REVIEWER"}};
  project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69,
                              .promptId = "prompt-a", .plannedTakeId = "take-a"}};
  production::ProductionProjectRepository repository{produced.workspace};
  CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-14T10:00:00Z"}));
  const auto source = produced.root / "raw.wav";
  CHECK(seam::voicebank::writeWav(source, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24},
      seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
  const auto raw = repository.importRaw(project, source,
      {.takeId = "take-a", .promptId = "prompt-a", .coverageKey = "sustain:a", .pitchLayer = 69},
      {.action = "import", .subjectId = "take-a", .operatorId = "producer",
       .occurredAtUtc = "2026-09-14T10:01:00Z"});
  CHECK(raw);
  const auto operation = repository.stageOperation(project, "take-a", "",
      {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.2F}, "normalized-a");
  CHECK(operation);
  const auto derived = repository.commitStaged(project, operation.value(), "revision-a",
      "producer", "2026-09-14T10:02:00Z", "take-a", "");
  CHECK(derived);
  auto& request = produced.request;
  request.manifest = {.id = "original.singer.candidate", .version = "1.0.0",
      .displayName = "Original Singer Candidate", .characterId = {}, .characterVersion = {},
      .language = seam::domain::Language::Japanese, .expectedSampleRate = 48000U,
      .styles = {"original"}};
  request.manifest.units.push_back({.id = "a-69", .alias = "a", .phones = {"a"},
      .kind = seam::voicebank::UnitKind::Sustain,
      .audioPath = std::filesystem::path{"audio"} / (derived.value().outputSha256 + ".wav"),
      .rootMidi = 69, .style = "original", .take = 1, .priority = 0, .gainDb = 0.0F,
      .renderer = seam::voicebank::RendererHint::ClassicPsola,
      .markers = {.audioOffset = 0, .consonantEnd = 0, .vowelOnset = 0, .stableStart = 480,
                  .loopStart = 480, .loopEnd = 4800, .releaseStart = 5280, .audioEnd = 5760},
      .pitchMarks = {}, .enabled = true});
  for (std::int64_t period = 1;; ++period) {
    const auto frame = static_cast<seam::time::SampleFrame>(
        std::llround(static_cast<double>(period) * 48000.0 / 440.0));
    if (frame >= request.manifest.units.front().markers.audioEnd) break;
    request.manifest.units.front().pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = true});
  }
  const auto beforePreparation = production::encodeProductionProject(project);
  const auto packet = production::prepareSampleCandidateReview(produced.workspace, project, request.manifest);
  CHECK(packet);
  CHECK(production::encodeProductionProject(project) == beforePreparation);
  const auto receipt = production::commitSampleCandidateReview(produced.workspace, project, packet.value(),
      "reviewer", "2026-09-14T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(receipt);
  CHECK(receipt.value().durabilityConfirmed);
  CHECK(receipt.value().candidate);
  request = *receipt.value().candidate;
  return produced;
}

}  // namespace

// M1.P3 automated exit: one deterministic source -> edit -> review -> candidate ->
// package -> install -> new song -> export -> save/reopen path, with a genuine
// refusal and retry and an immutability check after a later producer draft change.
TEST_CASE("original singer workflow packages installs and renders a new song from reviewed material") {
  namespace production = seam::voicebank_production;
  using namespace seam;
  auto produced = produceReviewedCandidate();
  const auto published = production::publishSampleCandidate(produced.workspace, produced.project,
      produced.request, produced.root / "candidate");
  CHECK(published);
  CHECK(!published.value().releaseEligible);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(published.value().root / "manifest.json");
  CHECK(manifest);

  // Package the candidate as a signed distribution artifact.
  auto key = distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = produced.root / "original-singer.seambank";
  CHECK(distribution::packSeambank(published.value().root, package, key.value()));
  CHECK(std::filesystem::exists(package));

  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = produced.root / "cache",
      .voicebankRoots = {},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = true,
  });
  CHECK(session);
  auto dialog = std::make_unique<FakeDialog>();
  auto* dialogPtr = dialog.get();
  // The tampered package is refused first, and the same action succeeds on retry.
  const auto tampered = produced.root / "tampered.seambank";
  {
    auto bytes = core::readFileBytesLimited(package, 16U * 1024U * 1024U);
    CHECK(bytes);
    bytes.value().back() ^= std::byte{1};
    CHECK(core::durableAtomicWriteNew(tampered, bytes.value()));
  }
  dialogPtr->responses = {tampered, package};
  auto controller = standalone::StandaloneApplicationController::create(*session.value(), std::move(dialog),
      std::make_unique<FakePrompt>(), standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = produced.root / "autosaves",
          .recentProjectsPath = produced.root / "recent.json",
          .voicebankInstallRoot = produced.root / "voicebanks",
          .trustedVoicebankKeys = {key.value().publicKey},
          .developmentTrustRoot = std::nullopt,
          .allowDevelopmentVoicebanks = false,
          .defaultNewProject = {.name = "Original Singer Song", .tempoBpm = 120.0,
                                .sampleRate = 48000U, .outputChannels = 2U,
                                .initialVoicebank = std::nullopt},
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(!controller.value()->dispatch(platform::ApplicationCommand::InstallVoicebank));
  CHECK(controller.value()->voicebankCards().empty());
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::InstallVoicebank));
  CHECK(controller.value()->voicebankCards().size() == 1U);
  const auto& card = controller.value()->voicebankCards().front();
  CHECK(card.installed);
  CHECK(card.trust == voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(card.contentHash == published.value().contentSha256);
  CHECK(controller.value()->selectVoicebank(card.id, card.version, card.contentHash));

  auto [lyric, note] = session.value()->runtime().document().factory().makeNote(
      time::Tick{0}, time::Tick{1920}, 69U, U"あ", domain::Language::Japanese);
  CHECK(session.value()->runtime().execute(std::make_unique<application::AddNoteCommand>(
      session.value()->regionId(), std::move(lyric), std::move(note))));
  const auto coverage = controller.value()->selectedRegionCoverage();
  CHECK(coverage);
  CHECK(coverage.value().complete());

  // The producer workspace is gone before the new song is exported.
  std::error_code error;
  std::filesystem::rename(produced.workspace, produced.root / "producer-unavailable", error);
  CHECK(!error);
  CHECK(!std::filesystem::exists(produced.workspace));
  authoring::ExportSettings settings;
  settings.includeMaster = true;
  settings.includeStems = true;
  const auto exported = controller.value()->exportSet(produced.root / "export", settings);
  if (!exported) throw test::Failure{"original singer export failed: " + exported.error().message};
  CHECK(std::filesystem::exists(exported.value().masterPath));
  CHECK(std::filesystem::file_size(exported.value().masterPath) > 44U);
  CHECK(exported.value().files.size() >= 2U);

  // Save and reopen the new song, then confirm the binding and audio survive.
  const auto songPath = produced.root / "original-singer.seam";
  dialogPtr->responses.push_back(songPath);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SaveProjectAs));
  CHECK(std::filesystem::exists(songPath));
  dialogPtr->responses.push_back(songPath);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenProject));
  const auto* reopened = session.value()->runtime().document().session().project()
                             .findVocalTrack(session.value()->trackId());
  CHECK(reopened != nullptr);
  CHECK(reopened->voicebank.contentHash == published.value().contentSha256);
  const auto repeated = controller.value()->exportSet(produced.root / "export-again", settings);
  if (!repeated) throw test::Failure{"original singer re-export failed: " + repeated.error().message};
  CHECK(repeated.value().masterSha256 == exported.value().masterSha256);

  // A later producer draft change must not reach the installed bank or the song.
  const auto songBytes = core::readFileBytesLimited(songPath, 16U * 1024U * 1024U);
  CHECK(songBytes);
  // Locate the installed bank by scanning the install root instead of assuming a
  // directory convention the installer owns.
  const auto installedRoot = [&]() -> std::optional<std::filesystem::path> {
    std::error_code scanError;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             produced.root / "voicebanks", scanError)) {
      if (scanError) break;
      if (entry.is_regular_file() && entry.path().filename() == "manifest.json") {
        return entry.path().parent_path();
      }
    }
    return std::nullopt;
  }();
  CHECK(installedRoot.has_value());
  const auto installedManifest = voicebank::ManifestJsonCodec{}.load(*installedRoot / "manifest.json");
  CHECK(installedManifest);
  const auto installedContent = voicebank::computeVoicebankContentHash(installedManifest.value(), *installedRoot);
  CHECK(installedContent);
  CHECK(installedContent.value() == card.contentHash);
  auto& producerProject = produced.project;
  producerProject.unitAssignments.front().state = production::UnitQueueState::MarkerReview;
  producerProject.lastDurableGeneration = {};
  const auto afterDraftChange = core::readFileBytesLimited(songPath, 16U * 1024U * 1024U);
  CHECK(afterDraftChange);
  CHECK(afterDraftChange.value() == songBytes.value());
  const auto stillInstalled = voicebank::ManifestJsonCodec{}.load(
      *installedRoot / "manifest.json");
  CHECK(stillInstalled);
  CHECK(stillInstalled.value() == installedManifest.value());
}
