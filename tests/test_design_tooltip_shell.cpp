// The kit tooltip inside the SING shell: icon-only controls and elided labels at every window size,
// keyboard focus and Escape, damage, and accessibility (the tip is the node's own text, never a node).
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/design/tooltip.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace seam;
using namespace std::chrono_literals;
using native_ui::KeyEvent;
using native_ui::NativeKey;
using native_ui::PointerButton;
using native_ui::PointerEvent;
using native_ui::SemanticAction;
using native_ui::SemanticNode;
using native_ui::SemanticRole;
using native_ui::design::Contrast;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::SingShell;

struct TooltipFixture final {
  application::ProjectFactory factory{9600U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;
  std::chrono::steady_clock::time_point now{std::chrono::steady_clock::time_point{} + 10h};
  native_ui::PixelSurface surface;
  double width{1600.0};
  double height{900.0};

  explicit TooltipFixture(DesignPreferences preferences = {})
      : session(makeProject()), controller{session, factory, regionId, native_ui::EditorHostCallbacks{}} {
    controller.resize(width, height);
    shell.activate({}, preferences);
    shell.setUiClock([this] { return now; });
    shell.setRetainedSurface(true);
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Tooltips");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"\u3042",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }

  void resize(double w, double h) {
    width = w;
    height = h;
    controller.resize(w, h);
  }

  // One real frame into a surface kept between frames, then the tree a host would publish.
  bool frame() {
    if (!shell.prepareFrame(controller, width, height)) return false;
    const auto w = static_cast<std::uint32_t>(std::lround(width));
    const auto h = static_cast<std::uint32_t>(std::lround(height));
    if (surface.width() != w || surface.height() != h) surface = native_ui::PixelSurface{w, h};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    if (!shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick()))
      return false;
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    return true;
  }

  std::vector<SemanticNode> nodes() const {
    std::vector<SemanticNode> out;
    const std::function<void(const SemanticNode&)> collect = [&](const SemanticNode& node) {
      for (const auto& child : node.children) {
        auto copy = child;
        copy.children.clear();
        out.push_back(std::move(copy));
        collect(child);
      }
    };
    collect(shell.accessibilityTree().root());
    return out;
  }

  void move(ui::Point p) {
    static_cast<void>(shell.pointerMove(controller, PointerEvent{.position = p}));
  }
};

ui::Point middle(ui::Rect r) { return {r.x + r.width * 0.5, r.y + r.height * 0.5}; }

std::string lower(std::string_view text) {
  std::string out{text};
  for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// The node carries the label's words (the layout sweeps' rule for an elided line, section 8).
bool carries(const SemanticNode& node, std::string_view text) {
  const auto needle = lower(text);
  std::string all;
  for (const auto* field : {&node.name, &node.value, &node.description, &node.editableValue}) {
    const auto value = lower(*field);
    if (value.find(needle) != std::string::npos) return true;
    all += value + '\n';
  }
  std::size_t start = 0U;
  auto words = 0U;
  while (start < needle.size()) {
    const auto end = std::min(needle.find(' ', start), needle.size());
    if (end > start) {
      if (all.find(needle.substr(start, end - start)) == std::string::npos) return false;
      ++words;
    }
    start = end + 1U;
  }
  return words > 1U;
}

bool overlaps(ui::Rect a, ui::Rect b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool insideWindow(ui::Rect box, double w, double h) {
  const auto m = native_ui::design::kTooltipMargin - 0.01;
  return box.x >= m && box.y >= m && box.right() <= w - m && box.bottom() <= h - m;
}

bool covers(const native_ui::FrameDamage& damage, ui::Rect box) {
  if (damage.full) return true;
  for (const auto& r : damage.rects)
    if (r.x <= box.x && r.y <= box.y && r.right() >= box.right() && r.bottom() >= box.bottom())
      return true;
  return false;
}

// The controls whose only label is an icon, by the ids the shell publishes.
bool iconOnly(const SemanticNode& node) {
  return node.id.starts_with("shell.workspace.") || node.id == "shell.settings" ||
         node.id == "shell.workspace-menu" || node.id == "toolbar.transport" ||
         node.id == native_ui::design::kSingerMenuButtonId;
}

// A point inside the node that no smaller tooltip control covers.
std::optional<ui::Point> pointOn(const SemanticNode& node, const std::vector<SemanticNode>& all) {
  for (const auto fy : {0.5, 0.25, 0.75}) {
    for (const auto fx : {0.5, 0.25, 0.75}) {
      const ui::Point p{node.bounds.x + node.bounds.width * fx, node.bounds.y + node.bounds.height * fy};
      auto hidden = false;
      for (const auto& other : all) {
        if (other.id == node.id || !native_ui::design::tooltipRole(other.role)) continue;
        const auto area = other.bounds.width * other.bounds.height;
        if (area < node.bounds.width * node.bounds.height && other.bounds.x <= p.x &&
            p.x < other.bounds.right() && other.bounds.y <= p.y && p.y < other.bounds.bottom())
          hidden = true;
      }
      if (!hidden) return p;
    }
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("tooltips: every described control explains itself after the delay, inside the window and "
          "clear of the control, from 480x320 to 3840x2160") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  struct Look final {
    DesignMode mode;
    Contrast contrast;
  };
  constexpr std::array<std::array<double, 2U>, 8U> kSizes{{{480.0, 320.0},
                                                           {720.0, 480.0},
                                                           {1024.0, 640.0},
                                                           {1280.0, 800.0},
                                                           {1600.0, 900.0},
                                                           {1920.0, 1080.0},
                                                           {2560.0, 1440.0},
                                                           {3840.0, 2160.0}}};
  std::set<std::string> iconIdsSeen;
  std::size_t shown = 0U;
  for (const auto look : {Look{DesignMode::Emo, Contrast::Standard}, Look{DesignMode::Scene, Contrast::High}}) {
    TooltipFixture f{DesignPreferences{.mode = look.mode, .contrast = look.contrast}};
    for (const auto& size : kSizes) {
      // SCENE High Contrast is checked at the smallest, the canonical and the largest size.
      if (look.mode == DesignMode::Scene && size[0] != 480.0 && size[0] != 1600.0 && size[0] != 3840.0)
        continue;
      f.resize(size[0], size[1]);
      CHECK(f.frame());
      const auto published = f.nodes();
      const auto nodeCount = published.size();
      for (const auto& node : published) {
        if (!native_ui::design::tooltipRole(node.role) || node.description.empty() ||
            node.bounds.width <= 0.0)
          continue;
        const auto point = pointOn(node, published);
        if (!point.has_value()) continue;
        // Park the pointer where nothing explains itself, and let any handoff window lapse.
        f.move({-10.0, -10.0});
        f.now += 1s;
        CHECK(f.frame());
        f.move(*point);
        CHECK(f.frame());
        CHECK(!f.shell.lastFrameTooltip().has_value());
        CHECK(f.shell.nextFrameDue().has_value());
        f.now += native_ui::design::kTooltipDelay;
        CHECK(f.frame());
        const auto& tip = f.shell.lastFrameTooltip();
        CHECK(tip.has_value());
        if (!tip.has_value()) continue;
        CHECK(tip->subject == node.id);
        // The tip says what the node publishes: its description, after the whole text of a label
        // it shows elided.
        CHECK(tip->text.ends_with(node.description));
        const auto expected = f.shell.tooltipSubjectFor(node.id);
        CHECK(expected.has_value() && expected->text == tip->text);
        CHECK(insideWindow(tip->box, size[0], size[1]));
        CHECK(!overlaps(tip->box, node.bounds));
        CHECK(!overlaps(tip->box, ui::Rect{point->x, point->y, 1.0, 1.0}));
        // It adds no node: the tree is the same size, and nothing is published at its card.
        const auto after = f.nodes();
        CHECK(after.size() == nodeCount);
        for (const auto& other : after)
          CHECK(!(other.bounds.x == tip->box.x && other.bounds.y == tip->box.y &&
                  other.bounds.width == tip->box.width));
        CHECK(!f.shell.nextFrameDue().has_value());
        if (iconOnly(node)) iconIdsSeen.insert(node.id);
        ++shown;
      }
    }
  }
  std::printf("tooltips shown and checked: %zu\n", shown);
  // Every icon-only control was reached: the five workspace icons, settings, the play button and
  // the SINGER card's menu button (the workspace menu replaces the tabs at the narrowest sizes).
  for (const auto* id : {"shell.workspace.sing", "shell.workspace.voice", "shell.workspace.tune",
                         "shell.workspace.mix", "shell.workspace.export", "shell.settings",
                         "toolbar.transport", "shell.singer-menu", "shell.workspace-menu"})
    CHECK(iconIdsSeen.contains(id));
}

TEST_CASE("tooltips: showing and hiding one damages only its card, and a press or Escape hides it") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  TooltipFixture f;
  CHECK(f.frame());
  CHECK(f.frame());
  const auto settings = f.shell.layout().settings;
  f.move(middle(settings));
  CHECK(f.frame());
  f.now += native_ui::design::kTooltipDelay;
  CHECK(f.frame());
  const auto tip = f.shell.lastFrameTooltip();
  CHECK(tip.has_value());
  CHECK(tip->text == native_ui::design::tr(native_ui::design::Str::TipAudioSettings));
  CHECK(!f.shell.lastFrameDamage().full);
  CHECK(covers(f.shell.lastFrameDamage(), tip->box));
  // Escape hides it and does nothing else.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.frame());
  CHECK(!f.shell.lastFrameTooltip().has_value());
  CHECK(!f.shell.lastFrameDamage().full);
  CHECK(covers(f.shell.lastFrameDamage(), tip->box));
  // It stays hidden while the pointer stays on the same control.
  f.now += 2s;
  f.move({settings.x + 2.0, settings.y + 2.0});
  CHECK(f.frame());
  CHECK(!f.shell.lastFrameTooltip().has_value());
  // Another control starts a new delay; a press on it hides the tip again.
  const auto menu = f.shell.layout().workspaceTab[2];
  f.move(middle(menu));
  f.now += native_ui::design::kTooltipDelay;
  CHECK(f.frame());
  CHECK(f.shell.lastFrameTooltip().has_value());
  CHECK(f.shell.lastFrameTooltip()->subject == "shell.workspace.tune");
  static_cast<void>(f.shell.pointerDown(
      f.controller, PointerEvent{.position = middle(menu), .button = PointerButton::Left}));
  static_cast<void>(f.shell.pointerUp(
      f.controller, PointerEvent{.position = middle(menu), .button = PointerButton::Left}));
  f.now += 2s;
  CHECK(f.frame());
  CHECK(!f.shell.lastFrameTooltip().has_value());
}

TEST_CASE("tooltips: keyboard focus shows the focused control's description, and Escape keeps focus") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  TooltipFixture f;
  CHECK(f.frame());
  // Tab until the settings button holds focus.
  std::string focused;
  for (int i = 0; i < 64 && focused != "shell.settings"; ++i) {
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
    CHECK(f.frame());
    const auto* node = f.shell.accessibilityTree().focusedNode();
    focused = node != nullptr ? node->id : std::string{};
  }
  CHECK(focused == "shell.settings");
  CHECK(!f.shell.lastFrameTooltip().has_value());
  f.now += native_ui::design::kTooltipDelay;
  CHECK(f.frame());
  const auto tip = f.shell.lastFrameTooltip();
  CHECK(tip.has_value());
  CHECK(tip->subject == "shell.settings");
  CHECK(tip->text == native_ui::design::tr(native_ui::design::Str::TipAudioSettings));
  CHECK(!overlaps(tip->box, f.shell.layout().settings));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.frame());
  CHECK(!f.shell.lastFrameTooltip().has_value());
  const auto* still = f.shell.accessibilityTree().focusedNode();
  CHECK(still != nullptr && still->id == "shell.settings");
  // Tab to the next control: its tip follows after the delay.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
  CHECK(f.frame());
  f.now += native_ui::design::kTooltipDelay;
  CHECK(f.frame());
  const auto* next = f.shell.accessibilityTree().focusedNode();
  if (next != nullptr && f.shell.tooltipSubjectFor(next->id).has_value()) {
    CHECK(f.shell.lastFrameTooltip().has_value());
    CHECK(f.shell.lastFrameTooltip()->subject == next->id);
  }
}

TEST_CASE("tooltips: an elided label shows its whole text, which a published node carries") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // Pseudo-localized text is 40% longer, so the narrow sizes elide labels across the shell.
  const auto pseudo = native_ui::design::ShellStringTable::pseudoLocalized(0.4);
  const native_ui::design::ScopedShellStrings installed{pseudo};
  std::size_t checked = 0U;
  for (const auto size : std::array<std::array<double, 2U>, 2U>{{{480.0, 320.0}, {720.0, 480.0}}}) {
    TooltipFixture f;
    f.resize(size[0], size[1]);
    std::vector<native_ui::paint::TextRecord> elided;
    {
      native_ui::paint::ScopedTextCapture capture;
      CHECK(f.frame());
      for (const auto& record : capture.records())
        if (record.elided) elided.push_back(record);
    }
    CHECK(f.frame());
    const auto published = f.nodes();
    for (const auto& record : elided) {
      const auto point = middle(record.bounds);
      const auto subject = f.shell.tooltipSubjectAt(point);
      // Labels inside the score and the lane belong to editing and show nothing.
      if (!subject.has_value()) continue;
      if (subject->text.find(record.text) == std::string::npos) continue;  // a smaller control's own
      f.move({-10.0, -10.0});
      f.now += 1s;
      CHECK(f.frame());
      f.move(point);
      f.now += native_ui::design::kTooltipDelay;
      CHECK(f.frame());
      const auto& tip = f.shell.lastFrameTooltip();
      CHECK(tip.has_value());
      CHECK(tip->text.find(record.text) != std::string::npos);
      CHECK(insideWindow(tip->box, size[0], size[1]));
      CHECK(!overlaps(tip->box, tip->target));
      // The whole text is already on the node at that place (so the tip says nothing a screen
      // reader does not), and the tip adds no node.
      auto carried = false;
      for (const auto& node : published)
        if (overlaps(node.bounds, record.bounds) && carries(node, record.text)) carried = true;
      if (!carried)
        std::cerr << "not carried: '" << record.text << "' at " << record.bounds.x << ","
                  << record.bounds.y << " " << record.bounds.width << "x" << record.bounds.height
                  << " subject " << tip->subject << "\n";
      CHECK(carried);
      CHECK(f.nodes().size() == published.size());
      ++checked;
    }
  }
  std::printf("elided labels hovered: %zu\n", checked);
  CHECK(checked > 0U);
}
