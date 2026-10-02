// Voicebank Studio driven through the surface its native window uses: painted frames, accessibility
// actions and field values, and key events. Only operating-system effects are replaced. Dialogs
// answer from a script, audition audio goes to a silent threaded device, singers install into a
// temporary folder, and the song-editor launch is recorded instead of performed. The editor half then
// opens the handed-off project in a fresh standalone session and exports what it sings, so the whole
// create -> edit -> save -> publish -> install -> sing path is exercised without an AppKit panel.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "studio_app.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace seam;
using native_ui::NativeKey;
using native_ui::SemanticAction;
using native_ui::SemanticNode;

struct DialogScript final {
  std::map<platform::FileDialogPurpose, std::vector<std::optional<std::filesystem::path>>> paths;
  std::vector<std::optional<platform::ProceduralSingerPublishInput>> publishInputs;
  std::vector<std::optional<platform::IFileDialog::NewProducerWorkspaceInput>> newWorkspaces;
  std::vector<std::optional<platform::IFileDialog::ProductionWorkspaceInput>> workspaces;
  std::vector<std::optional<platform::SourceRegistrationInput>> sourceRegistrations;
  std::vector<std::string> sourceRegistrationSummaries;
  std::vector<std::optional<std::string>> reviewerRegistrations;
  std::vector<std::string> reviewerRegistrationSummaries;
  std::vector<std::optional<platform::SampleManifestDraftIdentityInput>> sampleDraftIdentities;
  std::vector<std::optional<platform::SourceQualityDecisionInput>> sourceQualityDecisions;
  std::vector<std::optional<std::string>> sampleReviewers;
  std::vector<bool> sampleReviewConfirmations;
  std::vector<std::string> sampleReviewSummaries;
  std::vector<platform::FileDialogRequest> requests;
};

template <typename Value>
std::optional<Value> nextAnswer(std::vector<std::optional<Value>>& queue) {
  if (queue.empty()) return std::nullopt;
  auto next = std::move(queue.front());
  queue.erase(queue.begin());
  return next;
}

// Answers every modal from the script; an empty queue is the user pressing Cancel.
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
  core::Result<std::optional<platform::ProceduralSingerPublishInput>>
  chooseProceduralSingerPublishInput() override {
    return nextAnswer(script_->publishInputs);
  }
  core::Result<std::optional<NewProducerWorkspaceInput>> chooseNewProducerWorkspace() override {
    return nextAnswer(script_->newWorkspaces);
  }
  core::Result<std::optional<ProductionWorkspaceInput>> chooseProductionWorkspace() override {
    return nextAnswer(script_->workspaces);
  }
  core::Result<std::optional<platform::SourceRegistrationInput>> chooseSourceRegistration(
      std::string_view summary) override {
    script_->sourceRegistrationSummaries.emplace_back(summary);
    return nextAnswer(script_->sourceRegistrations);
  }
  core::Result<std::optional<std::string>> chooseReviewerRegistration(std::string_view summary) override {
    script_->reviewerRegistrationSummaries.emplace_back(summary);
    return nextAnswer(script_->reviewerRegistrations);
  }
  core::Result<std::optional<platform::SampleManifestDraftIdentityInput>> chooseSampleManifestDraftIdentity() override {
    return nextAnswer(script_->sampleDraftIdentities);
  }
  core::Result<std::optional<platform::SourceQualityDecisionInput>> chooseSourceQualityDecision(
      std::string_view, const std::vector<std::string>&) override {
    return nextAnswer(script_->sourceQualityDecisions);
  }
  core::Result<std::optional<std::string>> chooseSampleReviewer(const std::vector<std::string>&) override {
    return nextAnswer(script_->sampleReviewers);
  }
  core::Result<bool> confirmSampleReview(std::string_view summary, bool) override {
    script_->sampleReviewSummaries.emplace_back(summary);
    if (script_->sampleReviewConfirmations.empty()) return core::Result<bool>{false};
    const auto next = script_->sampleReviewConfirmations.front();
    script_->sampleReviewConfirmations.erase(script_->sampleReviewConfirmations.begin());
    return core::Result<bool>{next};
  }

private:
  std::shared_ptr<DialogScript> script_;
};

// A microphone the test controls: it can refuse to open the way CoreAudio reports denied access,
// deliver audio blocks, and disappear mid-capture the way an unplugged interface does.
struct MicrophoneScript final {
  std::string denial;
  platform::IAudioInputProcessor* processor{nullptr};
  bool running{false};
  std::size_t opens{0U};
  platform::AudioInputDeviceStats stats{};
  platform::AudioInputDeviceInfo info{.backend = "Harness microphone", .deviceName = "harness-mic",
      .sampleRate = 48000U, .blockFrames = 256U, .physical = true};

  void sing(double frequencyHz, double seconds) {
    const auto samples = test::support::sineWave(48000U, frequencyHz, seconds, 0.25F);
    for (std::size_t offset = 0U; offset < samples.size(); offset += 256U) {
      const auto count = std::min<std::size_t>(256U, samples.size() - offset);
      CHECK(processor != nullptr && running);
      if (processor == nullptr || !running) return;
      processor->process({.sampleRate = 48000.0, .frameCount = count,
                          .mono = std::span<const float>{samples}.subspan(offset, count)});
      ++stats.callbacks;
      stats.frames += count;
    }
  }
};

class ScriptedMicrophone final : public platform::IAudioInputDevice {
public:
  explicit ScriptedMicrophone(std::shared_ptr<MicrophoneScript> script) : script_(std::move(script)) {}
  core::Result<void> open(const platform::AudioInputDeviceConfig& config,
                          platform::IAudioInputProcessor& processor) override {
    ++script_->opens;
    if (!script_->denial.empty()) return core::failure(core::ErrorCode::IoError, script_->denial);
    if (config.sampleRate != script_->info.sampleRate || config.blockFrames != script_->info.blockFrames)
      return core::failure(core::ErrorCode::InvalidArgument, "Unexpected capture format");
    script_->processor = &processor;
    return core::success();
  }
  core::Result<void> start() override {
    script_->running = true;
    return core::success();
  }
  void stop() noexcept override { script_->running = false; }
  bool running() const noexcept override { return script_->running; }
  platform::AudioInputDeviceInfo info() const override { return script_->info; }
  platform::AudioInputDeviceStats stats() const noexcept override { return script_->stats; }

private:
  std::shared_ptr<MicrophoneScript> script_;
};

// An audition output the test controls, as the microphone above is. It plays nothing. It can refuse to
// open, as a device that has gone away does, and it can refuse to say that it has stopped, as a platform
// whose stop returns an error does: the device is then still the audition's, and still running.
struct OutputScript final {
  platform::IAudioProcessor* processor{nullptr};
  platform::AudioDeviceInfo info{};
  bool running{false};
  bool failOpen{false};
  bool failStop{false};
  bool destroyed{false};
  unsigned opens{0U};
  unsigned starts{0U};
  unsigned stops{0U};
  std::uint64_t callbacks{0U};
};

class ScriptedOutput final : public platform::IAudioDevice {
public:
  explicit ScriptedOutput(std::shared_ptr<OutputScript> script) : script_(std::move(script)) {}
  ~ScriptedOutput() override {
    // The last attempt every device makes, whatever it answers.
    static_cast<void>(stop());
    script_->destroyed = true;
    script_->processor = nullptr;
  }
  core::Result<void> open(const platform::AudioDeviceConfig& config,
                          platform::IAudioProcessor& processor) override {
    ++script_->opens;
    if (script_->failOpen)
      return core::failure(core::ErrorCode::IoError, "scripted audition output will not open");
    script_->processor = &processor;
    script_->info = {.backend = "harness-output", .deviceId = "harness-output",
                     .deviceName = "harness output", .sampleRate = config.sampleRate,
                     .blockFrames = config.blockFrames, .outputChannels = config.outputChannels,
                     .physical = false};
    return core::success();
  }
  core::Result<void> start() override {
    script_->running = true;
    ++script_->starts;
    return core::success();
  }
  core::Result<void> stop() noexcept override {
    ++script_->stops;
    if (script_->running && script_->failStop)
      return core::failure(core::ErrorCode::IoError, "scripted audition output did not stop");
    script_->running = false;
    return core::success();
  }
  bool running() const noexcept override { return script_->running; }
  platform::AudioDeviceInfo info() const override { return script_->info; }
  // One callback per look while it runs, so the session sees progress without a clock.
  platform::AudioDeviceStats stats() const noexcept override {
    if (script_->running) ++script_->callbacks;
    return {.callbacks = script_->callbacks};
  }

private:
  std::shared_ptr<OutputScript> script_;
};

struct Launch final {
  std::filesystem::path document;
  std::filesystem::path application;
};

const SemanticNode* findSuffix(const SemanticNode& node, const std::string& suffix) {
  if (node.id.ends_with(suffix)) return &node;
  for (const auto& child : node.children)
    if (const auto* found = findSuffix(child, suffix)) return found;
  return nullptr;
}

void collectButtons(const SemanticNode& node, std::vector<SemanticNode>& buttons) {
  if (node.role == native_ui::SemanticRole::Button) buttons.push_back(node);
  for (const auto& child : node.children) collectButtons(child, buttons);
}

class StudioHarness final {
public:
  std::shared_ptr<DialogScript> dialogs = std::make_shared<DialogScript>();
  std::shared_ptr<std::vector<Launch>> launches = std::make_shared<std::vector<Launch>>();
  std::unique_ptr<voicebank_studio_native::IVoicebankStudioApp> app;

  explicit StudioHarness(std::filesystem::path root,
                         std::shared_ptr<MicrophoneScript> microphone = nullptr,
                         std::function<std::unique_ptr<platform::IAudioDevice>()> audioDevice = {})
      : root_(std::move(root)), singers_(root_ / "singers"), banks_(root_ / "voicebanks"),
        editor_(root_ / "Project SEAM.app") {
    std::filesystem::create_directories(singers_);
    std::filesystem::create_directories(banks_);
    voicebank_studio_native::StudioPlatform hooks;
    hooks.fileDialog = [script = dialogs] { return std::make_unique<ScriptedDialog>(script); };
    hooks.audioDevice = [] { return platform::createThreadedAudioDevice(); };
    // A silent threaded device unless a test brings its own.
    if (audioDevice) hooks.audioDevice = std::move(audioDevice);
    if (microphone) {
      hooks.recordingInput.physical = [microphone] {
        return std::make_unique<ScriptedMicrophone>(microphone);
      };
      hooks.recordingInput.synthetic = {};
    }
    hooks.singerRoots = [singers = singers_] {
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
    // The shipped app opens physical input; the scripted microphone stands in for it. Without one
    // the synthetic test input is used, as the command-line probe does.
    app = voicebank_studio_native::createVoicebankStudioApp(microphone == nullptr, std::move(hooks));
  }

  [[nodiscard]] const std::filesystem::path& singers() const noexcept { return singers_; }
  [[nodiscard]] const std::filesystem::path& voicebanks() const noexcept { return banks_; }
  [[nodiscard]] const std::filesystem::path& editor() const noexcept { return editor_; }

  void resize(double width, double height) {
    width_ = width;
    height_ = height;
    app->resized(width, height, 1.0);
  }

  // One window frame: paint polls the Designer, recording and producer workers exactly as the
  // native window's frames do, and rebuilds the accessibility tree the platform bridge reads.
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

  core::Result<void> perform(std::string_view suffix, SemanticAction action) {
    const auto target = node(suffix);
    if (!target)
      return core::failure(core::ErrorCode::NotFound,
                           "No accessible element ends with " + std::string{suffix});
    return app->dispatchAccessibility(target->id, action);
  }

  core::Result<void> activate(std::string_view suffix) {
    return perform(suffix, SemanticAction::Activate);
  }

  core::Result<void> setValue(std::string_view suffix, std::string_view value) {
    const auto target = node(suffix);
    if (!target)
      return core::failure(core::ErrorCode::NotFound,
                           "No accessible field ends with " + std::string{suffix});
    return app->setAccessibilityValue(target->id, value);
  }

  void key(NativeKey key, native_ui::InputModifiers modifiers) {
    app->keyDown(native_ui::KeyEvent{.key = key, .modifiers = modifiers});
  }

  void click(double x, double y) {
    const native_ui::PointerEvent event{.position = {x, y}, .button = native_ui::PointerButton::Left};
    app->pointerDown(event);
    app->pointerUp(event);
  }

  // Writes what the harness sees, at the given window size, when SEAM_STUDIO_APP_SNAPSHOT_DIR names a
  // directory; the checks never depend on it. The window returns to its previous size afterwards.
  void snapshot(std::string_view name, double width, double height) {
    const char* directory = std::getenv("SEAM_STUDIO_APP_SNAPSHOT_DIR");
    if (directory == nullptr || *directory == '\0') return;
    const auto previousWidth = width_, previousHeight = height_;
    resize(width, height);
    frame();
    CHECK(surface_.writePpm(std::filesystem::path{directory} / (std::string{name} + ".ppm")).hasValue());
    resize(previousWidth, previousHeight);
    frame();
  }

  template <typename Done>
  bool settle(Done done) {
    for (int index = 0; index < 10000; ++index) {
      frame();
      if (done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
  }

private:
  std::filesystem::path root_;
  std::filesystem::path singers_;
  std::filesystem::path banks_;
  std::filesystem::path editor_;
  double width_{1100.0};
  double height_{720.0};
  native_ui::PixelSurface surface_;
};

constexpr native_ui::InputModifiers kCommandOption{.alt = true, .command = true};

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

double midiHz(std::int32_t midi) {
  return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

std::size_t wavFiles(const std::filesystem::path& directory) {
  std::size_t count = 0U;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator{directory, error})
    if (entry.path().extension() == ".wav") ++count;
  return count;
}

}  // namespace

TEST_CASE("Voicebank Studio's own actions take a new voice from draft to a song the editor sings") {
  const auto root = test::support::temporaryDirectory("studio-app-journey");
  StudioHarness studio{root};
  auto& dialogs = *studio.dialogs;
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);

  // Designer home: create the draft through the accessible entry, then set one numeric control the
  // way a screen reader sets a field value.
  CHECK(studio.node("new").has_value());
  CHECK(studio.activate("new").hasValue());
  CHECK(studio.node("control.0").has_value());
  const auto initialOpenQuotient = studio.value("control.0");
  CHECK(studio.setValue("control.0", "0.55").hasValue());
  CHECK(studio.value("control.0") != initialOpenQuotient);
  CHECK(studio.value("status") == "Unsaved");

  // Save As is refused inside the installed-singer folder, then saves where the creator chose.
  const auto draft = root / "drafts" / "harness-voice.json";
  std::filesystem::create_directories(draft.parent_path());
  dialogs.paths[platform::FileDialogPurpose::SaveDesignerRecipe] = {
      studio.singers() / "draft.json", draft};
  CHECK(studio.activate("save-as").hasValue());
  CHECK(studio.value("error").find("installed singer") != std::string::npos);
  CHECK(!std::filesystem::exists(studio.singers() / "draft.json"));
  CHECK(studio.activate("save-as").hasValue());
  CHECK(studio.settle([&] { return studio.value("status") == "Saved"; }));
  CHECK(std::filesystem::exists(draft));

  // Publish through the keyboard shortcut. The first release form is cancelled and changes nothing.
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto keyPath = root / "keys" / "publisher.json";
  std::filesystem::create_directories(keyPath.parent_path());
  CHECK(distribution::savePrivateKey(key.value(), keyPath).hasValue());
  const std::string displayName =
      "Harness Voice With A Deliberately Long Display Name That Keeps Going Past Narrow Labels";
  const auto package = root / "out" / "harness-voice.seamsinger";
  std::filesystem::create_directories(package.parent_path());
  dialogs.publishInputs = {std::nullopt, platform::ProceduralSingerPublishInput{
                                             .version = "1.0.0", .displayName = displayName,
                                             .language = "ja"}};
  dialogs.paths[platform::FileDialogPurpose::SelectSingerSigningKey] = {keyPath};
  dialogs.paths[platform::FileDialogPurpose::PublishProceduralSinger] = {package};
  studio.key(NativeKey::P, kCommandOption);
  CHECK(!std::filesystem::exists(package));
  CHECK(studio.value("audition-state").find("PUBLISHED") == std::string::npos);
  studio.key(NativeKey::P, kCommandOption);
  CHECK(std::filesystem::exists(package));
  CHECK(studio.value("audition-state").starts_with("PUBLISHED SIGNED PACKAGE / NOT QUALITY-APPROVED"));

  // Install through its shortcut; the same toolbar slot then offers the song editor instead.
  CHECK(studio.node("install-published-singer").has_value());
  studio.key(NativeKey::I, kCommandOption);
  const auto installed = studio.value("audition-state");
  CHECK(installed.starts_with("INSTALLED FOR STANDALONE / NOT QUALITY-APPROVED / " + displayName));
  CHECK(installed.ends_with("/ CMD-ALT-O SONG EDITOR"));
  CHECK(!studio.node("install-published-singer").has_value());
  const auto songButton = studio.node("open-in-song-editor");
  CHECK(songButton.has_value() && songButton->enabled);

  // At the smallest window the hand-off stays on screen and every enabled button stays reachable,
  // while the long status remains complete for assistive technology.
  studio.resize(720.0, 520.0);
  {
    std::vector<SemanticNode> buttons;
    collectButtons(studio.frame().root(), buttons);
    CHECK(!buttons.empty());
    for (const auto& button : buttons) {
      if (!button.enabled || button.bounds.width <= 0.0) continue;
      CHECK(button.bounds.x >= 0.0);
      CHECK(button.bounds.x + button.bounds.width <= 720.0 + 1e-6);
    }
    const auto narrow = studio.node("open-in-song-editor");
    CHECK(narrow.has_value() && narrow->bounds.width > 0.0);
    CHECK(studio.value("audition-state") == installed);
  }
  studio.resize(1100.0, 720.0);

  // Cancelling the song location writes nothing and launches nothing.
  const auto songs = root / "songs";
  std::filesystem::create_directories(songs);
  CHECK(studio.activate("open-in-song-editor").hasValue());
  CHECK(studio.launches->empty());
  CHECK(std::filesystem::is_empty(songs));

  // The accessible hand-off: focus the action, activate it, and focus stays on it afterwards.
  const auto accessibleSong = songs / "Accessible Song.seam";
  dialogs.paths[platform::FileDialogPurpose::SaveProject] = {accessibleSong};
  CHECK(studio.perform("open-in-song-editor", SemanticAction::SetFocus).hasValue());
  const auto requestsBefore = dialogs.requests.size();
  CHECK(studio.activate("open-in-song-editor").hasValue());
  CHECK(dialogs.requests.size() == requestsBefore + 1U);
  if (dialogs.requests.size() == requestsBefore + 1U) {
    const auto& request = dialogs.requests.back();
    CHECK(request.purpose == platform::FileDialogPurpose::SaveProject);
    CHECK(request.extensions == (std::vector<std::string>{"seam"}));
    CHECK(request.suggestedName == displayName + " Song.seam");
  }
  CHECK(studio.launches->size() == 1U);
  if (studio.launches->size() != 1U) return;
  CHECK(studio.launches->front().document == std::filesystem::canonical(accessibleSong));
  CHECK(studio.launches->front().application == studio.editor());
  CHECK(studio.value("audition-state") ==
        "OPENED IN PROJECT SEAM / neutral / NOT QUALITY-APPROVED / Accessible Song.seam");
  const auto* focused = studio.frame().focusedNode();
  CHECK(focused != nullptr && focused->id.ends_with(".open-in-song-editor"));

  // The keyboard hand-off makes a second, separate song.
  const auto keyboardSong = songs / "Keyboard Song.seam";
  dialogs.paths[platform::FileDialogPurpose::SaveProject] = {keyboardSong};
  studio.key(NativeKey::O, kCommandOption);
  CHECK(studio.launches->size() == 2U);
  CHECK(std::filesystem::exists(keyboardSong));

  // Choosing an existing song is refused and leaves it byte-identical; nothing is launched.
  const auto accessibleDigest = core::sha256File(accessibleSong);
  CHECK(accessibleDigest.hasValue());
  dialogs.paths[platform::FileDialogPurpose::SaveProject] = {accessibleSong};
  CHECK(!studio.activate("open-in-song-editor").hasValue());
  CHECK(studio.launches->size() == 2U);
  CHECK(studio.value("error").find("never replaces") != std::string::npos);
  const auto unchanged = core::sha256File(accessibleSong);
  CHECK(unchanged.hasValue() && accessibleDigest.hasValue() &&
        unchanged.value() == accessibleDigest.value());

  // The editor half: a fresh standalone session opens the handed-off song with only the installed
  // singer available and sings a short phrase through it.
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = root / "cache", .voicebankRoots = {}, .sampleRate = 48000U,
      .outputChannels = 2U, .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = false});
  CHECK(session.hasValue());
  if (!session) return;
  auto editorDialog = std::make_unique<EditorDialog>();
  auto* editorResponses = editorDialog.get();
  standalone::StandaloneApplicationControllerConfig editorConfig{
      .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"};
  editorConfig.proceduralSingerRoots = {distribution::ProceduralSearchRoot{
      .path = studio.singers(), .kind = distribution::ProceduralRootKind::Installed}};
  editorConfig.renderableProceduralEngineId = std::string{voice_design::kSourceFilterEngineId};
  editorConfig.renderableProceduralEngineRevision = voice_design::kSourceFilterEngineRevision;
  auto controller = standalone::StandaloneApplicationController::create(
      *session.value(), std::move(editorDialog), std::make_unique<DiscardPrompt>(), editorConfig);
  CHECK(controller.hasValue());
  if (!controller) return;
  editorResponses->responses.push_back(accessibleSong);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenProject).hasValue());
  auto& runtime = session.value()->runtime();
  const auto diagnostics = runtime.diagnostics();
  CHECK(std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
    return diagnostic.code == "BANK_MISSING";
  }));
  const auto* track = runtime.document().session().project().findVocalTrack(runtime.selectedTrack());
  CHECK(track != nullptr && track->proceduralRecipe.has_value());
  if (track == nullptr || !track->proceduralRecipe) return;
  std::error_code pathError;
  const auto singerPath = std::filesystem::canonical(track->proceduralRecipe->path, pathError);
  CHECK(!pathError);
  const auto singerRoot = std::filesystem::canonical(studio.singers(), pathError);
  CHECK(!pathError);
  const auto insideSingers = singerPath.lexically_relative(singerRoot);
  CHECK(!insideSingers.empty() && *insideSingers.begin() != "..");
  time::Tick start{0};
  for (const char32_t* lyric : {U"あ", U"い", U"う"}) {
    auto [token, note] = runtime.document().factory().makeNote(
        start, time::Tick{480}, 69U, std::u32string{lyric}, domain::Language::Japanese);
    CHECK(runtime.execute(std::make_unique<application::AddNoteCommand>(
                              runtime.selectedRegion(), std::move(token), std::move(note)))
              .hasValue());
    start = start + time::Tick{480};
  }
  authoring::ExportSettings settings;
  settings.includeMaster = true;
  const auto exported = controller.value()->exportSet(root / "export", settings);
  CHECK(exported.hasValue());
  if (!exported) return;
  CHECK(exported.value().masterSha256.size() == 64U);
  CHECK(std::filesystem::exists(exported.value().masterPath));
  // The export is the installed singer actually singing, not a silent file of the right length.
  const auto master = voicebank::readWav(exported.value().masterPath);
  CHECK(master.hasValue());
  if (!master) return;
  float peak = 0.0F;
  for (const auto sample : master.value().interleaved) peak = std::max(peak, std::fabs(sample));
  // Three 480-tick notes at 120 BPM: the phrase itself, whatever release tail follows it.
  const auto phraseFrames = static_cast<std::size_t>(
      48000.0 * 3.0 * 480.0 / static_cast<double>(time::kDefaultPpq) * 0.5);
  CHECK(master.value().frameCount() >= phraseFrames);
  CHECK(peak > 0.01F);
}

// A cancelled or interrupted job must not cost the creator the work already on screen. This case
// drives the narrow window, a long workspace name, an interrupted capture and a cancelled publication
// through the app surface, and checks after each one that focus, selection, reviewed history and the
// saved files are exactly what they were before.
TEST_CASE("Voicebank Studio keeps focus and durable work across narrow windows and cancellations") {
  const auto root = test::support::temporaryDirectory("studio-app-recovery");
  auto microphone = std::make_shared<MicrophoneScript>();
  StudioHarness studio{root, microphone};
  auto& dialogs = *studio.dialogs;
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());

  const std::string longProjectId = "bank-recovery-" + std::string(48U, 'x');
  const auto workspace = root / (longProjectId + "-workspace");
  dialogs.newWorkspaces = {platform::IFileDialog::NewProducerWorkspaceInput{
      .destination = workspace, .projectId = longProjectId, .producerId = "producer",
      .inventory = platform::IFileDialog::NewProducerWorkspaceInput::Inventory::JapaneseVowelStarter}};
  CHECK(studio.activate("create-producer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject() != nullptr; }));
  const auto* initialProject = studio.app->productionProject();
  CHECK(initialProject != nullptr);
  if (initialProject == nullptr) return;
  const auto rows = initialProject->unitAssignments;
  CHECK(!rows.empty());
  if (rows.empty()) return;

  const auto license = root / "recovery-license.txt";
  const std::string licenseText =
      "Harness recovery consent: recording, transformation, bank redistribution and commercial renders.";
  CHECK(core::durableAtomicWriteTextNew(license, licenseText).hasValue());
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] { const auto control = studio.node("source-license"); return control && control->enabled; }));
  dialogs.paths[platform::FileDialogPurpose::SourceLicenseEvidence] = {license};
  CHECK(studio.activate("source-license").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("source-register"); return control && control->enabled; }));
  dialogs.sourceRegistrations = {platform::SourceRegistrationInput{
      .id = "recovery-singer", .kind = "human", .rights = "pass",
      .permissions = {"yes", "yes", "yes", "yes"}}};
  CHECK(studio.activate("source-register").hasValue());
  CHECK(studio.settle([&] {
    return studio.app->productionProject()->selectedSourceStrategyId == "recovery-singer";
  }));
  dialogs.reviewerRegistrations = {std::string{"listener"}};
  CHECK(studio.activate("register-reviewer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject()->operators.size() == 2U; }));
  CHECK(studio.settle([&] { const auto back = studio.node("back"); return back && back->enabled; }));
  CHECK(studio.activate("back").hasValue());

  // Focus a control, then shrink to the minimum and grow back: focus, selection and status survive.
  CHECK(studio.settle([&] { const auto button = studio.node("import-wav"); return button && button->enabled; }));
  CHECK(studio.perform("import-wav", SemanticAction::SetFocus).hasValue());
  const auto* focusedBefore = studio.frame().focusedNode();
  CHECK(focusedBefore != nullptr);
  if (focusedBefore == nullptr) return;
  const auto focusedId = focusedBefore->id;
  const auto selectedBefore = studio.app->productionProject()->unitAssignments.front().plannedTakeId;
  studio.resize(720.0, 520.0);
  {
    std::vector<SemanticNode> buttons;
    collectButtons(studio.frame().root(), buttons);
    CHECK(!buttons.empty());
    for (const auto& button : buttons) {
      if (!button.enabled || button.bounds.width <= 0.0) continue;
      CHECK(button.bounds.x >= 0.0);
      CHECK(button.bounds.x + button.bounds.width <= 720.0 + 1e-6);
    }
    const auto record = studio.node("record");
    CHECK(record.has_value() && record->enabled);
    CHECK(studio.value("microphone").starts_with("Not capturing"));
  }
  studio.resize(1100.0, 720.0);
  CHECK(studio.settle([&] {
    const auto* focused = studio.frame().focusedNode();
    return focused != nullptr && focused->id == focusedId;
  }));
  const auto* focusedAfter = studio.frame().focusedNode();
  CHECK(focusedAfter != nullptr && focusedAfter->id == focusedId);
  CHECK(studio.app->productionProject()->unitAssignments.front().plannedTakeId == selectedBefore);

  // A capture that is interrupted leaves no take, no saved file and no queue change; Record retries.
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return button && button->enabled && button->id.ends_with(".0.record");
  }));
  CHECK(studio.activate("record").hasValue());
  microphone->sing(midiHz(rows.front().pitchLayer), 0.2);
  studio.key(NativeKey::Escape, {});
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return button && button->name == "Record a take for the selected row";
  }));
  CHECK(studio.app->productionProject()->takes.empty());
  CHECK(studio.app->productionQueues().missing == rows.size());

  // A capture whose publication is refused is retained: the WAV stays, the take does not exist yet,
  // and the retry publishes that same file rather than singing again.
  CHECK(core::durableAtomicWriteText(license, "Edited after registration").hasValue());
  CHECK(studio.activate("record").hasValue());
  microphone->sing(midiHz(rows.front().pitchLayer), 0.3);
  CHECK(studio.activate("record").hasValue());
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return button && button->name == "Retry publishing the recorded take";
  }));
  const auto kept = studio.app->lastRecording();
  CHECK(!kept.empty() && std::filesystem::is_regular_file(kept));
  CHECK(studio.app->productionProject()->takes.empty());
  CHECK(studio.app->productionQueues().missing == rows.size());
  CHECK(core::durableAtomicWriteText(license, licenseText).hasValue());
  studio.key(NativeKey::R, {});
  CHECK(studio.settle([&] { return studio.app->productionQueues().markerReview == 1U; }));
  CHECK(studio.app->lastRecording() == kept);
  CHECK(wavFiles(kept.parent_path()) == 1U);
  const auto generation = studio.app->productionProject()->lastDurableGeneration;

  // A fresh Studio reopens the folder with the take, the reviewer and the source intact.
  studio.app.reset();
  StudioHarness reopened{root, microphone};
  CHECK(reopened.app->open(options).hasValue());
  reopened.resize(720.0, 520.0);
  reopened.dialogs->workspaces = {platform::IFileDialog::ProductionWorkspaceInput{
      .root = workspace, .operatorId = "producer", .producerFolder = true}};
  CHECK(reopened.activate("back").hasValue());
  CHECK(reopened.settle([&] { return reopened.app->productionProject() != nullptr; }));
  const auto* recovered = reopened.app->productionProject();
  CHECK(recovered != nullptr);
  if (recovered == nullptr) return;
  CHECK(recovered->lastDurableGeneration == generation);
  CHECK(recovered->selectedSourceStrategyId == "recovery-singer");
  CHECK(recovered->operators.size() == 2U);
  CHECK(recovered->takes.size() == 1U);
  CHECK(reopened.app->productionQueues().markerReview == 1U);
  CHECK(reopened.app->productionQueues().missing == rows.size() - 1U);
}

// The whole signed-bank route through Studio's own surface: record the starter inventory, qualify the
// source, register a reviewer, capture and accept every unit, publish, sign, install, and hand the
// installed bank to the song editor as a song. The editor half then opens that song in a fresh
// standalone session and exports what it renders, so the export is the installed bank actually singing.
TEST_CASE("Voicebank Studio takes reviewed recordings to an installed bank the song editor sings with") {
  const auto root = test::support::temporaryDirectory("studio-app-bank-journey");
  auto microphone = std::make_shared<MicrophoneScript>();
  StudioHarness studio{root, microphone};
  auto& dialogs = *studio.dialogs;
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);

  // A vowel starter workspace keeps this journey to ten recording rows at two pitches.
  const auto workspace = root / "starter-workspace";
  dialogs.newWorkspaces = {platform::IFileDialog::NewProducerWorkspaceInput{
      .destination = workspace, .projectId = "bank-journey", .producerId = "producer",
      .inventory = platform::IFileDialog::NewProducerWorkspaceInput::Inventory::JapaneseVowelStarter}};
  CHECK(studio.activate("create-producer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject() != nullptr; }));
  const auto* project = studio.app->productionProject();
  CHECK(project != nullptr);
  if (project == nullptr) return;
  const auto rows = project->unitAssignments;
  CHECK(!rows.empty());
  if (rows.empty()) return;
  const auto selectedStyle = rows.front().style;

  // The source license and its explicit declaration, then the reviewer, exactly as the producer does.
  const auto license = root / "bank-journey-license.txt";
  const std::string licenseText =
      "Harness bank journey consent: recording, transformation, bank redistribution and commercial renders.";
  CHECK(core::durableAtomicWriteTextNew(license, licenseText).hasValue());
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] { const auto control = studio.node("source-license"); return control && control->enabled; }));
  dialogs.paths[platform::FileDialogPurpose::SourceLicenseEvidence] = {license};
  CHECK(studio.activate("source-license").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("source-register"); return control && control->enabled; }));
  dialogs.sourceRegistrations = {platform::SourceRegistrationInput{
      .id = "bank-journey-singer", .kind = "human", .rights = "pass",
      .permissions = {"yes", "yes", "yes", "yes"}}};
  CHECK(studio.activate("source-register").hasValue());
  CHECK(studio.settle([&] {
    return studio.app->productionProject()->selectedSourceStrategyId == "bank-journey-singer";
  }));
  dialogs.reviewerRegistrations = {std::string{"listener"}};
  CHECK(studio.activate("register-reviewer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject()->operators.size() == 2U; }));
  CHECK(studio.settle([&] { const auto back = studio.node("back"); return back && back->enabled; }));
  CHECK(studio.activate("back").hasValue());

  // Every row is recorded from the scripted microphone at its own pitch layer.
  for (std::size_t row = 0U; row < rows.size(); ++row) {
    CHECK(studio.settle([&] {
      const auto button = studio.node("record");
      return button && button->enabled && button->id.ends_with("." + std::to_string(row) + ".record");
    }));
    CHECK(studio.activate("record").hasValue());
    CHECK(microphone->running);
    microphone->sing(midiHz(rows[row].pitchLayer), 0.3);
    CHECK(studio.activate("record").hasValue());
    CHECK(studio.settle([&] {
      const auto button = studio.node("record");
      return button && button->name == "Record a take for the selected row";
    }));
    if (row + 1U < rows.size()) {
      studio.key(NativeKey::Down, {});
      CHECK(studio.settle([&] {
        const auto button = studio.node("record");
        return button && button->id.ends_with("." + std::to_string(row + 1U) + ".record");
      }));
    }
  }
  CHECK(studio.app->productionQueues().markerReview == rows.size());

  // Source evidence and an independent source-quality decision: source decisions never approve units.
  const auto qualityEvidence = root / "bank-journey-source-quality.txt";
  CHECK(core::durableAtomicWriteTextNew(qualityEvidence,
      "Harness source coverage and listening assessed by the reviewer for this journey").hasValue());
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] { const auto control = studio.node("source-evidence"); return control && control->enabled; }));
  dialogs.paths[platform::FileDialogPurpose::SourceQualityEvidence] = {qualityEvidence};
  CHECK(studio.activate("source-evidence").hasValue());
  CHECK(studio.settle([&] { const auto control = studio.node("source-decision"); return control && control->enabled; }));
  dialogs.sourceQualityDecisions = {platform::SourceQualityDecisionInput{
      .id = "bank-journey-quality", .reviewerId = "listener", .coverage = "pass", .listening = "pass"}};
  CHECK(studio.activate("source-decision").hasValue());
  CHECK(studio.settle([&] {
    const auto* current = studio.app->productionProject();
    return current && current->unitAssignments.front().state == voicebank_production::UnitQueueState::MarkerReview &&
        studio.value("status").find("SOURCE QUALITY RECORDED") != std::string::npos;
  }));
  // The draft gives every row a manifest so each unit can be captured, reviewed and accepted.
  dialogs.sampleDraftIdentities = {platform::SampleManifestDraftIdentityInput{
      .id = "bank-journey", .version = "1.0.0", .displayName = "Bank Journey",
      .language = "ja", .style = selectedStyle}};
  const auto draftRoot = root / "bank-journey-draft";
  dialogs.paths[platform::FileDialogPurpose::CreateSampleManifestDraft] = {draftRoot};
  CHECK(studio.activate("create-draft").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("DRAFT COMMITTED / OPENED") != std::string::npos;
  }));

  for (std::size_t index = 0U; index < rows.size(); ++index) {
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
    if (index + 1U < rows.size()) {
      CHECK(studio.activate("next-unit").hasValue());
      CHECK(studio.settle([&] {
        return studio.value("status").find("REVIEW COMMITTED") == std::string::npos ||
            studio.node("capture").has_value();
      }));
    }
  }

  // Publication, signing, installation and the song hand-off are four separate explicit steps.
  const auto candidate = root / "bank-candidate";
  dialogs.paths[platform::FileDialogPurpose::PublishSampleCandidate] = {candidate};
  CHECK(studio.activate("publish").hasValue());
  CHECK(studio.settle([&] {
    return studio.value("status").find("ENGINEERING CANDIDATE COMMITTED") != std::string::npos;
  }));
  CHECK(studio.settle([&] { const auto control = studio.node("sign-bank"); return control && control->enabled; }));
  studio.snapshot("bank-candidate-published-1100x720", 1100.0, 720.0);
  // At the smallest window the whole signed-bank row stays on screen and reachable.
  studio.snapshot("bank-candidate-published-720x520", 720.0, 520.0);
  {
    const auto narrow = studio.node("sign-bank");
    CHECK(narrow.has_value() && narrow->enabled);
    CHECK(narrow && narrow->bounds.x >= 0.0 && narrow->bounds.right() <= 720.0 + 1e-6);
    CHECK(narrow && narrow->bounds.bottom() <= 520.0 + 1e-6);
  }

  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto keyPath = root / "keys" / "bank-signer.json";
  std::filesystem::create_directories(keyPath.parent_path());
  CHECK(distribution::savePrivateKey(key.value(), keyPath).hasValue());
  const auto package = root / "out" / "bank-journey.seambank";
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
  const auto installedContentHash = installed->contentHash;

  // Cancelling the song location writes nothing and launches nothing.
  CHECK(studio.settle([&] {
    const auto control = studio.node("open-bank-in-song-editor"); return control && control->enabled;
  }));
  CHECK(studio.activate("open-bank-in-song-editor").hasValue());
  CHECK(studio.launches->empty());

  const auto songs = root / "songs";
  std::filesystem::create_directories(songs);
  const auto song = songs / "Bank Journey Song.seam";
  dialogs.paths[platform::FileDialogPurpose::SaveProject] = {song};
  CHECK(studio.activate("open-bank-in-song-editor").hasValue());
  CHECK(studio.launches->size() == 1U);
  CHECK(std::filesystem::exists(song));
  CHECK(studio.value("status").starts_with("OPENED IN PROJECT SEAM / " + selectedStyle));

  // The editor half: a fresh standalone session sees only the installed bank, opens the handed-off
  // song with no missing-bank diagnostic, sings through it and exports non-silent audio.
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
  CHECK(std::find_if(controller.value()->voicebankCards().begin(), controller.value()->voicebankCards().end(),
      [&](const auto& card) { return card.contentHash == installedContentHash; }) !=
      controller.value()->voicebankCards().end());
  editorResponses->responses.push_back(song);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenProject).hasValue());
  auto& runtime = session.value()->runtime();
  for (const auto& diagnostic : runtime.diagnostics())
    CHECK(diagnostic.code != "BANK_MISSING");
  const auto* track = runtime.document().session().project().findVocalTrack(runtime.selectedTrack());
  CHECK(track != nullptr);
  if (track == nullptr) return;
  CHECK(track->voicebank.contentHash == installedContentHash);
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

TEST_CASE("Voicebank Studio records and imports takes through its own actions; refused or lost input stays actionable") {
  const auto root = test::support::temporaryDirectory("studio-app-recording");
  auto microphone = std::make_shared<MicrophoneScript>();
  StudioHarness studio{root, microphone};
  auto& dialogs = *studio.dialogs;
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);

  // Nothing is open, so a take would have nowhere to go: Record refuses without opening the microphone.
  const auto nowhere = studio.app->startRecording();
  CHECK(!nowhere.hasValue());
  if (!nowhere) CHECK(nowhere.error().code == core::ErrorCode::InvalidState);
  CHECK(microphone->opens == 0U);

  // A new creator starts from the Designer home by creating a producer folder.
  const auto workspace = root / "voice-workspace";
  dialogs.newWorkspaces = {platform::IFileDialog::NewProducerWorkspaceInput{
      .destination = workspace, .projectId = "harness-voice", .producerId = "producer"}};
  CHECK(studio.activate("create-producer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject() != nullptr; }));
  if (studio.app->productionProject() == nullptr) return;
  const auto rows = studio.app->productionProject()->unitAssignments;
  CHECK(rows.size() >= 2U);
  if (rows.size() < 2U) return;
  CHECK(studio.app->productionProject()->takes.empty());
  const auto missing = studio.app->productionQueues().missing;
  CHECK(missing == rows.size());

  // Takes need an authorized source: register one in sample review from a license document.
  const auto license = root / "harness-singer-license.txt";
  const std::string licenseText =
      "Harness singer consent: recording, transformation, bank redistribution and commercial renders.";
  CHECK(core::durableAtomicWriteTextNew(license, licenseText).hasValue());
  studio.key(NativeKey::Q, {});
  CHECK(studio.settle([&] {
    const auto control = studio.node("source-license");
    return control && control->enabled;
  }));
  dialogs.paths[platform::FileDialogPurpose::SourceLicenseEvidence] = {license};
  CHECK(studio.activate("source-license").hasValue());
  CHECK(studio.settle([&] {
    const auto control = studio.node("source-register");
    return control && control->enabled;
  }));
  dialogs.sourceRegistrations = {platform::SourceRegistrationInput{
      .id = "harness-singer", .kind = "human", .rights = "pass",
      .permissions = {"yes", "yes", "yes", "yes"}}};
  CHECK(studio.activate("source-register").hasValue());
  CHECK(studio.settle([&] {
    return studio.app->productionProject()->selectedSourceStrategyId == "harness-singer";
  }));
  CHECK(dialogs.sourceRegistrationSummaries.size() == 1U);
  // The producer declares a reviewer before any review. An identity keeps its one role, so the
  // producer's own ID is refused and the workspace is unchanged until a new ID is entered.
  dialogs.reviewerRegistrations = {std::string{"producer"}, std::string{"listener"}};
  CHECK(studio.activate("register-reviewer").hasValue());
  CHECK(studio.settle([&] { return studio.value("status").find("already registered") != std::string::npos; }));
  CHECK(studio.app->productionProject()->operators.size() == 1U);
  CHECK(studio.activate("register-reviewer").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionProject()->operators.size() == 2U; }));
  CHECK(studio.app->productionProject()->operators.back().operatorId == "listener");
  CHECK(studio.app->productionProject()->operators.back().role == "REVIEWER");
  CHECK(dialogs.reviewerRegistrationSummaries.size() == 2U);
  if (dialogs.reviewerRegistrationSummaries.size() == 2U)
    CHECK(dialogs.reviewerRegistrationSummaries.back().find("REGISTERED REVIEWERS none") != std::string::npos);
  CHECK(studio.value("status").starts_with("REVIEWER REGISTERED / listener"));
  studio.snapshot("sample-review-reviewer-registered-720x520", 720.0, 520.0);
  studio.snapshot("sample-review-reviewer-registered-1100x720", 1100.0, 720.0);
  CHECK(studio.settle([&] {
    const auto back = studio.node("back");
    return back && back->enabled;
  }));
  CHECK(studio.activate("back").hasValue());

  // Microphone access refused: nothing is captured or written, the reason stays readable after the
  // next key press, and Record stays available for the retry.
  microphone->denial =
      "Microphone access was denied. In System Settings > Privacy & Security > Microphone, "
      "allow SEAM Voicebank Studio, then retry Record.";
  {
    const auto button = studio.node("record");
    CHECK(button.has_value() && button->enabled);
    CHECK(button && button->name == "Record a take for the selected row");
  }
  CHECK(studio.value("microphone").starts_with("Not capturing"));
  const auto denied = studio.activate("record");
  CHECK(!denied.hasValue());
  CHECK(microphone->opens == 1U);
  const auto deniedStatus = "NO TAKE STARTED / " + microphone->denial;
  CHECK(studio.value("status") == deniedStatus);
  studio.key(NativeKey::Left, {});
  CHECK(studio.value("status") == deniedStatus);
  {
    const auto button = studio.node("record");
    CHECK(button.has_value() && button->enabled);
  }
  CHECK(!studio.node("discard-recording").has_value());
  CHECK(studio.app->lastRecording().empty());
  CHECK(studio.app->productionProject()->takes.empty());

  // The interface disappears mid-take: the capture ends, nothing is published, Record can retry.
  microphone->denial.clear();
  CHECK(studio.activate("record").hasValue());
  CHECK(microphone->opens == 2U);
  CHECK(microphone->running);
  CHECK(studio.value("microphone") == "Capturing from Harness microphone / harness-mic");
  {
    const auto button = studio.node("record");
    CHECK(button && button->enabled && button->name == "Stop recording and publish the take");
  }
  // The capture pins its row: importing another WAV meanwhile is refused.
  const auto importWhileCapturing = studio.activate("import-wav");
  CHECK(!importWhileCapturing.hasValue());
  if (!importWhileCapturing) CHECK(importWhileCapturing.error().code == core::ErrorCode::Conflict);
  microphone->sing(midiHz(rows[0].pitchLayer), 0.2);
  microphone->running = false;
  CHECK(studio.value("status") ==
        "NO TAKE RECORDED / Recording input stopped unexpectedly; no take was published, retry Record");
  CHECK(studio.value("microphone").starts_with("Not capturing"));
  {
    const auto button = studio.node("record");
    CHECK(button && button->enabled && button->name == "Record a take for the selected row");
  }
  CHECK(!studio.node("discard-recording").has_value());
  CHECK(studio.app->productionQueues().missing == missing);
  CHECK(studio.app->lastRecording().empty());
  studio.snapshot("producer-microphone-lost-1100x720", 1100.0, 720.0);

  // The license evidence changes after registration, so the first publication is refused. The capture
  // and its WAV are retained; the creator can retry or discard rather than sing the take again.
  CHECK(core::durableAtomicWriteText(license, "Edited after registration").hasValue());
  CHECK(studio.activate("record").hasValue());
  microphone->sing(midiHz(rows[0].pitchLayer), 0.5);
  studio.click(99.0, 89.0);  // The pointer reaches the same button: R STOP + PUBLISH.
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return button && button->name == "Retry publishing the recorded take";
  }));
  {
    const auto retry = studio.node("record");
    CHECK(retry && retry->enabled);
    const auto discard = studio.node("discard-recording");
    CHECK(discard && discard->enabled);
  }
  CHECK(!studio.value("status").empty());
  studio.snapshot("producer-publication-retry-720x520", 720.0, 520.0);
  const auto recorded = studio.app->lastRecording();
  CHECK(!recorded.empty() && std::filesystem::is_regular_file(recorded));
  CHECK(studio.app->lastRecordedFrames() == 24000U);
  CHECK(studio.app->productionProject()->takes.empty());
  CHECK(!studio.activate("import-wav").hasValue());

  // Restoring the evidence and pressing R publishes the retained capture without a second WAV.
  CHECK(core::durableAtomicWriteText(license, licenseText).hasValue());
  studio.key(NativeKey::R, {});
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return studio.app->productionQueues().markerReview == 1U && button &&
           button->name == "Record a take for the selected row";
  }));
  CHECK(studio.app->lastRecording() == recorded);
  CHECK(wavFiles(recorded.parent_path()) == 1U);
  const auto recordedDigest = core::sha256File(recorded);
  CHECK(recordedDigest.hasValue());
  {
    const auto* project = studio.app->productionProject();
    CHECK(project->takes.size() == 1U);
    CHECK(project->reviews.empty());
    if (project->takes.size() == 1U && recordedDigest) {
      const auto& take = project->takes.front();
      CHECK(take.takeId == rows[0].plannedTakeId);
      CHECK(take.state == voicebank_production::UnitQueueState::MarkerReview);
      CHECK(take.rawAssetSha256 == recordedDigest.value());
      const auto binding = std::find_if(project->sourceBindings.begin(), project->sourceBindings.end(),
          [&](const auto& candidate) { return candidate.id == take.sourceBindingId; });
      CHECK(binding != project->sourceBindings.end());
      if (binding != project->sourceBindings.end()) CHECK(binding->strategy.id == "harness-singer");
    }
    CHECK(project->unitAssignments[0].takeId == rows[0].plannedTakeId);
    CHECK(!project->unitAssignments[0].markerReviewed);
    CHECK(!project->unitAssignments[0].pitchReviewed);
  }
  CHECK(studio.app->productionQueues().missing == missing - 1U);
  const auto recordedAudio = voicebank::readWav(recorded);
  CHECK(recordedAudio.hasValue());
  if (recordedAudio) {
    CHECK(recordedAudio.value().sampleRate == 48000U);
    CHECK(recordedAudio.value().channels == 1U);
    CHECK(recordedAudio.value().frameCount() == 24000U);
  }

  // Next row: when publication fails again the creator can discard the capture; the saved WAV stays.
  studio.key(NativeKey::Down, {});
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return button && button->id.ends_with(".1.record");
  }));
  CHECK(core::durableAtomicWriteText(license, "Edited again").hasValue());
  CHECK(studio.activate("record").hasValue());
  microphone->sing(midiHz(rows[1].pitchLayer), 0.3);
  // Stopping is reachable through accessibility while every other target is locked by the capture.
  CHECK(!studio.activate("import-wav").hasValue());
  CHECK(studio.activate("record").hasValue());
  CHECK(studio.settle([&] {
    const auto button = studio.node("record");
    return button && button->name == "Retry publishing the recorded take";
  }));
  const auto kept = studio.app->lastRecording();
  CHECK(kept != recorded && std::filesystem::is_regular_file(kept));
  CHECK(studio.activate("discard-recording").hasValue());
  CHECK(studio.value("status") == "Recording capture discarded; saved file kept at " + kept.string());
  CHECK(std::filesystem::is_regular_file(kept));
  {
    const auto button = studio.node("record");
    CHECK(button && button->enabled && button->name == "Record a take for the selected row");
  }
  CHECK(!studio.node("discard-recording").has_value());
  CHECK(studio.app->productionProject()->takes.size() == 1U);
  CHECK(core::durableAtomicWriteText(license, licenseText).hasValue());

  // The same row takes an existing WAV through the import action and lands in the same review queue.
  const auto external = root / "external-take.wav";
  const auto tone = test::support::sineWave(48000U, midiHz(rows[1].pitchLayer), 0.4, 0.25F);
  CHECK(voicebank::writeWav(external, {.sampleRate = 48000U, .channels = 1U,
                                       .sampleFormat = voicebank::WavSampleFormat::Pcm24},
                            tone)
            .hasValue());
  dialogs.paths[platform::FileDialogPurpose::ImportAudio] = {external};
  CHECK(studio.activate("import-wav").hasValue());
  CHECK(studio.settle([&] { return studio.app->productionQueues().markerReview == 2U; }));
  const auto externalDigest = core::sha256File(external);
  CHECK(externalDigest.hasValue());
  {
    const auto* project = studio.app->productionProject();
    CHECK(project->takes.size() == 2U);
    const auto imported = std::find_if(project->takes.begin(), project->takes.end(),
        [&](const auto& take) { return take.takeId == rows[1].plannedTakeId; });
    CHECK(imported != project->takes.end());
    if (imported != project->takes.end() && externalDigest) {
      CHECK(imported->rawAssetSha256 == externalDigest.value());
      CHECK(imported->state == voicebank_production::UnitQueueState::MarkerReview);
      CHECK(!imported->sourceBindingId.empty());
    }
    CHECK(project->reviews.empty());
  }

  // Both takes are durable: a fresh Studio opens the producer folder from the Designer home, and
  // opening it does not touch the microphone.
  const auto generation = studio.app->productionProject()->lastDurableGeneration;
  studio.app.reset();
  StudioHarness reopened{root, microphone};
  CHECK(reopened.app->open(options).hasValue());
  reopened.resize(1100.0, 720.0);
  reopened.dialogs->workspaces = {platform::IFileDialog::ProductionWorkspaceInput{
      .root = workspace, .operatorId = "producer", .producerFolder = true}};
  CHECK(reopened.activate("back").hasValue());
  CHECK(reopened.settle([&] { return reopened.app->productionProject() != nullptr; }));
  if (reopened.app->productionProject() == nullptr) return;
  CHECK(reopened.app->productionProject()->lastDurableGeneration == generation);
  CHECK(reopened.app->productionProject()->selectedSourceStrategyId == "harness-singer");
  CHECK(reopened.app->productionProject()->operators.size() == 2U);
  CHECK(reopened.app->productionQueues().markerReview == 2U);
  CHECK(reopened.app->productionQueues().missing == missing - 2U);
  CHECK(microphone->opens == 4U);
}

TEST_CASE("Voicebank Studio names the audition that plays, and one that cannot replace it changes neither the name nor the answer") {
  const auto root = test::support::temporaryDirectory("studio-audition-stop-refused");
  std::vector<std::shared_ptr<OutputScript>> outputs;
  bool refuseToOpen = false;
  StudioHarness studio{root, nullptr, [&] {
    auto script = std::make_shared<OutputScript>();
    script->failOpen = refuseToOpen;
    outputs.push_back(script);
    return std::make_unique<ScriptedOutput>(script);
  }};
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);
  CHECK(studio.activate("new").hasValue());
  // The starter's vowel is rendered and kept as A; then the voice changes, and its vowel is B.
  studio.key(NativeKey::Space, {});
  CHECK(studio.settle([&] { return studio.value("audition-state") == "Vowel ready"; }));
  studio.key(NativeKey::B, {.command = true});
  CHECK(studio.node("reference-state").has_value());
  const auto original = studio.value("control.0");
  CHECK(studio.setValue("control.0", original == "0.550000" ? "0.65" : "0.55").hasValue());
  CHECK(studio.value("control.0") != original);
  studio.key(NativeKey::Space, {});
  CHECK(studio.settle([&] { return studio.value("audition-state") == "Vowel ready"; }));
  CHECK(outputs.empty());

  // A device that will not open: nothing plays, so nothing is named, and the creator is told.
  refuseToOpen = true;
  studio.key(NativeKey::Space, {});
  CHECK(outputs.size() == 1U);
  if (outputs.size() != 1U) return;
  CHECK(outputs.front()->opens == 1U);
  CHECK(outputs.front()->starts == 0U);
  CHECK(outputs.front()->destroyed);
  CHECK(studio.app->lastError() == "scripted audition output will not open");
  CHECK(studio.value("audition-state") == "Vowel ready");
  refuseToOpen = false;

  // The same key with a device that opens: B plays and is named, and the error is gone.
  studio.key(NativeKey::Space, {});
  CHECK(studio.value("audition-state") == "CURRENT B / NOT APPROVED");
  CHECK(studio.app->lastError().empty());
  CHECK(outputs.size() == 2U);
  if (outputs.size() != 2U) return;
  const auto playingB = outputs[1];
  CHECK(playingB->running);

  // The platform does not say that B's device stopped. Asking for A is refused: B is still playing
  // and is still B, the device made for A never opened, and the action answers with the failure.
  playingB->failStop = true;
  const auto refusedA = studio.activate("play-reference");
  CHECK(!refusedA.hasValue());
  if (!refusedA.hasValue()) CHECK(refusedA.error().message == "scripted audition output did not stop");
  CHECK(studio.value("audition-state") == "CURRENT B / NOT APPROVED");
  CHECK(studio.app->lastError() == "scripted audition output did not stop");
  CHECK(playingB->running);
  CHECK(!playingB->destroyed);
  CHECK(outputs.size() == 3U);
  if (outputs.size() != 3U) return;
  CHECK(outputs[2]->opens == 0U);
  CHECK(outputs[2]->destroyed);

  // Space is a stop while something plays, and it asks again. A device that still does not stop
  // keeps its name and the error; one that stops lets go of both.
  studio.key(NativeKey::Space, {});
  CHECK(studio.value("audition-state") == "CURRENT B / NOT APPROVED");
  CHECK(studio.app->lastError() == "scripted audition output did not stop");
  CHECK(playingB->running);
  playingB->failStop = false;
  studio.key(NativeKey::Space, {});
  CHECK(!playingB->running);
  CHECK(playingB->destroyed);
  CHECK(studio.value("audition-state") == "Vowel ready");
  CHECK(studio.app->lastError().empty());

  // With nothing playing, A is asked for and named. Then the same refusal the other way round:
  // asking for B while A plays leaves A named, and the answer is the failure.
  CHECK(studio.activate("play-reference").hasValue());
  CHECK(studio.value("audition-state") == "REFERENCE A / NOT APPROVED");
  const auto playingA = outputs.back();
  CHECK(playingA->running);
  playingA->failStop = true;
  const auto refusedB = studio.activate("play-current");
  CHECK(!refusedB.hasValue());
  CHECK(studio.value("audition-state") == "REFERENCE A / NOT APPROVED");
  CHECK(studio.app->lastError() == "scripted audition output did not stop");
  CHECK(playingA->running);
  CHECK(!playingA->destroyed);
  // It goes when the platform lets it, and B replaces it.
  playingA->failStop = false;
  CHECK(studio.activate("play-current").hasValue());
  CHECK(studio.value("audition-state") == "CURRENT B / NOT APPROVED");
  CHECK(studio.app->lastError().empty());
  CHECK(playingA->destroyed);
  CHECK(outputs.back()->running);
}

namespace {

// The first accessible element, in this frame, whose id contains idPart and whose name contains
// namePart. The Designer's source controls carry a per-source suffix in their ids, so they cannot be
// found by the id's end as the fixed ones are.
std::optional<SemanticNode> findContaining(StudioHarness& studio, std::string_view idPart,
                                           std::string_view namePart = {}) {
  const auto search = [&](const SemanticNode& node, const auto& self) -> const SemanticNode* {
    if (node.id.find(idPart) != std::string::npos && node.name.find(namePart) != std::string::npos)
      return &node;
    for (const auto& child : node.children)
      if (const auto* found = self(child, self)) return found;
    return nullptr;
  };
  const auto* found = search(studio.frame().root(), search);
  if (found == nullptr) return std::nullopt;
  return *found;
}

// One of the Designer's source auditions (a plosive burst, a frication noise, an articulation tail),
// asked for through its accessible Play control while reference A plays on an output that will not
// say that it has stopped. The source's start is refused, so the answer is the failure, A stays
// named and running, and the output made for the source never opens. When the output lets go, the
// same control plays the source.
void checkSourceAuditionAfterRefusedStop(std::string_view tag, std::string_view controlName,
                                         std::string_view renderPart, std::string_view playPart) {
  const auto root = test::support::temporaryDirectory("studio-source-audition-" + std::string{tag});
  std::vector<std::shared_ptr<OutputScript>> outputs;
  StudioHarness studio{root, nullptr, [&] {
    auto script = std::make_shared<OutputScript>();
    outputs.push_back(script);
    return std::make_unique<ScriptedOutput>(script);
  }};
  voicebank_studio_native::Options options;
  options.startDesigner = true;
  CHECK(studio.app->open(options).hasValue());
  studio.resize(1100.0, 720.0);
  CHECK(studio.activate("new").hasValue());
  studio.key(NativeKey::Space, {});
  CHECK(studio.settle([&] { return studio.value("audition-state") == "Vowel ready"; }));
  CHECK(studio.activate("pin-reference").hasValue());

  // Only the keyboard focus moves: the control's page is reached through the Designer's own Next
  // control, and neither the recipe nor the audition pose changes.
  auto control = findContaining(studio, ".control.", controlName);
  for (int page = 0; !control && page < 100; ++page) {
    const auto next = studio.node("next");
    if (!next || !next->enabled) break;
    CHECK(studio.activate("next").hasValue());
    control = findContaining(studio, ".control.", controlName);
  }
  CHECK(control.has_value());
  if (!control) return;
  CHECK(studio.app->dispatchAccessibility(control->id, SemanticAction::SetFocus).hasValue());
  const auto render = findContaining(studio, renderPart);
  CHECK(render.has_value());
  if (!render) return;
  CHECK(render->enabled);
  CHECK(studio.app->dispatchAccessibility(render->id, SemanticAction::Activate).hasValue());
  CHECK(studio.settle([&] {
    const auto play = findContaining(studio, playPart);
    return play && play->enabled;
  }));

  // Reference A is retained on an output that refuses to stop.
  CHECK(studio.activate("play-reference").hasValue());
  CHECK(studio.value("audition-state") == "REFERENCE A / NOT APPROVED");
  CHECK(!outputs.empty());
  if (outputs.empty()) return;
  const auto playingA = outputs.back();
  playingA->failStop = true;
  const auto source = findContaining(studio, playPart);
  CHECK(source.has_value());
  if (!source || !source->enabled) return;
  const auto refused = studio.app->dispatchAccessibility(source->id, SemanticAction::Activate);
  CHECK(!refused.hasValue());
  if (!refused.hasValue()) CHECK(refused.error().message == "scripted audition output did not stop");
  CHECK(studio.value("audition-state") == "REFERENCE A / NOT APPROVED");
  CHECK(studio.app->lastError() == "scripted audition output did not stop");
  CHECK(playingA->running);
  CHECK(!playingA->destroyed);
  CHECK(outputs.back()->opens == 0U);
  CHECK(outputs.back()->destroyed);

  // The output lets go: the same control plays the source, and A is gone.
  playingA->failStop = false;
  const auto retry = findContaining(studio, playPart);
  CHECK(retry.has_value());
  if (!retry || !retry->enabled) return;
  CHECK(studio.app->dispatchAccessibility(retry->id, SemanticAction::Activate).hasValue());
  CHECK(playingA->destroyed);
  CHECK(outputs.back()->running);
  CHECK(studio.value("audition-state") != "REFERENCE A / NOT APPROVED");
  CHECK(studio.app->lastError().empty());
}

}  // namespace

TEST_CASE("Voicebank Studio's frication audition keeps the retained audition when the output will not stop") {
  checkSourceAuditionAfterRefusedStop("frication", "NOISE CENTER", ".render-frication.",
                                      ".play-frication.");
}

TEST_CASE("Voicebank Studio's plosive audition keeps the retained audition when the output will not stop") {
  checkSourceAuditionAfterRefusedStop("plosive", "BURST CENTER", ".render-plosive.",
                                      ".play-plosive.");
}

TEST_CASE("Voicebank Studio's articulation audition keeps the retained audition when the output will not stop") {
  checkSourceAuditionAfterRefusedStop("articulation", "TAIL CENTER", ".render-articulation.",
                                      ".play-articulation.");
}
