// The EMO/SCENE shell inside the CLAP editor runtime: while EXPORT covers the score, the plug-in's
// accessibility and key paths refuse retained score elements the same way the standalone does.
// This is library evidence for the embedded editor, not FL Studio host evidence.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

using namespace seam;
using native_ui::KeyEvent;
using native_ui::NativeKey;
using native_ui::SemanticAction;
using native_ui::SemanticNode;

void paintFrame(clap_editor::EditorRuntime& runtime) {
  native_ui::PixelSurface surface{1600U, 900U};
  native_ui::RasterCanvas canvas{surface, 1.0};
  runtime.paint(canvas);
}

const SemanticNode* childById(const std::vector<SemanticNode>& children, std::string_view id) {
  const auto found = std::find_if(children.begin(), children.end(),
                                  [id](const SemanticNode& node) { return node.id == id; });
  return found == children.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("CLAP shell: EXPORT refuses retained score elements and editing keys") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  runtime.activateDesignShell(
      native_ui::design::DesignPreferences{.mode = native_ui::design::DesignMode::Emo});
  runtime.resize(1600.0, 900.0);
  paintFrame(runtime);
  const auto notesBefore = runtime.projectCopy().noteCount();
  CHECK(notesBefore > 0U);
  const auto initial = runtime.accessibilitySnapshot();
  CHECK(childById(initial.children, "shell.workspace.export") != nullptr);
  CHECK(initial.virtualizedNoteCount > 0U);
  const auto notes = runtime.accessibilityNotes(0U, 1U);
  CHECK(!notes.empty());
  if (notes.empty()) return;
  const auto noteId = notes.front().id;
  // On screen the note is reachable. Select it and move it off the grid, so an editor Q
  // (quantize) reached by a shortcut the plug-in does not own would visibly move it back.
  CHECK(runtime.dispatchAccessibility(noteId, SemanticAction::Activate));
  CHECK(runtime.controller().pianoRoll().moveSelection(time::Tick{17}, 0));
  const auto offGrid = runtime.projectCopy();
  const auto* movedRegion = offGrid.findRegion(runtime.controller().selectedRegion());
  CHECK(movedRegion != nullptr && !movedRegion->notes.empty());
  if (movedRegion == nullptr || movedRegion->notes.empty()) return;
  const auto movedId = movedRegion->notes.front().id;
  const auto movedTick = offGrid.findNote(movedId)->startTick;
  CHECK(runtime.dispatchAccessibility("shell.workspace.export", SemanticAction::Activate));
  paintFrame(runtime);
  const auto snapshot = runtime.accessibilitySnapshot();
  CHECK(snapshot.virtualizedNoteCount == 0U);
  const auto* run = childById(snapshot.children, "shell.export.run");
  CHECK(run != nullptr);
  if (run != nullptr) {
    CHECK(!run->enabled);
    CHECK(run->description.find("export from your DAW") != std::string::npos);
  }
  const auto revision = runtime.revision();
  CHECK(!runtime.dispatchAccessibility(noteId, SemanticAction::EditText));
  CHECK(!runtime.controller().textInputActive());
  CHECK(!runtime.dispatchAccessibility(noteId, SemanticAction::Activate));
  CHECK(!runtime.setAccessibilityValue(noteId, "HIDDEN"));
  runtime.keyDown(KeyEvent{.key = NativeKey::Delete, .modifiers = {.alt = true}});
  runtime.keyDown(KeyEvent{.key = NativeKey::Delete});
  runtime.keyDown(KeyEvent{.key = NativeKey::D, .modifiers = {.command = true}});
  // A plug-in implements no Quit: Command-Q must not fall through to the editor's Q (quantize).
  runtime.keyDown(KeyEvent{.key = NativeKey::Q, .modifiers = {.command = true}});
  runtime.keyDown(KeyEvent{.key = NativeKey::S, .modifiers = {.command = true}});
  CHECK(runtime.revision() == revision);
  CHECK(runtime.projectCopy().noteCount() == notesBefore);
  CHECK(runtime.projectCopy().findNote(movedId)->startTick == movedTick);
  // Escape returns to SING, where the same note acts again.
  runtime.keyDown(KeyEvent{.key = NativeKey::Escape});
  paintFrame(runtime);
  const auto back = runtime.accessibilitySnapshot();
  CHECK(back.virtualizedNoteCount > 0U);
  CHECK(childById(back.children, "shell.export.run") == nullptr);
  CHECK(runtime.dispatchAccessibility(noteId, SemanticAction::SetFocus));
}

TEST_CASE("CLAP shell: the notes show the region's own preview render once it is ready") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  runtime.activateDesignShell(
      native_ui::design::DesignPreferences{.mode = native_ui::design::DesignMode::Scene});
  runtime.resize(1600.0, 900.0);
  const auto waveform = [&runtime]() -> std::optional<SemanticNode> {
    paintFrame(runtime);
    const auto snapshot = runtime.accessibilitySnapshot();
    const auto* node = childById(snapshot.children, "shell.waveform");
    return node == nullptr ? std::nullopt : std::optional<SemanticNode>{*node};
  };
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
  std::optional<SemanticNode> last;
  while (std::chrono::steady_clock::now() < deadline) {
    last = waveform();
    if (last && last->value == "Showing the current render") break;
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  if (!last || last->value != "Showing the current render")
    std::cerr << "CLAP waveform stayed '" << (last ? last->value : "<missing>") << "' ("
              << (last ? last->description : "") << ")\n";
  CHECK(last.has_value() && last->value == "Showing the current render");
}

namespace {

// Every published node, depth first, so a test can find a control wherever the shell nests it.
const SemanticNode* findNode(const std::vector<SemanticNode>& nodes, std::string_view id) {
  for (const auto& node : nodes) {
    if (node.id == id) return &node;
    if (const auto* found = findNode(node.children, id)) return found;
  }
  return nullptr;
}

// A second vocal track with two regions, beside the default track, so track and region choices in
// MIX have somewhere to go.
struct MixProject final {
  domain::TrackId firstTrack;
  domain::RegionId firstRegion;
  domain::TrackId harmony;
  domain::RegionId harmonyA;
  domain::RegionId harmonyB;
};

MixProject addHarmony(clap_editor::EditorRuntime& runtime) {
  auto project = runtime.projectCopy();
  application::ProjectFactory factory{8000U};
  factory.synchronizeWith(project);
  MixProject ids{.firstTrack = runtime.trackId(), .firstRegion = runtime.regionId()};
  ids.harmony = factory.addVocalTrack(project, "HARMONY");
  ids.harmonyA = factory.addRegion(project, ids.harmony, "HARMONY A", time::Tick{960}, time::Tick{3840});
  ids.harmonyB = factory.addRegion(project, ids.harmony, "HARMONY B", time::Tick{7680}, time::Tick{3840});
  CHECK(runtime.replaceProject(std::move(project)).hasValue());
  return ids;
}

}  // namespace

// The classic plug-in overlay cycled the track and the region, toggled mute and solo and stepped the
// output channels 1/2/4/6/8. In the shell all of them live in MIX, and each one moves the plug-in's
// own state: the runtime's track and region (what it renders) and the project's output channels
// (what the plug-in's ports follow), not just the controller's view of them.
TEST_CASE("CLAP shell: MIX reaches track and region selection, mute, solo and output channels") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  runtime.activateDesignShell(
      native_ui::design::DesignPreferences{.mode = native_ui::design::DesignMode::Emo});
  runtime.resize(1600.0, 900.0);
  const auto ids = addHarmony(runtime);
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility("shell.workspace.mix", SemanticAction::Activate));
  paintFrame(runtime);
  const auto region = [](domain::RegionId id) { return "shell.mix.region." + id.toString(); };
  const auto track = [](domain::TrackId id, std::string_view control) {
    return "shell.mix.track." + id.toString() + std::string{control};
  };
  const auto snapshot = runtime.accessibilitySnapshot();
  for (const auto& id : {region(ids.firstRegion), region(ids.harmonyA), region(ids.harmonyB),
                         track(ids.harmony, ".mute"), track(ids.harmony, ".solo"),
                         std::string{"shell.mix.output-channels"}}) {
    if (findNode(snapshot.children, id) == nullptr) std::cerr << "MIX does not publish " << id << '\n';
    CHECK(findNode(snapshot.children, id) != nullptr);
  }

  // A region on another track moves the track with it; a second region on that track moves only
  // the region; the first track's region moves both back. Each is the runtime's own selection.
  CHECK(runtime.dispatchAccessibility(region(ids.harmonyA), SemanticAction::Activate));
  CHECK(runtime.trackId() == ids.harmony);
  CHECK(runtime.regionId() == ids.harmonyA);
  CHECK(runtime.controller().selectedRegion() == ids.harmonyA);
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility(region(ids.harmonyB), SemanticAction::Activate));
  CHECK(runtime.trackId() == ids.harmony);
  CHECK(runtime.regionId() == ids.harmonyB);
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility(region(ids.firstRegion), SemanticAction::Activate));
  CHECK(runtime.trackId() == ids.firstTrack);
  CHECK(runtime.regionId() == ids.firstRegion);
  CHECK(runtime.controller().selectedTrack() == ids.firstTrack);

  // Mute and solo are the strip's own toggles, one project edit each, on the strip's track only.
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility(track(ids.harmony, ".mute"), SemanticAction::Activate));
  CHECK(runtime.projectCopy().findVocalTrack(ids.harmony)->muted);
  CHECK(!runtime.projectCopy().findVocalTrack(ids.firstTrack)->muted);
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility(track(ids.harmony, ".solo"), SemanticAction::Activate));
  CHECK(runtime.projectCopy().findVocalTrack(ids.harmony)->solo);
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility(track(ids.harmony, ".mute"), SemanticAction::Activate));
  CHECK(!runtime.projectCopy().findVocalTrack(ids.harmony)->muted);
  CHECK(runtime.regionId() == ids.firstRegion);

  // The master card's output-channel control steps the project's (and so the ports') channel count
  // through 1, 2, 4, 6 and 8 and around, and back down with Decrement.
  const auto channels = [&runtime] {
    return static_cast<unsigned>(runtime.projectCopy().routing().deviceOutputChannels);
  };
  CHECK(channels() == 2U);
  for (const unsigned expected : {4U, 6U, 8U, 1U, 2U}) {
    paintFrame(runtime);
    CHECK(runtime.dispatchAccessibility("shell.mix.output-channels", SemanticAction::Activate));
    CHECK(channels() == expected);
  }
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility("shell.mix.output-channels", SemanticAction::Decrement));
  CHECK(channels() == 1U);
  paintFrame(runtime);
  const auto* output = findNode(runtime.accessibilitySnapshot().children, "shell.mix.output-channels");
  CHECK(output != nullptr);
  if (output != nullptr) CHECK(output->value.starts_with("1"));
  const auto routed = runtime.projectCopy();
  const auto* master = routed.routing().findBus(routed.routing().masterBus);
  CHECK(master != nullptr);
  if (master != nullptr) CHECK(master->channelCount == 1U);
}
