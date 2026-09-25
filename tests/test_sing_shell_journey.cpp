// A headless journey through the SING shell of the production standalone: pick a voice, write a
// phrase, save it, reopen it in a fresh app, and export it, each step through the same commands the
// window's pointer, keys and accessibility reach. This is command, persistence and export evidence
// with the development fixture bank. It is not Finder, native window or FL Studio host evidence,
// and it says nothing about vocal quality or a production bank.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/native_editor_app.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

using namespace std::chrono_literals;
using seam::native_ui::KeyEvent;
using seam::native_ui::NativeKey;
using seam::native_ui::PointerButton;
using seam::native_ui::PointerEvent;
using seam::native_ui::SemanticAction;
using seam::native_ui::SemanticNode;

class JourneyDialog final : public seam::platform::IFileDialog {
public:
  explicit JourneyDialog(std::vector<std::filesystem::path> answers) : answers_(std::move(answers)) {}
  seam::core::Result<std::optional<std::filesystem::path>> choose(
      const seam::platform::FileDialogRequest& request) override {
    purposes.push_back(request.purpose);
    if (answers_.empty()) return std::optional<std::filesystem::path>{};
    auto answer = answers_.front();
    answers_.erase(answers_.begin());
    return std::optional<std::filesystem::path>{answer};
  }
  std::vector<seam::platform::FileDialogPurpose> purposes;

private:
  std::vector<std::filesystem::path> answers_;
};

class KeepPrompt final : public seam::platform::IUnsavedChangesPrompt {
public:
  seam::core::Result<seam::platform::UnsavedDecision> choose(std::string_view) override {
    return seam::platform::UnsavedDecision::Cancel;
  }
};

struct JourneyApp final {
  std::unique_ptr<seam::standalone::NativeEditorApp> app;
  JourneyDialog* dialog{nullptr};
  seam::native_ui::PixelSurface surface{1600U, 900U};

  JourneyApp(const std::filesystem::path& root, std::vector<std::filesystem::path> answers) {
    seam::standalone::NativeEditorAppConfig config;
    config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
    config.applicationSupportRoot = root;
    config.forceThreadedAudio = true;
    config.allowDevelopmentVoicebanks = true;
    config.authoring.bindFirstAvailableVoicebank = true;
    config.authoring.allowDevelopmentVoicebanks = true;
    config.authoring.sampleRate = 48000U;
    config.authoring.outputChannels = 2U;
    config.authoring.voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
        .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        .kind = seam::voicebank::VoicebankRootKind::Development,
    }};
    config.designShell = true;
    config.designPreferences = seam::native_ui::design::DesignPreferences{
        .mode = seam::native_ui::design::DesignMode::Scene};
    auto owned = std::make_unique<JourneyDialog>(std::move(answers));
    dialog = owned.get();
    auto shared = std::make_shared<std::unique_ptr<JourneyDialog>>(std::move(owned));
    config.fileDialogFactory = [shared]() -> std::unique_ptr<seam::platform::IFileDialog> {
      return std::move(*shared);
    };
    config.unsavedChangesPromptFactory = [] { return std::make_unique<KeepPrompt>(); };
    auto created = seam::standalone::NativeEditorApp::create(std::move(config));
    CHECK(created);
    if (created) app = std::move(created).value();
  }

  void paint() {
    seam::native_ui::RasterCanvas canvas{surface, 1.0};
    app->resized(1600.0, 900.0, 1.0);
    app->paint(canvas);
  }
  const SemanticNode& root() const { return app->accessibilityTree()->root(); }
  bool shellPresents() const { return root().id == "shell"; }
  const SemanticNode* find(std::string_view id) const {
    const auto search = [id](const SemanticNode& node, const auto& self) -> const SemanticNode* {
      if (node.id == id) return &node;
      for (const auto& child : node.children)
        if (const auto* found = self(child, self); found != nullptr) return found;
      return nullptr;
    };
    return search(root(), search);
  }
  void doubleClick(seam::ui::Point p) {
    const PointerEvent down{.position = p, .button = PointerButton::Left, .modifiers = {},
                            .clickCount = 2};
    app->pointerDown(down);
    app->pointerUp(down);
  }
  seam::domain::VocalRegion* region() {
    return app->authoring().runtime().document().session().project().findRegion(
        app->authoring().regionId());
  }
  const seam::domain::VocalTrack* track() {
    const auto& project = app->authoring().runtime().document().session().project();
    return project.vocalTracks().empty() ? nullptr : &project.vocalTracks().front();
  }
};

std::string lyricOf(seam::domain::VocalRegion& region, const seam::domain::Note& note) {
  const auto* lyric = region.findLyric(note.lyricTokenId);
  return lyric == nullptr ? std::string{} : seam::domain::toUtf8(lyric->surface);
}

}  // namespace

TEST_CASE("sing shell journey: voice, phrase, save, reopen and export through real commands") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  const auto root = seam::test::support::temporaryDirectory("sing-shell-journey");
  const auto projectPath = root / "Journey.seam";
  const auto exportPath = root / "Journey Export";

  seam::domain::VoicebankReference voice;
  std::string authoredLyric;
  std::uint8_t authoredKey{0U};
  {
    JourneyApp first{root / "first", {projectPath}};
    CHECK(first.app != nullptr);
    if (first.app == nullptr) return;
    first.paint();
    CHECK(first.shellPresents());

    // 1. Voice: the VOICE tab opens the real voice browser; choosing a card selects that bank.
    CHECK(first.app->dispatchAccessibility("shell.workspace.voice", SemanticAction::Activate));
    CHECK(first.app->authoring().controller().voicebankBrowserVisible());
    first.paint();
    CHECK(!first.shellPresents());
    CHECK(first.find("voicebank.card.0") != nullptr);
    CHECK(first.app->dispatchAccessibility("voicebank.card.0", SemanticAction::Activate));
    CHECK(!first.app->authoring().controller().voicebankBrowserVisible());
    first.paint();
    CHECK(first.shellPresents());
    CHECK(first.track() != nullptr);
    if (first.track() == nullptr) return;
    voice = first.track()->voicebank;
    CHECK(!voice.id.empty());
    CHECK(!voice.contentHash.empty());

    // 2. Phrase: a double-click in the SING grid draws a note; a double-click on it edits the lyric.
    const auto* grid = first.find("timeline");
    CHECK(grid != nullptr);
    if (grid == nullptr) return;
    const seam::ui::Point spot{grid->bounds.x + 240.0, grid->bounds.y + grid->bounds.height * 0.5};
    CHECK(first.region() != nullptr && first.region()->notes.empty());
    first.doubleClick(spot);
    first.paint();
    CHECK(first.region()->notes.size() == 1U);
    if (first.region()->notes.size() != 1U) return;
    const auto notes = first.app->accessibilityTree()->materializeNotes(0U, 4U);
    CHECK(notes.size() == 1U);
    if (notes.empty()) return;
    const auto& noteBounds = notes.front().bounds;
    CHECK(noteBounds.width > 0.0);
    first.doubleClick({noteBounds.x + std::min(6.0, noteBounds.width * 0.5),
                       noteBounds.y + noteBounds.height * 0.5});
    CHECK(first.app->authoring().controller().textInputActive());
    first.app->textCommit(U"\u3053");
    first.paint();
    auto* region = first.region();
    authoredLyric = lyricOf(*region, region->notes.front());
    authoredKey = static_cast<std::uint8_t>(region->notes.front().midiKey);
    CHECK(authoredLyric == "\u3053");

    // 3. Save As (Command-Shift-S reaches the application command through the shell).
    first.app->keyDown(KeyEvent{.key = NativeKey::S,
                                .modifiers = {.shift = true, .command = true}});
    CHECK(std::filesystem::is_regular_file(projectPath));
    CHECK(!first.dialog->purposes.empty());
    first.paint();
    CHECK(!first.app->authoring().controller().sceneState().dirty);
    first.app->shutdownAudio();
  }

  // 4. Reopen in a fresh application: the singer identity and the authored phrase come back.
  JourneyApp second{root / "second", {exportPath}};
  CHECK(second.app != nullptr);
  if (second.app == nullptr) return;
  CHECK(second.app->openProject(projectPath));
  second.paint();
  CHECK(second.shellPresents());
  CHECK(second.track() != nullptr);
  if (second.track() == nullptr) return;
  CHECK(second.track()->voicebank.id == voice.id);
  CHECK(second.track()->voicebank.version == voice.version);
  CHECK(second.track()->voicebank.contentHash == voice.contentHash);
  auto* reopened = second.region();
  CHECK(reopened != nullptr && reopened->notes.size() == 1U);
  if (reopened == nullptr || reopened->notes.size() != 1U) return;
  CHECK(lyricOf(*reopened, reopened->notes.front()) == authoredLyric);
  CHECK(reopened->notes.front().midiKey == authoredKey);

  // 5. Export from the EXPORT workspace: it shows the real plan, then runs Export Set.
  CHECK(second.app->dispatchAccessibility("shell.workspace.export", SemanticAction::Activate));
  second.paint();
  const auto* panel = second.find("shell.export.panel");
  CHECK(panel != nullptr);
  if (panel != nullptr) {
    CHECK(panel->value.find("24-bit WAV") != std::string::npos);
    CHECK(panel->value.find("48 kHz") != std::string::npos);
    CHECK(panel->value.find("one stem per track") != std::string::npos);
  }
  // The score is covered: no note or timeline is published under the export panel.
  CHECK(second.find("timeline") == nullptr);
  CHECK(second.app->accessibilityTree()->virtualizedNoteCount() == 0U);
  CHECK(second.app->dispatchAccessibility("shell.export.run", SemanticAction::Activate));
  const auto deadline = std::chrono::steady_clock::now() + 90s;
  std::optional<seam::authoring::ExportResult> last;
  while (std::chrono::steady_clock::now() < deadline) {
    second.paint();
    last = second.app->authoring().controller().sceneState().lastExport;
    if (last.has_value()) break;
    std::this_thread::sleep_for(20ms);
  }
  if (!last.has_value()) {
    const auto progress = second.app->authoring().controller().sceneState().exportProgress;
    std::fprintf(stderr, "export progress state=%d files=%llu/%llu output=%s dialogs=%zu error=%s\n",
                 static_cast<int>(progress.state),
                 static_cast<unsigned long long>(progress.completedFiles),
                 static_cast<unsigned long long>(progress.totalFiles),
                 progress.currentOutput.c_str(), second.dialog->purposes.size(),
                 second.app->lastError().c_str());
  }
  CHECK(last.has_value());
  if (!last.has_value()) return;
  CHECK(last->state == seam::authoring::ExportState::Committed);
  CHECK(std::filesystem::is_regular_file(last->masterPath));
  CHECK(std::filesystem::is_regular_file(last->receiptPath));
  CHECK(last->files.size() >= 2U);  // the master and at least one stem
  const auto master = seam::voicebank::readWav(last->masterPath);
  CHECK(master);
  if (master) {
    CHECK(master.value().sampleRate == 48000U);
    CHECK(master.value().channels == 2U);
    double peak = 0.0;
    for (const auto sample : master.value().interleaved) peak = std::max(peak, std::abs(static_cast<double>(sample)));
    // The rendered phrase is audible: the master is not silence.
    CHECK(peak > 1e-3);
  }
  second.paint();
  const auto* status = second.find("shell.export.last");
  CHECK(status != nullptr && status->value.find("Written") != std::string::npos);
  second.app->shutdownAudio();
}

TEST_CASE("sing shell journey: a retained note cannot be edited through the host while EXPORT is up") {
  const auto root = seam::test::support::temporaryDirectory("journey-export-hidden-note");
  JourneyApp f{root, {}};
  CHECK(f.app != nullptr);
  if (f.app == nullptr) return;
  f.paint();
  CHECK(f.shellPresents());
  const auto* grid = f.find("timeline");
  CHECK(grid != nullptr);
  if (grid == nullptr) return;
  f.doubleClick({grid->bounds.x + 240.0, grid->bounds.y + grid->bounds.height * 0.5});
  f.paint();
  CHECK(f.region()->notes.size() == 1U);
  const auto id = "note." + f.region()->notes.front().id.toString();
  const auto lyric = lyricOf(*f.region(), f.region()->notes.front());
  CHECK(f.app->dispatchAccessibility("shell.workspace.export", SemanticAction::Activate));
  f.paint();
  CHECK(f.app->accessibilityTree()->virtualizedNoteCount() == 0U);
  // A screen reader or automation client that kept the old element gets a refusal, and no field
  // opens that a later commit could write into.
  const auto edit = f.app->dispatchAccessibility(id, SemanticAction::EditText);
  CHECK(!edit);
  CHECK(!f.app->authoring().controller().textInputActive());
  f.app->textCommit(U"HIDDEN");
  CHECK(!f.app->setAccessibilityValue(id, "HIDDEN"));
  CHECK(!f.app->dispatchAccessibility(id, SemanticAction::Activate));
  // Alt-Delete, the modified delete the note editor honours, does not reach the covered score.
  f.app->keyDown(KeyEvent{.key = NativeKey::Delete, .modifiers = {.alt = true}});
  f.app->keyDown(KeyEvent{.key = NativeKey::Delete});
  CHECK(f.region()->notes.size() == 1U);
  CHECK(lyricOf(*f.region(), f.region()->notes.front()) == lyric);
  // Escape brings the score back and the same id acts again.
  f.app->keyDown(KeyEvent{.key = NativeKey::Escape});
  f.paint();
  CHECK(f.find(id) != nullptr || f.app->accessibilityTree()->virtualizedNoteCount() == 1U);
  CHECK(f.app->dispatchAccessibility(id, SemanticAction::SetFocus));
  f.app->shutdownAudio();
}
