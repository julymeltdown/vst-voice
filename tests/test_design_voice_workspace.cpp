// The VOICE workspace, driven through a real SingShell, NativeEditorController and a real Voice
// Designer session: knob and formant gestures as single session undo steps, Escape, pose chips,
// the protected-root save refusal, the honest panel of a host without a designer, geometry at every
// contract size and accessibility nodes whose bounds are the hit rectangles.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/design/voice_workspace.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/native_ui/voice_designer_session.hpp"
#include "seam/voice_design/voice_recipe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
using native_ui::VoiceDesignerSession;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::ShellHostActions;
using native_ui::design::SingShell;
using native_ui::design::Workspace;

struct VoiceFixture final {
  application::ProjectFactory factory{9700U};
  domain::RegionId regionId{};
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;
  native_ui::PixelSurface surface{1600U, 900U};
  double width{1600.0};
  double height{900.0};
  VoiceDesignerSession designer;
  std::optional<std::filesystem::path> nextPath;
  std::vector<std::shared_ptr<const voicebank::AudioBuffer>> played;
  std::optional<float> level;

  explicit VoiceFixture(bool withDesigner = true)
      : session(makeProject()),
        controller{session, factory, regionId, native_ui::EditorHostCallbacks{}} {
    controller.resize(width, height);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
    if (!withDesigner) return;
    if (!designer.createJapaneseStarter()) throw test::Failure{"starter recipe was refused"};
    ShellHostActions actions;
    actions.voice.designer = [this] { return &designer; };
    actions.voice.choosePath = [this](bool, const std::filesystem::path&)
        -> core::Result<std::optional<std::filesystem::path>> { return nextPath; };
    actions.voice.play = [this](std::shared_ptr<const voicebank::AudioBuffer> audio) -> core::Result<void> {
      played.push_back(std::move(audio));
      level = 0.5F;
      return core::success();
    };
    actions.voice.stop = [this] { level.reset(); };
    actions.voice.level = [this] { return level; };
    shell.setHostActions(std::move(actions));
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Design voice");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    const auto trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{1920}, 72U, U"\u3042",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }

  const voice_design::VoiceRecipe& recipe() const { return designer.model()->recipe(); }
  bool canUndo() const { return designer.model()->canUndo(); }

  bool frame() {
    if (!shell.prepareFrame(controller, width, height)) return false;
    static_cast<void>(surface.resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)));
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }

  bool frameAt(double w, double h) {
    width = w;
    height = h;
    controller.resize(w, h);
    return frame();
  }

  bool openVoice() {
    if (!frame()) return false;
    if (!shell.dispatchSemantic(controller, "shell.workspace.voice", SemanticAction::Activate)) return false;
    return shell.workspace() == Workspace::Voice && frame();
  }

  std::vector<SemanticNode> nodes() {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    return shell.accessibilityTree().root().children;
  }

  // Every published node, children included.
  std::vector<SemanticNode> flat() {
    std::vector<SemanticNode> out;
    const auto walk = [&out](const SemanticNode& node, const auto& self) -> void {
      out.push_back(node);
      for (const auto& child : node.children) self(child, self);
    };
    for (const auto& node : nodes()) walk(node, walk);
    return out;
  }

  std::optional<SemanticNode> node(std::string_view id) {
    for (const auto& n : flat())
      if (n.id == id) return n;
    return std::nullopt;
  }

  ui::Rect bounds(std::string_view id) {
    const auto found = node(id);
    if (!found) throw test::Failure{"missing node " + std::string{id}};
    return found->bounds;
  }

  std::string focusedId() {
    static_cast<void>(nodes());
    const auto* focused = shell.accessibilityTree().focusedNode();
    return focused == nullptr ? std::string{} : focused->id;
  }
};

PointerEvent press(ui::Point p, bool shift = false) {
  return {.position = p, .button = PointerButton::Left, .modifiers = {.shift = shift}};
}

ui::Point center(ui::Rect r) { return {r.x + r.width * 0.5, r.y + r.height * 0.5}; }

bool intersects(ui::Rect a, ui::Rect b) {
  return a.x < b.right() - 0.01 && b.x < a.right() - 0.01 && a.y < b.bottom() - 0.01 &&
         b.y < a.bottom() - 0.01;
}

bool inside(ui::Rect inner, ui::Rect outer) {
  return inner.x >= outer.x - 0.01 && inner.y >= outer.y - 0.01 &&
         inner.right() <= outer.right() + 0.01 && inner.bottom() <= outer.bottom() + 0.01;
}

}  // namespace

TEST_CASE("VOICE opens from its tab and shows the three modules and the envelope editor") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  CHECK(!f.controller.voicebankBrowserVisible());
  for (const auto* id : {"shell.voice.source", "shell.voice.resonance", "shell.voice.noise",
                         "shell.voice.output", "shell.voice.envelope"})
    CHECK(f.node(id).has_value());
  // Values come from the session's recipe: the starter's first pose is "a" with F1 at 800 Hz.
  const auto f1 = f.node("shell.voice.formant.1");
  CHECK(f1.has_value());
  if (f1) CHECK(f1->value.starts_with("800 Hz"));
  const auto knob = f.node("shell.voice.knob.pitch-depth");
  CHECK(knob.has_value());
  if (knob) {
    CHECK(knob->name == "Pitch depth \u00A2");
    CHECK(knob->numericValue == f.recipe().modulation.jitterCents);
  }
  // Escape leaves VOICE for SING.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.workspace() == Workspace::Sing);
}

TEST_CASE("a VOICE knob drag is one session undo step with the recipe value changed") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  CHECK(!f.canUndo());
  const auto before = f.recipe().phonation.openQuotient;
  const auto knob = center(f.bounds("shell.voice.knob.open-quotient"));
  CHECK(f.shell.pointerDown(f.controller, press(knob)).hasValue());
  CHECK(f.designer.model()->gestureActive());
  CHECK(f.shell.pointerMove(f.controller, press({knob.x, knob.y - 24.0})).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({knob.x, knob.y - 48.0})).hasValue());
  // Previewed, not committed: nothing to undo until release.
  CHECK(!f.canUndo());
  CHECK(f.shell.pointerUp(f.controller, press({knob.x, knob.y - 48.0})).hasValue());
  CHECK(!f.designer.model()->gestureActive());
  // 48 of 240 points over the 0.05..0.95 range.
  CHECK(std::abs(f.recipe().phonation.openQuotient - (before + 0.18)) < 1e-9);
  CHECK(f.canUndo());
  CHECK(f.designer.undo(f.designer.epoch(), f.designer.model()->revision()).hasValue());
  CHECK(f.recipe().phonation.openQuotient == before);
  CHECK(!f.canUndo());
  // A keyboard step is one edit too, and Command-Z in VOICE undoes the designer, not the song.
  const auto songRevision = f.controller.documentRevision();
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.knob.rate", SemanticAction::Increment).hasValue());
  CHECK(std::abs(f.recipe().modulation.rateHz - 0.1) < 1e-9);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Z, .modifiers = {.command = true}}));
  CHECK(f.recipe().modulation.rateHz == 0.0);
  CHECK(f.controller.documentRevision() == songRevision);
  CHECK(f.shell.routeUndo(true).has_value());
  CHECK(std::abs(f.recipe().modulation.rateHz - 0.1) < 1e-9);
}

TEST_CASE("a formant handle drag moves frequency and gain; Shift-drag sets its bandwidth") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  const auto start = f.recipe().poses[0].formants[1];  // F2 of "a": 1250 Hz, -3 dB, 110 Hz
  const auto handle = center(f.bounds("shell.voice.formant.2"));
  const auto plot = f.bounds("shell.voice.envelope");
  CHECK(inside(f.bounds("shell.voice.formant.2"), plot));
  CHECK(f.shell.pointerDown(f.controller, press(handle)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({handle.x + 30.0, handle.y - 20.0})).hasValue());
  CHECK(!f.canUndo());
  CHECK(f.shell.pointerUp(f.controller, press({handle.x + 30.0, handle.y - 20.0})).hasValue());
  const auto moved = f.recipe().poses[0].formants[1];
  CHECK(moved.frequencyHz > start.frequencyHz + 50.0);
  CHECK(moved.gainDb > start.gainDb + 1.0);
  CHECK(moved.bandwidthHz == start.bandwidthHz);
  CHECK(f.canUndo());
  // Shift-drag: 60 points to the right doubles the bandwidth and leaves frequency and gain.
  CHECK(f.frame());
  const auto again = center(f.bounds("shell.voice.formant.2"));
  CHECK(f.shell.pointerDown(f.controller, press(again, true)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({again.x + 60.0, again.y + 15.0}, true)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({again.x + 60.0, again.y + 15.0}, true)).hasValue());
  const auto widened = f.recipe().poses[0].formants[1];
  CHECK(std::abs(widened.bandwidthHz - 2.0 * start.bandwidthHz) <= 1.0);
  CHECK(widened.frequencyHz == moved.frequencyHz);
  CHECK(widened.gainDb == moved.gainDb);
  // Two gestures, two undo steps.
  CHECK(f.designer.undo(f.designer.epoch(), f.designer.model()->revision()).hasValue());
  CHECK(f.recipe().poses[0].formants[1] == moved);
  CHECK(f.designer.undo(f.designer.epoch(), f.designer.model()->revision()).hasValue());
  CHECK(f.recipe().poses[0].formants[1] == start);
}

TEST_CASE("Escape in the middle of a VOICE drag commits nothing and stays in VOICE") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  const auto original = f.recipe();
  const auto knob = center(f.bounds("shell.voice.knob.tilt"));
  CHECK(f.shell.pointerDown(f.controller, press(knob)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({knob.x, knob.y - 60.0})).hasValue());
  CHECK(f.recipe().phonation.spectralTiltDbPerOctave != original.phonation.spectralTiltDbPerOctave);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.workspace() == Workspace::Voice);
  CHECK(f.shell.pointerUp(f.controller, press({knob.x, knob.y - 60.0})).hasValue());
  CHECK(f.recipe() == original);
  CHECK(!f.canUndo());
  const auto handle = center(f.bounds("shell.voice.formant.1"));
  CHECK(f.shell.pointerDown(f.controller, press(handle)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({handle.x + 40.0, handle.y - 30.0})).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.pointerUp(f.controller, press({handle.x + 40.0, handle.y - 30.0})).hasValue());
  CHECK(f.recipe() == original);
  CHECK(!f.canUndo());
  CHECK(!f.designer.model()->gestureActive());
  CHECK(f.shell.workspace() == Workspace::Voice);
}

TEST_CASE("a VOICE pose chip selects the pose the envelope edits") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  CHECK(f.node("shell.voice.pose.0").has_value() && f.node("shell.voice.pose.0")->selected);
  CHECK(f.node("shell.voice.pose.2").has_value());
  CHECK(f.node("shell.voice.pose.2")->role == SemanticRole::RadioButton);
  // By pointer: "u", whose F1 is 350 Hz.
  CHECK(f.shell.pointerDown(f.controller, press(center(f.bounds("shell.voice.pose.2")))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(center(f.bounds("shell.voice.pose.2")))).hasValue());
  CHECK(f.designer.auditionPose() == 2U);
  CHECK(f.frame());
  CHECK(f.node("shell.voice.pose.2")->selected);
  CHECK(f.node("shell.voice.formant.1")->value.starts_with("350 Hz"));
  // By accessibility: "i".
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.pose.1", SemanticAction::Activate).hasValue());
  CHECK(f.designer.auditionPose() == 1U);
  CHECK(f.node("shell.voice.formant.1")->value.starts_with("300 Hz"));
  // Selecting a pose is not a recipe edit.
  CHECK(!f.canUndo());
}

TEST_CASE("VOICE refuses Save As into an installed singer's root and writes nothing") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto root = test::support::temporaryDirectory("voice-workspace-protected");
  const auto singers = root / "Singers";
  std::filesystem::create_directories(singers);
  f.designer.setProtectedRoots({singers});
  f.nextPath = singers / "draft.json";
  CHECK(f.openVoice());
  // Save As is a SOURCE card action.
  CHECK(f.shell.pointerDown(f.controller, press(center(f.bounds("shell.voice.source.more")))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(center(f.bounds("shell.voice.source.more")))).hasValue());
  CHECK(f.node("shell.voice.menu.save-as").has_value());
  const auto refused = f.shell.dispatchSemantic(f.controller, "shell.voice.menu.save-as", SemanticAction::Activate);
  CHECK(!refused.hasValue());
  if (!refused) CHECK(refused.error().message == "A draft cannot be saved inside an installed singer");
  CHECK(!f.designer.busy());
  CHECK(!std::filesystem::exists(singers / "draft.json"));
  CHECK(f.designer.path().empty());
  // A path outside the protected root saves through the same command.
  f.nextPath = root / "draft.json";
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.source.more", SemanticAction::Activate).hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.menu.save-as", SemanticAction::Activate).hasValue());
  // The save runs in the background; the workspace collects it on the frames that follow.
  for (int i = 0; i < 500 && f.designer.busy(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    CHECK(f.frame());
  }
  CHECK(f.frame());
  CHECK(std::filesystem::exists(root / "draft.json"));
  CHECK(!f.designer.model()->dirty());
  CHECK(f.node("shell.voice.recipe")->description == "Saved draft.json");
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST_CASE("a host without a designer shows VOICE's honest panel and offers the browser") {
  VoiceFixture f{false};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  const auto panel = f.node("shell.voice.unavailable");
  CHECK(panel.has_value());
  if (panel) CHECK(panel->value == "Voice design runs in the standalone Project SEAM app.");
  CHECK(!f.node("shell.voice.source").has_value());
  CHECK(!f.node("shell.voice.envelope").has_value());
  const auto browser = f.node("shell.voice.browser");
  CHECK(browser.has_value() && browser->role == SemanticRole::Button);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.browser", SemanticAction::Activate).hasValue());
  CHECK(f.controller.voicebankBrowserVisible());
}

TEST_CASE("VOICE controls never overlap or leave the workspace at any contract size") {
  for (const auto& [w, h] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                             std::pair{1100.0, 720.0}, std::pair{1600.0, 900.0}}) {
    VoiceFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.openVoice());
    CHECK(f.frameAt(w, h));
    const auto area = f.shell.workspaceArea();
    std::vector<std::string> views{""};
    if (f.node("shell.voice.view.source")) views = {"source", "resonance", "noise"};
    for (const auto& view : views) {
      if (!view.empty()) {
        CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.view." + view, SemanticAction::Activate).hasValue());
        CHECK(f.frame());
      }
      std::vector<SemanticNode> voice;
      for (const auto& node : f.nodes())
        if (node.id.starts_with("shell.voice.")) voice.push_back(node);
      // Each module is either shown or one tab away; the envelope is there when RESONANCE is.
      for (const auto* card : {"source", "resonance", "noise"}) {
        const auto shown = std::any_of(voice.begin(), voice.end(), [&](const SemanticNode& n) {
          return n.id == std::string{"shell.voice."} + card || n.id == std::string{"shell.voice.view."} + card;
        });
        CHECK(shown);
      }
      const auto resonanceShown = std::any_of(voice.begin(), voice.end(), [](const SemanticNode& n) {
        return n.id == "shell.voice.resonance";
      });
      const auto envelope = std::find_if(voice.begin(), voice.end(), [](const SemanticNode& n) {
        return n.id == "shell.voice.envelope";
      });
      CHECK(resonanceShown == (envelope != voice.end()));
      if (envelope != voice.end()) {
        CHECK(envelope->bounds.width >= 60.0 && envelope->bounds.height >= 30.0);
        for (const auto& handle : envelope->children) CHECK(inside(handle.bounds, envelope->bounds));
      }
      for (std::size_t i = 0U; i < voice.size(); ++i) {
        const auto& a = voice[i];
        CHECK(a.bounds.width >= 8.0 && a.bounds.height >= 8.0);
        if (!inside(a.bounds, area))
          throw test::Failure{a.id + " leaves the workspace at " + std::to_string(w) + "x" + std::to_string(h)};
        if (a.role == SemanticRole::Panel) continue;  // a module holds its own controls
        for (std::size_t j = i + 1U; j < voice.size(); ++j) {
          const auto& b = voice[j];
          if (b.role == SemanticRole::Panel) continue;
          if (intersects(a.bounds, b.bounds))
            throw test::Failure{a.id + " overlaps " + b.id + " at " + std::to_string(w) + "x" +
                                std::to_string(h) + (view.empty() ? "" : " in " + view)};
        }
      }
      // Controls sit inside the module that owns them.
      for (const auto& card : voice) {
        if (card.role != SemanticRole::Panel) continue;
        const auto name = card.id.substr(std::string_view{"shell.voice."}.size());
        const std::string prefix = name == "source" ? "shell.voice.knob." : name == "resonance" ? "shell.voice.pose."
                                   : name == "noise" ? "shell.voice.frication." : "shell.voice.ab.";
        for (const auto& control : voice)
          if (control.id.starts_with(prefix) && !inside(control.bounds, card.bounds))
            throw test::Failure{control.id + " is outside " + card.id + " at " + std::to_string(w) + "x" +
                                std::to_string(h)};
      }
    }
  }
}

TEST_CASE("VOICE accessibility nodes have real roles and their bounds are the hit rectangles") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  const auto expectRole = [&f](std::string_view id, SemanticRole role) {
    const auto n = f.node(id);
    CHECK(n.has_value());
    if (n) CHECK(n->role == role);
  };
  expectRole("shell.voice.source", SemanticRole::Panel);
  expectRole("shell.voice.envelope", SemanticRole::Lane);
  expectRole("shell.voice.formant.1", SemanticRole::Slider);
  expectRole("shell.voice.knob.aspiration", SemanticRole::Slider);
  expectRole("shell.voice.nasal", SemanticRole::Slider);
  expectRole("shell.voice.pose.0", SemanticRole::RadioButton);
  expectRole("shell.voice.play", SemanticRole::Button);
  expectRole("shell.voice.ab.a", SemanticRole::RadioButton);
  expectRole("shell.voice.frication.seed", SemanticRole::TextField);
  expectRole("shell.voice.singer", SemanticRole::Status);
  const auto seed = f.node("shell.voice.frication.seed");
  CHECK(seed.has_value() && seed->value == "7130");  // the exact seed of the first frication, "s"
  // A press at a node's centre lands on that node: focus follows it and a click edits nothing.
  const auto original = f.recipe();
  std::vector<std::string> ids;
  for (const auto& n : f.flat())
    if ((n.id.starts_with("shell.voice.knob.") || n.id.starts_with("shell.voice.formant.") ||
         n.id == "shell.voice.nasal" || n.id.ends_with(".more")))
      ids.push_back(n.id);
  CHECK(ids.size() >= 6U + 3U + 1U + 4U);
  for (const auto& id : ids) {
    const auto at = center(f.bounds(id));
    CHECK(f.shell.pointerDown(f.controller, press(at)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(at)).hasValue());
    if (f.focusedId() != id) throw test::Failure{"a press on " + id + " focused " + f.focusedId()};
    if (id.ends_with(".more")) {
      CHECK(f.node("shell.voice.menu.save").has_value() || f.node("shell.voice.menu.remove-pose").has_value() ||
            f.node("shell.voice.menu.add-frication").has_value() || f.node("shell.voice.menu.stop").has_value());
      // Escape closes the card menu and stays in VOICE.
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
      CHECK(f.shell.workspace() == Workspace::Voice);
      CHECK(!f.node("shell.voice.menu.save").has_value());
    }
    CHECK(f.frame());
  }
  CHECK(f.recipe() == original);
  CHECK(!f.canUndo());
}

TEST_CASE("the listening singer's ring follows the measured audition level, idle otherwise") {
  VoiceFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openVoice());
  CHECK(f.node("shell.voice.singer")->value.starts_with("Idle"));
  // Play renders B through the session, then plays it through the host once it is ready.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.play", SemanticAction::Activate).hasValue());
  for (int i = 0; i < 500 && f.played.empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    CHECK(f.frame());
  }
  CHECK(f.played.size() == 1U);
  if (!f.played.empty()) CHECK(f.played.front() == f.designer.auditionAudio());
  CHECK(f.frame());
  const auto singer = f.node("shell.voice.singer");
  CHECK(singer.has_value());
  if (singer) CHECK(singer->value == "Listening, -6.0 dBFS");
  CHECK(f.node("shell.voice.level")->value == "-6.0 dBFS");
  // Playback ends: the singer is idle again and the level says nothing plays.
  f.level.reset();
  CHECK(f.frame());
  CHECK(f.node("shell.voice.singer")->value == "Idle, Nothing playing");
  // An edit invalidates B, so the next Play renders again rather than replaying stale audio.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.knob.aspiration", SemanticAction::Increment).hasValue());
  CHECK(f.designer.auditionAudio() == nullptr);
}
