#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/native_editor_app.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

namespace {
using namespace seam;
constexpr auto kWarning = "INSTALL_DURABILITY_UNCONFIRMED";

class InstallDialog final : public platform::IFileDialog {
public:
  explicit InstallDialog(std::shared_ptr<std::filesystem::path> selected) : selected_{std::move(selected)} {}
  core::Result<std::optional<std::filesystem::path>> choose(const platform::FileDialogRequest&) override {
    return std::optional{*selected_};
  }
private:
  std::shared_ptr<std::filesystem::path> selected_;
};

struct Packages final {
  std::filesystem::path root{test::support::temporaryDirectory("installation-notices")};
  distribution::SigningKeyPair key;
  std::filesystem::path sample, procedural;
  std::string sampleDigest, proceduralDigest, proceduralId;
  Packages() {
    const auto generated = distribution::generateSigningKeyPair(); CHECK(generated); key = generated.value();
    const auto source = root / "sample-source";
    std::filesystem::create_directories(source / "audio");
    const auto samples = test::support::sineWave(48000U, 220.0, 0.1);
    CHECK(voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U, samples));
    auto manifest = test::support::makeManifest({test::support::makeUnit(
        "a", {"a"}, "audio/a.wav", 60, voicebank::UnitKind::Sustain, samples.size())});
    manifest.id = "notice.bank"; manifest.version = "1.0.0";
    CHECK(voicebank::ManifestJsonCodec{}.save(manifest, source / "manifest.json"));
    CHECK(core::durableAtomicWriteTextNew(source / "license.txt", "Synthetic engineering fixture, not production approval."));
    sample = root / "sample.seambank";
    const auto packed = distribution::packSeambank(source, sample, key); CHECK(packed);
    sampleDigest = packed.value().packageDigest;
    const auto recipe = voice_design::loadVoiceRecipeResource(
        std::filesystem::path{SEAM_TEST_SOURCE_DIR} / "assets/pilots/seam-song-01/recipe.json"); CHECK(recipe);
    procedural = root / "singer.seamsinger";
    distribution::PublishProceduralSingerOptions options;
    options.version = "1.0.0"; options.language = "ja";
    const auto singer = distribution::publishProceduralSingerFromRecipe(recipe.value(), root / "singer-source",
        procedural, key, options); CHECK(singer);
    proceduralDigest = singer.value().container.packageDigest; proceduralId = singer.value().manifest.id;
  }
};

std::vector<authoring::Diagnostic> warnings(const standalone::NativeEditorApp& app) {
  std::vector<authoring::Diagnostic> result;
  for (const auto& entry : app.authoring().controller().diagnosticPanel().entries())
    if (entry.diagnostic.code == kWarning) result.push_back(entry.diagnostic);
  return result;
}
std::size_t warningIndex(const standalone::NativeEditorApp& app, std::string_view digest) {
  const auto& entries = app.authoring().controller().diagnosticPanel().entries();
  for (std::size_t i = 0; i < entries.size(); ++i)
    if (entries[i].diagnostic.code == kWarning &&
        std::find(entries[i].diagnostic.affectedIds.begin(), entries[i].diagnostic.affectedIds.end(), digest) !=
            entries[i].diagnostic.affectedIds.end()) return i;
  return entries.size();
}
void repaint(standalone::NativeEditorApp& app) {
  CHECK(native_ui::paint::vectorBackendAvailable());
  native_ui::PixelSurface surface{1280U, 720U};
  native_ui::RasterCanvas canvas{surface, 1.0};
  app.paint(canvas);
}
}  // namespace

TEST_CASE("Standalone menu retains committed installation warnings across frames and dismisses only that resource") {
  Packages files;
  auto selected = std::make_shared<std::filesystem::path>(files.sample);
  int faultMode = 0; // 0: precommit failure, 1: postcommit sync failure, 2: normal.
  standalone::NativeEditorAppConfig config;
  config.runtimeMode = standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = files.root / "app";
  config.trustedVoicebankKeys = {files.key.publicKey};
  config.forceThreadedAudio = true;
  config.fileDialogFactory = [selected] { return std::make_unique<InstallDialog>(selected); };
  config.installFaultInjector = [&](distribution::InstallStage stage) {
    if ((faultMode == 0 && stage == distribution::InstallStage::BeforeCommit) ||
        (faultMode == 1 && stage == distribution::InstallStage::AfterCommitBeforeSync))
      return core::failure(core::ErrorCode::IoError, "simulated storage sync failure");
    return core::success();
  };
  auto app = standalone::NativeEditorApp::create(std::move(config)); CHECK(app);
  auto& controller = app.value()->authoring().controller();
  const auto project = app.value()->authoring().runtime().document().session().project();
  CHECK(!app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallVoicebank));
  CHECK(warnings(*app.value()).empty());
  CHECK(!std::filesystem::exists(files.root / "app/Data/Voicebanks/notice.bank/1.0.0"));

  faultMode = 1;
  CHECK(app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallVoicebank));
  CHECK(std::filesystem::is_regular_file(files.root / "app/Data/Voicebanks/notice.bank/1.0.0/install-receipt.json"));
  auto shown = warnings(*app.value()); CHECK(shown.size() == 1U);
  CHECK(shown.front().severity == authoring::DiagnosticSeverity::Warning);
  CHECK(authoring::DiagnosticRegistry::validate(shown.front()));
  CHECK(shown.front().detail.find(files.sampleDigest) != std::string::npos);
  CHECK(shown.front().detail.find("simulated storage sync failure") != std::string::npos);
  CHECK((shown.front().actions == std::vector{authoring::DiagnosticAction::Dismiss, authoring::DiagnosticAction::CopyDiagnostic}));
  CHECK(native_ui::presentDiagnostic(shown.front()).title == "Installed; storage sync unconfirmed");
  repaint(*app.value()); repaint(*app.value());
  CHECK(warnings(*app.value()) == shown); // No occurrence inflation on repaint.

  *selected = files.procedural;
  CHECK(app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallProceduralSinger));
  CHECK(std::filesystem::is_regular_file(files.root / "app/Data/Singers" / files.proceduralId / "1.0.0/install-receipt.json"));
  CHECK(warnings(*app.value()).size() == 2U);
  repaint(*app.value());
  const auto beforeOther = app.value()->authoring().runtime().diagnostics();
  CHECK(controller.activateDiagnostic(warningIndex(*app.value(), files.sampleDigest), authoring::DiagnosticAction::Dismiss));
  repaint(*app.value());
  shown = warnings(*app.value()); CHECK(shown.size() == 1U);
  CHECK(shown.front().detail.find(files.proceduralDigest) != std::string::npos);
  CHECK(app.value()->authoring().runtime().diagnostics() == beforeOther);

  // Reusing the already-installed sample is not a new publication and cannot recreate its warning.
  *selected = files.sample;
  CHECK(app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallVoicebank));
  repaint(*app.value()); CHECK(warnings(*app.value()) == shown);
  CHECK(controller.activateDiagnostic(warningIndex(*app.value(), files.proceduralDigest), authoring::DiagnosticAction::Dismiss));
  repaint(*app.value()); CHECK(warnings(*app.value()).empty());
  CHECK(app.value()->authoring().runtime().document().session().project() == project);
}

TEST_CASE("Normally synced standalone installations and an existing bank produce no durability warning") {
  Packages files;
  auto selected = std::make_shared<std::filesystem::path>(files.sample);
  standalone::NativeEditorAppConfig config;
  config.runtimeMode = standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = files.root / "normal-app";
  config.trustedVoicebankKeys = {files.key.publicKey}; config.forceThreadedAudio = true;
  config.fileDialogFactory = [selected] { return std::make_unique<InstallDialog>(selected); };
  auto app = standalone::NativeEditorApp::create(std::move(config)); CHECK(app);
  CHECK(app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallVoicebank));
  CHECK(app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallVoicebank));
  *selected = files.procedural;
  CHECK(app.value()->dispatchApplicationCommand(platform::ApplicationCommand::InstallProceduralSinger));
  repaint(*app.value()); CHECK(warnings(*app.value()).empty());
}
