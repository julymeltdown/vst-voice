// M1.P3 connected regression with the generation campaign inside the chain:
// recipe -> planned campaign -> rendered held-out preflight -> committed collected take ->
// native manifest draft -> native review decision -> native candidate publication -> signed
// package -> install -> new song export and save/reopen, with the collected audio hash checked
// from the campaign receipt through to the installed bank.
//
// Every identity here is a synthetic fixture. This proves the connected path, not a qualified
// singer, not musical quality and not an independent reviewer decision.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/project.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace seam;
namespace production = voicebank_production;
using Controller = native_ui::VoicebankStudioController;
using Phase = Controller::GenerationCampaignProgress::Phase;

// Own-thread worker control: the controller runs its work on a future, and the test only polls it.
core::Result<void> drain(Controller& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{180};
  while (controller.proceduralImportBusy()) {
    auto result = controller.pollProceduralCandidateImport();
    if (!result) return result;
    if (std::chrono::steady_clock::now() >= deadline)
      throw test::Failure{"Studio campaign worker did not finish"};
    if (controller.proceduralImportBusy()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return core::success();
}

// The producer recipe the campaign renders from: one vowel pose and one frication source, in the
// single style the fixture workspace owns.
voice_design::VoiceRecipe campaignRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "original-singer-campaign";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  return recipe;
}

// The gate under test: a campaign may not advance until the held-out phrases that exercise every
// class it declares have rendered audibly beside it.
void preflightCampaign(const Controller& controller, const std::filesystem::path& directory) {
  const auto path = controller.generationCampaignPath();
  const auto bytes = core::readTextFileLimited(path, 32U * 1024U * 1024U);
  CHECK(bytes);
  if (!bytes) return;
  const auto report = authoring::runInventoryPreflight(bytes.value(), controller.generationCampaignSha256(),
      directory / "preflight");
  CHECK(report);
  if (report) CHECK(report.value().passed);
}

struct InstallDialog final : platform::IFileDialog {
  core::Result<std::optional<std::filesystem::path>> choose(
      const platform::FileDialogRequest&) override {
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto result = responses.front();
    responses.erase(responses.begin());
    return result;
  }
  std::vector<std::optional<std::filesystem::path>> responses;
};

struct DiscardPrompt final : platform::IUnsavedChangesPrompt {
  core::Result<platform::UnsavedDecision> choose(std::string_view) override {
    return platform::UnsavedDecision::Discard;
  }
};

}  // namespace

TEST_CASE("a collected generation campaign take becomes the installed bank of a new song") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("original-singer-campaign-workflow");
  const auto workspace = root / "producer";
  const auto license = root / "source-notice.txt";
  CHECK(core::durableAtomicWriteTextNew(license,
      "GENERATED_TEST_FIXTURE_ONLY: no production singer qualification"));
  const auto licenseSha = core::sha256File(license);
  CHECK(licenseSha);
  production::VoicebankProductionProject project{
      .projectId = "original-singer-campaign", .inventoryId = "test-inventory",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "test-synthesis",
      .licenseLocator = license.string(), .licenseSha256 = licenseSha.value(),
      .immutableAssetRoot = "assets"};
  project.schemaVersion = production::kProductionStyleSchemaVersion;
  project.language = "ja";
  project.sourceStrategies.push_back({
      .id = "test-synthesis", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true,
                      .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = licenseSha.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY"});
  project.operators = {{.operatorId = "producer", .role = "PRODUCER"},
                       {.operatorId = "reviewer", .role = "REVIEWER"}};
  project.unitAssignments = {{.coverageKey = "cv:s:a", .pitchLayer = 69, .promptId = "prompt-sa",
                              .plannedTakeId = "take-sa", .style = "neutral"}};
  production::ProductionProjectRepository repository{workspace};
  CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-14T12:00:00Z"}));
  const auto recipePath = root / "recipe.json";
  CHECK(voice_design::saveVoiceRecipeFile(recipePath, campaignRecipe()));
  Controller controller;
  CHECK(controller.openProductionProject(workspace, project.inventorySha256, "producer"));
  CHECK(drain(controller));
  CHECK(controller.productionProject());

  // The campaign is planned, gated by the rendered held-out preflight, and advanced to completion.
  const auto campaign = root / "campaign";
  CHECK(controller.beginGenerationCampaignPlan(recipePath, {"take-sa"}, campaign, 1U));
  CHECK(drain(controller));
  const auto planned = controller.generationCampaignProgress();
  CHECK(planned);
  CHECK(planned && planned->phase == Phase::Planned);
  CHECK(planned && planned->totalBatches == 1U);
  CHECK(controller.productionProject()->takes.empty());
  preflightCampaign(controller, campaign);
  CHECK(controller.beginGenerationCampaignAdvance(controller.generationCampaignPath(),
      controller.generationCampaignSha256(), "2026-09-14T12:01:00Z"));
  CHECK(drain(controller));
  const auto complete = controller.generationCampaignProgress();
  CHECK(complete);
  CHECK(complete && complete->phase == Phase::Complete);
  CHECK(complete && complete->completedBatches == 1U);
  // What the campaign committed is unapproved marker-review material, not a finished resource.
  CHECK(controller.productionProject());
  CHECK(controller.productionProject()->takes.size() == 1U);
  if (controller.productionProject()->takes.empty()) return;
  const auto generatedSha = controller.productionProject()->takes.front().rawAssetSha256;
  CHECK(generatedSha.size() == 64U);
  CHECK(controller.productionProject()->takes.front().state == production::UnitQueueState::MarkerReview);
  const auto durable = repository.recover();
  CHECK(durable);
  CHECK(durable && durable.value().takes.size() == 1U);

  // A style-owned producer requires an explicit current quality assessment before any unit of its
  // material can be published, so the native action records one against the collected material.
  const auto qualityContext = controller.captureSampleReviewContext();
  CHECK(qualityContext);
  const auto evidence = root / "source-quality.txt";
  CHECK(core::durableAtomicWriteTextNew(evidence,
      "SYNTHETIC TEST EVIDENCE: the coverage and listening outcomes below are fixture declarations"));
  CHECK(controller.beginSourceQualityEvidenceCapture(qualityContext.value(), evidence));
  CHECK(drain(controller));
  CHECK(controller.sourceQualityInspection());
  if (!controller.sourceQualityInspection() || controller.sourceQualityInspection()->reviewers.empty())
    return;
  const auto quality = *controller.sourceQualityInspection();
  CHECK(controller.beginSourceQualityDecision(quality, "assessment-1", quality.reviewers.front(),
      production::Feasibility::Pass, production::Feasibility::Pass, "2026-09-14T12:00:30Z"));
  CHECK(drain(controller));
  CHECK(controller.sourceQualityReceipt());
  CHECK(controller.sourceQualityReceipt() && controller.sourceQualityReceipt()->durabilityConfirmed);

  // The collected material becomes an editable manifest draft through the native action.
  const auto draftContext = controller.captureSampleReviewContext();
  CHECK(draftContext);
  const production::SampleManifestDraftIdentity identity{.id = "original.singer.generated",
      .version = "1.0.0", .displayName = "Generated Candidate",
      .language = domain::Language::Japanese, .style = "neutral"};
  CHECK(controller.beginSampleManifestDraftCreation(draftContext.value(), identity, root / "draft"));
  CHECK(drain(controller));
  const auto draft = controller.createdSampleManifestDraft();
  CHECK(draft);
  CHECK(draft && draft->missingAssignments.empty());
  CHECK(draft && draft->durabilityConfirmed);
  CHECK(draft && std::filesystem::exists(draft->root / "manifest.json"));
  CHECK(!controller.manifest().units.empty());
  if (draft && !controller.manifest().units.empty()) {
    const std::vector<std::string> expectedPhones{"s", "a"};
    CHECK(controller.manifest().units.front().phones == expectedPhones);
    // The audio in the draft is the audio the campaign committed.
    const auto draftAudio = core::sha256File(draft->root / controller.manifest().units.front().audioPath);
    CHECK(draftAudio);
    CHECK(!draftAudio || draftAudio.value() == generatedSha);
  }

  // A decision is required: publication before one is refused and leaves nothing behind.
  CHECK(controller.beginSelectedSampleReview());
  CHECK(drain(controller));
  CHECK(controller.sampleReviewInspection());
  const auto premature = controller.captureSampleReviewContext();
  CHECK(premature);
  CHECK(controller.beginSampleCandidatePublication(premature.value(), root / "premature"));
  CHECK(!drain(controller));
  CHECK(!std::filesystem::exists(root / "premature"));
  CHECK(!controller.publishedSampleCandidate());
  CHECK(!controller.sampleReviewReceipt());

  // A marker edit is a real edit of the selected unit through the native action, and it is saved
  // before anything reviews it.
  CHECK(controller.selectedUnit() != nullptr);
  if (controller.selectedUnit() == nullptr) return;
  const auto beforeEdit = controller.selectedUnit()->markers.stableStart;
  CHECK(controller.moveSelectedMarker(ui::AcousticMarkerKind::StableStart,
      controller.microscope().frameToPixel(beforeEdit + 8)));
  CHECK(controller.selectedUnit()->markers.stableStart != beforeEdit);
  CHECK(controller.dirty());
  CHECK(controller.save());
  CHECK(!controller.dirty());

  // The first decision rejects the edited unit, and nothing is publishable from it.
  CHECK(controller.beginSelectedSampleReview());
  CHECK(drain(controller));
  if (!controller.sampleReviewInspection()) return;
  CHECK(controller.selectSampleReviewer(controller.sampleReviewInspection()->context, "reviewer"));
  CHECK(controller.beginSampleReviewDecision(controller.sampleReviewInspection()->context, "reviewer",
      production::SampleCandidateReviewDecision::Reject, "2026-09-14T12:02:00Z"));
  CHECK(drain(controller));
  const auto rejected = controller.sampleReviewReceipt();
  CHECK(rejected);
  CHECK(rejected && !rejected->candidate);
  CHECK(controller.productionProject());
  CHECK(controller.productionProject()->unitAssignments.front().state == production::UnitQueueState::Rejected);
  const auto afterRejection = controller.captureSampleReviewContext();
  CHECK(afterRejection);
  CHECK(controller.beginSampleCandidatePublication(afterRejection.value(), root / "rejected"));
  CHECK(!drain(controller));
  CHECK(!std::filesystem::exists(root / "rejected"));

  // The retake is the product's own action: inspect the new material, import it as a superseding
  // take of the rejected assignment, and check that nothing from the rejected take travels with it.
  const auto retakeWav = root / "retake.wav";
  CHECK(voicebank::writeWav(retakeWav, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = voicebank::WavSampleFormat::Pcm24},
      test::support::sineWave(48000U, 440.0, 0.18, 0.18F)));
  CHECK(controller.inspectTake(retakeWav, 69));
  CHECK(controller.importSelectedTake(retakeWav, "2026-09-14T12:03:00Z"));
  CHECK(controller.productionProject());
  const auto retakeTakeId = controller.productionProject()->unitAssignments.front().takeId;
  CHECK(!retakeTakeId.empty());
  CHECK(retakeTakeId != "take-sa");
  CHECK(controller.productionProject()->unitAssignments.front().state == production::UnitQueueState::MarkerReview);
  const auto retaken = std::find_if(controller.productionProject()->takes.begin(),
      controller.productionProject()->takes.end(),
      [&](const production::TakeRecord& take) { return take.takeId == retakeTakeId; });
  CHECK(retaken != controller.productionProject()->takes.end());
  if (retaken == controller.productionProject()->takes.end()) return;
  const auto retakeSha = retaken->rawAssetSha256;
  CHECK(retakeSha.size() == 64U);
  CHECK(retakeSha != generatedSha);
  // The assessment recorded for the previous material does not qualify this one.
  CHECK(!production::requireTakeSourceQualification(*controller.productionProject(), retakeTakeId));

  const auto retakeContext = controller.captureSampleReviewContext();
  CHECK(retakeContext);
  const auto retakeEvidence = root / "source-quality-retake.txt";
  CHECK(core::durableAtomicWriteTextNew(retakeEvidence,
      "SYNTHETIC TEST EVIDENCE: reassessment of the retaken material"));
  CHECK(controller.beginSourceQualityEvidenceCapture(retakeContext.value(), retakeEvidence));
  CHECK(drain(controller));
  CHECK(controller.sourceQualityInspection());
  if (!controller.sourceQualityInspection() || controller.sourceQualityInspection()->reviewers.empty())
    return;
  const auto retakeQuality = *controller.sourceQualityInspection();
  CHECK(controller.beginSourceQualityDecision(retakeQuality, "assessment-2", retakeQuality.reviewers.front(),
      production::Feasibility::Pass, production::Feasibility::Pass, "2026-09-14T12:03:30Z"));
  CHECK(drain(controller));
  CHECK(controller.sourceQualityReceipt());
  CHECK(production::requireTakeSourceQualification(*controller.productionProject(), retakeTakeId));

  // A new draft from the retaken material, an explicit new review that accepts it, then publication.
  const production::SampleManifestDraftIdentity retakeIdentity{.id = "original.singer.retake",
      .version = "1.0.0", .displayName = "Retaken Candidate",
      .language = domain::Language::Japanese, .style = "neutral"};
  const auto retakeDraftContext = controller.captureSampleReviewContext();
  CHECK(retakeDraftContext);
  CHECK(controller.beginSampleManifestDraftCreation(retakeDraftContext.value(), retakeIdentity,
      root / "draft-retake"));
  CHECK(drain(controller));
  const auto retakeDraft = controller.createdSampleManifestDraft();
  CHECK(retakeDraft);
  CHECK(retakeDraft && retakeDraft->missingAssignments.empty());
  CHECK(!controller.manifest().units.empty());
  if (retakeDraft && !controller.manifest().units.empty()) {
    const auto draftAudio = core::sha256File(retakeDraft->root / controller.manifest().units.front().audioPath);
    CHECK(draftAudio);
    CHECK(!draftAudio || draftAudio.value() == retakeSha);
  }
  CHECK(controller.beginSelectedSampleReview());
  CHECK(drain(controller));
  if (!controller.sampleReviewInspection()) return;
  CHECK(controller.selectSampleReviewer(controller.sampleReviewInspection()->context, "reviewer"));
  CHECK(controller.beginSampleReviewDecision(controller.sampleReviewInspection()->context, "reviewer",
      production::SampleCandidateReviewDecision::Accept, "2026-09-14T12:04:00Z"));
  CHECK(drain(controller));
  const auto receipt = controller.sampleReviewReceipt();
  CHECK(receipt);
  CHECK(receipt && receipt->durabilityConfirmed);
  if (receipt && !receipt->candidate)
    throw test::Failure{"the retake review produced no candidate: diagnostic=[" +
        receipt->diagnostic + "] status=[" + controller.sampleReviewStatus() + "]"};
  CHECK(receipt && receipt->candidate);

  const auto approved = controller.captureSampleReviewContext();
  CHECK(approved);
  CHECK(controller.beginSampleCandidatePublication(approved.value(), root / "candidate"));
  CHECK(drain(controller));
  const auto published = controller.publishedSampleCandidate();
  CHECK(published);
  CHECK(published && published->durabilityConfirmed);
  CHECK(published && !published->releaseEligible);
  if (!published) return;
  CHECK(std::filesystem::exists(published->root / "manifest.json"));

  // Package the candidate as a signed distribution artifact and install it through the product.
  auto key = distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = root / "original-singer.seambank";
  CHECK(distribution::packSeambank(published->root, package, key.value()));
  CHECK(std::filesystem::exists(package));

  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache", .voicebankRoots = {}, .sampleRate = 48000U,
      .outputChannels = 2U, .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = true});
  CHECK(session);
  auto dialog = std::make_unique<InstallDialog>();
  auto* dialogPtr = dialog.get();
  dialogPtr->responses = {package};
  auto application = standalone::StandaloneApplicationController::create(*session.value(),
      std::move(dialog), std::make_unique<DiscardPrompt>(),
      standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
          .voicebankInstallRoot = root / "voicebanks",
          .trustedVoicebankKeys = {key.value().publicKey}, .developmentTrustRoot = std::nullopt,
          .allowDevelopmentVoicebanks = false,
          .defaultNewProject = {.name = "Generated Singer Song", .tempoBpm = 120.0,
                                .sampleRate = 48000U, .outputChannels = 2U,
                                .initialVoicebank = std::nullopt},
          .stateChanged = {}});
  CHECK(application);
  CHECK(application.value()->dispatch(platform::ApplicationCommand::InstallVoicebank));
  CHECK(application.value()->voicebankCards().size() == 1U);
  if (application.value()->voicebankCards().empty()) return;
  const auto& card = application.value()->voicebankCards().front();
  CHECK(card.installed);
  CHECK(card.trust == voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(card.contentHash == published->contentSha256);
  CHECK(application.value()->selectVoicebank(card.id, card.version, card.contentHash));

  // The installed bank carries the campaign's own audio, so the song below sings generated material
  // rather than a hand-built fixture that merely shares a path.
  std::error_code scanError;
  std::optional<std::filesystem::path> installedRoot;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(root / "voicebanks", scanError)) {
    if (scanError) break;
    if (entry.is_regular_file() && entry.path().filename() == "manifest.json")
      installedRoot = entry.path().parent_path();
  }
  CHECK(installedRoot.has_value());
  if (!installedRoot) return;
  const auto installedManifest = voicebank::ManifestJsonCodec{}.load(*installedRoot / "manifest.json");
  CHECK(installedManifest);
  if (!installedManifest || installedManifest.value().units.empty()) return;
  const auto installedAudio = core::sha256File(*installedRoot / installedManifest.value().units.front().audioPath);
  CHECK(installedAudio);
  CHECK(!installedAudio || installedAudio.value() == retakeSha);

  auto [lyric, note] = session.value()->runtime().document().factory().makeNote(
      time::Tick{0}, time::Tick{1920}, 69U, U"\u3055", domain::Language::Japanese);
  CHECK(session.value()->runtime().execute(std::make_unique<application::AddNoteCommand>(
      session.value()->regionId(), std::move(lyric), std::move(note))));
  const auto coverage = application.value()->selectedRegionCoverage();
  CHECK(coverage);
  CHECK(coverage && coverage.value().complete());

  authoring::ExportSettings settings;
  settings.includeMaster = true;
  settings.includeStems = true;
  const auto exported = application.value()->exportSet(root / "export", settings);
  if (!exported) throw test::Failure{"generated singer export failed: " + exported.error().message};
  CHECK(std::filesystem::exists(exported.value().masterPath));
  CHECK(std::filesystem::file_size(exported.value().masterPath) > 44U);
  CHECK(exported.value().files.size() >= 2U);

  // The producer inputs are gone, and the song still saves, reopens and re-exports identically.
  std::filesystem::rename(workspace, root / "producer-unavailable", scanError);
  CHECK(!scanError);
  const auto songPath = root / "generated-singer.seam";
  dialogPtr->responses.push_back(songPath);
  CHECK(application.value()->dispatch(platform::ApplicationCommand::SaveProjectAs));
  CHECK(std::filesystem::exists(songPath));
  dialogPtr->responses.push_back(songPath);
  CHECK(application.value()->dispatch(platform::ApplicationCommand::OpenProject));
  const auto* reopened = session.value()->runtime().document().session().project()
                             .findVocalTrack(session.value()->trackId());
  CHECK(reopened != nullptr);
  if (reopened != nullptr) CHECK(reopened->voicebank.contentHash == published->contentSha256);
  const auto repeated = application.value()->exportSet(root / "export-again", settings);
  if (!repeated) throw test::Failure{"generated singer re-export failed: " + repeated.error().message};
  CHECK(repeated.value().masterSha256 == exported.value().masterSha256);
}
