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

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

namespace {

class FakeDialog final : public seam::platform::IFileDialog {
public:
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
  const auto coverage = controller.value()->selectedRegionCoverage();
  CHECK(coverage);
  CHECK(!coverage.value().complete());
  CHECK(coverage.value().summary.missingUnitCount >= 1U);
}
