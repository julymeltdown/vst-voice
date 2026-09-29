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
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
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

struct DialogScript final {
  std::map<platform::FileDialogPurpose, std::vector<std::optional<std::filesystem::path>>> paths;
  std::vector<std::optional<platform::ProceduralSingerPublishInput>> publishInputs;
  std::vector<platform::FileDialogRequest> requests;
};

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
    if (script_->publishInputs.empty()) return std::optional<platform::ProceduralSingerPublishInput>{};
    auto next = script_->publishInputs.front();
    script_->publishInputs.erase(script_->publishInputs.begin());
    return next;
  }

private:
  std::shared_ptr<DialogScript> script_;
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

  explicit StudioHarness(std::filesystem::path root)
      : root_(std::move(root)), singers_(root_ / "singers"), editor_(root_ / "Project SEAM.app") {
    std::filesystem::create_directories(singers_);
    voicebank_studio_native::StudioPlatform hooks;
    hooks.fileDialog = [script = dialogs] { return std::make_unique<ScriptedDialog>(script); };
    hooks.audioDevice = [] { return platform::createThreadedAudioDevice(); };
    hooks.singerRoots = [singers = singers_] {
      return std::vector<distribution::ProceduralSearchRoot>{
          {singers, distribution::ProceduralRootKind::Installed}};
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

  [[nodiscard]] const std::filesystem::path& singers() const noexcept { return singers_; }
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
