// A headless journey through the SING shell of the production standalone: pick a voice, write a
// phrase, save it, reopen it in a fresh app, and export it, each step through the same commands the
// window's pointer, keys and accessibility reach. This is command, persistence and export evidence
// with the development fixture bank. It is not Finder, native window or FL Studio host evidence,
// and it says nothing about vocal quality or a production bank.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/list_entry_ids.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/native_editor_app.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
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

// A window that paints nothing and counts the frames the app asks it for, so a test can pump frames
// the way the native window does: when one was requested, and when the app's deadline has passed.
class FrameCounter final : public seam::native_ui::INativeWindow {
public:
  seam::core::Result<void> open(const seam::native_ui::NativeWindowConfig&,
                                seam::native_ui::INativeWindowClient&) override {
    return seam::core::success();
  }
  int run() override { return 0; }
  void requestRepaint() noexcept override { requests.fetch_add(1U); }
  void beginTextInput(const seam::native_ui::TextInputRequest&) override {}
  void endTextInput() noexcept override {}
  seam::native_ui::PixelSurface snapshot() const override { return seam::native_ui::PixelSurface{1U, 1U}; }
  std::string backendName() const override { return "frame-counter"; }
  std::atomic<std::uint64_t> requests{0U};
};

struct JourneyApp final {
  std::unique_ptr<seam::standalone::NativeEditorApp> app;
  JourneyDialog* dialog{nullptr};
  seam::native_ui::PixelSurface surface{1600U, 900U};

  // Reduce Motion and the clock that the animation reads are for the tests that count frames.
  JourneyApp(const std::filesystem::path& root, std::vector<std::filesystem::path> answers,
             bool reduceMotion = false,
             std::function<std::chrono::steady_clock::time_point()> uiClock = {}) {
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
    config.designPreferences = seam::native_ui::design::DesignPreferences{
        .mode = seam::native_ui::design::DesignMode::Scene, .reduceMotion = reduceMotion};
    config.uiClock = std::move(uiClock);
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

// The snapshots the autosave service has written for an app created on this test root (whose data
// directory is Data), counting only those taken at this document revision when one is given. A
// snapshot is a project file and a metadata file beside it, written last, so it counts once it is
// complete.
std::size_t autosaveSnapshots(const std::filesystem::path& testRoot,
                              const std::string& revision = {}) {
  const auto marker = revision.empty() ? std::string{"revision-"} : "revision-" + revision + "-";
  const auto autosaves = testRoot / "Data" / "Autosaves";
  std::error_code error;
  if (!std::filesystem::is_directory(autosaves, error)) return 0U;
  std::size_t count = 0U;
  for (std::filesystem::recursive_directory_iterator it{autosaves, error}, end;
       !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().string();
    if (name.starts_with(marker) && name.ends_with(".meta.json")) ++count;
  }
  return count;
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

    // 1. Voice: the VOICE tab opens the Voice Designer workspace, whose session the app creates on
    // first use; the singer card's Change voice opens the real voice browser, and choosing a card
    // selects that bank.
    CHECK(first.app->dispatchAccessibility("shell.workspace.voice", SemanticAction::Activate));
    first.paint();
    CHECK(first.shellPresents());
    CHECK(!first.app->authoring().controller().voicebankBrowserVisible());
    CHECK(first.find("shell.voice.source") != nullptr);
    CHECK(first.find("shell.voice.envelope") != nullptr);
    CHECK(first.find("shell.voice.new") != nullptr);
    CHECK(first.app->dispatchAccessibility("shell.workspace.sing", SemanticAction::Activate));
    first.paint();
    CHECK(first.app->dispatchAccessibility("shell.change-voice", SemanticAction::Activate));
    CHECK(first.app->authoring().controller().voicebankBrowserVisible());
    first.paint();
    // The browser is the shell's own sheet; its cards are the shell's nodes.
    CHECK(first.shellPresents());
    const auto& installedCards = first.app->authoring().controller().sceneState().voicebankCards;
    CHECK(!installedCards.empty());
    if (installedCards.empty()) return;
    const auto firstCardId = seam::native_ui::voicebankCardId(0U, installedCards.front());
    CHECK(first.find(firstCardId) != nullptr);
    CHECK(first.app->dispatchAccessibility(firstCardId, SemanticAction::Activate));
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

TEST_CASE("sing shell journey: the notes show the region's own render, and not once the score changes") {
  const auto root = seam::test::support::temporaryDirectory("journey-note-waveform");
  JourneyApp f{root, {}};
  CHECK(f.app != nullptr);
  if (f.app == nullptr) return;
  f.paint();
  const auto* grid = f.find("timeline");
  CHECK(grid != nullptr);
  if (grid == nullptr) return;
  const auto gridBounds = grid->bounds;
  f.doubleClick({gridBounds.x + 240.0, gridBounds.y + gridBounds.height * 0.5});
  f.paint();
  CHECK(f.region()->notes.size() == 1U);
  // The development fixture bank sings こ; give every new note that lyric through its field.
  const auto singKo = [&f] {
    for (const auto& note : f.region()->notes) {
      if (lyricOf(*f.region(), note) == "\u3053") continue;
      const auto id = "note." + note.id.toString();
      CHECK(f.app->dispatchAccessibility(id, SemanticAction::EditText));
      f.app->textCommit(U"\u3053");
      f.paint();
      return;
    }
  };
  singKo();
  const auto waveformValue = [&f]() -> std::string {
    const auto* node = f.find("shell.waveform");
    return node == nullptr ? std::string{"<missing>"} : node->value;
  };
  const auto waitFor = [&](std::string_view expected) {
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < deadline) {
      f.paint();
      if (waveformValue() == expected) return true;
      std::this_thread::sleep_for(20ms);
    }
    const auto* node = f.find("shell.waveform");
    std::fprintf(stderr, "waveform stayed '%s' (%s)\n", waveformValue().c_str(),
                 node == nullptr ? "" : node->description.c_str());
    return false;
  };
  // A real render of this region publishes its own mono audio; the notes draw it.
  CHECK(waitFor("Showing the current render"));
  // An edit makes that audio out of date at once: nothing is drawn until the next render lands.
  f.doubleClick({gridBounds.x + 600.0, gridBounds.y + gridBounds.height * 0.5});
  f.paint();
  CHECK(f.region()->notes.size() == 2U);
  CHECK(waveformValue() != "Showing the current render");
  singKo();
  CHECK(waitFor("Showing the current render"));
  f.app->shutdownAudio();
}

TEST_CASE("sing shell journey: after detachWindow no render or envelope worker touches the window") {
  class CountingWindow final : public seam::native_ui::INativeWindow {
  public:
    seam::core::Result<void> open(const seam::native_ui::NativeWindowConfig&,
                                  seam::native_ui::INativeWindowClient&) override {
      return seam::core::success();
    }
    int run() override { return 0; }
    void requestRepaint() noexcept override { repaints.fetch_add(1U); }
    void beginTextInput(const seam::native_ui::TextInputRequest&) override {}
    void endTextInput() noexcept override {}
    seam::native_ui::PixelSurface snapshot() const override { return seam::native_ui::PixelSurface{1U, 1U}; }
    std::string backendName() const override { return "counting"; }
    std::atomic<std::uint64_t> repaints{0U};
  };
  const auto root = seam::test::support::temporaryDirectory("journey-detach-window");
  JourneyApp f{root, {}};
  CHECK(f.app != nullptr);
  if (f.app == nullptr) return;
  auto window = std::make_unique<CountingWindow>();
  f.app->setWindow(*window);
  f.paint();
  const auto* grid = f.find("timeline");
  CHECK(grid != nullptr);
  if (grid == nullptr) return;
  const auto gridBounds = grid->bounds;
  const auto addKo = [&](double x) {
    f.doubleClick({gridBounds.x + x, gridBounds.y + gridBounds.height * 0.5});
    f.paint();
    for (const auto& note : f.region()->notes) {
      if (lyricOf(*f.region(), note) == "\u3053") continue;
      CHECK(f.app->dispatchAccessibility("note." + note.id.toString(), SemanticAction::EditText));
      f.app->textCommit(U"\u3053");
      f.paint();
      return;
    }
  };
  const auto showing = [&f] {
    const auto* node = f.find("shell.waveform");
    return node != nullptr && node->value == "Showing the current render";
  };
  addKo(240.0);
  auto deadline = std::chrono::steady_clock::now() + 60s;
  while (!showing() && std::chrono::steady_clock::now() < deadline) {
    f.paint();
    std::this_thread::sleep_for(20ms);
  }
  CHECK(showing());
  CHECK(window->repaints.load() > 0U);  // the window was really in use
  f.app->detachWindow();
  const auto before = window->repaints.load();
  // A new edit renders on the render thread and builds a new envelope on a worker; neither may
  // reach the detached window. The window stays alive (so a stray call is counted, not a
  // use-after-free) until both have completed, then it is destroyed while the app lives on.
  addKo(600.0);
  deadline = std::chrono::steady_clock::now() + 60s;
  while (!showing() && std::chrono::steady_clock::now() < deadline) {
    f.paint();
    std::this_thread::sleep_for(20ms);
  }
  CHECK(showing());
  CHECK(before > 0U);
  CHECK(window->repaints.load() == before);
  window.reset();
  addKo(900.0);
  deadline = std::chrono::steady_clock::now() + 60s;
  while (!showing() && std::chrono::steady_clock::now() < deadline) {
    f.paint();
    std::this_thread::sleep_for(20ms);
  }
  CHECK(showing());
  f.app->shutdownAudio();
}

TEST_CASE("sing shell journey: a settled window asks for no frame, and an idle singer asks for one a breath at a time") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  using Clock = std::chrono::steady_clock;
  const auto breath = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(seam::native_ui::design::CharacterAnimator::kBreathFrameSeconds));
  const auto root = seam::test::support::temporaryDirectory("journey-settled-frames");
  for (const bool reduceMotion : {true, false}) {
    // The animation reads this clock, which the test moves by hand: a blink cannot fall among the
    // frames it counts, and the fades that a window starts with finish as the clock passes them.
    const auto now = std::make_shared<Clock::time_point>(Clock::time_point{} + 10s);
    JourneyApp f{root / (reduceMotion ? "still" : "moving"), {}, reduceMotion, [now] { return *now; }};
    CHECK(f.app != nullptr);
    if (f.app == nullptr) return;
    FrameCounter window;
    f.app->setWindow(window);
    f.paint();
    seam::native_ui::RasterCanvas canvas{f.surface, 1.0};
    for (int frame = 0; frame < 20; ++frame) {
      *now += 50ms;
      f.app->paint(canvas);
      std::this_thread::sleep_for(5ms);
    }
    // Nothing is going on now. However many frames are painted, none asks for the next: a window
    // that did would repaint at the display's rate for ever. Under Reduce Motion no animation is due
    // either; otherwise the breath is, a breath after the frame that painted it.
    for (int frame = 0; frame < 30; ++frame) {
      *now += 30ms;
      const auto before = window.requests.load();
      f.app->paint(canvas);
      CHECK(window.requests.load() == before);
      const auto due = f.app->nextFrameDue();
      if (reduceMotion) {
        // What a frame is still due for is the autosave of a document with unsaved changes, and only
        // that. Nothing has been requested yet, so it is due one interval after the clock's zero.
        const auto unsaved = f.app->authoring().runtime().document().dirty();
        CHECK(due.has_value() == unsaved);
        if (due.has_value()) CHECK(*due == Clock::time_point{} + 60s);
      } else {
        CHECK(due.has_value());
        CHECK(due == *now + breath);
      }
    }
    f.app->detachWindow();
    f.app->shutdownAudio();
  }
}

TEST_CASE("sing shell journey: an edited document is autosaved by a window that has nothing to animate") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  using Clock = std::chrono::steady_clock;
  const auto root = seam::test::support::temporaryDirectory("journey-idle-autosave");
  // Under Reduce Motion nothing animates, so once the window has settled no frame is asked for, and
  // the autosave tick that a frame does runs only if the app says when it is due. The interval counts
  // on the clock that the animation reads, which the test moves by hand: no minute is waited out.
  const auto now = std::make_shared<Clock::time_point>(Clock::time_point{} + 10s);
  JourneyApp f{root, {}, true, [now] { return *now; }};
  CHECK(f.app != nullptr);
  if (f.app == nullptr) return;
  FrameCounter window;
  f.app->setWindow(window);
  // The window is detached before it is destroyed, whatever way the test ends.
  struct Detach final {
    JourneyApp& journey;
    ~Detach() {
      journey.app->detachWindow();
      journey.app->shutdownAudio();
    }
  } detach{f};
  f.paint();
  seam::native_ui::RasterCanvas canvas{f.surface, 1.0};
  for (int frame = 0; frame < 20; ++frame) {
    *now += 50ms;
    f.app->paint(canvas);
    std::this_thread::sleep_for(5ms);
  }
  // What the native window's loop does between events: it paints a frame when one was asked for and
  // when the app's own deadline has passed, and otherwise paints none. Returns how many it painted.
  const auto pump = [&] {
    int frames = 0;
    while (frames < 100) {
      const bool asked = window.requests.exchange(0U) != 0U;
      const auto due = f.app->nextFrameDue();
      if (!asked && !(due.has_value() && *due <= *now)) break;
      static_cast<void>(f.app->paintFrame(canvas));
      ++frames;
    }
    return frames;
  };
  const auto showing = [&f] {
    const auto* node = f.find("shell.waveform");
    return node != nullptr && node->value == "Showing the current render";
  };
  // Paints what is asked for, with real time passing so that the render and envelope workers can
  // publish, until the render is on show and the window asks for nothing more.
  const auto settle = [&] {
    const auto limit = Clock::now() + 60s;
    int quiet = 0;
    while (Clock::now() < limit && !(quiet >= 10 && showing())) {
      *now += 1ms;
      quiet = pump() == 0 ? quiet + 1 : 0;
      std::this_thread::sleep_for(20ms);
    }
    CHECK(showing());
    CHECK(quiet >= 10);
  };
  const auto* grid = f.find("timeline");
  CHECK(grid != nullptr);
  if (grid == nullptr) return;
  const auto gridBounds = grid->bounds;
  const auto addKo = [&](double x) {
    f.doubleClick({gridBounds.x + x, gridBounds.y + gridBounds.height * 0.5});
    f.paint();
    for (const auto& note : f.region()->notes) {
      if (lyricOf(*f.region(), note) == "\u3053") continue;
      CHECK(f.app->dispatchAccessibility("note." + note.id.toString(), SemanticAction::EditText));
      f.app->textCommit(U"\u3053");
      f.paint();
      return;
    }
  };
  const auto revisionOf = [&f] {
    return std::to_string(f.app->authoring().runtime().document().session().revision());
  };
  // The service writes on its own thread, in real time.
  const auto snapshotAppears = [&](const std::string& revision) {
    const auto limit = Clock::now() + 60s;
    while (autosaveSnapshots(root, revision) == 0U && Clock::now() < limit)
      std::this_thread::sleep_for(20ms);
    return autosaveSnapshots(root, revision) != 0U;
  };

  // 1. A phrase is written, and the window is left alone. Its first snapshot is due one interval
  // after the clock's zero, which nothing animating or asking would bring a frame to.
  addKo(240.0);
  settle();
  CHECK(f.app->authoring().runtime().document().dirty());
  const auto first = revisionOf();
  const auto due = f.app->nextFrameDue();
  CHECK(due.has_value());
  if (!due.has_value()) return;
  CHECK(*due > *now);
  CHECK(*due <= *now + 60s);
  CHECK(autosaveSnapshots(root) == 0U);
  CHECK(pump() == 0);
  *now = *due - 1s;
  CHECK(pump() == 0);
  CHECK(autosaveSnapshots(root) == 0U);
  *now = *due + 1s;
  CHECK(pump() == 1);
  CHECK(snapshotAppears(first));

  // 2. The deadline then moves a whole interval on from the frame that took the snapshot, and an edit
  // made after it reaches a snapshot the same way, with nobody touching the window.
  const auto tickedAt = *now;
  CHECK(f.app->nextFrameDue() == tickedAt + 60s);
  addKo(600.0);
  settle();
  const auto second = revisionOf();
  CHECK(second != first);
  CHECK(f.app->nextFrameDue() == tickedAt + 60s);
  *now = tickedAt + 59s;
  CHECK(pump() == 0);
  CHECK(autosaveSnapshots(root, second) == 0U);
  *now = tickedAt + 61s;
  CHECK(pump() == 1);
  CHECK(snapshotAppears(second));
  CHECK(f.app->nextFrameDue() == tickedAt + 121s);
  *now = tickedAt + 120s;
  CHECK(pump() == 0);
}

TEST_CASE("sing shell journey: saving takes the autosave deadline of a window that has nothing to animate away, and an edit brings it back") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  using Clock = std::chrono::steady_clock;
  const auto root = seam::test::support::temporaryDirectory("journey-clean-deadline");
  const auto path = root / "Saved.seam";
  // The deadline is a document's unsaved changes waiting for their snapshot; a document that has none
  // has nothing to wake a still window for, however long the window stays open. The clock stays
  // under the first deadline, so no snapshot is ever requested here.
  const auto now = std::make_shared<Clock::time_point>(Clock::time_point{} + 10s);
  JourneyApp f{root / "app", {path}, true, [now] { return *now; }};
  CHECK(f.app != nullptr);
  if (f.app == nullptr) return;
  FrameCounter window;
  f.app->setWindow(window);
  struct Detach final {
    JourneyApp& journey;
    ~Detach() {
      journey.app->detachWindow();
      journey.app->shutdownAudio();
    }
  } detach{f};
  f.paint();
  seam::native_ui::RasterCanvas canvas{f.surface, 1.0};
  const auto settle = [&] {
    for (int frame = 0; frame < 40; ++frame) {
      *now += 25ms;
      f.app->paint(canvas);
      std::this_thread::sleep_for(10ms);
    }
  };
  const auto dirty = [&f] { return f.app->authoring().runtime().document().dirty(); };
  const auto* grid = f.find("timeline");
  CHECK(grid != nullptr);
  if (grid == nullptr) return;
  const auto gridBounds = grid->bounds;
  const auto edit = [&](double x) {
    f.doubleClick({gridBounds.x + x, gridBounds.y + gridBounds.height * 0.5});
    f.paint();
  };

  // An edit leaves unsaved changes: the first snapshot is due one interval after the clock's zero.
  edit(240.0);
  settle();
  CHECK(dirty());
  CHECK(f.app->nextFrameDue() == Clock::time_point{} + 60s);

  // Save As (Command-Shift-S): nothing is unsaved, so a still window asks for nothing at all, and
  // time passing does not bring one back.
  f.app->keyDown(KeyEvent{.key = NativeKey::S, .modifiers = {.shift = true, .command = true}});
  CHECK(std::filesystem::is_regular_file(path));
  CHECK(!dirty());
  settle();
  CHECK(!f.app->nextFrameDue().has_value());
  window.requests.store(0U);
  for (int second = 0; second < 10; ++second) {
    *now += 1s;
    CHECK(!f.app->nextFrameDue().has_value());
  }
  CHECK(window.requests.load() == 0U);

  // A further edit brings the deadline back, and Save (Command-S) takes it away again.
  edit(600.0);
  settle();
  CHECK(dirty());
  CHECK(f.app->nextFrameDue() == Clock::time_point{} + 60s);
  f.app->keyDown(KeyEvent{.key = NativeKey::S, .modifiers = {.command = true}});
  CHECK(!dirty());
  settle();
  CHECK(!f.app->nextFrameDue().has_value());
}
