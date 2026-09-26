// MIX workspace: channel strips, master and audio device card, driven through a real
// NativeEditorController and EditorSession inside the presented SING shell.
#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/audio_settings.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/shell_workspace.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace seam;
using native_ui::KeyEvent;
using native_ui::NativeKey;
using native_ui::PointerButton;
using native_ui::PointerEvent;
using native_ui::SemanticAction;
using native_ui::SemanticNode;
using native_ui::SemanticRole;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::SingShell;
using native_ui::design::Workspace;

// The ShellFixture of test_design_shell_input.cpp, with several tracks and an optional second bus.
struct MixFixture final {
  application::ProjectFactory factory{9600U};
  std::vector<domain::TrackId> tracks;
  domain::RegionId regionId{};
  domain::NoteId noteId{};  // one note on the first track's region
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;
  // The pixels of the last frame(), for checks on what was drawn.
  std::vector<std::uint32_t> pixels;
  double pixelWidth{0.0};

  explicit MixFixture(std::size_t trackCount = 3U, bool secondBus = false)
      : session(makeProject(trackCount, secondBus)), controller{session, factory, regionId} {
    controller.resize(1600.0, 900.0);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
  }

  domain::Project makeProject(std::size_t trackCount, bool secondBus) {
    auto project = factory.createProject("Mix");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    for (std::size_t i = 0U; i < trackCount; ++i) {
      const auto id = factory.addVocalTrack(project, "Voice " + std::to_string(i + 1U));
      const auto region = factory.addRegion(project, id, "Phrase", time::Tick{0}, time::Tick{7680});
      if (i == 0U) {
        regionId = region;
        auto [lyric, note] =
            factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"\u3042", domain::Language::Japanese);
        noteId = note.id;
        auto* phrase = project.findRegion(region);
        phrase->lyrics.push_back(std::move(lyric));
        phrase->notes.push_back(std::move(note));
      }
      tracks.push_back(id);
    }
    if (trackCount > 1U) {
      auto* second = project.findVocalTrack(tracks[1]);
      second->gainDb = -6.0F;
      second->pan = -0.5F;
      second->muted = true;
    }
    if (secondBus) {
      project.routing().buses.push_back(
          domain::AudioBus{.id = domain::BusId{2U}, .name = "Stems", .channelCount = 2U});
      project.routing().sends.push_back(domain::BusSend{.sourceBus = domain::BusId{2U},
                                                        .destinationBus = domain::BusId{1U},
                                                        .matrix = domain::RoutingMatrix::identity(2U)});
    }
    return project;
  }

  const domain::VocalTrack& track(std::size_t index) const {
    return *session.project().findVocalTrack(tracks[index]);
  }

  bool frame(double width = 1600.0, double height = 900.0) {
    controller.resize(width, height);
    if (!shell.prepareFrame(controller, width, height)) return false;
    native_ui::PixelSurface surface{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    const auto painted = shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
    pixels.assign(surface.pixels().begin(), surface.pixels().end());
    pixelWidth = width;
    return painted;
  }

  // Presents the shell and opens MIX through its real tab.
  void openMix(double width = 1600.0, double height = 900.0) {
    CHECK(frame(width, height));
    CHECK(shell.dispatchSemantic(controller, "shell.workspace.mix", SemanticAction::Activate).hasValue());
    CHECK(shell.workspace() == Workspace::Mix);
    CHECK(frame(width, height));
  }

  void refresh() {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
  }

  std::vector<SemanticNode> mixNodes() {
    refresh();
    std::vector<SemanticNode> nodes;
    for (const auto& node : shell.accessibilityTree().root().children)
      if (node.id.starts_with("shell.mix.")) nodes.push_back(node);
    return nodes;
  }

  SemanticNode node(std::string_view id) {
    for (auto& candidate : mixNodes())
      if (candidate.id == id) return candidate;
    throw test::Failure{"no published node " + std::string{id}};
  }

  bool published(std::string_view id) {
    const auto nodes = mixNodes();
    return std::any_of(nodes.begin(), nodes.end(), [id](const auto& n) { return n.id == id; });
  }

  std::string focusedId() {
    refresh();
    const auto* focused = shell.accessibilityTree().focusedNode();
    return focused != nullptr ? focused->id : std::string{};
  }

  std::string stripId(std::size_t index) const { return "shell.mix.track." + tracks[index].toString(); }
  std::string controlId(std::size_t index, std::string_view control) const {
    return stripId(index) + "." + std::string{control};
  }
  domain::RegionId region(std::size_t index) const { return track(index).regions.front().id; }
  std::string regionNode(std::size_t index) const { return "shell.mix.region." + region(index).toString(); }
};

PointerEvent press(ui::Point p, bool shift = false, int clicks = 1) {
  return {.position = p, .button = PointerButton::Left, .modifiers = {.shift = shift}, .clickCount = clicks};
}

ui::Point center(ui::Rect r) { return {r.x + r.width * 0.5, r.y + r.height * 0.5}; }

bool overlaps(ui::Rect a, ui::Rect b) {
  constexpr double e = 1e-6;
  return a.x < b.right() - e && b.x < a.right() - e && a.y < b.bottom() - e && b.y < a.bottom() - e;
}

bool inside(ui::Rect inner, ui::Rect outer) {
  constexpr double e = 1e-6;
  return inner.x >= outer.x - e && inner.y >= outer.y - e && inner.right() <= outer.right() + e &&
         inner.bottom() <= outer.bottom() + e;
}

// Pixels that differ between two frames of the same size, counted where `where` says.
template <typename Where>
std::size_t changedPixels(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b,
                          double width, Where where) {
  std::size_t changed = 0U;
  const auto columns = static_cast<std::size_t>(width);
  for (std::size_t i = 0U; i < a.size() && i < b.size(); ++i) {
    const ui::Point p{static_cast<double>(i % columns) + 0.5, static_cast<double>(i / columns) + 0.5};
    if (a[i] != b[i] && where(p)) ++changed;
  }
  return changed;
}

bool within(ui::Rect r, ui::Point p) {
  return p.x >= r.x && p.y >= r.y && p.x < r.right() && p.y < r.bottom();
}

std::size_t undoDepth(application::EditorSession& session) {
  std::size_t depth = 0U;
  while (session.canUndo()) {
    CHECK(session.undo().hasValue());
    ++depth;
  }
  return depth;
}

}  // namespace

TEST_CASE("MIX publishes one channel strip per project track with its document values") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto nodes = f.mixNodes();
  std::vector<SemanticNode> strips;
  for (const auto& node : nodes)
    if (node.id.starts_with("shell.mix.track.") && node.role == SemanticRole::Panel) strips.push_back(node);
  CHECK(strips.size() == f.tracks.size());
  for (std::size_t i = 0U; i < f.tracks.size(); ++i) {
    CHECK(strips[i].id == f.stripId(i));
    CHECK(strips[i].name == f.track(i).name);
    if (i > 0U) CHECK(strips[i].bounds.x > strips[i - 1U].bounds.right());
  }
  // The second track's document values: -6 dB, 50% left, muted, not soloed, routed to Master.
  const auto gain = f.node(f.controlId(1U, "gain"));
  CHECK(gain.role == SemanticRole::Slider);
  CHECK(gain.value == "-6.0 dB");
  CHECK_NEAR(*gain.numericValue, -6.0, 1e-9);
  CHECK(gain.numericMinimum.has_value() && gain.numericMaximum.has_value());
  const auto pan = f.node(f.controlId(1U, "pan"));
  CHECK(pan.role == SemanticRole::Slider);
  CHECK(pan.value == "Left 50%");
  CHECK_NEAR(*pan.numericValue, -50.0, 1e-9);
  const auto mute = f.node(f.controlId(1U, "mute"));
  CHECK(mute.role == SemanticRole::CheckBox && mute.selected && mute.value == "On");
  const auto solo = f.node(f.controlId(1U, "solo"));
  CHECK(solo.role == SemanticRole::CheckBox && !solo.selected && solo.value == "Off");
  const auto route = f.node(f.controlId(1U, "route"));
  CHECK(route.role == SemanticRole::Button && route.value == "Master");
  // With one output bus there is nowhere else to send a track, and the button says so.
  CHECK(!route.enabled);
  CHECK(!f.shell.dispatchSemantic(f.controller, route.id, SemanticAction::Activate).hasValue());
  // The master strip reports its bus and states that no output level is measured.
  const auto master = f.node("shell.mix.master");
  CHECK(master.role == SemanticRole::Status);
  CHECK(master.value.find("Master bus") != std::string::npos);
  CHECK(master.value.find("not measured") != std::string::npos);
  CHECK(!f.session.canUndo());
}

TEST_CASE("a fader drag and a pan drag each commit exactly one undoable mix command") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto startSelected = f.controller.selectedTrack();
  CHECK(startSelected != f.tracks[2]);
  // Fader on the third track: 40 points up raises it above unity. Nothing commits until release.
  const auto fader = f.node(f.controlId(2U, "gain")).bounds;
  const auto p = center(fader);
  CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({p.x, p.y - 20.0})).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({p.x, p.y - 40.0})).hasValue());
  CHECK(f.track(2).gainDb == 0.0F);
  CHECK(!f.session.canUndo());
  CHECK(f.shell.pointerUp(f.controller, press({p.x, p.y - 40.0})).hasValue());
  const auto raised = f.track(2).gainDb;
  CHECK(raised > 0.0F);
  CHECK(raised <= 24.0F);
  // The edit is addressed to the strip's track; the editor stays on the track it was on.
  CHECK(f.controller.selectedTrack() == startSelected);
  CHECK_NEAR(*f.node(f.controlId(2U, "gain")).numericValue, std::round(raised * 10.0) / 10.0, 1e-4);
  CHECK(f.session.canUndo());
  CHECK(f.session.undo().hasValue());
  CHECK(f.track(2).gainDb == 0.0F);
  CHECK(!f.session.canUndo());

  // Pan on the first track: 60 points up is a quarter of the 240-point sweep, so 0.5 right.
  CHECK(f.frame());
  const auto knob = f.node(f.controlId(0U, "pan")).bounds;
  const auto k = center(knob);
  CHECK(f.shell.pointerDown(f.controller, press(k)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({k.x, k.y - 60.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({k.x, k.y - 60.0})).hasValue());
  CHECK_NEAR(f.track(0).pan, 0.5, 1e-6);
  CHECK(f.track(0).gainDb == 0.0F);
  CHECK(!f.track(0).muted && !f.track(0).solo);
  CHECK(undoDepth(f.session) == 1U);
  CHECK_NEAR(f.track(0).pan, 0.0, 1e-9);

  // A press and release without movement commits nothing; a double-click centres as one command.
  CHECK(f.frame());
  const auto second = center(f.node(f.controlId(1U, "pan")).bounds);
  CHECK(f.shell.pointerDown(f.controller, press(second)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(second)).hasValue());
  CHECK(!f.session.canUndo());
  CHECK(f.shell.pointerDown(f.controller, press(second, false, 2)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(second, false, 2)).hasValue());
  CHECK(f.track(1).pan == 0.0F);
  CHECK(f.track(1).gainDb == -6.0F && f.track(1).muted);
  CHECK(undoDepth(f.session) == 1U);
  CHECK(f.track(1).pan == -0.5F);
}

TEST_CASE("mute and solo toggle through pointer and accessibility, one command each") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto startSelected = f.controller.selectedTrack();
  const auto mute = center(f.node(f.controlId(0U, "mute")).bounds);
  CHECK(f.shell.pointerDown(f.controller, press(mute)).hasValue());
  CHECK(!f.track(0).muted);  // commits on release
  CHECK(f.shell.pointerUp(f.controller, press(mute)).hasValue());
  CHECK(f.track(0).muted);
  CHECK(f.node(f.controlId(0U, "mute")).selected);
  // A press that leaves the button before release commits nothing.
  const auto solo = f.node(f.controlId(0U, "solo")).bounds;
  CHECK(f.shell.pointerDown(f.controller, press(center(solo))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({solo.right() + 30.0, solo.bottom() + 30.0})).hasValue());
  CHECK(!f.track(0).solo);
  // Accessibility Toggle, and Space on the focused box through the shell's key path.
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(0U, "solo"), SemanticAction::Toggle).hasValue());
  CHECK(f.track(0).solo);
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(2U, "mute"), SemanticAction::SetFocus).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Space}));
  CHECK(f.track(2).muted);
  CHECK(f.controller.selectedTrack() == startSelected);
  // Other fields are untouched, and each toggle is its own undo step.
  CHECK(f.track(0).gainDb == 0.0F && f.track(0).pan == 0.0F);
  CHECK(undoDepth(f.session) == 3U);
  CHECK(!f.track(0).muted && !f.track(0).solo && !f.track(2).muted);
}

TEST_CASE("Escape in the middle of a MIX drag commits nothing") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto revision = f.controller.documentRevision();
  const auto selected = f.controller.selectedTrack();
  for (const auto* control : {"gain", "pan"}) {
    const auto p = center(f.node(f.controlId(1U, control)).bounds);
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({p.x, p.y - 50.0})).hasValue());
    CHECK(f.shell.bodyWorkspace()->gestureActive());
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    // The first Escape abandons the gesture; MIX stays open.
    CHECK(f.shell.workspace() == Workspace::Mix);
    CHECK(!f.shell.bodyWorkspace()->gestureActive());
    CHECK(f.shell.pointerUp(f.controller, press({p.x, p.y - 50.0})).hasValue());
  }
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
  CHECK(f.track(1).gainDb == -6.0F && f.track(1).pan == -0.5F);
  CHECK(f.controller.selectedTrack() == selected);
}

TEST_CASE("MIX sliders answer Increment and arrow keys, and node bounds are the hit rectangles") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(0U, "gain"), SemanticAction::Increment).hasValue());
  CHECK_NEAR(f.track(0).gainDb, 1.0, 1e-6);
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(0U, "pan"), SemanticAction::Decrement).hasValue());
  CHECK_NEAR(f.track(0).pan, -0.05, 1e-6);
  // Keys on a focused fader: Up is one step, Shift-Up a fine step; each is one command.
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(0U, "gain"), SemanticAction::SetFocus).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
  CHECK_NEAR(f.track(0).gainDb, 2.0, 1e-5);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up, .modifiers = {.shift = true}}));
  CHECK_NEAR(f.track(0).gainDb, 2.1, 1e-5);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Down}));
  CHECK_NEAR(f.track(0).gainDb, 1.1, 1e-5);
  CHECK(f.focusedId() == f.controlId(0U, "gain"));
  // Mute and solo are check boxes: Increment is not one of their actions.
  CHECK(!f.shell.dispatchSemantic(f.controller, f.controlId(0U, "mute"), SemanticAction::Increment).hasValue());
  CHECK(undoDepth(f.session) == 5U);

  // Every control is hit exactly within its published bounds, and the press focuses it.
  CHECK(f.frame());
  for (const auto& id : {f.controlId(1U, "gain"), f.controlId(1U, "pan"), f.controlId(1U, "mute"),
                         f.controlId(1U, "solo"), f.controlId(1U, "route"),
                         std::string{"shell.mix.audio-settings"}}) {
    const auto b = f.node(id).bounds;
    for (const auto p : {ui::Point{b.x + 0.5, b.y + 0.5}, ui::Point{b.right() - 0.5, b.bottom() - 0.5},
                         center(b)}) {
      CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
      CHECK(f.focusedId() == id);
      // Every one of these presses starts a gesture; Escape abandons it.
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
      CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
    }
    // Just outside the bounds the press reaches something else.
    const ui::Point outside{b.x - 1.0, b.y - 1.0};
    CHECK(f.shell.pointerDown(f.controller, press(outside)).hasValue());
    CHECK(f.focusedId() != id);
    if (f.shell.bodyWorkspace()->gestureActive())
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(f.shell.pointerUp(f.controller, press(outside)).hasValue());
    CHECK(f.shell.workspace() == Workspace::Mix);
  }
  CHECK(!f.session.canUndo());
  CHECK(!f.controller.audioSettingsVisible());
}

TEST_CASE("the route button cycles the output bus and the audio card opens the real settings") {
  MixFixture f{2U, true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto route = f.node(f.controlId(0U, "route"));
  CHECK(route.enabled && route.value == "Master");
  const auto p = center(route.bounds);
  CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
  CHECK(f.track(0).outputRoute.bus == domain::BusId{2U});
  CHECK(f.node(f.controlId(0U, "route")).value == "Stems");
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(0U, "route"), SemanticAction::Activate).hasValue());
  CHECK(f.track(0).outputRoute.bus == domain::BusId{1U});
  CHECK(undoDepth(f.session) == 2U);
  CHECK(f.track(0).outputRoute.bus == domain::BusId{1U});

  // The settings button opens the existing audio settings; the shell yields to that surface.
  CHECK(f.frame());
  const auto settings = f.node("shell.mix.audio-settings");
  CHECK(settings.role == SemanticRole::Button);
  const auto s = center(settings.bounds);
  CHECK(f.shell.pointerDown(f.controller, press(s)).hasValue());
  CHECK(!f.controller.audioSettingsVisible());
  CHECK(f.shell.pointerUp(f.controller, press(s)).hasValue());
  CHECK(f.controller.audioSettingsVisible());
  CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(!f.session.canUndo());
}

TEST_CASE("a MIX edit on another strip leaves the editor's track, region and notes selected") {
  MixFixture f{3U, true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // An audio strip too: soloing one must not leave the editor without a region.
  const domain::TrackId audioId{9990U};
  f.session.project().audioTracks().push_back(domain::AudioTrack{
      .id = audioId, .name = "Backing", .mediaPath = "/tmp/backing.wav", .mediaHash = "backing"});
  f.openMix();
  f.session.selection().selectOnly(f.noteId);
  const auto track = f.controller.selectedTrack();
  const auto region = f.controller.selectedRegion();
  CHECK(track == f.tracks[0]);
  CHECK(region == f.regionId);
  const auto selectionUnchanged = [&] {
    CHECK(f.controller.selectedTrack() == track);
    CHECK(f.controller.selectedRegion() == region);
    CHECK(f.controller.pianoRoll().regionId() == region);
    CHECK(f.session.selection().size() == 1U);
    CHECK(f.session.selection().contains(f.noteId));
  };

  // Mute on the third strip: one command, the selection stays, one undo restores the value.
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(2U, "mute"), SemanticAction::Toggle).hasValue());
  CHECK(f.track(2).muted);
  selectionUnchanged();
  CHECK(f.session.undo().hasValue());
  CHECK(!f.track(2).muted);
  CHECK(!f.session.canUndo());
  selectionUnchanged();

  // Pan on the second strip.
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(1U, "pan"), SemanticAction::Increment).hasValue());
  CHECK_NEAR(f.track(1).pan, -0.45, 1e-6);
  CHECK(f.track(1).gainDb == -6.0F && f.track(1).muted);
  selectionUnchanged();
  CHECK(f.session.undo().hasValue());
  CHECK(f.track(1).pan == -0.5F);
  CHECK(!f.session.canUndo());

  // Route on the third strip.
  CHECK(f.shell.dispatchSemantic(f.controller, f.controlId(2U, "route"), SemanticAction::Activate).hasValue());
  CHECK(f.track(2).outputRoute.bus == domain::BusId{2U});
  selectionUnchanged();
  CHECK(f.session.undo().hasValue());
  CHECK(f.track(2).outputRoute.bus == domain::BusId{1U});
  CHECK(!f.session.canUndo());

  // Solo on the audio strip.
  const auto audioSolo = "shell.mix.track." + audioId.toString() + ".solo";
  CHECK(f.shell.dispatchSemantic(f.controller, audioSolo, SemanticAction::Toggle).hasValue());
  CHECK(f.session.project().audioTracks().front().solo);
  selectionUnchanged();
  CHECK(f.controller.selectedRegion().valid());
  CHECK(f.session.undo().hasValue());
  CHECK(!f.session.project().audioTracks().front().solo);
  CHECK(!f.session.canUndo());
  selectionUnchanged();
}

TEST_CASE("the device card states a format only for a device the host reported as running") {
  MixFixture f{1U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  // Nothing reported: the settings defaults are no device's format.
  auto device = f.node("shell.mix.device").value;
  CHECK(device.find("No device reported") != std::string::npos);
  CHECK(device.find("kHz") == std::string::npos);
  // A running backend without reported settings is still no format.
  f.controller.setAudioState(true, "CoreAudio");
  device = f.node("shell.mix.device").value;
  CHECK(device == "CoreAudio, No device reported");

  f.controller.setAudioSettings(
      authoring::AudioSettings{.deviceId = "studio", .sampleRate = 44100U, .blockFrames = 512U},
      {native_ui::EditorSceneState::AudioDeviceOption{.id = "studio", .name = "Studio", .selected = true}},
      0U, 0U);
  CHECK(f.node("shell.mix.device").value == "Studio, 44.1 kHz \u00b7 512 frames");
  // The same device, stopped: its last settings are not a running format.
  f.controller.setAudioState(false, "CoreAudio");
  device = f.node("shell.mix.device").value;
  CHECK(device == "Studio, Device not running");
}

TEST_CASE("MIX strips never overlap or leave the body, and scroll sideways when they do not fit") {
  MixFixture f{7U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  for (const auto& [width, height] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                                      std::pair{1100.0, 720.0}, std::pair{1600.0, 900.0}}) {
    CHECK(f.frame(width, height));
    CHECK(f.shell.workspace() == Workspace::Mix);
    const auto area = f.shell.workspaceArea();
    const auto nodes = f.mixNodes();
    std::vector<SemanticNode> strips;
    std::vector<SemanticNode> panels;  // arrangement, master, device card, settings, scroll band
    std::vector<SemanticNode> regions;
    for (const auto& node : nodes) {
      CHECK(node.bounds.width > 0.0 && node.bounds.height > 0.0);
      CHECK(inside(node.bounds, area));
      if (node.id == "shell.mix.panel" || node.id == "shell.mix.master-meter") continue;
      const auto isStrip = node.id.starts_with("shell.mix.track.") &&
                           node.id.find('.', std::string_view{"shell.mix.track."}.size()) == std::string::npos;
      if (isStrip) strips.push_back(node);
      else if (node.id.starts_with("shell.mix.region.")) regions.push_back(node);
      else if (!node.id.starts_with("shell.mix.track.")) panels.push_back(node);
    }
    CHECK(!strips.empty());
    // Strips keep their minimum size: compact strips where the body is short, regular ones above.
    for (const auto& strip : strips) CHECK(strip.bounds.height >= 112.0 - 1e-6);
    // The meter sits inside the master strip.
    CHECK(inside(f.node("shell.mix.master-meter").bounds, f.node("shell.mix.master").bounds));
    // The arrangement strip hides only where compact strips need the whole body (480x320). Where
    // shown, it holds one region per track, lane by lane in track order, none overlapping.
    const auto arrangementShown = f.published("shell.mix.arrangement");
    CHECK(arrangementShown == (height > 320.0));
    if (arrangementShown) {
      const auto arrangement = f.node("shell.mix.arrangement").bounds;
      CHECK_NEAR(arrangement.width, area.width - 24.0, 1e-6);  // full body width
      CHECK(regions.size() == 7U);
      for (std::size_t i = 0U; i < regions.size(); ++i) {
        CHECK(regions[i].id == f.regionNode(i));
        CHECK(inside(regions[i].bounds, arrangement));
        if (i > 0U) CHECK(regions[i].bounds.y >= regions[i - 1U].bounds.bottom() - 1e-6);
        for (std::size_t j = i + 1U; j < regions.size(); ++j) CHECK(!overlaps(regions[i].bounds, regions[j].bounds));
      }
      for (const auto& strip : strips) CHECK(strip.bounds.y >= arrangement.bottom());
    } else {
      CHECK(regions.empty());
    }
    for (std::size_t i = 0U; i < strips.size(); ++i) {
      for (std::size_t j = i + 1U; j < strips.size(); ++j) CHECK(!overlaps(strips[i].bounds, strips[j].bounds));
      std::vector<ui::Rect> controls;
      for (const auto& node : nodes)
        if (node.id.starts_with(strips[i].id + ".")) controls.push_back(node.bounds);
      CHECK(!controls.empty());
      for (std::size_t a = 0U; a < controls.size(); ++a) {
        CHECK(inside(controls[a], strips[i].bounds));
        for (std::size_t b = a + 1U; b < controls.size(); ++b) CHECK(!overlaps(controls[a], controls[b]));
      }
      for (const auto& panel : panels) CHECK(!overlaps(panel.bounds, strips[i].bounds));
    }
    // The arrangement, master, device card (with its settings button inside) and scroll band stay
    // apart.
    for (std::size_t a = 0U; a < panels.size(); ++a)
      for (std::size_t b = a + 1U; b < panels.size(); ++b) {
        const auto& x = panels[a];
        const auto& y = panels[b];
        const auto nested = (x.id == "shell.mix.device" && y.id == "shell.mix.audio-settings") ||
                            (y.id == "shell.mix.device" && x.id == "shell.mix.audio-settings");
        if (nested) CHECK(inside(x.id == "shell.mix.device" ? y.bounds : x.bounds,
                                 x.id == "shell.mix.device" ? x.bounds : y.bounds));
        else CHECK(!overlaps(x.bounds, y.bounds));
      }
    // Seven strips fit only at the widest body; everywhere else the strips scroll.
    const auto scrolls = f.published("shell.mix.scroll");
    CHECK(scrolls == (width < 1600.0));
    if (!scrolls) {
      CHECK(strips.size() == 7U);
      continue;
    }
    // Strips scroll by whole strips and are never cut at the viewport edge.
    const auto viewportRight = f.node("shell.mix.master").bounds.x;
    for (const auto& strip : strips) CHECK(strip.bounds.right() <= viewportRight);
    const auto widths = strips.front().bounds.width;
    for (const auto& strip : strips) CHECK_NEAR(strip.bounds.width, widths, 1e-6);
    // A sideways scroll moves the strips; Increment walks to the last track, which becomes visible.
    const auto before = f.node(f.stripId(0U)).bounds;
    CHECK(f.shell.scroll(f.controller, 30.0, 0.0, center(area), {}));
    CHECK(f.node(f.stripId(0U)).bounds.x == before.x);  // less than half a strip: no move yet
    CHECK(f.shell.scroll(f.controller, before.width, 0.0, center(area), {}));
    CHECK(!f.published(f.stripId(0U)));
    CHECK(f.published(f.stripId(1U)));
    CHECK_NEAR(f.node(f.stripId(1U)).bounds.x, before.x, 1e-6);
    for (int i = 0; i < 10; ++i)
      CHECK(f.shell.dispatchSemantic(f.controller, "shell.mix.scroll", SemanticAction::Increment).hasValue());
    CHECK(f.published(f.stripId(6U)));
    const auto last = f.node(f.stripId(6U)).bounds;
    CHECK_NEAR(last.width, widths, 1e-6);  // the last strip is shown whole at the end of the range
    CHECK(last.right() <= viewportRight);
    for (int i = 0; i < 10; ++i)
      CHECK(f.shell.dispatchSemantic(f.controller, "shell.mix.scroll", SemanticAction::Decrement).hasValue());
    CHECK(f.published(f.stripId(0U)));
    CHECK_NEAR(f.node(f.stripId(0U)).bounds.x, before.x, 1e-6);
  }
  CHECK(!f.session.canUndo());
}

TEST_CASE("the master meter lights from a measured output level and stays an empty scale without one") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto revision = f.controller.documentRevision();
  const auto master = f.node("shell.mix.master").bounds;
  auto meter = f.node("shell.mix.master-meter");
  CHECK(meter.role == SemanticRole::Status);
  CHECK(meter.value == "Not measured");
  CHECK(!meter.numericValue.has_value());
  CHECK(inside(meter.bounds, master));
  CHECK(f.node("shell.mix.master").value.find("not measured") != std::string::npos);
  const auto empty = f.pixels;

  // Stereo, -6 dBFS left and -12 dBFS right, held peaks -3.1 and -10.5, clipped since the reset.
  f.controller.setOutputLevel(native_ui::EditorSceneState::OutputLevel{
      .peak = {0.5F, 0.25F}, .hold = {0.7F, 0.3F}, .bus = "Master", .clipped = true});
  CHECK(f.frame());
  meter = f.node("shell.mix.master-meter");
  CHECK(meter.role == SemanticRole::ProgressIndicator);
  CHECK(meter.value == "-3.1 dBFS");
  CHECK_NEAR(*meter.numericValue, -3.1, 1e-9);
  CHECK(*meter.numericMinimum == -60.0);
  CHECK(*meter.numericMaximum == 0.0);
  CHECK(meter.description == "Master bus: left -6.0 dBFS, right -12.0 dBFS. Clipped.");
  CHECK(f.node("shell.mix.master").value.find("Output peak -3.1 dBFS, clipped") != std::string::npos);
  // The bars, hold markers, clip light and readout change the meter well and nothing else in MIX.
  const auto area = f.shell.workspaceArea();
  const auto lit = f.pixels;
  CHECK(changedPixels(empty, lit, f.pixelWidth, [&](ui::Point p) { return within(meter.bounds, p); }) > 200U);
  CHECK(changedPixels(empty, lit, f.pixelWidth,
                      [&](ui::Point p) { return within(area, p) && !within(meter.bounds, p); }) == 0U);

  // A silent output is measured too: the scale floor, not "Not measured".
  f.controller.setOutputLevel(native_ui::EditorSceneState::OutputLevel{.peak = {0.0F, 0.0F}, .hold = {}, .bus = "Master"});
  meter = f.node("shell.mix.master-meter");
  CHECK(meter.role == SemanticRole::ProgressIndicator);
  CHECK(meter.value == "-inf dBFS");
  CHECK(*meter.numericValue == -60.0);

  // When the host stops measuring, the meter returns to the empty scale it started with.
  f.controller.setOutputLevel(std::nullopt);
  CHECK(f.frame());
  CHECK(f.node("shell.mix.master-meter").value == "Not measured");
  CHECK(changedPixels(empty, f.pixels, f.pixelWidth, [&](ui::Point p) { return within(area, p); }) == 0U);
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
}

TEST_CASE("an arrangement region click selects its track and region without changing the document") {
  MixFixture f{3U};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.openMix();
  const auto arrangement = f.node("shell.mix.arrangement");
  CHECK(arrangement.role == SemanticRole::Timeline);
  CHECK(arrangement.value == "3 regions, playhead at bar 1");
  std::vector<SemanticNode> nodes;
  for (std::size_t i = 0U; i < 3U; ++i) {
    const auto node = f.node(f.regionNode(i));
    CHECK(node.role == SemanticRole::Button);
    CHECK(node.name == "Voice " + std::to_string(i + 1U) + ", Phrase, bars 1 to 2");
    CHECK(node.selected == (i == 0U));
    CHECK(inside(node.bounds, arrangement.bounds));
    // Every region starts at the song start and lasts two bars of the four-bar axis.
    if (i > 0U) {
      CHECK(node.bounds.y >= nodes.back().bounds.bottom());
      CHECK_NEAR(node.bounds.x, nodes.back().bounds.x, 1e-6);
      CHECK_NEAR(node.bounds.width, nodes.back().bounds.width, 1e-6);
    }
    nodes.push_back(node);
  }
  f.session.selection().selectOnly(f.noteId);
  const auto revision = f.controller.documentRevision();

  // One click selects the second track and its region as view state only.
  const auto second = center(nodes[1].bounds);
  CHECK(f.shell.pointerDown(f.controller, press(second)).hasValue());
  CHECK(!f.shell.bodyWorkspace()->gestureActive());
  CHECK(f.shell.pointerUp(f.controller, press(second)).hasValue());
  CHECK(f.controller.selectedTrack() == f.tracks[1]);
  CHECK(f.controller.selectedRegion() == f.region(1));
  CHECK(f.controller.pianoRoll().regionId() == f.region(1));
  CHECK(f.shell.workspace() == Workspace::Mix);
  CHECK(f.focusedId() == f.regionNode(1));
  CHECK(f.node(f.regionNode(1)).selected);
  CHECK(!f.node(f.regionNode(0)).selected);
  CHECK(f.node(f.stripId(1U)).selected);
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());

  // Activate does the same for assistive technology.
  CHECK(f.shell.dispatchSemantic(f.controller, f.regionNode(2), SemanticAction::Activate).hasValue());
  CHECK(f.controller.selectedTrack() == f.tracks[2]);
  CHECK(f.controller.selectedRegion() == f.region(2));
  CHECK(!f.shell.dispatchSemantic(f.controller, f.regionNode(2), SemanticAction::Increment).hasValue());

  // Node bounds are the hit rectangles: every corner selects that region, just past its end is
  // the empty lane (which focuses the arrangement and leaves the selection alone).
  CHECK(f.frame());
  for (std::size_t i = 0U; i < 3U; ++i) {
    const auto b = f.node(f.regionNode(i)).bounds;
    for (const auto p : {ui::Point{b.x + 0.5, b.y + 0.5}, ui::Point{b.right() - 0.5, b.bottom() - 0.5},
                         ui::Point{b.x + 0.5, b.bottom() - 0.5}, center(b)}) {
      CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
      CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
      CHECK(f.focusedId() == f.regionNode(i));
      CHECK(f.controller.selectedRegion() == f.region(i));
    }
    const ui::Point past{b.right() + 1.5, b.y + b.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(past)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(past)).hasValue());
    CHECK(f.focusedId() == "shell.mix.arrangement");
    CHECK(f.controller.selectedRegion() == f.region(i));
  }
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());

  // A double-click opens SING on the region.
  const auto target = center(f.node(f.regionNode(1)).bounds);
  CHECK(f.shell.pointerDown(f.controller, press(target)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(target)).hasValue());
  CHECK(f.shell.workspace() == Workspace::Mix);
  CHECK(f.shell.pointerDown(f.controller, press(target, false, 2)).hasValue());
  CHECK(f.shell.workspace() == Workspace::Sing);
  CHECK(f.shell.pointerUp(f.controller, press(target, false, 2)).hasValue());
  CHECK(f.controller.selectedTrack() == f.tracks[1]);
  CHECK(f.controller.selectedRegion() == f.region(1));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
  CHECK(f.frame());
}
