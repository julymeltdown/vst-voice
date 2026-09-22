#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/core/sha256.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/authoring/audio_measurement_capture.hpp"
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
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/language_resolver.hpp"

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

// Learns the phone symbols the engine will actually request for one lyric, so the
// fixture bank can cover them instead of guessing unit names.
std::vector<std::string> symbolsFor(std::u32string lyric) {
  seam::application::ProjectFactory factory{9600U};
  auto project = factory.createProject("Symbol probe");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", seam::time::Tick{0},
                                        seam::time::Tick{1920});
  auto [lyricToken, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{1920}, 69U,
                                             std::move(lyric), seam::domain::Language::Japanese);
  auto* target = project.findRegion(region);
  target->lyrics.push_back(std::move(lyricToken));
  target->notes.push_back(std::move(note));
  target->sortNotes();
  const auto pronunciation = seam::phonemizer::resolvePronunciation(*target);
  if (!pronunciation) throw seam::test::Failure{"phonemizer probe failed: " + pronunciation.error().message};
  std::vector<std::string> symbols;
  for (const auto& token : pronunciation.value().pronunciation.tokens) {
    if (std::find(symbols.begin(), symbols.end(), token.symbol) == symbols.end()) {
      symbols.push_back(token.symbol);
    }
  }
  return symbols;
}

// A multi-unit installed bank: one sustain unit per phone the phrase needs.
std::filesystem::path createMultiUnitPackage(const std::filesystem::path& root,
    const seam::distribution::SigningKeyPair& key, const std::vector<std::string>& symbols) {
  const auto source = root / "source";
  std::filesystem::create_directories(source / "audio");
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 0.15);
  CHECK(seam::voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U, samples));
  std::vector<seam::voicebank::Unit> units;
  for (const auto& symbol : symbols) {
    units.push_back(seam::test::support::makeUnit(symbol, {symbol}, "audio/a.wav", 60,
                                                  seam::voicebank::UnitKind::Sustain,
                                                  samples.size()));
  }
  auto manifest = seam::test::support::makeManifest(std::move(units));
  manifest.id = "standalone.installed.multi";
  manifest.version = "2.0.0";
  manifest.displayName = "Installed Multi Bank";
  seam::voicebank::ManifestJsonCodec codec;
  CHECK(codec.save(manifest, source / "manifest.json"));
  std::ofstream(source / "license.txt") << "test fixture\n";
  const auto package = root / "installed-multi.seambank";
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
  const auto relinkDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (session.value()->runtime().renderer().stats().submitted <=
             submittedBeforeRelink &&
         std::chrono::steady_clock::now() < relinkDeadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
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

TEST_CASE("explicit bank refresh revokes current contextual audio and measurement after a losing source changes") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("contextual-refresh");
  const auto bankRoot = root / "bank";
  std::filesystem::create_directories(bankRoot / "audio");
  auto samples = test::support::sineWave(48000U, 440.0, 0.15);
  auto first = test::support::makeUnit("a-main", {"a"}, "audio/main.wav", 69,
      voicebank::UnitKind::Sustain, samples.size());
  auto losing = first; losing.id = "a-losing"; losing.audioPath = "audio/losing.wav"; losing.take = 2;
  CHECK(voicebank::writeMonoPcm16Wav(bankRoot / first.audioPath, 48000U, samples));
  CHECK(voicebank::writeMonoPcm16Wav(bankRoot / losing.audioPath, 48000U, samples));
  const auto bank = test::support::makeManifest({first, losing});
  CHECK(voicebank::ManifestJsonCodec{}.save(bank, bankRoot / "manifest.json"));
  const auto key = distribution::generateSigningKeyPair(); CHECK(key);
  const auto otherPackage = createPackage(root / "another-bank", key.value());
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache",
      .voicebankRoots = {{.path = bankRoot, .kind = voicebank::VoicebankRootKind::Development}},
      .sampleRate = 48000U, .outputChannels = 2U, .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = true});
  CHECK(session);
  auto controller = standalone::StandaloneApplicationController::create(*session.value(),
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(),
      standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
          .voicebankInstallRoot = root / "installed", .trustedVoicebankKeys = {key.value().publicKey},
          .allowDevelopmentVoicebanks = true});
  CHECK(controller); CHECK(controller.value()->voicebankCards().size() == 1U);
  const auto card = controller.value()->voicebankCards().front();
  CHECK(controller.value()->selectVoicebank(card.id, card.version, card.contentHash));
  auto& runtime = session.value()->runtime();
  auto [lyric, note] = runtime.document().factory().makeNote(time::Tick{0}, time::Tick{480}, 69U,
      U"あ", domain::Language::Japanese);
  CHECK(runtime.execute(std::make_unique<application::AddNoteCommand>(session.value()->regionId(), lyric, note)));
  runtime.setRenderQuality(rendering::RenderQuality::Final);
  runtime.requestPreview(true);
  const auto waitCurrent = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
      const auto publication = runtime.renderer().acquireCurrent();
      if (publication && publication->quality == rendering::RenderQuality::Final &&
          publication->projectRevision == runtime.document().session().revision()) return;
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    CHECK(false);
  };
  waitCurrent();
  runtime.requestPreview(false);
  runtime.invalidatePreview();
  CHECK(!runtime.renderer().acquireCurrent());
  const auto submittedAfterInvalidation = runtime.renderer().stats().submitted;
  const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{150};
  while (std::chrono::steady_clock::now() < drainDeadline) {
    CHECK(runtime.renderer().stats().submitted == submittedAfterInvalidation);
    CHECK(!runtime.renderer().acquireCurrent());
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  runtime.requestPreview(true);
  waitCurrent();
  const auto current = runtime.renderer().acquireCurrent(); CHECK(current);
  CHECK(current->quality == rendering::RenderQuality::Final);
  CHECK(current->result.activeUnitPlan.size() == 1U);
  CHECK(current->result.activeUnitPlan.front().unitId == "a-main");
  const auto capture = authoring::AudioMeasurementCapture::prepare(runtime.document().session(), runtime.renderer());
  CHECK(capture); CHECK(capture.value().matches(runtime.document().session(), runtime.renderer()));
  const auto revision = runtime.document().session().revision();
  const auto requestId = current->requestId;
  const auto pcm = current->result.interleaved;
  for (auto& sample : samples) sample *= 0.6F;
  CHECK(voicebank::writeMonoPcm16Wav(bankRoot / losing.audioPath, 48000U, samples));
  // Public installation performs the explicit private browser/catalog refresh.
  // No document edit or track reselection should be needed to revoke old evidence.
  CHECK(controller.value()->installVoicebank(otherPackage));
  CHECK(runtime.document().session().revision() == revision);
  CHECK(!runtime.voicebanks().resolveTrack(runtime.document().session().project(), session.value()->trackId()).resolved());
  const auto after = runtime.renderer().acquireCurrent();
  CHECK(!after || after->requestId != requestId);
  CHECK(!capture.value().matches(runtime.document().session(), runtime.renderer()));
  CHECK(!authoring::AudioMeasurementCapture::prepare(runtime.document().session(), runtime.renderer()));
  CHECK(capture.value().source().result.interleaved == pcm); // Historical frozen PCM remains usable as history.
}

// M1.P3 item 6: the installed-song regression must go past its one-unit fixture.
// A bank covering the phrase's actual phones is installed, the producer inputs are
// then removed, and the new song must still export, save and reopen with the same
// resource binding and the same audio.
TEST_CASE("installed multi-unit bank renders saves and reopens a new song without producer inputs") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("u3-installed-song");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key);
  auto symbols = symbolsFor(U"か");
  for (const auto& symbol : symbolsFor(U"さ")) {
    if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) symbols.push_back(symbol);
  }
  CHECK(symbols.size() >= 3U);
  const auto package = createMultiUnitPackage(root, key.value(), symbols);
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
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
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(dialog), std::make_unique<FakePrompt>(),
      standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .voicebankInstallRoot = root / "voicebanks",
          .trustedVoicebankKeys = {key.value().publicKey},
          .developmentTrustRoot = std::nullopt,
          .allowDevelopmentVoicebanks = false,
          .defaultNewProject = {
              .name = "Installed Song",
              .tempoBpm = 120.0,
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::InstallVoicebank));
  CHECK(controller.value()->voicebankCards().size() == 1U);
  const auto& card = controller.value()->voicebankCards().front();
  CHECK(card.installed);
  CHECK(card.trust == voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(controller.value()->selectVoicebank(card.id, card.version, card.contentHash));

  // An unfamiliar two-syllable phrase drawn only from the installed bank.
  const std::array<std::u32string, 2U> lyrics{U"か", U"さ"};
  for (std::size_t index = 0U; index < lyrics.size(); ++index) {
    auto [lyric, note] = session.value()->runtime().document().factory().makeNote(
        time::Tick{960 * static_cast<std::int64_t>(index)}, time::Tick{960}, 60U + static_cast<std::uint8_t>(index * 2U),
        lyrics[index], domain::Language::Japanese);
    CHECK(session.value()->runtime().execute(
        std::make_unique<application::AddNoteCommand>(session.value()->regionId(), std::move(lyric), std::move(note))));
  }
  const auto coverage = controller.value()->selectedRegionCoverage();
  CHECK(coverage);
  CHECK(coverage.value().complete());

  // The new song must not depend on the producer's generation inputs.
  std::error_code error;
  std::filesystem::remove_all(root / "source", error);
  CHECK(!error);
  CHECK(!std::filesystem::exists(root / "source"));

  authoring::ExportSettings settings;
  settings.includeMaster = true;
  settings.includeStems = true;
  const auto exported = controller.value()->exportSet(root / "export", settings);
  if (!exported) throw test::Failure{"installed-bank export failed: " + exported.error().message};
  CHECK(std::filesystem::exists(exported.value().masterPath));
  CHECK(std::filesystem::file_size(exported.value().masterPath) > 44U);
  CHECK(!exported.value().masterSha256.empty());
  CHECK(exported.value().files.size() >= 2U);
  for (const auto& file : exported.value().files) CHECK(std::filesystem::exists(file.path));

  // Save and reopen through the same controller, then re-export: the binding and
  // the audio must both survive the round trip.
  const auto saved = root / "installed-song.seam";
  dialogPtr->responses.push_back(saved);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::SaveProjectAs));
  CHECK(std::filesystem::exists(saved));
  dialogPtr->responses.push_back(saved);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenProject));
  const auto* reopened = session.value()->runtime().document().session().project()
                             .findVocalTrack(session.value()->trackId());
  CHECK(reopened != nullptr);
  CHECK(reopened->voicebank.id == card.id);
  CHECK(reopened->voicebank.version == card.version);
  CHECK(reopened->voicebank.contentHash == card.contentHash);
  const auto reopenedCoverage = controller.value()->selectedRegionCoverage();
  CHECK(reopenedCoverage);
  CHECK(reopenedCoverage.value().complete());
  const auto repeated = controller.value()->exportSet(root / "export-again", settings);
  if (!repeated) throw test::Failure{"installed-bank re-export failed: " + repeated.error().message};
  CHECK(repeated.value().masterSha256 == exported.value().masterSha256);
}

TEST_CASE("the application selects an installed procedural singer by identity, not by path") {
  const auto root = seam::test::support::temporaryDirectory("installed-procedural-picker");
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "installed-picker";
  recipe.seed = 77U;
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto source = root / "source";
  std::filesystem::create_directories(source);
  auto encoded = seam::voice_design::encodeVoiceRecipe(recipe);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  std::ofstream(source / "recipe.json", std::ios::binary | std::ios::trunc) << encoded.value();
  seam::distribution::ProceduralSingerManifest manifest;
  manifest.id = "seam.installed.pilot";
  manifest.version = "1.0.0";
  manifest.displayName = "Installed Pilot";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = 13U;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = seam::core::sha256Hex(encoded.value());
  manifest.phones = {"a"};
  seam::distribution::ProceduralSingerManifestJsonCodec manifestCodec;
  auto manifestText = manifestCodec.encode(manifest);
  CHECK(manifestText.hasValue());
  if (!manifestText) return;
  std::ofstream(source / "manifest.json", std::ios::binary | std::ios::trunc) << manifestText.value();
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(seam::distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());
  const auto installRoot = root / "singers";
  seam::distribution::InstallProceduralOptions installOptions;
  installOptions.verification = seam::distribution::VerifySeambankOptions{
      .limits = {}, .trustedPublicKeys = {key.value().publicKey}, .requireTrustedSigner = true};
  auto installed = seam::distribution::installProceduralPackage(packagePath, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;

  auto session = seam::standalone::AuthoringSession::create(
      seam::standalone::AuthoringSessionConfig{.cacheRoot = root / "cache", .voicebankRoots = {},
          .sampleRate = 48000U, .outputChannels = 2U, .bindFirstAvailableVoicebank = false,
          .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  seam::standalone::StandaloneApplicationControllerConfig config{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  config.proceduralSingerRoots = {seam::distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = seam::distribution::ProceduralRootKind::Installed}};
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session.value(), std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config);
  CHECK(controller.hasValue());
  if (!controller) return;
  auto& runtime = session.value()->runtime();
  const auto trackId = runtime.selectedTrack();
  CHECK(trackId.valid());
  const auto original = runtime.document().session().project();

  // Without a declared renderable engine the application refuses rather than guessing.
  auto listed = controller.value()->installedProceduralSingers();
  CHECK(!listed.hasValue());
  CHECK(!controller.value()->dispatch(
      seam::platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(runtime.document().session().project() == original);

  // A singer built for another engine is present on disk but is not offered as a choice.
  config.renderableProceduralEngineId = "seam.source-filter.other";
  auto foreign = seam::standalone::StandaloneApplicationController::create(
      *session.value(), std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config);
  CHECK(foreign.hasValue());
  if (!foreign) return;
  CHECK(!foreign.value()->dispatch(
      seam::platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(runtime.document().session().project() == original);

  // Cancelling the chooser leaves the track untouched.
  config.renderableProceduralEngineId = manifest.engineId;
  config.renderableProceduralEngineRevision = manifest.engineRevision;
  auto cancellingDialog = std::make_unique<FakeDialog>();
  auto* cancellingPicker = cancellingDialog.get();
  cancellingPicker->styleResponse = std::nullopt;
  auto cancelling = seam::standalone::StandaloneApplicationController::create(
      *session.value(), std::move(cancellingDialog), std::make_unique<FakePrompt>(), config);
  CHECK(cancelling.hasValue());
  if (!cancelling) return;
  CHECK(cancelling.value()->dispatch(
      seam::platform::ApplicationCommand::SelectInstalledProceduralSinger));
  CHECK(cancellingPicker->offeredStyles.size() == 1U);
  CHECK(cancellingPicker->offeredStyles.front().find("Installed Pilot") != std::string::npos);
  CHECK(runtime.document().session().project() == original);

  // Choosing the offered singer records the installed identity and its installed recipe path.
  auto choosingDialog = std::make_unique<FakeDialog>();
  auto* choosingPicker = choosingDialog.get();
  choosingPicker->styleResponse = cancellingPicker->offeredStyles.front();
  auto choosing = seam::standalone::StandaloneApplicationController::create(
      *session.value(), std::move(choosingDialog), std::make_unique<FakePrompt>(), config);
  CHECK(choosing.hasValue());
  if (!choosing) return;
  CHECK(choosing.value()->dispatch(
      seam::platform::ApplicationCommand::SelectInstalledProceduralSinger));
  const auto chosen = runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe;
  CHECK(chosen.has_value());
  if (!chosen) return;
  // The recorded identity is the one the renderer validates, derived from the recipe rather than
  // from the manifest's release version. Removing the diagnostic probe is part of the same slice.
  CHECK(chosen->resource.id == recipe.id);
  CHECK(chosen->resource.version == "1");
  CHECK(chosen->resource.contentHash == seam::core::sha256Hex(encoded.value()));
  CHECK(chosen->style == "neutral");
  CHECK(chosen->path == (installed.value().installDirectory / "recipe.json").string());
  CHECK(runtime.undo());
  CHECK(!runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe.has_value());
}
