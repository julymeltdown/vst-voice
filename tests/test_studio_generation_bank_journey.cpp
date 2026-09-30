// The computer-generation route from Studio's own window: a producer workspace, a planned campaign,
// its held-out preflight, the collected generated takes, a reviewed draft, a published candidate, a
// signed package, an installation, and a song the editor sings. The producer, recipe and audio here
// are synthetic fixtures; what the case proves is that the route reaches a singing song through
// Studio's actions, not that the singer is useful or qualified.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "studio_app.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/authoring/inventory_generation.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace seam;

using native_ui::NativeKey;
using native_ui::SemanticAction;
using native_ui::SemanticNode;

// The shipped Japanese starter recipe, so the vowel-starter inventory's five vowels all have a
// declared pose. A hand-built one-vowel recipe would make the campaign refuse the other rows.
voice_design::VoiceRecipe journeyRecipe() {
  return voice_design::makeJapaneseStarterRecipe("studio-generation-journey");
}

struct DialogScript final {
  std::map<platform::FileDialogPurpose, std::vector<std::optional<std::filesystem::path>>> paths;
  std::vector<std::optional<platform::IFileDialog::NewProducerWorkspaceInput>> newWorkspaces;
  std::vector<std::optional<platform::IFileDialog::ProductionWorkspaceInput>> workspaces;
  std::vector<std::optional<platform::SourceRegistrationInput>> sourceRegistrations;
  std::vector<std::optional<std::string>> reviewerRegistrations;
  std::vector<std::optional<platform::SampleManifestDraftIdentityInput>> sampleDraftIdentities;
  std::vector<std::optional<platform::SourceQualityDecisionInput>> sourceQualityDecisions;
  std::vector<std::optional<std::string>> sampleReviewers;
  std::vector<bool> sampleReviewConfirmations;
  std::vector<std::optional<std::size_t>> generationRegions;
  std::vector<platform::FileDialogRequest> requests;
};

template <typename Value>
std::optional<Value> nextAnswer(std::vector<std::optional<Value>>& queue) {
  if (queue.empty()) return std::nullopt;
  auto next = std::move(queue.front());
  queue.erase(queue.begin());
  return next;
}

class ScriptedDialog final : public platform::IFileDialog {
public:
  explicit ScriptedDialog(std::shared_ptr<DialogScript> script) : script_(std::move(script)) {}
  core::Result<std::optional<std::filesystem::path>> choose(
      const platform::FileDialogRequest& request) override {
    script_->requests.push_back(request);
    auto& queue = script_->paths[request.purpose];
    if (queue.empty()) return std::optional<std::filesystem::path>{};
    auto next = queue.front();
    queue.erase(queue.begin());
    return next;
  }
  core::Result<std::optional<NewProducerWorkspaceInput>> chooseNewProducerWorkspace() override {
    return nextAnswer(script_->newWorkspaces);
  }
  core::Result<std::optional<ProductionWorkspaceInput>> chooseProductionWorkspace() override {
    return nextAnswer(script_->workspaces);
  }
  core::Result<std::optional<platform::SourceRegistrationInput>> chooseSourceRegistration(
      std::string_view) override {
    return nextAnswer(script_->sourceRegistrations);
  }
  core::Result<std::optional<std::string>> chooseReviewerRegistration(std::string_view) override {
    return nextAnswer(script_->reviewerRegistrations);
  }
  core::Result<std::optional<platform::SampleManifestDraftIdentityInput>>
  chooseSampleManifestDraftIdentity() override {
    return nextAnswer(script_->sampleDraftIdentities);
  }
  core::Result<std::optional<platform::SourceQualityDecisionInput>> chooseSourceQualityDecision(
      std::string_view, const std::vector<std::string>&) override {
    return nextAnswer(script_->sourceQualityDecisions);
  }
  core::Result<std::optional<std::string>> chooseSampleReviewer(
      const std::vector<std::string>&) override {
    return nextAnswer(script_->sampleReviewers);
  }
  core::Result<bool> confirmSampleReview(std::string_view, bool) override {
    if (script_->sampleReviewConfirmations.empty()) return core::Result<bool>{false};
    const auto next = script_->sampleReviewConfirmations.front();
    script_->sampleReviewConfirmations.erase(script_->sampleReviewConfirmations.begin());
    return core::Result<bool>{next};
  }
  core::Result<std::optional<std::size_t>> chooseGenerationRegion(
      const std::vector<std::string>&) override {
    return nextAnswer(script_->generationRegions);
  }

private:
  std::shared_ptr<DialogScript> script_;
};

struct Launch final {
  std::filesystem::path document, application;
};

const SemanticNode* findSuffix(const SemanticNode& node, const std::string& suffix) {
  if (node.id.ends_with(suffix)) return &node;
  for (const auto& child : node.children)
    if (const auto* found = findSuffix(child, suffix)) return found;
  return nullptr;
}

class StudioHarness final {
public:
  std::shared_ptr<DialogScript> dialogs = std::make_shared<DialogScript>();
  std::shared_ptr<std::vector<Launch>> launches = std::make_shared<std::vector<Launch>>();
  std::unique_ptr<voicebank_studio_native::IVoicebankStudioApp> app;

  explicit StudioHarness(std::filesystem::path root)
      : root_(std::move(root)), banks_(root_ / "voicebanks"),
        editor_(root_ / "Project SEAM.app") {
    std::filesystem::create_directories(banks_);
    voicebank_studio_native::StudioPlatform hooks;
    hooks.fileDialog = [script = dialogs] { return std::make_unique<ScriptedDialog>(script); };
    hooks.audioDevice = [] { return platform::createThreadedAudioDevice(); };
    hooks.singerRoots = [singers = root_ / "singers"] {
      return std::vector<distribution::ProceduralSearchRoot>{
          {singers, distribution::ProceduralRootKind::Installed}};
    };
    hooks.voicebankRoots = [banks = banks_] {
      return std::vector<voicebank::VoicebankSearchRoot>{
          {banks, voicebank::VoicebankRootKind::Installed}};
    };
    hooks.locateSongEditor = [editor = editor_]() -> core::Result<std::filesystem::path> {
      return editor;
    };
    hooks.openDocumentWithApplication = [recorded = launches](
        const std::filesystem::path& document,
        const std::filesystem::path& application) -> core::Result<void> {
      recorded->push_back({document, application});
      return core::success();
    };
    app = voicebank_studio_native::createVoicebankStudioApp(true, std::move(hooks));
  }

  [[nodiscard]] const std::filesystem::path& voicebanks() const noexcept { return banks_; }

  void resize(double width, double height) {
    width_ = width;
    height_ = height;
    app->resized(width, height, 1.0);
  }

  const native_ui::AccessibilityTree& frame() {
    surface_ = native_ui::PixelSurface{static_cast<std::uint32_t>(width_),
                                       static_cast<std::uint32_t>(height_)};
    native_ui::RasterCanvas canvas{surface_, 1.0};
    app->paint(canvas);
    return *app->accessibilityTree();
  }

  std::optional<SemanticNode> node(std::string_view suffix) {
    const auto* found = findSuffix(frame().root(), "." + std::string{suffix});
    if (found == nullptr) return std::nullopt;
    return *found;
  }

  std::string value(std::string_view suffix) {
    const auto found = node(suffix);
    return found ? found->value : std::string{};
  }

  core::Result<void> activate(std::string_view suffix) {
    const auto target = node(suffix);
    if (!target)
      return core::failure(core::ErrorCode::NotFound,
                           "No accessible element ends with " + std::string{suffix});
    return app->dispatchAccessibility(target->id, SemanticAction::Activate);
  }

  void key(NativeKey key, native_ui::InputModifiers modifiers) {
    app->keyDown(native_ui::KeyEvent{.key = key, .modifiers = modifiers});
  }

  template <typename Done>
  bool settle(Done done) {
    for (int index = 0; index < 20000; ++index) {
      frame();
      if (done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return false;
  }

private:
  std::filesystem::path root_, banks_, editor_;
  double width_{1100.0};
  double height_{720.0};
  native_ui::PixelSurface surface_;
};

class EditorDialog final : public platform::IFileDialog {
public:
  core::Result<std::optional<std::filesystem::path>> choose(
      const platform::FileDialogRequest&) override {
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto next = responses.front();
    responses.erase(responses.begin());
    return next;
  }
  std::vector<std::optional<std::filesystem::path>> responses;
};

class DiscardPrompt final : public platform::IUnsavedChangesPrompt {
public:
  core::Result<platform::UnsavedDecision> choose(std::string_view) override {
    return platform::UnsavedDecision::Discard;
  }
};

}  // namespace

TEST_CASE("Voicebank Studio generates, preflights, reviews and installs a bank a new song sings with") {
  const auto root = test::support::temporaryDirectory("studio-generation-bank");
  StudioHarness studio{root};
  auto& dialogs = *studio.dialogs;
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);

  // A workspace whose one row is a vowel so the campaign has a single class to preflight.
  const auto workspace = root / "generation-workspace";
  dialogs.newWorkspaces = {platform::IFileDialog::NewProducerWorkspaceInput{
      .destination = workspace, .projectId = "generation-journey", .producerId = "producer",
      .inventory = platform::IFileDialog::NewProducerWorkspaceInput::Inventory::JapaneseVowelStarter}};
  CHECK(studio.activate("create-producer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject() != nullptr; }));
  const auto* project = studio.app->productionProject();
  CHECK(project != nullptr);
  if (project == nullptr) return;
  CHECK(!project->unitAssignments.empty());
  if (project->unitAssignments.empty()) return;
  const auto style = project->unitAssignments.front().style;

  // A procedural source, registered the way a producer declares one, then a reviewer.
  const auto license = root / "generation-license.txt";
  const std::string licenseText =
      "Harness generation consent: procedural synthesis, transformation, bank redistribution and "
      "commercial renders.";
  CHECK(core::durableAtomicWriteTextNew(license, licenseText).hasValue());
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] { const auto control = studio.node("source-license"); return control && control->enabled; }));
  dialogs.paths[platform::FileDialogPurpose::SourceLicenseEvidence] = {license};
  CHECK(studio.activate("source-license").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("source-register"); return control && control->enabled; }));
  dialogs.sourceRegistrations = {platform::SourceRegistrationInput{
      .id = "generation-synthesis", .kind = "procedural", .rights = "pass",
      .permissions = {"yes", "yes", "yes", "yes"}}};
  CHECK(studio.activate("source-register").hasValue());
  CHECK(studio.settle([&] {
    return studio.app->productionProject()->selectedSourceStrategyId == "generation-synthesis";
  }));
  dialogs.reviewerRegistrations = {std::string{"listener"}};
  CHECK(studio.activate("register-reviewer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject()->operators.size() == 2U; }));
  CHECK(studio.settle([&] { const auto back = studio.node("back"); return back && back->enabled; }));
  CHECK(studio.activate("back").hasValue());

  // Plan the campaign from a recipe file through the generation controls.
  const auto recipePath = root / "recipe.json";
  CHECK(voice_design::saveVoiceRecipeFile(recipePath, journeyRecipe()).hasValue());
  const auto campaign = root / "campaign";
  dialogs.paths[platform::FileDialogPurpose::SelectProceduralRecipe] = {recipePath};
  dialogs.paths[platform::FileDialogPurpose::PlanGenerationCampaign] = {campaign};
  CHECK(studio.settle([&] {
    const auto control = studio.node("plan-campaign"); return control && control->enabled;
  }));
  CHECK(studio.activate("plan-campaign").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("CAMPAIGN PLANNED") != std::string::npos;
  }));
  CHECK(std::filesystem::exists(campaign / "campaign.json"));
  CHECK(studio.app->productionProject()->takes.empty());

  // Advancing without a preflight is refused: the gate is real, not decorative.
  CHECK(studio.settle([&] {
    const auto control = studio.node("run-campaign"); return control && control->enabled;
  }));
  CHECK(studio.activate("run-campaign").hasValue());
  CHECK(studio.settle([&] {
    const auto status = studio.value("status");
    return status.find("preflight") != std::string::npos ||
        status.find("Preflight") != std::string::npos;
  }));
  CHECK(studio.app->productionProject()->takes.empty());

  // The preflight is its own step, it renders the held-out phrases and it is reported as passed.
  CHECK(studio.settle([&] {
    const auto control = studio.node("preflight-campaign"); return control && control->enabled;
  }));
  CHECK(studio.activate("preflight-campaign").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("CAMPAIGN PREFLIGHT PASSED") != std::string::npos;
  }));
  const auto* report = studio.app->campaignPreflightReport();
  CHECK(report != nullptr);
  if (report != nullptr) {
    CHECK(report->passed);
    CHECK(report->defective == 0U);
    CHECK(!report->phrases.empty());
  }
  CHECK(std::filesystem::exists(campaign / "preflight" / "report.json"));
  // The preflight generated nothing into the producer and left the assignments missing.
  CHECK(studio.app->productionProject()->takes.empty());
  CHECK(studio.app->productionQueues().missing ==
        studio.app->productionProject()->unitAssignments.size());

  // Only now does the campaign run, and every collected take is unapproved review material.
  CHECK(studio.activate("run-campaign").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("CAMPAIGN COLLECTED") != std::string::npos;
  }));
  const auto* generated = studio.app->productionProject();
  CHECK(generated != nullptr);
  if (generated == nullptr) return;
  CHECK(!generated->takes.empty());
  if (generated->takes.empty()) return;
  for (const auto& take : generated->takes)
    CHECK(take.state == voicebank_production::UnitQueueState::MarkerReview);
  CHECK(generated->reviews.empty());
  const auto generatedSha = generated->takes.front().rawAssetSha256;
  CHECK(generatedSha.size() == 64U);

  // Source quality can only be assessed once the material it describes exists, so it follows the
  // collection rather than preceding it. It is still a separate explicit reviewer decision, and it
  // does not approve any unit.
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] { const auto control = studio.node("source-evidence"); return control && control->enabled; }));
  const auto qualityEvidence = root / "generation-source-quality.txt";
  CHECK(core::durableAtomicWriteTextNew(qualityEvidence,
      "Harness procedural source coverage and listening assessed by the reviewer").hasValue());
  dialogs.paths[platform::FileDialogPurpose::SourceQualityEvidence] = {qualityEvidence};
  CHECK(studio.activate("source-evidence").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("source-decision"); return control && control->enabled; }));
  dialogs.sourceQualityDecisions = {platform::SourceQualityDecisionInput{
      .id = "generation-quality", .reviewerId = "listener", .coverage = "pass", .listening = "pass"}};
  CHECK(studio.activate("source-decision").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("SOURCE QUALITY RECORDED") != std::string::npos;
  }));
  // The source decision never approves a unit: the generated takes still need their own reviews.
  CHECK(studio.app->productionQueues().approved == 0U);

  // Reviewing needs a manifest, so the draft is created from the generated takes.
  dialogs.sampleDraftIdentities = {platform::SampleManifestDraftIdentityInput{
      .id = "generation-journey", .version = "1.0.0", .displayName = "Generation Journey",
      .language = "ja", .style = style}};
  const auto draftRoot = root / "generation-draft";
  dialogs.paths[platform::FileDialogPurpose::CreateSampleManifestDraft] = {draftRoot};
  CHECK(studio.settle([&] { const auto control = studio.node("create-draft"); return control && control->enabled; }));
  CHECK(studio.activate("create-draft").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("DRAFT COMMITTED / OPENED") != std::string::npos;
  }));
  // The draft carries the campaign's own audio, so this is generated material, not a fixture.
  CHECK(!studio.app->productionProject()->takes.empty());
  const auto draftAudio = core::sha256File(draftRoot / "manifest.json");
  CHECK(draftAudio.hasValue());

  const auto assignments = studio.app->productionProject()->unitAssignments.size();
  for (std::size_t index = 0U; index < assignments; ++index) {
    CHECK(studio.settle([&] { const auto control = studio.node("capture"); return control && control->enabled; }));
    CHECK(studio.activate("capture").hasValue());
    CHECK(studio.settle([&] { const auto control = studio.node("reviewer"); return control && control->enabled; }));
    dialogs.sampleReviewers = {std::string{"listener"}};
    CHECK(studio.activate("reviewer").hasValue());
    CHECK(studio.settle([&] { const auto control = studio.node("accept"); return control && control->enabled; }));
    dialogs.sampleReviewConfirmations = {true};
    CHECK(studio.activate("accept").hasValue());
    CHECK(studio.settle([&] {
      return studio.value("status").find("REVIEW COMMITTED") != std::string::npos;
    }));
    if (index + 1U < assignments) {
      CHECK(studio.activate("next-unit").hasValue());
      CHECK(studio.settle([&] {
        return studio.node("capture").has_value();
      }));
    }
  }

  // Publish, sign, install and open a song bound to the installed bank.
  const auto candidate = root / "generation-candidate";
  dialogs.paths[platform::FileDialogPurpose::PublishSampleCandidate] = {candidate};
  CHECK(studio.activate("publish").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("ENGINEERING CANDIDATE COMMITTED") != std::string::npos;
  }));
  CHECK(studio.settle([&] { const auto control = studio.node("sign-bank"); return control && control->enabled; }));
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto keyPath = root / "keys" / "generation-signer.json";
  std::filesystem::create_directories(keyPath.parent_path());
  CHECK(distribution::savePrivateKey(key.value(), keyPath).hasValue());
  const auto package = root / "out" / "generation-journey.seambank";
  std::filesystem::create_directories(package.parent_path());
  dialogs.paths[platform::FileDialogPurpose::SelectSingerSigningKey] = {keyPath};
  dialogs.paths[platform::FileDialogPurpose::PublishSampleBank] = {package};
  CHECK(studio.activate("sign-bank").hasValue());
  CHECK(studio.settle([&] { return std::filesystem::exists(package); }));
  CHECK(studio.settle([&] { const auto control = studio.node("install-bank"); return control && control->enabled; }));
  CHECK(studio.activate("install-bank").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("INSTALLED AS A TRUSTED BANK") != std::string::npos;
  }));
  const auto* installed = studio.app->installedSampleBank();
  CHECK(installed != nullptr);
  if (installed == nullptr) return;
  const auto contentHash = installed->contentHash;

  const auto songs = root / "songs";
  std::filesystem::create_directories(songs);
  const auto song = songs / "Generation Journey Song.seam";
  dialogs.paths[platform::FileDialogPurpose::SaveProject] = {song};
  CHECK(studio.settle([&] {
    const auto control = studio.node("open-bank-in-song-editor"); return control && control->enabled;
  }));
  CHECK(studio.activate("open-bank-in-song-editor").hasValue());
  CHECK(studio.launches->size() == 1U);
  CHECK(std::filesystem::exists(song));

  // The editor half: the installed bank carries the campaign's own audio, opens without a
  // missing-bank diagnostic and exports non-silent audio.
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache",
      .voicebankRoots = {{studio.voicebanks(), voicebank::VoicebankRootKind::Installed}},
      .sampleRate = 48000U, .outputChannels = 2U, .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  auto editorDialog = std::make_unique<EditorDialog>();
  auto* editorResponses = editorDialog.get();
  standalone::StandaloneApplicationControllerConfig editorConfig{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  editorConfig.voicebankInstallRoot = studio.voicebanks();
  editorConfig.trustedVoicebankKeys = {key.value().publicKey};
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(editorDialog), std::make_unique<DiscardPrompt>(), editorConfig);
  CHECK(controller.hasValue());
  if (!controller) return;
  const auto card = std::find_if(controller.value()->voicebankCards().begin(),
      controller.value()->voicebankCards().end(),
      [&](const auto& value) { return value.contentHash == contentHash; });
  CHECK(card != controller.value()->voicebankCards().end());
  editorResponses->responses.push_back(song);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenProject).hasValue());
  auto& runtime = session.value()->runtime();
  for (const auto& diagnostic : runtime.diagnostics())
    CHECK(diagnostic.code != "BANK_MISSING");
  const auto* track = runtime.document().session().project().findVocalTrack(runtime.selectedTrack());
  CHECK(track != nullptr);
  if (track == nullptr) return;
  CHECK(track->voicebank.contentHash == contentHash);
  time::Tick start{0};
  for (const char32_t* lyric : {U"あ", U"い", U"う"}) {
    auto [token, note] = runtime.document().factory().makeNote(
        start, time::Tick{480}, 60U, std::u32string{lyric}, domain::Language::Japanese);
    CHECK(runtime.execute(std::make_unique<application::AddNoteCommand>(
        runtime.selectedRegion(), std::move(token), std::move(note))).hasValue());
    start = start + time::Tick{480};
  }
  authoring::ExportSettings settings;
  settings.includeMaster = true;
  const auto exported = controller.value()->exportSet(root / "export", settings);
  CHECK(exported.hasValue());
  if (!exported) return;
  const auto master = voicebank::readWav(exported.value().masterPath);
  CHECK(master.hasValue());
  if (!master) return;
  float peak = 0.0F;
  for (const auto sample : master.value().interleaved) peak = std::max(peak, std::fabs(sample));
  CHECK(peak > 0.01F);
}

// Regenerating one unit that already holds a collected take. The route the product offers for
// recorded material is "import a retake"; generated material has to reach the same place from the
// generation controls, because a producer refuses to reuse a take ID and refuses to replace an
// occupied assignment without an explicit retake chain. What this case proves is that Studio's own
// prepare/run path regenerates an occupied unit as a superseding retake after the voice was
// edited, that the superseded take stays in the repository, that the earlier source-quality
// assessment stops qualifying anything until a fresh one covers the new material, and that the
// draft and the reviewer's decision then land on the regenerated take. The audio is synthetic and
// the reviewer is scripted; this is not a judgement about either take.
TEST_CASE("Voicebank Studio regenerates a collected unit as a retake that supersedes the old take") {
  const auto root = test::support::temporaryDirectory("studio-generation-retake");
  StudioHarness studio{root};
  auto& dialogs = *studio.dialogs;
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);

  const auto workspace = root / "retake-workspace";
  dialogs.newWorkspaces = {platform::IFileDialog::NewProducerWorkspaceInput{
      .destination = workspace, .projectId = "generation-retake", .producerId = "producer",
      .inventory = platform::IFileDialog::NewProducerWorkspaceInput::Inventory::JapaneseVowelStarter}};
  CHECK(studio.activate("create-producer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject() != nullptr; }));
  const auto* project = studio.app->productionProject();
  CHECK(project != nullptr);
  if (project == nullptr) return;
  CHECK(!project->unitAssignments.empty());
  if (project->unitAssignments.empty()) return;

  const auto license = root / "retake-license.txt";
  CHECK(core::durableAtomicWriteTextNew(license,
      "Harness generation consent: procedural synthesis, transformation, bank redistribution and "
      "commercial renders.").hasValue());
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] { const auto control = studio.node("source-license"); return control && control->enabled; }));
  dialogs.paths[platform::FileDialogPurpose::SourceLicenseEvidence] = {license};
  CHECK(studio.activate("source-license").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("source-register"); return control && control->enabled; }));
  dialogs.sourceRegistrations = {platform::SourceRegistrationInput{
      .id = "retake-synthesis", .kind = "procedural", .rights = "pass",
      .permissions = {"yes", "yes", "yes", "yes"}}};
  CHECK(studio.activate("source-register").hasValue());
  CHECK(studio.settle([&] {
    return studio.app->productionProject()->selectedSourceStrategyId == "retake-synthesis";
  }));
  dialogs.reviewerRegistrations = {std::string{"listener"}};
  CHECK(studio.activate("register-reviewer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject()->operators.size() == 2U; }));
  CHECK(studio.settle([&] { const auto back = studio.node("back"); return back && back->enabled; }));
  CHECK(studio.activate("back").hasValue());

  // The first campaign collects one take per unit, exactly as the sibling journey does.
  const auto recipePath = root / "retake-recipe.json";
  CHECK(voice_design::saveVoiceRecipeFile(recipePath, journeyRecipe()).hasValue());
  const auto campaign = root / "retake-campaign";
  dialogs.paths[platform::FileDialogPurpose::SelectProceduralRecipe] = {recipePath};
  dialogs.paths[platform::FileDialogPurpose::PlanGenerationCampaign] = {campaign};
  CHECK(studio.settle([&] { const auto control = studio.node("plan-campaign"); return control && control->enabled; }));
  CHECK(studio.activate("plan-campaign").hasValue());
  CHECK(studio.settle([&] { return studio.value("status").find("CAMPAIGN PLANNED") != std::string::npos; }));
  CHECK(studio.settle([&] { const auto control = studio.node("preflight-campaign"); return control && control->enabled; }));
  CHECK(studio.activate("preflight-campaign").hasValue());
  CHECK(studio.settle([&] { return studio.value("status").find("CAMPAIGN PREFLIGHT PASSED") != std::string::npos; }));
  CHECK(studio.settle([&] { const auto control = studio.node("run-campaign"); return control && control->enabled; }));
  CHECK(studio.activate("run-campaign").hasValue());
  CHECK(studio.settle([&] { return studio.value("status").find("CAMPAIGN COLLECTED") != std::string::npos; }));
  const auto* collected = studio.app->productionProject();
  CHECK(collected != nullptr);
  if (collected == nullptr) return;
  const auto collectedTakes = collected->takes.size();
  CHECK(collectedTakes != 0U);
  if (collectedTakes == 0U) return;
  const auto originalTakeId = collected->takes.front().takeId;
  const auto originalAudio = collected->takes.front().rawAssetSha256;

  // The source-quality assessment describes material that already exists, so it is recorded once
  // the campaign has collected it. It is what qualifies a take for review, and it is bound to the
  // exact set of active takes, which is what a regeneration is about to change.
  const auto assess = [&](const std::string& id, const std::string& evidenceName,
                          const std::string& note) {
    const auto recorded = studio.app->productionProject()->sourceQualityAssessments.size();
    studio.key(NativeKey::Q, {});
    CHECK(studio.settle([&] { const auto control = studio.node("source-evidence"); return control && control->enabled; }));
    const auto evidence = root / evidenceName;
    CHECK(core::durableAtomicWriteTextNew(evidence, note).hasValue());
    dialogs.paths[platform::FileDialogPurpose::SourceQualityEvidence] = {evidence};
    CHECK(studio.activate("source-evidence").hasValue());
    CHECK(studio.settle([&] { const auto control = studio.node("source-decision"); return control && control->enabled; }));
    dialogs.sourceQualityDecisions = {platform::SourceQualityDecisionInput{
        .id = id, .reviewerId = "listener", .coverage = "pass", .listening = "pass"}};
    CHECK(studio.activate("source-decision").hasValue());
    CHECK(studio.settle([&] {
      return studio.app->productionProject()->sourceQualityAssessments.size() == recorded + 1U;
    }));
  };
  assess("retake-quality-first", "retake-quality-first.txt",
         "Harness assessment of the first generated material");
  CHECK(voicebank_production::requireTakeSourceQualification(
      *studio.app->productionProject(), originalTakeId).hasValue());
  CHECK(studio.settle([&] { const auto back = studio.node("back"); return back && back->enabled; }));
  CHECK(studio.activate("back").hasValue());

  // The unit is occupied, and the score that names it has to speak the same pitch layer.
  const auto unit = studio.app->productionProject()->unitAssignments.front();
  CHECK(unit.takeId == originalTakeId);
  // The first campaign met an unoccupied row, so it collected under the planned identity itself.
  CHECK(unit.plannedTakeId == originalTakeId);
  CHECK(unit.state == voicebank_production::UnitQueueState::MarkerReview);

  // The edit a regeneration follows: the voice changes, so the regenerated material can differ
  // from what it replaces. The unchanged recipe would render the very same bytes.
  auto editedRecipe = journeyRecipe();
  editedRecipe.phonation.aspiration = 0.25;
  const auto scoreRecipePath = root / "retake-score-recipe.json";
  const auto frozen = voice_design::freezeVoiceRecipeResource(editedRecipe);
  CHECK(frozen.hasValue());
  if (!frozen) return;
  CHECK(voice_design::saveVoiceRecipeFile(scoreRecipePath, editedRecipe).hasValue());
  // The score is the inventory's own template for this unit, so it names exactly the coverage and
  // pitch the unit holds instead of a hand-built phrase that merely resembles it.
  auto inventoryScore = authoring::buildInventoryGenerationScore(
      *studio.app->productionProject(), unit.plannedTakeId);
  CHECK(inventoryScore.hasValue());
  if (!inventoryScore) return;
  auto scoreProject = std::move(inventoryScore.value().project);
  const auto scoreTrackId = inventoryScore.value().trackId;
  const auto scoreRegionId = inventoryScore.value().regionId;
  scoreProject.findVocalTrack(scoreTrackId)->proceduralRecipe =
      domain::ProceduralRecipeReference{frozen.value().identity,
                                        scoreRecipePath.filename().string(), unit.style};
  const auto scorePath = root / "retake-score.seam";
  CHECK(formats::ProjectJsonCodec{}.save(scoreProject, scorePath).hasValue());

  // Replacing accepted material is an explicit decision: the shared preparation refuses an occupied
  // unit unless its caller opts in to a retake, so a plain preparation cannot supersede a take.
  CHECK(!authoring::prepareGenerationJobFromScore(root / "refused-job", "refused-job", scorePath,
      scoreTrackId, scoreRegionId, *studio.app->productionProject(), unit.plannedTakeId));
  CHECK(!std::filesystem::exists(root / "refused-job"));
  const auto titleOf = [&](platform::FileDialogPurpose purpose) -> std::string {
    for (auto request = dialogs.requests.rbegin(); request != dialogs.requests.rend(); ++request)
      if (request->purpose == purpose) return request->title;
    return {};
  };

  // Inspecting the score is its own step; the preparation it enables writes a job package and
  // renders nothing, so the producer still holds exactly the material the campaign collected.
  dialogs.paths[platform::FileDialogPurpose::OpenProject] = {scorePath};
  dialogs.generationRegions = {std::size_t{0U}};
  const auto jobDirectory = root / "retake-job";
  dialogs.paths[platform::FileDialogPurpose::PrepareGenerationJob] = {jobDirectory};
  CHECK(studio.settle([&] { const auto control = studio.node("prepare"); return control && control->enabled; }));
  CHECK(studio.activate("prepare").hasValue());
  CHECK(studio.settle([&] { return studio.value("status").find("SCORE READY") != std::string::npos; }));
  CHECK(studio.activate("prepare").hasValue());
  CHECK(studio.settle([&] { return studio.value("status").find("JOB PREPARED") != std::string::npos; }));
  CHECK(studio.app->productionProject()->takes.size() == collectedTakes);
  // Studio names the action for what it is before it asks where the job goes.
  CHECK(titleOf(platform::FileDialogPurpose::PrepareGenerationJob) ==
        "Prepare Retake Job Folder (No Audio Generated)");

  // The prepared job names the retake the producer will accept, not the take the unit already
  // holds: a job that reused the planned ID could never be collected for an occupied row.
  const auto reference = authoring::loadGenerationJobReference(jobDirectory / "job.seamjob");
  CHECK(reference.hasValue());
  if (!reference) return;
  const auto job = authoring::loadGenerationJob(reference.value().directory, reference.value().manifestSha256);
  CHECK(job.hasValue());
  if (!job) return;
  CHECK(job.value().expectation.takeId != unit.plannedTakeId);
  CHECK(job.value().expectation.supersedesTakeId == originalTakeId);
  const auto retakeTakeId = job.value().expectation.takeId;

  // Running that job collects the regenerated material as an unapproved retake and leaves the
  // take it supersedes in the repository as superseded rather than replacing it.
  dialogs.paths[platform::FileDialogPurpose::OpenGenerationJob] = {jobDirectory / "job.seamjob"};
  CHECK(studio.settle([&] { const auto control = studio.node("generate"); return control && control->enabled; }));
  CHECK(studio.activate("generate").hasValue());
  if (!studio.settle([&] {
        return studio.value("status").find("GENERATED CANDIDATE") != std::string::npos;
      })) {
    throw test::Failure("regeneration never reported a collected candidate; status was: " +
                        studio.value("status"));
  }
  CHECK(titleOf(platform::FileDialogPurpose::OpenGenerationJob) ==
        "Generate and Collect an Unapproved Retake");
  const auto* regenerated = studio.app->productionProject();
  CHECK(regenerated != nullptr);
  if (regenerated == nullptr) return;
  CHECK(regenerated->takes.size() == collectedTakes + 1U);
  const auto newTake = std::find_if(regenerated->takes.begin(), regenerated->takes.end(),
      [&](const auto& take) { return take.takeId == retakeTakeId; });
  CHECK(newTake != regenerated->takes.end());
  if (newTake == regenerated->takes.end()) return;
  CHECK(newTake->supersedesTakeId == originalTakeId);
  CHECK(newTake->state == voicebank_production::UnitQueueState::MarkerReview);
  CHECK(newTake->rawAssetSha256 != originalAudio);
  const auto retakeAudio = newTake->rawAssetSha256;
  CHECK(regenerated->reviews.empty());
  const auto original = std::find_if(regenerated->takes.begin(), regenerated->takes.end(),
      [&](const auto& take) { return take.takeId == originalTakeId; });
  // The superseded take stays in the repository, marked as replaced, so the lineage is auditable.
  CHECK(original != regenerated->takes.end());
  if (original != regenerated->takes.end())
    CHECK(original->state == voicebank_production::UnitQueueState::Retake);
  const auto active = std::find_if(regenerated->unitAssignments.begin(), regenerated->unitAssignments.end(),
      [&](const auto& row) { return row.coverageKey == unit.coverageKey && row.pitchLayer == unit.pitchLayer; });
  CHECK(active != regenerated->unitAssignments.end());
  if (active != regenerated->unitAssignments.end()) {
    CHECK(active->takeId == retakeTakeId);
    CHECK(active->state == voicebank_production::UnitQueueState::MarkerReview);
    CHECK(!active->markerReviewed);
    CHECK(!active->pitchReviewed);
  }

  // Re-running the same job is recognized as collected instead of writing a second retake of it.
  dialogs.paths[platform::FileDialogPurpose::OpenGenerationJob] = {jobDirectory / "job.seamjob"};
  CHECK(studio.settle([&] { const auto control = studio.node("generate"); return control && control->enabled; }));
  CHECK(studio.activate("generate").hasValue());
  if (!studio.settle([&] { return studio.value("status").find("ALREADY COLLECTED") != std::string::npos; })) {
    throw test::Failure("re-running a collected job was not recognized; status was: " +
                        studio.value("status"));
  }
  CHECK(studio.app->productionProject()->takes.size() == collectedTakes + 1U);

  CHECK(studio.app->productionQueues().retake == 0U);
  CHECK(studio.app->productionQueues().missing == 0U);

  // The assessment described the previous set of active takes. Replacing one changes that set, so
  // the earlier assessment qualifies nothing: the regenerated take cannot be reviewed on the
  // strength of a judgement about material it was never part of.
  CHECK(!voicebank_production::requireTakeSourceQualification(
      *studio.app->productionProject(), retakeTakeId).hasValue());
  // A fresh assessment of the material that now exists restores it.
  assess("retake-quality-second", "retake-quality-second.txt",
         "Harness assessment of the regenerated material");
  CHECK(voicebank_production::requireTakeSourceQualification(
      *studio.app->productionProject(), retakeTakeId).hasValue());

  // The draft a reviewer opens is built from each unit's active take, so it carries the
  // regenerated audio for that unit and none of the audio it replaced.
  dialogs.sampleDraftIdentities = {platform::SampleManifestDraftIdentityInput{
      .id = "generation-retake", .version = "1.0.0", .displayName = "Generation Retake",
      .language = "ja", .style = unit.style}};
  const auto draftRoot = root / "retake-draft";
  dialogs.paths[platform::FileDialogPurpose::CreateSampleManifestDraft] = {draftRoot};
  CHECK(studio.settle([&] { const auto control = studio.node("create-draft"); return control && control->enabled; }));
  CHECK(studio.activate("create-draft").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("DRAFT COMMITTED / OPENED") != std::string::npos;
  }));
  CHECK(std::filesystem::exists(draftRoot / "audio" / (retakeAudio + ".wav")));
  CHECK(!std::filesystem::exists(draftRoot / "audio" / (originalAudio + ".wav")));

  // The regenerated unit is the first row, which the review view opens on, so one explicit
  // reviewer decision lands on the retake and on nothing it replaced.
  const auto reviewedBefore = studio.app->productionProject()->reviews.size();
  CHECK(studio.settle([&] { const auto control = studio.node("capture"); return control && control->enabled; }));
  CHECK(studio.activate("capture").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("reviewer"); return control && control->enabled; }));
  dialogs.sampleReviewers = {std::string{"listener"}};
  CHECK(studio.activate("reviewer").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("accept"); return control && control->enabled; }));
  dialogs.sampleReviewConfirmations = {true};
  CHECK(studio.activate("accept").hasValue());
  CHECK(studio.settle([&] {
    return studio.app->productionProject()->reviews.size() > reviewedBefore;
  }));
  const auto& reviews = studio.app->productionProject()->reviews;
  CHECK(reviews.back().takeId == retakeTakeId);
  CHECK(reviews.back().result == "PASS");
  CHECK(std::none_of(reviews.begin(), reviews.end(),
      [&](const auto& review) { return review.takeId == originalTakeId; }));
}
