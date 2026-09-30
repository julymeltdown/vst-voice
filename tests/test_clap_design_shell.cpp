// The EMO/SCENE shell inside the CLAP editor runtime: while EXPORT covers the score, the plug-in's
// accessibility and key paths refuse retained score elements the same way the standalone does.
// This is library evidence for the embedded editor, not FL Studio host evidence.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
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

// A plug-in cannot audition a seam's alternate render: the DAW plays the song's own render. The
// shell says so instead of offering B: the Phonemes lane's hint and the status line name the
// refusal, and B changes nothing.
TEST_CASE("CLAP shell: seam B preview is refused honestly in the plug-in") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  runtime.activateDesignShell(
      native_ui::design::DesignPreferences{.mode = native_ui::design::DesignMode::Emo});
  runtime.resize(1600.0, 900.0);
  paintFrame(runtime);
  CHECK(!runtime.controller().sceneState().seamPreviewConnected);
  // Open the Phonemes lane and press the seam band at the first note's start.
  const auto tab = findNode(runtime.accessibilitySnapshot().children, "shell.lane-tab.phonemes");
  CHECK(tab != nullptr);
  if (tab == nullptr) return;
  const ui::Point tabCenter{tab->bounds.x + tab->bounds.width * 0.5, tab->bounds.y + tab->bounds.height * 0.5};
  runtime.pointerDown({tabCenter, native_ui::PointerButton::Left, {}, 1});
  runtime.pointerUp({tabCenter, native_ui::PointerButton::Left, {}, 1});
  paintFrame(runtime);
  const auto snapshot = runtime.accessibilitySnapshot();
  const auto* seamBand = findNode(snapshot.children, "shell.lane.band.seam");
  const auto notes = runtime.accessibilityNotes(0U, 1U);
  CHECK(seamBand != nullptr);
  CHECK(!notes.empty());
  if (seamBand == nullptr || notes.empty()) return;
  const ui::Point seam{notes.front().bounds.x + 1.0, seamBand->bounds.y + seamBand->bounds.height * 0.5};
  runtime.pointerDown({seam, native_ui::PointerButton::Left, {}, 1});
  runtime.pointerUp({seam, native_ui::PointerButton::Left, {}, 1});
  const auto selected = runtime.controller().sceneState();
  CHECK(selected.selectedSeam.has_value());
  if (!selected.selectedSeam.has_value()) return;
  const auto status = native_ui::design::singStatusMessage(selected);
  CHECK(status.text.find("Seam B preview is not available in the plug-in") != std::string::npos);
  const auto revision = runtime.revision();
  runtime.keyDown(KeyEvent{.key = NativeKey::B});
  CHECK(!runtime.controller().sceneState().seamPreviewAlternate);
  CHECK(runtime.revision() == revision);
  const auto refused = runtime.controller().toggleSelectedSeamPreview();
  CHECK(!refused);
  if (!refused) {
    CHECK(refused.error().code == core::ErrorCode::Unsupported);
    CHECK(refused.error().message.find("plug-in") != std::string::npos);
  }
}

TEST_CASE("CLAP shell: the shell is activated once, by the first frame, not at construction") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  // Constructing the runtime loads no design assets: a host may never open the editor.
  CHECK(runtime.designShellActivations() == 0U);
  runtime.resize(1600.0, 900.0);
  CHECK(runtime.designShellActivations() == 1U);
  paintFrame(runtime);
  paintFrame(runtime);
  runtime.resize(1100.0, 720.0);
  paintFrame(runtime);
  CHECK(runtime.designShellActivations() == 1U);
  // The first frame presents the shell, with its default look.
  CHECK(childById(runtime.accessibilitySnapshot().children, "shell.workspace.export") != nullptr);
  // A paint without a resize activates it as well.
  clap_editor::EditorRuntime painted{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  paintFrame(painted);
  paintFrame(painted);
  CHECK(painted.designShellActivations() == 1U);
}

TEST_CASE("CLAP shell: the plug-in's export refusal reads from the string table") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto pseudo = native_ui::design::ShellStringTable::pseudoLocalized(0.4);
  const native_ui::design::ScopedShellStrings scope{pseudo};
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  runtime.resize(1600.0, 900.0);
  paintFrame(runtime);
  CHECK(runtime.dispatchAccessibility("shell.workspace.export", SemanticAction::Activate));
  paintFrame(runtime);
  const auto snapshot = runtime.accessibilitySnapshot();
  const auto* run = childById(snapshot.children, "shell.export.run");
  CHECK(run != nullptr);
  if (run != nullptr)
    CHECK(run->description == pseudo.text(native_ui::design::Str::InAPlugInExportFromYour));
}

namespace {

// The plug-in editor on its default score, shown in the shell.
struct ShellRuntime final {
  clap_editor::EditorRuntime runtime{
      std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        voicebank::VoicebankRootKind::Development}}};
  ShellRuntime() {
    runtime.activateDesignShell(
        native_ui::design::DesignPreferences{.mode = native_ui::design::DesignMode::Emo});
    runtime.resize(1600.0, 900.0);
  }
};

// Somewhere for the host to point the editor: a second region on the lead's track and a new track with a
// region of its own. The editor ends where it began, on the lead's first region.
struct RetargetPlaces final {
  domain::TrackId leadTrack;
  domain::RegionId leadRegion;
  domain::RegionId secondRegion;
  domain::TrackId otherTrack;
  domain::RegionId otherRegion;
};

RetargetPlaces addRetargetPlaces(clap_editor::EditorRuntime& runtime) {
  RetargetPlaces places{.leadTrack = runtime.trackId(), .leadRegion = runtime.regionId()};
  const auto second = runtime.controller().addRegionToSelectedTrack();
  CHECK(second.hasValue());
  if (second) places.secondRegion = second.value();
  const auto track = runtime.controller().addVocalTrack("OTHER");
  CHECK(track.hasValue());
  if (track) places.otherTrack = track.value();
  const auto region = runtime.controller().addRegionToSelectedTrack();
  CHECK(region.hasValue());
  if (region) places.otherRegion = region.value();
  CHECK(runtime.selectTrack(places.leadTrack).hasValue());
  CHECK(runtime.selectRegion(places.leadRegion).hasValue());
  return places;
}

std::vector<domain::NoteId> notesOf(clap_editor::EditorRuntime& runtime, domain::RegionId region) {
  std::vector<domain::NoteId> ids;
  const auto project = runtime.projectCopy();
  if (const auto* found = project.findRegion(region)) {
    for (const auto& note : found->notes) ids.push_back(note.id);
  }
  return ids;
}

// The lead's first note, selected the way a creator selects it: through the shell.
void selectLeadNote(clap_editor::EditorRuntime& runtime) {
  paintFrame(runtime);
  // The snapshot publishes the shell's notes, which the paged list below reads.
  CHECK(runtime.accessibilitySnapshot().virtualizedNoteCount > 0U);
  const auto notes = runtime.accessibilityNotes(0U, 1U);
  CHECK(!notes.empty());
  if (notes.empty()) return;
  CHECK(runtime.dispatchAccessibility(notes.front().id, SemanticAction::Activate));
  CHECK(runtime.controller().sceneState().selectedNoteCount == 1U);
}

// The host points the editor somewhere else through the plug-in's own selection API. The controller is
// rebuilt around the new place, and the note selection, which belongs to the session the controllers
// share, must not come with it: the editor shows another region, and a key press acts on what it shows.
void checkRetargetDropsNoteSelection(bool toOtherTrack) {
  ShellRuntime shell;
  auto& runtime = shell.runtime;
  const auto places = addRetargetPlaces(runtime);
  const auto leadNotes = notesOf(runtime, places.leadRegion);
  CHECK(!leadNotes.empty());
  selectLeadNote(runtime);
  const auto retargeted = toOtherTrack ? runtime.selectTrack(places.otherTrack)
                                       : runtime.selectRegion(places.secondRegion);
  CHECK(retargeted.hasValue());
  CHECK(runtime.controller().selectedRegion() ==
        (toOtherTrack ? places.otherRegion : places.secondRegion));
  CHECK(runtime.controller().sceneState().selectedNoteCount == 0U);
  runtime.keyDown(KeyEvent{.key = NativeKey::Delete});
  CHECK(notesOf(runtime, places.leadRegion) == leadNotes);
}

}  // namespace

TEST_CASE("CLAP shell: a host that points the editor at another region cannot delete a note it left behind") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  checkRetargetDropsNoteSelection(false);
}

TEST_CASE("CLAP shell: a host that points the editor at another track cannot delete a note it left behind") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  checkRetargetDropsNoteSelection(true);
}

TEST_CASE("CLAP shell: a host that points the editor at where it already is keeps the note selection") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellRuntime shell;
  auto& runtime = shell.runtime;
  const auto places = addRetargetPlaces(runtime);
  selectLeadNote(runtime);
  CHECK(runtime.selectRegion(places.leadRegion).hasValue());
  CHECK(runtime.controller().sceneState().selectedNoteCount == 1U);
  CHECK(runtime.selectTrack(places.leadTrack).hasValue());
  CHECK(runtime.controller().sceneState().selectedNoteCount == 1U);
}

// A key the editor refuses as things stand used to do nothing at all in the plug-in: the result was
// dropped, and the creator was left with a key that did nothing and no word why. It is now a notice in the
// editor's own diagnostics. This is library evidence for the embedded editor, not FL Studio host evidence.
TEST_CASE("CLAP shell: a key the editor refuses is shown as a notice that coalesces and leaves the project alone") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellRuntime shell;
  auto& runtime = shell.runtime;
  const auto project = runtime.projectCopy();
  const auto revision = runtime.revision();
  const auto notices = [&] {
    std::vector<authoring::Diagnostic> found;
    for (const auto& entry : runtime.controller().diagnosticPanel().entries())
      if (entry.diagnostic.code == "EDIT_REFUSED") found.push_back(entry.diagnostic);
    return found;
  };
  CHECK(notices().empty());

  // Shift-L distributes lyrics over the selected notes, and none is selected, so the editor refuses it as
  // things stand. (A Command-modified key would not do: the shell keeps those for itself.)
  const KeyEvent distribute{.key = NativeKey::L, .modifiers = {.shift = true}};
  runtime.keyDown(distribute);
  auto shown = notices();
  CHECK(shown.size() == 1U);
  if (shown.size() != 1U) return;
  CHECK(shown.front().detail == "Select notes before distributing lyrics");
  CHECK(shown.front().severity == authoring::DiagnosticSeverity::Warning);
  CHECK(shown.front().occurrenceCount == 1U);
  // A notice belongs to the editor and not to the document: the project and its revision, which is what
  // the plug-in reports to the host as a change, are as they were.
  CHECK(runtime.projectCopy() == project);
  CHECK(runtime.revision() == revision);

  // The same refusal again is the same notice.
  runtime.keyDown(distribute);
  shown = notices();
  CHECK(shown.size() == 1U);
  if (shown.size() != 1U) return;
  CHECK(shown.front().occurrenceCount == 2U);
  CHECK(runtime.revision() == revision);

  // The creator dismisses it through the action the popover offers, which the editor answers itself:
  // the plug-in connects no diagnostic action of its own.
  CHECK(runtime.controller().activateDiagnostic(0U, authoring::DiagnosticAction::Dismiss));
  CHECK(notices().empty());
  CHECK(runtime.projectCopy() == project);
  CHECK(runtime.revision() == revision);

  // A commit that arrives with no field open has nothing to refuse, so it tells nothing.
  runtime.textCommit(U"stray");
  CHECK(notices().empty());
  CHECK(runtime.revision() == revision);

  // A commit that is refused while a field is open is told: the Find field took the document as it stood,
  // and the project changed before the commit.
  CHECK(runtime.controller().beginFindInput());
  CHECK(runtime.textInputActive());
  CHECK(runtime.controller().renameSelectedRegion("Renamed while finding"));
  CHECK(runtime.revision() != revision);
  runtime.textCommit(U"query");
  shown = notices();
  CHECK(shown.size() == 1U);
  if (shown.size() != 1U) return;
  CHECK(shown.front().detail == "Find/replace input belongs to a changed document or region");
}
