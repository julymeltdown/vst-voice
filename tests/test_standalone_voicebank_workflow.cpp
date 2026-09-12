#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/distribution/seambank.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/formats/json_value.hpp"
#include <functional>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

class FakeDialog final : public seam::platform::IFileDialog {
public:
  seam::core::Result<std::optional<bool>> chooseRecipePackaging() override {
    if (onPackaging) onPackaging();
    return packagingResponse;
  }
  std::optional<bool> packagingResponse{false};
  std::function<void()> onPackaging;
  seam::core::Result<std::optional<std::string>> chooseRecipeStyle(const std::vector<std::string>& styles) override {
    offeredStyles = styles;
    if (onStyleChoose) onStyleChoose();
    return styleResponse;
  }
  std::vector<std::string> offeredStyles;
  std::optional<std::string> styleResponse;
  std::function<void()> onStyleChoose;
  seam::core::Result<std::optional<std::filesystem::path>> choose(
      const seam::platform::FileDialogRequest& request) override {
    requests.push_back(request);
    if (onChoose) onChoose();
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto result = responses.front();
    responses.erase(responses.begin());
    return result;
  }
  std::vector<seam::platform::FileDialogRequest> requests;
  std::vector<std::optional<std::filesystem::path>> responses;
  std::function<void()> onChoose;
};

class FakePrompt final : public seam::platform::IUnsavedChangesPrompt {
public:
  seam::core::Result<seam::platform::UnsavedDecision> choose(
      std::string_view) override {
    return seam::platform::UnsavedDecision::Discard;
  }
};

std::filesystem::path createPackage(
    const std::filesystem::path& root,
    const seam::distribution::SigningKeyPair& key) {
  const auto source = root / "source";
  std::filesystem::create_directories(source / "audio");
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 0.15);
  CHECK(seam::voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U,
                                       samples));
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
                                    seam::voicebank::UnitKind::Sustain,
                                    samples.size())});
  manifest.id = "standalone.installed.bank";
  manifest.version = "1.0.0";
  manifest.displayName = "Installed Bank";
  seam::voicebank::ManifestJsonCodec codec;
  CHECK(codec.save(manifest, source / "manifest.json"));
  std::ofstream(source / "license.txt") << "test fixture\n";
  const auto package = root / "installed.seambank";
  CHECK(seam::distribution::packSeambank(source, package, key));
  return package;
}

}  // namespace

TEST_CASE("native recipe picker selects relinks cancels and rejects stale document results") {
  const auto root = seam::test::support::temporaryDirectory("native-recipe-picker");
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "picker-draft";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "moved.json", recipe));
  auto different = recipe; different.seed = 123U;
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "different.json", different));
  auto session = seam::standalone::AuthoringSession::create(
      seam::standalone::AuthoringSessionConfig{.cacheRoot = root / "cache", .voicebankRoots = {},
          .sampleRate = 48000U, .outputChannels = 2U, .bindFirstAvailableVoicebank = false,
          .allowDevelopmentVoicebanks = false}); CHECK(session);
  auto dialog = std::make_unique<FakeDialog>(); auto* picker = dialog.get();
  auto controller = seam::standalone::StandaloneApplicationController::create(*session.value(),
      std::move(dialog), std::make_unique<FakePrompt>(),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"}); CHECK(controller);
  auto& runtime = session.value()->runtime();
  const auto trackId = runtime.selectedTrack(); CHECK(trackId.valid());
  const auto original = runtime.document().session().project();
  const auto beforeEmptyBake = picker->requests.size();
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::BakeProceduralCandidates));
  CHECK(picker->requests.size() == beforeEmptyBake);
  picker->responses = {std::nullopt};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  CHECK(runtime.document().session().project() == original);
  picker->responses = {root / "singer.json"};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  const auto selected = runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe; CHECK(selected);
  CHECK(selected->path == (root / "singer.json").string());
  CHECK(picker->requests.back().purpose == seam::platform::FileDialogPurpose::SelectProceduralRecipe);
  picker->responses = {root / "different.json"};
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::RelinkProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  picker->responses = {root / "moved.json"};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::RelinkProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe->resource == selected->resource);
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe->path == (root / "moved.json").string());
  CHECK(runtime.undo()); CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  picker->onChoose = [&] {
    const auto copy = runtime.document().session().project();
    CHECK(runtime.document().replaceProject(copy));
  };
  picker->responses = {root / "different.json"};
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  picker->onChoose = {};
  auto multiple = recipe;
  auto soft = recipe.poses.front(); soft.style = "soft"; soft.formants[0].frequencyHz = 800.0;
  multiple.poses.push_back(soft);
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "multiple.json", multiple));
  picker->responses = {root / "multiple.json"}; picker->styleResponse.reset();
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  CHECK(picker->offeredStyles == std::vector<std::string>({"neutral", "soft"}));
  picker->responses = {root / "multiple.json"}; picker->styleResponse = "absent";
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  picker->responses = {root / "multiple.json"}; picker->styleResponse = "soft";
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe->style == "soft");
  CHECK(runtime.undo()); CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  picker->onStyleChoose = [&] {
    const auto copy = runtime.document().session().project();
    CHECK(runtime.document().replaceProject(copy));
  };
  picker->responses = {root / "multiple.json"};
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::SelectProceduralRecipe));
  CHECK(runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe == selected);
  picker->onStyleChoose = {};
  auto [lyric, note] = runtime.document().factory().makeNote(seam::time::Tick{0}, seam::time::Tick{960},
      69U, U"あ", seam::domain::Language::Japanese);
  CHECK(runtime.execute(std::make_unique<seam::application::AddNoteCommand>(runtime.selectedRegion(),
      std::move(lyric), std::move(note))));
  picker->responses = {root / "cancelled-export"}; picker->packagingResponse.reset();
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::ExportSet));
  CHECK(!std::filesystem::exists(root / "cancelled-export")); CHECK(!controller.value()->exportInProgress());
  picker->packagingResponse = true;
  picker->onPackaging = [&] {
    const auto copy = runtime.document().session().project();
    CHECK(runtime.document().replaceProject(copy));
  };
  picker->responses = {root / "stale-export"};
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::ExportSet));
  CHECK(!std::filesystem::exists(root / "stale-export"));
  picker->onPackaging = {};
  const auto beforeExport = runtime.document().session().project();
  picker->responses = {root / "recipe-export"};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::ExportSet));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (controller.value()->exportInProgress() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  CHECK(!controller.value()->exportInProgress());
  const auto exported = controller.value()->lastExport(); CHECK(exported);
  CHECK(exported->state == seam::authoring::ExportState::Committed);
  CHECK(std::filesystem::exists(root / "recipe-export/project.seam"));
  CHECK(runtime.document().session().project() == beforeExport);
  picker->packagingResponse = false; picker->responses = {root / "audio-only-export"};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::ExportSet));
  const auto audioDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (controller.value()->exportInProgress() && std::chrono::steady_clock::now() < audioDeadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  CHECK(!controller.value()->exportInProgress());
  CHECK(controller.value()->lastExport());
  CHECK(controller.value()->lastExport()->state == seam::authoring::ExportState::Committed);
  CHECK(!std::filesystem::exists(root / "audio-only-export/project.seam"));
  CHECK(!std::filesystem::exists(root / "audio-only-export/recipes"));
  picker->responses = {std::nullopt};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::BakeProceduralCandidates));
  CHECK(!controller.value()->exportInProgress());
  CHECK(picker->requests.back().purpose == seam::platform::FileDialogPurpose::BakeProceduralCandidates);
  picker->onChoose = [&] {
    const auto copy = runtime.document().session().project();
    CHECK(runtime.document().replaceProject(copy));
  };
  picker->responses = {root / "stale-bake"};
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::BakeProceduralCandidates));
  CHECK(!std::filesystem::exists(root / "stale-bake"));
  picker->onChoose = {};
  picker->responses = {root / "native-bake"};
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::BakeProceduralCandidates));
  const auto bakeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (controller.value()->exportInProgress() && std::chrono::steady_clock::now() < bakeDeadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  CHECK(!controller.value()->exportInProgress()); CHECK(controller.value()->lastExport());
  CHECK(controller.value()->lastExport()->state == seam::authoring::ExportState::Committed);
  CHECK(!std::filesystem::exists(root / "native-bake/master.wav"));
  CHECK(std::filesystem::exists(root / "native-bake/project.seam"));
  const auto candidatePath = root / "native-bake/candidates" /
      (trackId.toString() + "-" + runtime.selectedRegion().toString());
  CHECK(std::filesystem::exists(candidatePath.string() + ".wav"));
  const auto candidateText = seam::core::readTextFileLimited(candidatePath.string() + ".json", 4U * 1024U * 1024U); CHECK(candidateText);
  const auto candidateJson = seam::formats::parseJson(candidateText.value()); CHECK(candidateJson);
  CHECK(candidateJson.value().find("approval")->asString() == "unapproved");
  CHECK(runtime.document().session().project() == beforeExport);
}

TEST_CASE("standalone_voicebank_workflow_installs_browses_selects_and_reports_coverage") {
  const auto root = seam::test::support::temporaryDirectory("u3-standalone");
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = createPackage(root, key.value());

  auto session = seam::standalone::AuthoringSession::create(
      seam::standalone::AuthoringSessionConfig{
          .cacheRoot = root / "cache",
          .voicebankRoots = {},
          .sampleRate = 48000U,
          .outputChannels = 2U,
          .bindFirstAvailableVoicebank = false,
          .allowDevelopmentVoicebanks = true,
      });
  CHECK(session);
  auto dialog = std::make_unique<FakeDialog>();
  auto* dialogPtr = dialog.get();
  dialogPtr->responses = {package};
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session.value(), std::move(dialog), std::make_unique<FakePrompt>(),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .voicebankInstallRoot = root / "voicebanks",
          .trustedVoicebankKeys = {key.value().publicKey},
          .developmentTrustRoot = std::nullopt,
          .allowDevelopmentVoicebanks = false,
          .defaultNewProject = {
              .name = "Voicebank Workflow",
              .tempoBpm = 120.0,
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::InstallVoicebank));
  CHECK(dialogPtr->requests.size() == 1U);
  CHECK(dialogPtr->requests.front().purpose ==
        seam::platform::FileDialogPurpose::InstallVoicebank);
  CHECK(controller.value()->voicebankCards().size() == 1U);
  const auto& card = controller.value()->voicebankCards().front();
  CHECK(card.installed);
  CHECK(card.selectable);
  CHECK(card.trust == seam::voicebank::VoicebankTrust::TrustedInstalled);

  CHECK(controller.value()->selectVoicebank(card.id, card.version,
                                             card.contentHash));
  const auto* track = session.value()->runtime().document().session().project()
                          .findVocalTrack(session.value()->trackId());
  CHECK(track != nullptr);
  CHECK(track->voicebank.id == card.id);
  CHECK(track->voicebank.version == card.version);
  CHECK(track->voicebank.contentHash == card.contentHash);
  const auto menu = controller.value()->voicebanks();
  CHECK(menu.size() == 1U);
  CHECK(menu.front().selected);

  auto [lyric, note] = session.value()->runtime().document().factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"か",
      seam::domain::Language::Japanese);
  CHECK(session.value()->runtime().execute(
      std::make_unique<seam::application::AddNoteCommand>(
          session.value()->regionId(), std::move(lyric), std::move(note))));
  const auto submittedBeforeRelink =
      session.value()->runtime().renderer().stats().submitted;

  dialogPtr->responses.push_back(root / "voicebanks");
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::RelinkVoicebank));
  CHECK(dialogPtr->requests.size() == 2U);
  CHECK(dialogPtr->requests.back().purpose ==
        seam::platform::FileDialogPurpose::RelinkVoicebank);
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  CHECK(session.value()->runtime().renderer().stats().submitted >
        submittedBeforeRelink);
  const auto coverage = controller.value()->selectedRegionCoverage();
  CHECK(coverage);
  CHECK(!coverage.value().complete());
  CHECK(coverage.value().summary.missingUnitCount >= 1U);
  auto* reviewedRegion = session.value()->runtime().document().session().project().findRegion(session.value()->regionId());
  CHECK(reviewedRegion != nullptr);
  reviewedRegion->lyrics.front().surface = std::u32string(4097U, U'あ');
  const auto oversized = controller.value()->selectedRegionCoverage();
  CHECK(!oversized);
  CHECK(oversized.error().message.find("bounds") != std::string::npos);
}
