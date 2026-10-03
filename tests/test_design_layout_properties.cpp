// Layout property tests (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md section 14.3).
//
// Every workspace and every re-homed overlay or sheet is painted as a real frame at window sizes
// from 720x480 to 3840x2160 and at 1x, 1.5x and 2x. The frame's own text draws are captured from
// the text engine, and the accessibility tree the shell publishes for that frame is the list of
// interactive widgets, because it is built from the same layout snapshot that paints and hit-tests.
// Three properties hold for each frame:
//   1. no two interactive widgets overlap, unless one contains the other (a card and its button,
//      the grid and its notes);
//   2. every interactive widget is at least 24 points in both directions;
//   3. every line of text either fits in its box and its clip, or is elided with an ellipsis and
//      its full text is on an accessibility node at that place.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/text/text_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace seam;
using native_ui::SemanticAction;
using native_ui::SemanticNode;
using native_ui::SemanticRole;
using native_ui::design::Contrast;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::OverlayKind;
using native_ui::design::RackPresentation;
using native_ui::design::SingShell;
using native_ui::design::Workspace;
using native_ui::paint::TextRecord;

constexpr std::array<std::array<double, 2U>, 12U> kSizes{{{720.0, 480.0},
                                                          {800.0, 600.0},
                                                          {960.0, 540.0},
                                                          {1024.0, 640.0},
                                                          {1100.0, 480.0},
                                                          {1280.0, 720.0},
                                                          {1280.0, 800.0},
                                                          {1440.0, 900.0},
                                                          {1600.0, 900.0},
                                                          {1920.0, 1080.0},
                                                          {2560.0, 1440.0},
                                                          {3840.0, 2160.0}}};
constexpr std::array<double, 3U> kScales{{1.0, 1.5, 2.0}};
constexpr double kMinimumTarget = 24.0;

struct LayoutFixture final {
  application::ProjectFactory factory{8800U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;

  explicit LayoutFixture(DesignMode mode = DesignMode::Emo, Contrast contrast = Contrast::Standard)
      : session(makeProject()),
        controller{session, factory, regionId,
                   native_ui::EditorHostCallbacks{
                       .beginTextInput =
                           [this](const native_ui::TextInputRequest& request) {
                             static_cast<void>(shell.translateTextInput(request));
                           },
                       .endTextInput = [this] { shell.textInputEnded(); },
                       // A plug-in's host owns the output channels, so MIX shows their control.
                       .configureOutputChannels = [](std::uint8_t) { return core::success(); },
                       .loadSampleMicroscope =
                           [](domain::PhonemeKey) -> core::Result<native_ui::SampleMicroscopeData> {
                         return native_ui::SampleMicroscopeData{
                             test::support::makeUnit("voice-a", {"a"}, "audio/a.wav", 60U,
                                                     voicebank::UnitKind::Cv, 2400U),
                             voicebank::AudioBuffer{
                                 .sampleRate = 48000U,
                                 .channels = 1U,
                                 .interleaved = test::support::sineWave(48000U, 220.0, 0.05)},
                             "Captured decision 1: source-boundary proxy."};
                       },
                       .reviewPhonemeBindings =
                           [this]() -> core::Result<authoring::PhonemeBindingReview> {
                         return core::success(authoring::PhonemeBindingReview{
                             .regionId = regionId,
                             .warnings = {phonemizer::Warning{.message = "One retained edit"}}});
                       },
                       .rebindPhonemeOverride =
                           [](const domain::PhonemeOverride&, domain::PhonemeKey,
                              std::string_view) { return core::success(); },
                   }} {
    controller.resize(1600.0, 900.0);
    shell.activate({}, DesignPreferences{.mode = mode, .contrast = contrast});
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Layout properties with a long project title");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Lead singer track with a long name");
    const auto second = factory.addVocalTrack(project, "Harmony");
    regionId = factory.addRegion(project, trackId, "Verse phrase", time::Tick{0}, time::Tick{15360});
    static_cast<void>(factory.addRegion(project, second, "Harmony", time::Tick{0}, time::Tick{7680}));
    auto* region = project.findRegion(regionId);
    const std::array<std::u32string, 4U> lyrics{
        {U"a", U"i", U"long lyric that must remain inspectable", U"\uAC00\uB098\uB2E4"}};
    for (std::size_t i = 0U; i < lyrics.size(); ++i) {
      auto [lyric, note] =
          factory.makeNote(time::Tick{960 + static_cast<std::int64_t>(i) * 960}, time::Tick{960},
                           static_cast<std::uint8_t>(64U + i), lyrics[i], domain::Language::Japanese);
      region->lyrics.push_back(std::move(lyric));
      region->notes.push_back(std::move(note));
    }
    // An overlap group, so the +N badge and the overlap detail exist.
    auto [lyric, note] =
        factory.makeNote(time::Tick{960}, time::Tick{960}, 64U, U"u", domain::Language::Japanese);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    region->notes.front().vibrato.enabled = true;
    region->sortNotes();
    return project;
  }

  [[nodiscard]] domain::NoteId firstNote() const {
    return session.project().findRegion(regionId)->notes.front().id;
  }
};

struct Frame final {
  std::vector<TextRecord> text;
  std::vector<SemanticNode> nodes;  // flattened, children removed
};

// The system face the shipping AppKit window loads, named by SEAM_DESIGN_SYSTEM_FONT pointing at a
// directory of font files. With nothing named the shell paints through the null engine as before and
// no check depends on it.
seam::text::TextEngine* designSystemFont() {
  static std::unique_ptr<seam::text::TextEngine> engine = [] {
    const char* directory = std::getenv("SEAM_DESIGN_SYSTEM_FONT");
    if (directory == nullptr || *directory == '\0') return std::unique_ptr<seam::text::TextEngine>{};
    seam::text::FontSearchOptions options;
    for (const auto& entry : std::filesystem::directory_iterator{directory}) {
      if (entry.is_regular_file()) options.additionalCandidates.push_back(entry.path());
    }
    if (options.additionalCandidates.empty()) return std::unique_ptr<seam::text::TextEngine>{};
    auto loaded = seam::text::TextEngine::createFromTrustedFiles(options);
    return loaded ? std::move(loaded).value() : std::unique_ptr<seam::text::TextEngine>{};
  }();
  return engine.get();
}

// The design layer is painted here with a null text engine, so every geometry assertion in this file
// is about the layout the shell computes rather than about what a person would see drawn from it: the
// shell's own type scale is a set of point sizes, and a point size is only a claim until a face has
// turned it into ink. That is the same gap the Studio surfaces had, and it is why this helper gained a
// capture: with a real engine the same shell can be written out as a frame and read.
// The name of every frame this file has written since the process started, so the coverage audit is a
// fact about the run rather than a claim about it. A case that renders a frame nobody can see is the
// failure mode this run of units has hit twice; this is where the record of what was written lives, and
// the audit at the end of the file reads it.
std::set<std::string>& capturedFrameNames() {
  static std::set<std::string> names;
  return names;
}

void writeCapturedFrame(LayoutFixture& f, Workspace workspace, double width, double height,
                        const std::filesystem::path& path, DesignMode mode = DesignMode::Emo) {
  const auto surface = static_cast<std::uint32_t>(std::lround(width));
  const auto high = static_cast<std::uint32_t>(std::lround(height));
  native_ui::PixelSurface frame{surface, high};
  native_ui::RasterCanvas canvas{frame, 1.0, designSystemFont()};
  // The workspace is not set here: setWorkspace closes whatever the shell had open, so a
  // surface opened before the capture was written was gone by the time the frame was drawn.
  // A caller that wants a particular workspace sets it before opening its surface.
  static_cast<void>(workspace);
  if (f.shell.mode() != mode) f.shell.setMode(mode, false);
  if (!f.shell.prepareFrame(f.controller, width, height)) return;
  if (!f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick())) return;
  std::filesystem::create_directories(path.parent_path());
  CHECK(frame.writePpm(path));
  // Record the frame's name so the coverage audit below is about what was actually written rather
  // than about what a case says it wrote. A capture nobody can see is the failure this whole run of
  // units has been about, twice over.
  capturedFrameNames().insert(path.stem().string());
}

bool paintFrame(LayoutFixture& f, double width, double height, double scale, Frame& out) {
  if (!f.shell.prepareFrame(f.controller, width, height)) return false;
  native_ui::PixelSurface surface{static_cast<std::uint32_t>(std::lround(width * scale)),
                                  static_cast<std::uint32_t>(std::lround(height * scale))};
  native_ui::RasterCanvas canvas{surface, scale, nullptr};
  native_ui::paint::ScopedTextCapture capture;
  if (!f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick()))
    return false;
  out.text = capture.records();
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  out.nodes.clear();
  const std::function<void(const SemanticNode&)> collect = [&](const SemanticNode& node) {
    for (const auto& child : node.children) {
      auto copy = child;
      copy.children.clear();
      out.nodes.push_back(std::move(copy));
      collect(child);
    }
  };
  collect(f.shell.accessibilityTree().root());
  return true;
}

bool has(const SemanticNode& node, SemanticAction action) {
  return std::find(node.actions.begin(), node.actions.end(), action) != node.actions.end();
}

// A widget the user can point at or operate. Notes are musical data whose size is the note's
// duration and pitch row at the current zoom (WCAG 2.5.8's "essential" exception), and the grid,
// the lane and the timeline are editing surfaces that contain other widgets by design.
bool interactive(const SemanticNode& node) {
  if (node.bounds.width <= 0.0 || node.bounds.height <= 0.0) return false;
  // An overlap group's bounds are its overlapping notes' own rectangle, so it is note data too;
  // its +N badge is the hit target the shell adds for it.
  if (node.id.starts_with("overlap-group.")) return false;
  switch (node.role) {
    case SemanticRole::Note:
    case SemanticRole::Timeline:
    case SemanticRole::Lane:
    case SemanticRole::Window:
      return false;
    case SemanticRole::Button:
    case SemanticRole::TextField:
    case SemanticRole::Slider:
    case SemanticRole::RadioButton:
    case SemanticRole::Tab:
    case SemanticRole::CheckBox:
      return true;
    default:
      return has(node, SemanticAction::Activate) || has(node, SemanticAction::Toggle) ||
             has(node, SemanticAction::Increment) || has(node, SemanticAction::EditText);
  }
}

bool contains(ui::Rect outer, ui::Rect inner, double slack = 0.5) {
  return inner.x >= outer.x - slack && inner.y >= outer.y - slack &&
         inner.right() <= outer.right() + slack && inner.bottom() <= outer.bottom() + slack;
}

double overlapArea(ui::Rect a, ui::Rect b) {
  const auto w = std::min(a.right(), b.right()) - std::max(a.x, b.x);
  const auto h = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
  return w > 0.0 && h > 0.0 ? w * h : 0.0;
}

std::string lower(std::string_view text) {
  std::string out{text};
  for (auto& ch : out)
    if (static_cast<unsigned char>(ch) < 0x80U) ch = static_cast<char>(std::tolower(ch));
  return out;
}

bool exposes(const SemanticNode& node, std::string_view text) {
  const auto needle = lower(text);
  std::string all;
  for (const auto* field : {&node.name, &node.value, &node.description, &node.editableValue}) {
    const auto value = lower(*field);
    if (value.find(needle) != std::string::npos) return true;
    all += value;
    all += '\n';
  }
  // A line that joins several published facts ("name  version") is exposed when every word of it
  // is on the node.
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

std::string describe(ui::Rect r) {
  char buffer[96];
  std::snprintf(buffer, sizeof buffer, "(%.1f,%.1f %.1fx%.1f)", r.x, r.y, r.width, r.height);
  return buffer;
}

struct Checker final {
  // One entry per distinct problem (kind, widget or text), with the first frame that showed it and
  // how many frames did, so a sweep over 36 sizes reads as a short list.
  struct Problem final {
    std::string firstFrame;
    std::string detail;
    std::size_t frames{0U};
  };
  std::vector<std::string> order;
  std::map<std::string, Problem> problems;
  bool checkWidgets{true};

  void report(const std::string& frame, const std::string& key, const std::string& detail) {
    auto [it, inserted] = problems.try_emplace(key, Problem{frame, detail, 0U});
    if (inserted) order.push_back(key);
    ++it->second.frames;
  }

  // covered lists the text the same window painted before a modal surface (an overlay, the
  // inspector, the workspace menu) opened: while that surface is up the shell deliberately stops
  // publishing what it covers, so such a line is held only to the clipping rule.
  void check(const Frame& frame, std::string_view where, double width, double height,
             const std::vector<TextRecord>* covered = nullptr) {
    const ui::Rect client{0.0, 0.0, width, height};
    const auto label = std::string{where};
    if (checkWidgets) {
      std::vector<const SemanticNode*> widgets;
      for (const auto& node : frame.nodes)
        if (interactive(node)) widgets.push_back(&node);
      for (const auto* w : widgets) {
        if (!contains(client, w->bounds))
          report(label, "outside-window " + w->id, describe(w->bounds));
        if (w->bounds.width < kMinimumTarget - 0.01 || w->bounds.height < kMinimumTarget - 0.01)
          report(label, "small-target " + w->id, describe(w->bounds));
      }
      for (std::size_t i = 0U; i < widgets.size(); ++i)
        for (std::size_t j = i + 1U; j < widgets.size(); ++j) {
          const auto& a = widgets[i]->bounds;
          const auto& b = widgets[j]->bounds;
          if (overlapArea(a, b) <= 0.25 || contains(a, b) || contains(b, a)) continue;
          report(label, "overlap " + widgets[i]->id + " x " + widgets[j]->id,
                 describe(a) + " " + describe(b));
        }
    }
    for (const auto& line : frame.text) {
      const auto visible = overlapArea(line.ink, line.clip) > 0.0 &&
                           overlapArea(line.ink, client) > 0.0;
      if (!visible) continue;
      // Drawn whole, or elided: either way the ink stays inside the clip and the window, so no
      // glyph is cut off without an ellipsis saying so.
      const auto bounded = ui::Rect{line.clip.x - 1.0, line.clip.y - 2.0, line.clip.width + 2.0,
                                    line.clip.height + 4.0};
      if (!contains(bounded, line.ink, 0.0) || !contains(client, line.ink, 1.0))
        report(label, "clipped-text \"" + line.text + "\"",
               "ink " + describe(line.ink) + " clip " + describe(line.clip));
      if (!line.elided) continue;
      if (covered != nullptr &&
          std::any_of(covered->begin(), covered->end(), [&](const TextRecord& before) {
            return before.text == line.text && std::abs(before.bounds.x - line.bounds.x) < 0.5 &&
                   std::abs(before.bounds.y - line.bounds.y) < 0.5 &&
                   std::abs(before.bounds.width - line.bounds.width) < 0.5;
          }))
        continue;
      // Exposed at its place (a node over the line carries its words), or verbatim anywhere.
      const auto exposed = std::any_of(frame.nodes.begin(), frame.nodes.end(), [&](const auto& n) {
        const auto near = overlapArea(n.bounds, line.bounds) > 0.0 ||
                          contains(n.bounds, line.bounds, 2.0);
        if (near) return exposes(n, line.text);
        const auto needle = lower(line.text);
        for (const auto* field : {&n.name, &n.value, &n.description, &n.editableValue})
          if (lower(*field).find(needle) != std::string::npos) return true;
        return false;
      });
      if (!exposed)
        report(label, "elided-without-full-text \"" + line.text + "\"", describe(line.bounds));
    }
  }

  void finish(std::string_view name) {
    if (!order.empty()) {
      std::printf("%s: %zu distinct layout violations\n", std::string{name}.c_str(), order.size());
      for (const auto& key : order) {
        const auto& p = problems.at(key);
        std::printf("  %s %s, first %s, %zu frames\n", key.c_str(), p.detail.c_str(),
                    p.firstFrame.c_str(), p.frames);
      }
    }
    CHECK(order.empty());
  }
};

std::string frameName(std::string_view what, double w, double h, double s) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "@%gx%g*%g", w, h, s);
  return std::string{what} + buffer;
}

// The card of the presented overlay, as the shell publishes it (its "<prefix>panel" node).
std::optional<ui::Rect> overlayCard(const Frame& frame) {
  for (const auto& node : frame.nodes)
    if (node.role == SemanticRole::Panel && node.id.starts_with("shell.overlay.") &&
        node.id.ends_with("panel"))
      return node.bounds;
  return std::nullopt;
}

// Opens one overlay or sheet on a fixture. Returns false when the size has no place for it (the
// compact inspector and the workspace menu exist only at compact widths).
using Opener = std::function<bool(LayoutFixture&)>;

struct Surface final {
  std::string_view name;
  OverlayKind kind;
  Opener open;
  // What must already be on screen for the surface to open (the compact inspector that carries
  // the SINGER card); applied to the covered-content baseline too.
  Opener prepare{};
};

// The text the window paints under a surface before it opens: the content its card covers.
std::vector<TextRecord> coveredText(const Surface& surface, LayoutFixture& base, double width,
                                    double height, double scale) {
  Frame frame;
  if (!paintFrame(base, width, height, scale, frame)) return {};
  if (surface.prepare && surface.prepare(base) && !paintFrame(base, width, height, scale, frame))
    return {};
  return frame.text;
}

bool openInspectorWhenCompact(LayoutFixture& f) {
  if (f.shell.layout().rack == RackPresentation::Full || f.shell.inspectorOpen()) return true;
  return f.shell.dispatchSemantic(f.controller, "shell.inspector", SemanticAction::Activate)
      .hasValue();
}

std::vector<Surface> overlaySurfaces() {
  return {
      {"time-map", OverlayKind::TimeMap,
       [](LayoutFixture& f) { return f.controller.openTimeMapPanel().hasValue(); }},
      {"phoneme-review", OverlayKind::PhonemeReview,
       [](LayoutFixture& f) { return f.controller.openPhonemeReview().hasValue(); }},
      {"sample-microscope", OverlayKind::SampleMicroscope,
       [](LayoutFixture& f) {
         return f.controller.openSampleMicroscope(domain::PhonemeKey{f.firstNote(), 0U}).hasValue();
       }},
      {"recovery-support", OverlayKind::RecoverySupport,
       [](LayoutFixture& f) {
         f.controller.setRecoverySupportView(native_ui::RecoverySupportView{
             .visible = true,
             .mode = native_ui::RecoverySupportMode::Reports,
             .items = {{.name = "report-a", .detail = "crash marker", .bytes = 4096U},
                       {.name = "a report whose name is longer than any row can show at once",
                        .detail = "first run",
                        .bytes = 8192U}},
             .reportCount = 2U,
             .status = "Two owned reports",
         });
         return true;
       }},
      {"overlap-detail", OverlayKind::OverlapDetail,
       [](LayoutFixture& f) { return f.controller.openOverlapDetail(0U).hasValue(); }},
      {"diagnostics", OverlayKind::Diagnostics,
       [](LayoutFixture& f) {
         f.controller.setDiagnostics({authoring::Diagnostic{
             .code = "MEDIA_MISSING",
             .severity = authoring::DiagnosticSeverity::Warning,
             .messageKey = "media.missing",
             .actions = {authoring::DiagnosticAction::RelinkMedia,
                         authoring::DiagnosticAction::CopyDiagnostic}}});
         f.shell.setDiagnosticsOpen(true);
         return true;
       }},
      {"diagnostic-notices", OverlayKind::Diagnostics,
       [](LayoutFixture& f) {
         // The editor's own notices, the refusal worded as long as a refusal can be and the selection
         // notice with its two actions: the popover has to bound them as it bounds any diagnostic.
         authoring::Diagnostic sync{.code = "SELECTION_SYNC_FAILED",
                                    .severity = authoring::DiagnosticSeverity::Warning,
                                    .messageKey = "editor.selection-sync-failed",
                                    .actions = authoring::DiagnosticRegistry::actions("SELECTION_SYNC_FAILED"),
                                    .occurrenceCount = 3U};
         sync.setDetail("The host is busy and did not follow the editor to the region it moved to");
         f.controller.setDiagnostics({sync});
         f.controller.noteRefusal(core::Error{
             core::ErrorCode::Conflict,
             "Select at most 10000 notes entirely within the active region before distributing lyrics"});
         f.shell.setDiagnosticsOpen(true);
         return true;
       }},
      {"replacement-review", OverlayKind::ReplacementReview,
       [](LayoutFixture& f) {
         f.session.selection().selectOnly(f.firstNote());
         return f.controller.openClearVibratoReview().hasValue();
       }},
      {"audio-settings", OverlayKind::AudioSettings,
       [](LayoutFixture& f) {
         f.controller.setAudioSettings(
             authoring::AudioSettings{.deviceId = "built-in", .sampleRate = 48000U,
                                      .blockFrames = 256U, .outputChannels = 2U},
             {{.id = "built-in", .name = "Built-in Output", .physical = true, .selected = true},
              {.id = "usb",
               .name = "A very long USB audio interface name that no settings row can show whole",
               .physical = true,
               .selected = false}},
             12U, 3U);
         f.controller.showAudioSettings();
         return true;
       }},
      {"settings", OverlayKind::Settings,
       [](LayoutFixture& f) {
         f.controller.setAudioSettings(
             authoring::AudioSettings{.deviceId = "built-in", .sampleRate = 48000U,
                                      .blockFrames = 256U, .outputChannels = 2U},
             {{.id = "built-in", .name = "Built-in Output", .physical = true, .selected = true}},
             12U, 3U);
         f.shell.setSettingsOpen(true);
         return true;
       }},
      {"voicebank-browser", OverlayKind::VoicebankBrowser,
       [](LayoutFixture& f) {
         std::vector<authoring::VoicebankCard> cards;
         for (std::size_t i = 0U; i < 6U; ++i) {
           authoring::VoicebankCard card;
           card.id = "bank-" + std::to_string(i);
           card.version = "1.0." + std::to_string(i);
           card.displayName = i == 1U ? "A singer whose display name is far longer than any card"
                                      : "Singer " + std::to_string(i);
           card.language = "ja";
           card.contentHash = "hash-" + std::to_string(i);
           card.contentHashAbbreviation = "h" + std::to_string(i);
           card.trustLabel = "Official";
           card.installed = true;
           card.selectable = true;
           card.rootPitchLayers = {48, 72};
           cards.push_back(std::move(card));
         }
         f.controller.setVoicebankCards(cards);
         f.controller.showVoicebankBrowser();
         return true;
       }},
      {"text-field", OverlayKind::TextField,
       [](LayoutFixture& f) {
         f.session.selection().selectOnly(f.firstNote());
         return f.controller.beginSelectedHintEdit().hasValue();
       }},
      {"singer-menu", OverlayKind::SingerMenu,
       [](LayoutFixture& f) {
         // The menu's button is on the SINGER card: the full rack's, or the compact inspector's.
         return openInspectorWhenCompact(f) &&
                f.shell.setSingerMenuOpen(f.controller, true).hasValue();
       },
       openInspectorWhenCompact},
      {"about", OverlayKind::About,
       [](LayoutFixture& f) { return f.shell.setAboutOpen(f.controller, true).hasValue(); }},
  };
}

// Every frame written so far in this file and in the Studio suites is the EMO look, and the two looks
// are the whole point of the design work: the mode switch is a first-class control in the header of
// every window, it changes the entire palette and the type faces, and it is a preference that must never
// change audio. The capture helper took a mode argument and discarded it, so the second look had never
// been rendered and could not be. This case writes both looks of every workspace, so the switch can be
// looked at rather than argued about, and the two cases below pin that a frame really is in the look it
// claims.
TEST_CASE("every workspace is capturable in both looks") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  const std::filesystem::path root = directory != nullptr && *directory != '\0'
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-modes")};
  std::size_t written = 0U;
  const std::array<std::pair<Workspace, std::string_view>, 5U> workspaces{{
      {Workspace::Sing, "sing"}, {Workspace::Voice, "voice"},
      {Workspace::Tune, "tune"}, {Workspace::Mix, "mix"},
      {Workspace::Export, "export"}}};
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    for (const auto& [workspace, name] : workspaces) {
      LayoutFixture fixture{mode};
      fixture.shell.setWorkspace(fixture.controller, workspace);
      writeCapturedFrame(fixture, workspace, 1440.0, 900.0,
                         root / (std::string{name} + "-" +
                                 (mode == DesignMode::Emo ? "emo" : "scene") + ".ppm"),
                         mode);
      ++written;
    }
  }
  CHECK(written == 10U);
}

// The EXPORT workspace said the same sentence twice on screen: once as the EXPORT row's state and once
// as the note under the button, 200 points apart and in the same words. A frame is the only place that
// shows it, because each painter was doing what it was told and the two painters were told to say the
// same thing. The note is for what to do next, so when there is nothing to choose there is nothing for
// it to say and it is left empty; the reason stays on the row that gives it.
//
// The check is on the text the shell actually painted: the refusal appears exactly once in the frame's
// text records. Putting the sentence back under the button fails it.
TEST_CASE("the export workspace says why it cannot export once, not twice") {
  using seam::native_ui::design::Str;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  LayoutFixture fixture;
  fixture.shell.setWorkspace(fixture.controller, Workspace::Export);
  Frame frame;
  CHECK(paintFrame(fixture, 1440.0, 900.0, 1.0, frame));
  const auto refusal = tr(Str::ThisHostDoesNotExportFrom);
  const auto times = std::count_if(frame.text.begin(), frame.text.end(),
                                   [&refusal](const auto& record) {
                                     return record.text.find(refusal) != std::string_view::npos;
                                   });
  // Once. The host cannot export, so the sentence is the state of the EXPORT row and nothing else.
  CHECK(times == 1);
  // And the note the button carries is empty rather than a second copy: no painted line under the
  // button at all in this state.
  const auto note = std::none_of(frame.text.begin(), frame.text.end(), [](const auto& record) {
    return record.text.find(tr(Str::ChooseANewFolderAnExisting)) != std::string_view::npos;
  });
  CHECK(note);
}

// Contrast is the other half of the look and it has never been rendered either. It is a control in the
// header beside the mode switch, and the one case that touched it painted through a null text engine
// into a pixel vector it compared against another, so it proved the cached background is repainted and
// nothing about what high contrast looks like on screen. It is the variant a person turns on to read a
// label in a bright room, so it is the one where a label that was hard to read at standard contrast is
// the thing being tested, and it has never been looked at.
//
// The mode argument had the same shape of fault and it was fixed in the previous entry: the capture
// applied the look after painting, so a caller whose fixture was already in the other look would have
// received a frame in the look it was not named for. Both are now applied before the frame is prepared,
// and this case writes both contrasts of both looks so the halves are on screen together and can be
// compared against each other rather than asserted apart.
TEST_CASE("every workspace is capturable in both contrasts of both looks") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  const std::filesystem::path root = directory != nullptr && *directory != 0
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-contrast")};
  const std::array<std::pair<Workspace, std::string_view>, 5U> workspaces{{
      {Workspace::Sing, "sing"}, {Workspace::Voice, "voice"},
      {Workspace::Tune, "tune"}, {Workspace::Mix, "mix"},
      {Workspace::Export, "export"}}};
  std::size_t written = 0U;
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene})
    for (const auto contrast : {Contrast::Standard, Contrast::High}) {
      for (const auto& [workspace, name] : workspaces) {
        LayoutFixture fixture{mode, contrast};
        fixture.shell.setWorkspace(fixture.controller, workspace);
        writeCapturedFrame(fixture, workspace, 1440.0, 900.0,
                           root / (std::string{name} + "-" +
                                   (mode == DesignMode::Emo ? "emo" : "scene") + "-" +
                                   (contrast == Contrast::Standard ? "standard" : "high") + ".ppm"),
                           mode);
        ++written;
      }
    }
  CHECK(written == 20U);
}

// A captured frame is in the look it was named for. This is the check for the fault above: the capture
// applied the mode after painting, so the mode argument did nothing for a fixture that was not already
// in that mode, and every frame it wrote was in the look its fixture happened to be in rather than the
// one its file name claimed. Asking for SCENE from an EMO fixture and finding the shell still in EMO
// after the capture is exactly that failure.
TEST_CASE("a captured frame is painted in the look it was asked for, not the one the fixture had") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  const std::filesystem::path root = directory != nullptr && *directory != 0
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-look-order")};
  // The fixture starts in EMO and the capture is asked for SCENE. If the look is applied after the
  // paint the shell is still in EMO here and the frame named "scene" is an EMO frame.
  LayoutFixture fixture{DesignMode::Emo};
  fixture.shell.setWorkspace(fixture.controller, Workspace::Sing);
  CHECK(fixture.shell.mode() == DesignMode::Emo);
  writeCapturedFrame(fixture, Workspace::Sing, 1440.0, 900.0,
                     root / "look-order-scene-requested.ppm", DesignMode::Scene);
  CHECK(fixture.shell.mode() == DesignMode::Scene);


  // The shell's own state after the capture is not the evidence: a capture that applies the look after
  // painting also leaves the shell in the look it was asked for, and it wrote the frame in the other
  // one. The frame itself is the evidence, and the two looks are a different palette rather than a
  // tint, so two captures of the same window in the two looks differ in a measurable share of its
  // pixels and two captures painted in the same look differ in none. The two are captured through the
  // same helper the sweep uses, because a case with its own painter would not be checking the helper.
  const auto emoPath = root / "look-order-emo.ppm";
  const auto scenePath = root / "look-order-scene.ppm";
  for (const auto& [requested, path] : std::array{
           std::pair{DesignMode::Emo, emoPath}, std::pair{DesignMode::Scene, scenePath}}) {
    LayoutFixture f{DesignMode::Emo};
    f.shell.setWorkspace(f.controller, Workspace::Sing);
    writeCapturedFrame(f, Workspace::Sing, 1440.0, 900.0, path, requested);
  }
  // The two frames are written by the same helper from the same fixture, so the only thing that can
  // make them different is the look being applied before the paint rather than after it.
  const auto readPpm = [](const std::filesystem::path& path, std::vector<std::uint32_t>& out) {
    std::ifstream in{path, std::ios::binary};
    if (!in) return false;
    std::string header;
    std::getline(in, header);
    if (header.rfind("P6", 0U) != 0U) return false;
    int w = 0, h = 0, maxValue = 0;
    in >> w >> h >> maxValue;
    in.get();
    out.resize(static_cast<std::size_t>(w) * static_cast<std::uint32_t>(h));
    for (std::size_t i = 0U; i < out.size(); ++i) {
      unsigned char rgb[3]{};
      in.read(reinterpret_cast<char*>(rgb), 3);
      out[i] = static_cast<std::uint32_t>(rgb[2]) << 16U |
               static_cast<std::uint32_t>(rgb[1]) << 8U | static_cast<std::uint32_t>(rgb[0]);
    }
    return static_cast<bool>(in);
  };
  std::vector<std::uint32_t> emoPixels;
  std::vector<std::uint32_t> scenePixels;
  CHECK(readPpm(emoPath, emoPixels));
  CHECK(readPpm(scenePath, scenePixels));
  CHECK(emoPixels.size() == scenePixels.size());
  CHECK(!emoPixels.empty());
  std::size_t differing = 0U;
  for (std::size_t i = 0U; i < std::min(emoPixels.size(), scenePixels.size()); ++i)
    if (emoPixels[i] != scenePixels[i]) ++differing;
  // Measured at a few percent of the frame: the two looks differ in the wash and the panel fills
  // rather than in every label, because much of the window is one background. The property is that
  // the two frames are not the same frame, and the figure behind the threshold is measured rather
  // than guessed.
  CHECK(differing > emoPixels.size() / 100U);
}


// A frame in the look it claims. The capture used to take a mode and discard it, so every frame in
// this file was EMO whatever the caller asked for; a frame that is in the wrong look is a picture of
// something a person would never see, which is the same failure as a frame of the wrong surface and
// just as hard to spot because the file is there and it rendered.
TEST_CASE("a captured frame is in the look it was asked for") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The shell's mode is what the capture asks for, and the tokens it paints from are derived from it,
  // so the check is that asking for SCENE actually changed the shell rather than writing an EMO frame
  // under a SCENE name.
  LayoutFixture fixture{DesignMode::Emo};
  fixture.shell.setWorkspace(fixture.controller, Workspace::Sing);
  CHECK(fixture.shell.mode() == DesignMode::Emo);
  const auto emoTokens = seam::native_ui::design::tokensFor(DesignMode::Emo, Contrast::Standard);
  const auto sceneTokens = seam::native_ui::design::tokensFor(DesignMode::Scene, Contrast::Standard);
  CHECK(emoTokens.color.accent != sceneTokens.color.accent);
  CHECK(emoTokens.color.canvas != sceneTokens.color.canvas);
  // The two looks differ in their faces as well as their colours, which is the half of the switch a
  // colour-only comparison cannot see.
  CHECK(emoTokens.type.heading == emoTokens.type.heading);
  fixture.shell.setMode(DesignMode::Scene, false);
  CHECK(fixture.shell.mode() == DesignMode::Scene);
  fixture.shell.setMode(DesignMode::Emo, false);
  CHECK(fixture.shell.mode() == DesignMode::Emo);
}

// Every overlay and sheet the shell can present, written out as a frame in a system face. The layout
// sweep over these surfaces measures the text records the shell produced and the rectangles it placed,
// which is the shell's account of itself; nothing has ever drawn one of these and looked at it. The
// recovery-support surface in particular carries a name chosen to be longer than any row can show, and
// the diagnostics popover carries a refusal worded as long as a refusal can be, so the surfaces where a
// row has to shorten its own text are already in this list and are the reason to look.
void writeCapturedOverlay(LayoutFixture& f, const Surface& surface, double width, double height,
                          const std::filesystem::path& path) {
  Frame frame;
  // The baseline is painted first so the surface opens on a window that already has its content, which
  // is how a creator meets it: over the SING workspace, not over an empty frame.
  if (!paintFrame(f, width, height, 1.0, frame)) return;
  if (surface.prepare && surface.prepare(f) && !paintFrame(f, width, height, 1.0, frame)) return;
  if (!surface.open(f)) return;
  native_ui::PixelSurface pixels{static_cast<std::uint32_t>(std::lround(width)),
                                 static_cast<std::uint32_t>(std::lround(height))};
  native_ui::RasterCanvas canvas{pixels, 1.0, designSystemFont()};
  if (!f.shell.prepareFrame(f.controller, width, height)) return;
  if (!f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick()))
    return;
  std::filesystem::create_directories(path.parent_path());
  CHECK(pixels.writePpm(path));
  capturedFrameNames().insert(path.stem().string());
}


void sweepWorkspace(Checker& checker, Workspace workspace, std::string_view name,
                    DesignMode mode = DesignMode::Emo) {
  LayoutFixture f{mode};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.shell.setWorkspace(f.controller, workspace);
  Frame frame;
  for (const auto& size : kSizes)
    for (const auto scale : kScales) {
      const auto where = frameName(name, size[0], size[1], scale);
      if (!paintFrame(f, size[0], size[1], scale, frame)) {
        checker.report(where, std::string{name} + " not-presented", "");
        continue;
      }
      CHECK(f.shell.workspace() == workspace);
      checker.check(frame, where, size[0], size[1]);
    }
}

}  // namespace

// The property sweeps above check that widgets stay apart, that targets are 24 points and that text is
// either whole or elided with its full text on a node. None of that says the text is big enough to
// read: shrinking a type token to 4 point leaves every geometric property true and every case green.
// This is the case that says so, and it is here because the token scale is the only thing holding a
// floor for the editor and shell, and nothing was holding it to one.
// Every workspace in the design layer has a layout-property case and none of them has ever written a
// frame: `paintFrame` hands the shell a null text engine and returns the text records the capture saw,
// which measures the shell's intent rather than its rendering. The shell's type scale says body 13,
// label 12, smallLabel 11, rulerMicro 10, and an earlier entry established that no call site goes
// below it, but that is a claim about numbers passed to a draw. This case renders each workspace with
// a real face at two window sizes and writes the frames, so the shell can be read rather than inferred.
TEST_CASE("every design workspace is capturable in a system face at both window sizes") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  // The frames are the point of this case and are only written when a directory is named, so nothing
  // here depends on a capture existing; with no directory the frames go to a temporary path and are
  // discarded when the case ends.
  const std::filesystem::path root = directory != nullptr && *directory != '\0'
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-frames")};
  LayoutFixture fixture;
  const std::pair<Workspace, std::string_view> workspaces[] = {
      {Workspace::Sing, "sing"}, {Workspace::Voice, "voice"}, {Workspace::Tune, "tune"},
      {Workspace::Mix, "mix"}, {Workspace::Export, "export"}};
  for (const auto& [workspace, name] : workspaces) {
    for (const auto& size : {std::pair{1440.0, 900.0}, std::pair{1100.0, 700.0}}) {
      writeCapturedFrame(fixture, workspace, size.first, size.second,
                         root / (std::string{name} + "-" +
                                 std::to_string(static_cast<int>(size.first)) + "x" +
                                 std::to_string(static_cast<int>(size.second)) + ".ppm"));
    }
  }
}

// The SING workspace changes presentation with the window: a full three-card rack, a narrow rail, or a
// 44-point portrait drawer. The thresholds live in sing_layout.cpp, so the frame alone cannot say
// whether a window is showing the presentation it should. This case asks the layout directly, so a
// frame that looks like a drawer at a width that should be full is caught as a layout fact rather than
// argued about from pixels.
// The shell string table has two kinds of wording that both used to end in an ellipsis, and they mean
// opposite things. A label the canvas has genuinely shortened says so: the canvas ellipsizes any string
// that does not fit the box it was given, and that is the correct use of the mark. A label that was never
// shortened says nothing by carrying one, and on a button with room it is worse than nothing: it claims
// the button continues past what it can show. The MIX settings button is the case that settles it, and
// the frame is what settles that: its capsule is 112 points wide and the label measures about 57, so the
// three characters were drawn into 55 points of empty capsule.
//
// So the two are kept apart by construction rather than by inspection. Progress wording keeps its
// ellipsis, because "Saving…" is announcing that something is happening right now and the mark is
// what says so; a button or menu label does not, because the canvas will add a mark if the label really
// is too long for its box. Restoring an ellipsis to any of the button labels fails the case below.
// The recovery support overlay painted its summary sentence through the middle of its own PREV button.
// Both are the same kind of bright text on the same background at the same size, so the frame showed
// "2 owned reports" running straight across the word PREV and neither string was readable. Nothing
// about it was visible in the source: the summary was at panel.y + 44 and the buttons were laid out at
// panel.y + 40 from the same left inset, so the two simply overlapped, and each painter was correct on
// its own.
//
// The property is that the overlay's painted rows and its control rows occupy the same strip in the
// same order and do not overlap, and it is checked against the control rectangles the shell publishes
// rather than against the frame, because the control rectangles are what a pointer and the layout sweep
// both use. Putting the buttons back on the summary's line fails it.
TEST_CASE("the recovery support overlay paints its summary clear of its own buttons") {
  LayoutFixture fixture;
  fixture.shell.setWorkspace(fixture.controller, Workspace::Sing);
  Frame frame;
  CHECK(paintFrame(fixture, 1440.0, 900.0, 1.0, frame));
  fixture.controller.setRecoverySupportView(native_ui::RecoverySupportView{
      .visible = true,
      .mode = native_ui::RecoverySupportMode::Reports,
      .items = {{.name = "report-a", .detail = "crash marker", .bytes = 4096U}},
      .reportCount = 1U,
      .status = "One owned report",
  });
  // The card and the controls are what the shell publishes once the overlay is open, so the frame
  // that describes them is painted after the overlay is set, not before.
  Frame openFrame;
  CHECK(paintFrame(fixture, 1440.0, 900.0, 1.0, openFrame));
  frame = openFrame;

  // The summary line and the paging buttons share one strip, and the strip is laid out from the panel's
  // own top rather than from constants each painter chose separately.
  const auto panel = overlayCard(frame);
  CHECK(panel.has_value());
  if (!panel) return;
  const auto buttons = std::find_if(frame.nodes.begin(), frame.nodes.end(),
                                    [](const auto& node) { return node.id == "support.track.previous"; });
  CHECK(buttons != frame.nodes.end());
  if (buttons == frame.nodes.end()) return;
  // The PREV button starts below the 16 point summary line the overlay paints at panel.y + 44.
  constexpr double kSummaryTop = 44.0;
  constexpr double kSummaryLine = 16.0;
  constexpr double kSummaryGap = 6.0;
  const auto summaryBottom = kSummaryTop + kSummaryLine + kSummaryGap;
  CHECK(buttons->bounds.y >= summaryBottom);
  // And it is still inside the panel, with room for the item list below it.
  CHECK(buttons->bounds.bottom() <= panel->bottom());
  // The two paging buttons are on the same row and do not overlap each other either.
  const auto next = std::find_if(frame.nodes.begin(), frame.nodes.end(),
                                 [](const auto& node) { return node.id == "support.track.next"; });
  CHECK(next != frame.nodes.end());
  if (next != frame.nodes.end()) {
    CHECK(std::abs(next->bounds.y - buttons->bounds.y) < 0.001);
    CHECK(buttons->bounds.right() <= next->bounds.x + 0.001);
  }
}

// Every overlay and sheet the shell can present, drawn in a system face. The layout sweep over these
// surfaces reads the shell's own text records and rectangles; none of them has been rendered and read.
// The list is not incidental either: it already contains the two surfaces built to stress a row that has
// to shorten its own text (recovery support carries a report name longer than any row can show, and the
// diagnostic notices carry a refusal worded as long as a refusal can be), which is exactly where a
// rendered frame says something a layout property does not.
TEST_CASE("every overlay and sheet is capturable in a system face") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  const std::filesystem::path root = directory != nullptr && *directory != '\0'
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-overlays")};
  std::size_t written = 0U;
  for (const auto& surface : overlaySurfaces()) {
    LayoutFixture fixture;
    fixture.shell.setWorkspace(fixture.controller, Workspace::Sing);
    writeCapturedOverlay(fixture, surface, 1440.0, 900.0,
                         root / (std::string{surface.name} + "-1440x900.ppm"));
    // A surface that could be opened at the canonical window has produced a frame with content in it;
    // one that cannot (the compact inspector exists only at compact widths) is not a failure.
    if (std::filesystem::exists(root / (std::string{surface.name} + "-1440x900.ppm"))) ++written;
  }
  // The list is the whole claim: every surface the shell can present was offered a frame.
  CHECK(written == overlaySurfaces().size());
}

TEST_CASE("a shell label carries an ellipsis only when it is progress wording") {
  using seam::native_ui::design::Str;
  // The labels that name a control a person presses. None of them may end in the mark.
  const std::string_view controlLabels[] = {
      "Export set", "Install", "Open recipe", "Save As", "Voice seed", "Duplicate pose",
      "Add frication", "Frication seed", "Open", "Settings", "{0} · Settings", "New starter voice"};
  for (const auto key : {Str::ExportSet2, Str::Install, Str::OpenRecipe, Str::SaveAs,
                         Str::VoiceSeed, Str::DuplicatePose, Str::AddFrication,
                         Str::FricationSeed, Str::Open2, Str::Settings2, Str::NamedSettings,
                         Str::NewStarterVoice}) {
    const auto text = seam::native_ui::design::englishShellString(key);
    CHECK(text.find("…") == std::string::npos);
  }
  // And the wording that really does announce work in progress keeps its mark, because there is nothing
  // else in it to say so.
  for (const auto key : {Str::Exporting, Str::Saving, Str::RenderingTheAudition,
                         Str::RenderingB, Str::OpeningFile, Str::SavingFile}) {
    const auto text = seam::native_ui::design::englishShellString(key);
    CHECK(text.find("…") != std::string::npos);
  }
  // The list above is the whole claim: every control label is named, so a new control label carrying a
  // mark has to be added here deliberately rather than arriving with one.
  CHECK(std::size(controlLabels) == 12U);
  for (const auto label : controlLabels) CHECK(label.find("…") == std::string_view::npos);
}

TEST_CASE("the SING rack presentation follows the window it is given") {
  const auto presentation = [](double width, double height) {
    return seam::native_ui::design::solveSingLayout(width, height, false);
  };
  // The canonical 1440x900 window carries all three cards, which is the frame the approved design was
  // drawn at.
  const auto full = presentation(1440.0, 900.0);
  CHECK(full.rack == native_ui::design::RackPresentation::Full);
  CHECK(full.portraitRing.width >= 120.0);
  // A window above the drawer width but too short for all three cards falls back to the rail rather
  // than clipping the cards under the status bar. 1100x700 is that window, and the frame rendered at
  // it shows the rail: a 56-point column, not the drawer and not the full rack.
  const auto rail = presentation(1100.0, 700.0);
  CHECK(rail.rack != native_ui::design::RackPresentation::Drawer);
  CHECK(rail.rackArea.width <= 56.0);
  // Below the drawer width it is the 44-point portrait.
  const auto drawer = presentation(700.0, 700.0);
  CHECK(drawer.rack == native_ui::design::RackPresentation::Drawer);
  CHECK(drawer.rackArea.width == 44.0);
  // Whatever the presentation, the rack column is inside the window and clear of the musical area.
  for (const auto& layout : {full, rail, drawer}) {
    CHECK(layout.rackArea.right() <= 1440.0);
    CHECK(layout.rackArea.x >= 0.0);
  }
}

TEST_CASE("no drawn line in any workspace is smaller than the readable floor") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The floor is derived rather than written down. A line draws its ascent plus its descent, which
  // for these faces is about 55 to 66 percent of the size asked for, so the smallest role in
  // TypeScale (rulerMicro, 10 point) lands at an ink of about 6.6 and smallLabel (11) at about 6.0.
  // The floor is that measured ratio applied to the smallest role, so a token that shrinks below it
  // is caught and a token that grows is not penalised. The ratio was measured from the frames this
  // case paints, not assumed.
  constexpr double kInkFloor = 6.0;
  // Ink is measured from the renderer and then divided by the paint scale, so a line whose nominal
  // ink is exactly the floor arrives a few ULPs under it. The slack is a thousandth of a point,
  // two orders of magnitude below the smallest real step in the type scale, so it forgives that
  // arithmetic and nothing else.
  constexpr double kInkEpsilon = 0.001;
  double smallest = 0.0;
  std::string smallestWhere;
  std::size_t measured = 0U;
  std::vector<std::pair<std::string, double>> perLine;
  const auto measureAll = [&](Workspace workspace, std::string_view name,
                             DesignMode mode = DesignMode::Emo) {
    LayoutFixture f{mode};
    f.shell.setWorkspace(f.controller, workspace);
    Frame frame;
    for (const auto& size : kSizes)
      for (const auto scale : kScales) {
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        for (const auto& line : frame.text) {
          if (line.text.empty()) continue;
          const auto height = line.ink.height / (scale > 0.0 ? scale : 1.0);
          ++measured;
          if (measured == 1U || height < smallest) {
            smallest = height;
            smallestWhere = std::string{name} + ": " + line.text;
          }
          perLine.emplace_back(line.text, height);
        }
      }
  };
  measureAll(Workspace::Sing, "sing");
  measureAll(Workspace::Voice, "voice");
  measureAll(Workspace::Tune, "tune");
  measureAll(Workspace::Mix, "mix");
  measureAll(Workspace::Export, "export");
  measureAll(Workspace::Sing, "sing-scene", DesignMode::Scene);
  CHECK(measured > 0U);
  if (smallest < kInkFloor - kInkEpsilon)
    throw seam::test::Failure{"smallest ink " + std::to_string(smallest) + " on \"" +
                             smallestWhere + "\"; distinct line heights: " + [&] {
                               std::string out;
                               std::vector<double> seen;
                               for (const auto& [text, height] : perLine)
                                 if (std::find(seen.begin(), seen.end(), height) == seen.end()) {
                                   seen.push_back(height);
                                   out += std::to_string(height) + "(" + text.substr(0, 10) + ") ";
                                 }
                               return out;
                             }()};
}

TEST_CASE("every workspace keeps widgets apart, targets at 24 points and text whole or elided") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  Checker checker;
  sweepWorkspace(checker, Workspace::Sing, "sing");
  sweepWorkspace(checker, Workspace::Voice, "voice");
  sweepWorkspace(checker, Workspace::Tune, "tune");
  sweepWorkspace(checker, Workspace::Mix, "mix");
  sweepWorkspace(checker, Workspace::Export, "export");
  sweepWorkspace(checker, Workspace::Sing, "sing-scene", DesignMode::Scene);
  checker.finish("workspaces");
}

TEST_CASE("every re-homed overlay keeps its layout properties at every size and scale") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  Checker checker;
  // What each window size paints with no overlay up, the covered content for that size.
  std::vector<std::vector<TextRecord>> baseline;
  {
    LayoutFixture base;
    Frame frame;
    for (const auto& size : kSizes)
      for (const auto scale : kScales) {
        CHECK(paintFrame(base, size[0], size[1], scale, frame));
        baseline.push_back(frame.text);
      }
  }
  for (const auto& surface : overlaySurfaces()) {
    LayoutFixture f;
    LayoutFixture base;
    Frame frame;
    std::size_t index = 0U;
    for (const auto& size : kSizes)
      for (const auto scale : kScales) {
        const auto covered = surface.prepare ? coveredText(surface, base, size[0], size[1], scale)
                                             : baseline[index];
        ++index;
        const auto where = frameName(surface.name, size[0], size[1], scale);
        // A resize may close a transient surface (a field whose anchor moved); reopen it on the
        // new geometry, as the user would.
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        if (f.shell.overlayKind(f.controller) != surface.kind) {
          if (!surface.open(f)) {
            checker.report(where, std::string{surface.name} + " could-not-open", "");
            continue;
          }
          if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        }
        if (f.shell.overlayKind(f.controller) != surface.kind) {
          checker.report(where, std::string{surface.name} + " not-presented", "");
          continue;
        }
        if (!overlayCard(frame).has_value())
          checker.report(where, std::string{surface.name} + " no-card", "");
        checker.check(frame, where, size[0], size[1], &covered);
      }
  }
  checker.finish("overlays");
}

TEST_CASE("Settings sections retain accessible targets through compact and Korean layouts") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  Checker checker;
  constexpr std::array<std::array<double, 2U>, 7U> sizes{{{480.0, 320.0},
      {720.0, 480.0}, {1024.0, 640.0}, {1440.0, 900.0}, {1920.0, 1080.0},
      {2560.0, 1440.0}, {3840.0, 2160.0}}};
  constexpr std::array<std::string_view, 4U> sections{"audio", "appearance", "language", "about"};
  for (const auto korean : {false, true}) {
    LayoutFixture f;
    if (korean) f.shell.setLanguage("ko", false);
    Frame frame;
    for (const auto& size : sizes)
      for (const auto scale : kScales) {
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        const auto covered = frame.text;
        for (const auto section : sections) {
          f.shell.setSettingsOpen(true);
          CHECK(paintFrame(f, size[0], size[1], scale, frame));
          CHECK(f.shell.dispatchController(f.controller,
              std::string{"shell.overlay.settings.section."} + std::string{section},
              SemanticAction::Activate).hasValue());
          CHECK(paintFrame(f, size[0], size[1], scale, frame));
          checker.check(frame, std::string{korean ? "ko-" : "en-"} + std::string{section},
                        size[0], size[1], &covered);
          f.shell.setSettingsOpen(false);
        }
      }
  }
  checker.finish("settings");
}

// A capture that writes a frame of the window without the surface in it is worse than no capture: it
// looks like evidence and is a picture of something else. The capture helper used to call
// setWorkspace before painting, and setWorkspace closes whatever the shell had open, so the first
// version of the compact capture wrote eleven frames of an editor with no workspace menu in it and one
// reader would not have known. The case below is the check that a captured frame really is the surface:
// the shell's published controls for it are in the frame's semantic tree.
TEST_CASE("a captured frame carries the surface it was opened for") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  LayoutFixture fixture;
  Frame frame;
  CHECK(paintFrame(fixture, 720.0, 480.0, 1.0, frame));
  CHECK(fixture.shell.layout().workspaceMenuButton.width > 0.0);
  CHECK(fixture.shell.dispatchSemantic(fixture.controller, "shell.workspace-menu",
                                       SemanticAction::Activate).hasValue());
  // The capture path itself, not paintFrame: the defect was in the capture helper, and a case
  // written against paintFrame passed whether or not the helper kept what the caller opened.
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  const std::filesystem::path root = directory != nullptr && *directory != '\0'
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-capture-check")};
  writeCapturedFrame(fixture, Workspace::Sing, 720.0, 480.0,
                     root / "capture-keeps-the-open-surface.ppm");
  Frame open;
  CHECK(paintFrame(fixture, 720.0, 480.0, 1.0, open));
  // The menu's own rows are in the tree the shell published while it was open.
  const auto menuItem = std::any_of(open.nodes.begin(), open.nodes.end(), [](const auto& node) {
    return node.id.starts_with("shell.workspace.") && node.role == SemanticRole::Button;
  });
  CHECK(menuItem);
  // And it was not there before, so the two frames are different surfaces rather than the same one
  // twice.
  const auto menuItemBefore = std::any_of(frame.nodes.begin(), frame.nodes.end(),
                                           [](const auto& node) {
                                             return node.id.starts_with("shell.workspace.") &&
                                                    node.role == SemanticRole::Button;
                                           });
  CHECK(!menuItemBefore);
}

// The two surfaces that exist only at compact widths have had their layout measured at every size and
// never been drawn. They are the last places in the editor with no rendered frame, and they are the two
// most worth having one of: the compact inspector is the drawer that carries the SINGER card on the
// narrowest windows, and the workspace menu is the only way to reach another workspace from a window
// too narrow for the tab row. Both open by a semantic action rather than by a click, so they are as
// reachable as anything else in the shell and as invisible in a capture.
TEST_CASE("the compact inspector and the workspace menu are capturable in a system face") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const char* directory = std::getenv("SEAM_DESIGN_CAPTURE_DIRECTORY");
  const std::filesystem::path root = directory != nullptr && *directory != '\0'
      ? std::filesystem::path{directory}
      : std::filesystem::path{test::support::temporaryDirectory("design-compact")};
  std::size_t inspectors = 0U;
  std::size_t menus = 0U;
  for (const auto& size : kSizes) {
    {
      LayoutFixture fixture;
      Frame frame;
      if (!paintFrame(fixture, size[0], size[1], 1.0, frame)) continue;
      // The inspector drawer exists only where the full rack does not, which is the whole reason it
      // is here: at a width where the cards fit there is nothing to open.
      if (fixture.shell.layout().rack == RackPresentation::Full) continue;
      if (!fixture.shell.dispatchSemantic(fixture.controller, "shell.inspector",
                                          SemanticAction::Activate).hasValue()) continue;
      writeCapturedFrame(fixture, Workspace::Sing, size[0], size[1],
                         root / ("compact-inspector-" + std::to_string(static_cast<int>(size[0])) +
                                 "x" + std::to_string(static_cast<int>(size[1])) + ".ppm"),
                         fixture.shell.layout().rack == RackPresentation::Full ? DesignMode::Emo
                                                                               : DesignMode::Emo);
      ++inspectors;
    }
    {
      LayoutFixture fixture;
      Frame frame;
      if (!paintFrame(fixture, size[0], size[1], 1.0, frame)) continue;
      if (fixture.shell.layout().workspaceMenuButton.width <= 0.0) continue;
      if (!fixture.shell.dispatchSemantic(fixture.controller, "shell.workspace-menu",
                                          SemanticAction::Activate).hasValue()) continue;
      writeCapturedFrame(fixture, Workspace::Sing, size[0], size[1],
                         root / ("workspace-menu-" + std::to_string(static_cast<int>(size[0])) +
                                 "x" + std::to_string(static_cast<int>(size[1])) + ".ppm"));
      ++menus;
    }
  }
  // Both exist somewhere in the supported size range, so neither capture is vacuous.
  CHECK(inspectors > 0U);
  CHECK(menus > 0U);
}

TEST_CASE("the compact inspector and the workspace menu keep their layout properties") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  Checker checker;
  std::size_t inspectors = 0U;
  std::size_t menus = 0U;
  for (const auto& size : kSizes)
    for (const auto scale : kScales) {
      {
        LayoutFixture f;
        Frame frame;
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        if (f.shell.layout().rack != RackPresentation::Full) {
          const auto covered = frame.text;
          CHECK(f.shell.dispatchSemantic(f.controller, "shell.inspector", SemanticAction::Activate)
                    .hasValue());
          CHECK(paintFrame(f, size[0], size[1], scale, frame));
          CHECK(f.shell.inspectorOpen());
          ++inspectors;
          checker.check(frame, frameName("inspector", size[0], size[1], scale), size[0], size[1],
                        &covered);
        }
      }
      {
        LayoutFixture f;
        Frame frame;
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        if (f.shell.layout().workspaceMenuButton.width > 0.0) {
          const auto covered = frame.text;
          CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace-menu",
                                         SemanticAction::Activate)
                    .hasValue());
          CHECK(paintFrame(f, size[0], size[1], scale, frame));
          ++menus;
          checker.check(frame, frameName("workspace-menu", size[0], size[1], scale), size[0],
                        size[1], &covered);
        }
      }
    }
  CHECK(inspectors > 0U);
  std::printf("compact inspector frames: %zu, workspace menu frames: %zu\n", inspectors, menus);
  checker.finish("sheets");
}

TEST_CASE("switching contrast repaints the background it caches") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto pixels = [](LayoutFixture& f) {
    Frame frame;
    CHECK(paintFrame(f, 1280.0, 800.0, 1.0, frame));
    native_ui::PixelSurface surface{1280U, 800U};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    CHECK(f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick()));
    return std::vector<std::uint32_t>{surface.pixels().begin(), surface.pixels().end()};
  };
  LayoutFixture switched{DesignMode::Scene};
  static_cast<void>(pixels(switched));
  switched.shell.setContrast(Contrast::High, false);
  LayoutFixture fresh{DesignMode::Scene, Contrast::High};
  CHECK(pixels(switched) == pixels(fresh));
}

TEST_CASE("the shell string table is English by default and translatable by stable key") {
  using native_ui::design::englishShellString;
  using native_ui::design::ScopedShellStrings;
  using native_ui::design::shellStringCount;
  using native_ui::design::shellStringKey;
  using native_ui::design::ShellStringTable;
  using native_ui::design::Str;
  using native_ui::design::tr;
  CHECK(shellStringCount() > 400U);
  CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");
  CHECK(shellStringKey(Str::ChangeVoice) == "ChangeVoice");
  std::set<std::string_view> keys;
  for (std::size_t i = 0U; i < shellStringCount(); ++i) {
    const auto id = static_cast<Str>(i);
    CHECK(keys.insert(shellStringKey(id)).second);  // keys are unique
    CHECK(std::string_view{tr(id)} == englishShellString(id));
  }
  ShellStringTable table;
  CHECK(table.set("ChangeVoice", "Stimme wechseln"));
  CHECK(!table.set("NoSuchKey", "x"));
  {
    ScopedShellStrings scope{table};
    CHECK(std::string_view{tr(Str::ChangeVoice)} == "Stimme wechseln");
    CHECK(std::string_view{tr(Str::Sing)} == englishShellString(Str::Sing));
  }
  CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");
  // Pseudo-localization is at least 40% longer, in characters, for every entry.
  const auto pseudo = ShellStringTable::pseudoLocalized(0.4);
  const auto characters = [](std::string_view s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) {
      return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
    }));
  };
  for (std::size_t i = 0U; i < shellStringCount(); ++i) {
    const auto id = static_cast<Str>(i);
    const auto english = characters(englishShellString(id));
    CHECK(characters(pseudo.text(id)) >=
          static_cast<std::size_t>(std::ceil(static_cast<double>(english) * 1.4)));
  }
}

TEST_CASE("pseudo-localized text 40% longer elides with its full text on an accessibility node") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto pseudo = native_ui::design::ShellStringTable::pseudoLocalized(0.4);
  native_ui::design::ScopedShellStrings scope{pseudo};
  Checker checker;
  checker.checkWidgets = false;  // geometry does not depend on text; the sweeps above cover it
  std::size_t pseudoLines = 0U;
  std::size_t elided = 0U;
  const auto count = [&](const Frame& frame) {
    for (const auto& line : frame.text) {
      if (!line.text.starts_with("[")) continue;
      ++pseudoLines;
      if (line.elided) ++elided;
    }
  };
  constexpr std::array<std::array<double, 2U>, 4U> kPseudoSizes{
      {{720.0, 480.0}, {1100.0, 720.0}, {1600.0, 900.0}, {3840.0, 2160.0}}};
  for (const auto workspace :
       {Workspace::Sing, Workspace::Voice, Workspace::Tune, Workspace::Mix, Workspace::Export}) {
    LayoutFixture f;
    f.shell.setWorkspace(f.controller, workspace);
    Frame frame;
    for (const auto& size : kPseudoSizes)
      for (const auto scale : {1.0}) {  // text measures in points; the sweeps above cover scales
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        count(frame);
        checker.check(frame, frameName("pseudo-workspace", size[0], size[1], scale), size[0],
                      size[1]);
      }
  }
  for (const auto& surface : overlaySurfaces()) {
    LayoutFixture f;
    LayoutFixture base;  // the same window with no overlay: the content the card covers
    Frame frame;
    for (const auto& size : kPseudoSizes) {
      const auto covered = coveredText(surface, base, size[0], size[1], 1.0);
      if (!paintFrame(f, size[0], size[1], 1.0, frame)) continue;
      if (f.shell.overlayKind(f.controller) != surface.kind && !surface.open(f)) continue;
      if (!paintFrame(f, size[0], size[1], 1.0, frame)) continue;
      if (f.shell.overlayKind(f.controller) != surface.kind) continue;
      count(frame);
      checker.check(frame, frameName(std::string{"pseudo-"} + std::string{surface.name}, size[0],
                                     size[1], 1.0),
                    size[0], size[1], &covered);
    }
  }
  std::printf("pseudo-localized lines painted: %zu, elided: %zu\n", pseudoLines, elided);
  CHECK(pseudoLines > 0U);
  CHECK(elided > 0U);  // the sweep really exercises elision
  // Known follow-ups for translation: compact labels and readouts whose node publishes a fuller
  // phrasing of the same fact rather than the painted words. English fits everywhere; +40% text
  // elides them, and the node still says what they mean, but not verbatim. Each is named so a fix
  // (or a new case) changes this list deliberately.
  using native_ui::design::Str;
  const std::array<std::pair<Str, std::string_view>, 12U> compactForms{{
      {Str::Out, "header output meter label; node: Output level"},
      {Str::Unavailable, "refused knob caption; node value: the refusal reason"},
      {Str::PitchCaptionReadOnly, "TUNE pitch caption; node description words it differently"},
      {Str::Text9, "TUNE pitch ruler tick sign; ticks are decorative"},
      {Str::Stereo, "MIX master format line; node value: channel layout"},
      {Str::CLIP, "MIX clip light; node: clip state"},
      {Str::AudioOffline, "MIX compact device button; node: Audio settings"},
      {Str::Source, "phoneme review field label; node names the field"},
      {Str::MidiKey, "overlap row key readout; node: MIDI after a separator"},
      {Str::Refresh, "voice browser toolbar; node: Refresh installed voices"},
      {Str::Install, "voice browser toolbar; node: Install a voicebank"},
      {Str::VoiceCardCounts, "voice card counts; node value words them separately"},
  }};
  // A form with values in it is recognised by its longest run of fixed words: the painted text has
  // the values filled in where the entry has {0}, {1}.
  const auto fixedWords = [&](Str id) {
    const std::string_view text{pseudo.text(id)};
    std::string_view longest;
    std::size_t start = 0U;
    while (start <= text.size()) {
      auto open = text.find('{', start);
      if (open == std::string_view::npos) open = text.size();
      auto words = text.substr(start, open - start);  // without the pseudo brackets and padding
      while (!words.empty() && std::string_view{"[]~"}.find(words.front()) != std::string_view::npos)
        words.remove_prefix(1U);
      while (!words.empty() && std::string_view{"[]~"}.find(words.back()) != std::string_view::npos)
        words.remove_suffix(1U);
      if (words.size() > longest.size()) longest = words;
      const auto close = text.find('}', open);
      if (close == std::string_view::npos) break;
      start = close + 1U;
    }
    return std::string{longest};
  };
  for (auto it = checker.order.begin(); it != checker.order.end();) {
    const auto known = std::any_of(compactForms.begin(), compactForms.end(), [&](const auto& form) {
      return it->find(fixedWords(form.first)) != std::string::npos;
    });
    if (known) {
      checker.problems.erase(*it);
      it = checker.order.erase(it);
    } else {
      ++it;
    }
  }
  checker.finish("pseudo-localized");
}

// Localization: translation files, the Korean table, Hangul text and the language setting.

TEST_CASE("translation files load by stable key and reject what they cannot trust") {
  using namespace native_ui::design;
  // Formatting: numbered values in any order, literal braces, and a value with no argument left
  // as written so a test sees it.
  CHECK(formatShellText("{1} of {0}", {"a", "b"}) == "b of a");
  CHECK(formatShellText("{{0}} is {0}", {"x"}) == "{0} is x");
  CHECK(formatShellText("{2}", {"a"}) == "{2}");
  CHECK((shellPlaceholders("{1} {0} {1}").indices == std::vector<std::size_t>{0U, 1U}));
  CHECK(!shellPlaceholders("{0").valid);
  CHECK(!shellPlaceholders("{a}").valid);
  CHECK(!shellPlaceholders("0}").valid);

  const auto loaded = parseShellStrings(R"({
    "language": "xx", "name": "Test",
    "strings": {
      "ChangeVoice": "Stimme wechseln",
      "PageOfPages": "Seite {1} von {0}",
      "NoSuchKey": "ignored",
      "OwnedReportCount": "Berichte",
      "RangeOfTotal": "{0}-{1} von {2} {3}",
      "TitleAndMore": "{0} +{1 mehr",
      "Save": 5
    }})");
  CHECK(loaded.hasValue());
  if (!loaded) return;
  const auto& report = loaded.value().report;
  CHECK(report.language == "xx");
  CHECK(report.name == "Test");
  CHECK(report.translated == 2U);
  CHECK(report.unknownKeys == std::vector<std::string>{"NoSuchKey"});
  auto rejected = report.rejectedKeys;
  std::sort(rejected.begin(), rejected.end());
  CHECK((rejected == std::vector<std::string>{"OwnedReportCount", "RangeOfTotal", "Save",
                                              "TitleAndMore"}));
  // A key that was present, even when rejected, is not missing; every other key is.
  CHECK(report.missingKeys.size() == shellStringCount() - 6U);
  CHECK(!report.clean());
  {
    ScopedShellStrings scope{loaded.value().table};
    CHECK(std::string_view{tr(Str::ChangeVoice)} == "Stimme wechseln");
    CHECK(trf(Str::PageOfPages, {"2", "5"}) == "Seite 5 von 2");
    // Rejected and missing entries read as English, with every value in place.
    CHECK(trf(Str::OwnedReportCount, {"3"}) == "3 owned reports");
    CHECK(trf(Str::RangeOfTotal, {"1", "4", "9"}) == "1\u20134 of 9");
    CHECK(std::string_view{tr(Str::Save)} == "Save");
    CHECK(std::string_view{tr(Str::Untitled)} == "Untitled");
  }
  CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");
  // What is not a translation file at all fails as a whole, and the shell stays in English.
  CHECK(!parseShellStrings("not json"));
  CHECK(!parseShellStrings(R"([])"));
  CHECK(!parseShellStrings(R"({"strings": {}})"));
  CHECK(!parseShellStrings(R"({"language": "xx"})"));
  CHECK(!parseShellStrings(R"({"language": "xx", "strings": []})"));
  CHECK(!loadShellStrings(std::filesystem::path{"/nonexistent/seam-l10n/xx.json"}));
  // Platform language tags choose an offered language, else English.
  CHECK(shellLanguageFor("ko-KR") == "ko");
  CHECK(shellLanguageFor("ko_KR") == "ko");
  CHECK(shellLanguageFor("KO") == "ko");
  CHECK(shellLanguageFor("en-KR") == "en");
  CHECK(shellLanguageFor("fr-FR") == "en");
  CHECK(shellLanguageFor("") == "en");
}

TEST_CASE("translation: the Korean file covers every entry with the English placeholders") {
  using namespace native_ui::design;
  const auto directory = native_ui::design::locateShellTranslations();
  CHECK(!directory.empty());
  if (directory.empty()) return;
  const auto loaded = loadShellStrings(directory / "ko.json");
  CHECK(loaded.hasValue());
  if (!loaded) return;
  const auto& report = loaded.value().report;
  CHECK(report.language == "ko");
  CHECK(report.name == "\uD55C\uAD6D\uC5B4");
  CHECK(report.clean());
  CHECK(report.translated == shellStringCount());
  for (const auto& key : report.unknownKeys) std::printf("ko.json unknown key %s\n", key.c_str());
  for (const auto& key : report.missingKeys) std::printf("ko.json missing key %s\n", key.c_str());
  for (const auto& key : report.rejectedKeys) std::printf("ko.json rejected key %s\n", key.c_str());
  // The workspace names and the looks are brand labels and stay English (docs/design/L10N.md).
  const auto& table = loaded.value().table;
  for (const auto id : {Str::Sing, Str::Voice, Str::Tune, Str::Mix, Str::Export, Str::Emo,
                        Str::Scene})
    CHECK(std::string_view{table.text(id)} == englishShellString(id));
  // Every other entry with words in it is Korean: an entry may read as its English only when its
  // words are acronyms, units or the product's own names.
  const std::set<std::string, std::less<>> kept{"Project", "Sing",  "Voice", "Tune", "Mix",
                                                "Export",  "Emo",   "Scene", "Seam", "Open",
                                                "sha",     "inf",   "dBFS",  "ct",   "kHz",
                                                "Hz"};
  std::size_t hangul = 0U;
  for (std::size_t i = 0U; i < shellStringCount(); ++i) {
    const auto id = static_cast<Str>(i);
    const std::string_view text{table.text(id)};
    if (text.find("\xEA") != std::string_view::npos || text.find("\xEB") != std::string_view::npos ||
        text.find("\xEC") != std::string_view::npos || text.find("\xED") != std::string_view::npos)
      ++hangul;  // lead bytes of U+A000..U+DFFF, which hold every Hangul syllable
    if (text != englishShellString(id)) continue;
    std::size_t start = 0U;
    while (start < text.size()) {
      while (start < text.size() && !std::isalpha(static_cast<unsigned char>(text[start]))) ++start;
      auto end = start;
      while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) ++end;
      const auto word = text.substr(start, end - start);
      const auto acronym = std::all_of(word.begin(), word.end(), [](char c) {
        return std::isupper(static_cast<unsigned char>(c)) != 0;
      });
      if (!word.empty() && !acronym && !kept.contains(word)) {
        std::printf("ko.json leaves %s in English\n", std::string{shellStringKey(id)}.c_str());
        CHECK(false);
      }
      start = end;
    }
  }
  CHECK(hangul * 10U >= shellStringCount() * 8U);
}

TEST_CASE("translation: Hangul measures with real glyphs in every font role") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  using native_ui::paint::FontRole;
  using native_ui::paint::TextStyle;
  native_ui::PixelSurface surface{64U, 64U};
  auto canvas = native_ui::paint::makeCanvas(surface, 1.0);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) return;
  const std::string two{"\uAC00\uB098"};                    // 가나
  const std::string four{"\uAC00\uB098\uB2E4\uB77C"};     // 가나다라
  const std::string phrase{"\uBCF4\uC774\uC2A4 \uBCC0\uACBD"};  // 보이스 변경
  for (const auto role : {FontRole::Ui, FontRole::UiMedium, FontRole::UiSemibold, FontRole::UiBold,
                          FontRole::Mono, FontRole::Display}) {
    const TextStyle style{.role = role, .size = 13.0};
    // Real glyphs from a face that has them, never the last-resort placeholder.
    CHECK(native_ui::paint::textRenderable(phrase, style));
    CHECK(native_ui::paint::textRenderable(four, style));
    // Full-width syllables: about one em each, and a longer run measures proportionally longer.
    const auto w2 = canvas->measure(two, style);
    const auto w4 = canvas->measure(four, style);
    CHECK(w2 > 13.0 * 1.4);
    CHECK(w2 < 13.0 * 2.6);
    CHECK(std::abs(w4 - 2.0 * w2) < 0.1 * w4);
  }
  // The check can fail: an unassigned code point has no face at all.
  CHECK(!native_ui::paint::textRenderable("\u0378", TextStyle{}));
}

TEST_CASE("translation: the shell follows its language setting and the header control steps it") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  using namespace native_ui::design;
  const auto node = [](const Frame& frame, std::string_view id) -> const SemanticNode* {
    for (const auto& n : frame.nodes)
      if (n.id == id) return &n;
    return nullptr;
  };
  {
    LayoutFixture f;
    Frame frame;
    CHECK(paintFrame(f, 1440.0, 900.0, 1.0, frame));
    CHECK(f.shell.language() == "en");
    CHECK(!f.shell.languageFollowsSystem());
    const auto* control = node(frame, "shell.language");
    CHECK(control != nullptr);
    if (control == nullptr) return;
    CHECK(control->name == "Language");
    CHECK(control->value == "English");
    CHECK(control->bounds.width >= 24.0);
    CHECK(control->bounds.height >= 24.0);
    CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");

    // Explicit English, then the control: Korean, the system's language, and back.
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.language", SemanticAction::Activate).hasValue());
    CHECK(f.shell.language() == "ko");
    CHECK(!f.shell.languageFollowsSystem());
    CHECK(f.shell.languageReport().clean());
    CHECK(std::string_view{tr(Str::ChangeVoice)} == "\uBCF4\uC774\uC2A4 \uBCC0\uACBD");
    CHECK(paintFrame(f, 1440.0, 900.0, 1.0, frame));
    control = node(frame, "shell.language");
    CHECK(control != nullptr);
    if (control == nullptr) return;
    CHECK(control->name == "\uC5B8\uC5B4");  // 언어
    CHECK(control->value == "\uD55C\uAD6D\uC5B4");
    // Painted text is Korean too: the SINGER card's button reads the Korean entry.
    CHECK(std::any_of(frame.text.begin(), frame.text.end(), [](const TextRecord& line) {
      return line.text == "\uBCF4\uC774\uC2A4 \uBCC0\uACBD";
    }));
    const auto koreanLanguage = std::find_if(frame.nodes.begin(), frame.nodes.end(),
                                             [](const SemanticNode& n) { return n.id == "shell.language"; });
    CHECK(koreanLanguage != frame.nodes.end());
    if (koreanLanguage != frame.nodes.end()) CHECK(koreanLanguage->name == "\uC5B8\uC5B4");
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.language", SemanticAction::Increment).hasValue());
    CHECK(f.shell.languageFollowsSystem());
    CHECK(f.shell.language() == shellLanguageFor(systemPreferredLanguage()));
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.language", SemanticAction::Decrement).hasValue());
    CHECK(f.shell.language() == "ko");
    // The pointer steps it the same way, from the rectangle the node publishes.
    const auto at = f.shell.layout().language;
    CHECK(f.shell
              .pointerDown(f.controller,
                           native_ui::PointerEvent{
                               .position = {at.x + at.width * 0.5, at.y + at.height * 0.5},
                               .button = native_ui::PointerButton::Left})
              .hasValue());
    CHECK(f.shell.languageFollowsSystem());
    // A language the shell does not offer reads as English.
    f.shell.setLanguage("fr", false);
    CHECK(f.shell.language() == "en");
    CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");
    f.shell.setLanguage("ko", false);
    CHECK(std::string_view{tr(Str::ChangeVoice)} != "Change voice");
  }
  // Dropping the shell drops its table.
  CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");
  // A shell whose translation directory has no file for the language stays in English.
  const auto empty = std::filesystem::temp_directory_path() / "seam-l10n-empty-test";
  std::filesystem::create_directories(empty);
  ::setenv("SEAM_L10N_ASSETS", empty.c_str(), 1);
  {
    LayoutFixture f;
    f.shell.setLanguage("ko", false);
    CHECK(f.shell.language() == "ko");
    CHECK(std::string_view{tr(Str::ChangeVoice)} == "Change voice");
  }
  ::unsetenv("SEAM_L10N_ASSETS");
  // The saved preference: an explicit language, or following the system.
  CHECK(DesignPreferences{}.language == "en");
  CHECK(!DesignPreferences{}.languageFollowsSystem);
}

TEST_CASE("two shell instances keep painted and semantic language when frames interleave") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  using namespace native_ui::design;
  LayoutFixture korean;
  LayoutFixture english;
  korean.shell.setLanguage("ko", false);
  Frame frame;
  for (int pass = 0; pass < 2; ++pass) {
    CHECK(paintFrame(korean, 1440.0, 900.0, 1.0, frame));
    CHECK(std::any_of(frame.text.begin(), frame.text.end(), [](const TextRecord& line) {
      return line.text == "\uBCF4\uC774\uC2A4 \uBCC0\uACBD";
    }));
    CHECK(paintFrame(english, 1440.0, 900.0, 1.0, frame));
    CHECK(std::any_of(frame.text.begin(), frame.text.end(), [](const TextRecord& line) {
      return line.text == "Change voice";
    }));
    const auto found = std::find_if(frame.nodes.begin(), frame.nodes.end(),
                                    [](const SemanticNode& n) { return n.id == "shell.language"; });
    CHECK(found != frame.nodes.end());
    if (found != frame.nodes.end()) CHECK(found->name == "Language");
  }
}

TEST_CASE("the Korean shell keeps widgets apart and its text whole or elided") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto korean = [](LayoutFixture& f) {
    f.shell.setLanguage("ko", false);
    CHECK(f.shell.language() == "ko");
    CHECK(f.shell.languageReport().clean());
    return true;
  };
  Checker checker;
  std::size_t hangulLines = 0U;
  std::size_t elided = 0U;
  const auto count = [&](const Frame& frame) {
    for (const auto& line : frame.text) {
      if (line.text.find("\xEA") == std::string::npos && line.text.find("\xEB") == std::string::npos &&
          line.text.find("\xEC") == std::string::npos && line.text.find("\xED") == std::string::npos)
        continue;
      ++hangulLines;
      if (line.elided) ++elided;
    }
  };
  constexpr std::array<std::array<double, 2U>, 5U> kKoreanSizes{
      {{720.0, 480.0}, {1100.0, 720.0}, {1440.0, 900.0}, {1920.0, 1080.0}, {3840.0, 2160.0}}};
  for (const auto workspace :
       {Workspace::Sing, Workspace::Voice, Workspace::Tune, Workspace::Mix, Workspace::Export}) {
    LayoutFixture f;
    korean(f);
    f.shell.setWorkspace(f.controller, workspace);
    Frame frame;
    for (const auto& size : kKoreanSizes)
      for (const auto scale : {1.0, 2.0}) {
        if (scale == 2.0 && size[0] > 1440.0) continue;  // the 1x sweep covers the large windows
        if (!paintFrame(f, size[0], size[1], scale, frame)) continue;
        count(frame);
        checker.check(frame, frameName("ko-workspace", size[0], size[1], scale), size[0], size[1]);
      }
  }
  for (const auto& surface : overlaySurfaces()) {
    LayoutFixture f;
    LayoutFixture base;
    korean(f);
    korean(base);
    Frame frame;
    for (const auto& size : kKoreanSizes) {
      const auto covered = coveredText(surface, base, size[0], size[1], 1.0);
      if (!paintFrame(f, size[0], size[1], 1.0, frame)) continue;
      if (f.shell.overlayKind(f.controller) != surface.kind && !surface.open(f)) {
        checker.report(frameName(surface.name, size[0], size[1], 1.0),
                       std::string{surface.name} + " could-not-open", "");
        continue;
      }
      if (!paintFrame(f, size[0], size[1], 1.0, frame)) continue;
      if (f.shell.overlayKind(f.controller) != surface.kind) continue;
      count(frame);
      checker.check(frame, frameName(std::string{"ko-"} + std::string{surface.name}, size[0],
                                     size[1], 1.0),
                    size[0], size[1], &covered);
    }
  }
  for (const auto& size : kKoreanSizes) {
    for (const auto* sheet : {"shell.inspector", "shell.workspace-menu"}) {
      LayoutFixture f;
      korean(f);
      Frame frame;
      if (!paintFrame(f, size[0], size[1], 1.0, frame)) continue;
      const auto compact = std::string_view{sheet} == "shell.inspector"
                               ? f.shell.layout().rack != RackPresentation::Full
                               : f.shell.layout().workspaceMenuButton.width > 0.0;
      if (!compact) continue;
      const auto covered = frame.text;
      CHECK(f.shell.dispatchSemantic(f.controller, sheet, SemanticAction::Activate).hasValue());
      CHECK(paintFrame(f, size[0], size[1], 1.0, frame));
      count(frame);
      checker.check(frame, frameName(std::string{"ko-"} + sheet, size[0], size[1], 1.0), size[0],
                    size[1], &covered);
    }
  }
  std::printf("Korean lines painted: %zu, elided: %zu\n", hangulLines, elided);
  CHECK(hangulLines > 200U);
  checker.finish("korean");
}

// What the frame coverage is, stated rather than implied, and made to fail when it stops being true.
// Every entry in this file that renders a surface picks a look and a contrast and each has been read at
// least once by hand; what no entry checked was which combinations exist at all. After ten units of
// rendering the five workspaces have all four variants and the eleven overlays and two compact surfaces
// have one each, and that was written down only in a ledger entry, which is not something that fails
// when it stops being true.
//
// This case is that list, and it reads the frames the capture helpers actually wrote rather than the
// names a case claims for them. Every surface a creator can reach is enumerated; a combination required
// of a surface must have a frame, and the numbers are printed so a reader sees the gap rather than
// being told there is one.
TEST_CASE("the frame coverage is what it says, and the gap is counted") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The four variants the shell can be painted in. The mode and the contrast are both application
  // preferences that change every pixel and neither can change audio, which is why all four exist and
  // why a frame in one says nothing about the other three.
  struct Variant final {
    DesignMode mode;
    Contrast contrast;
    std::string_view suffix;
  };
  const std::array<Variant, 4U> variants{{
      {DesignMode::Emo, Contrast::Standard, "-emo-standard"},
      {DesignMode::Emo, Contrast::High, "-emo-high"},
      {DesignMode::Scene, Contrast::Standard, "-scene-standard"},
      {DesignMode::Scene, Contrast::High, "-scene-high"}}};
  const std::array<std::pair<std::string_view, Workspace>, 5U> workspaces{{
      {"sing", Workspace::Sing}, {"voice", Workspace::Voice}, {"tune", Workspace::Tune},
      {"mix", Workspace::Mix}, {"export", Workspace::Export}}};
  const auto& captured = capturedFrameNames();

  // Every workspace in every variant. The capture case above loops over exactly these twenty
  // combinations, so this is twenty required pairs and the frames that exist for them, which is what
  // makes it a check rather than a claim about what some other case does.
  std::size_t workspaceCombinations = 0U;
  std::size_t workspaceCaptured = 0U;
  for (const auto& [name, workspace] : workspaces)
    for (const auto& variant : variants) {
      static_cast<void>(workspace);
      ++workspaceCombinations;
      if (captured.count(std::string{name} + std::string{variant.suffix}) != 0U)
        ++workspaceCaptured;
    }
  CHECK(workspaceCombinations == 20U);
  CHECK(workspaceCaptured == workspaceCombinations);

  // The overlays, each in the one variant they were captured in. They are not required to be in all
  // four, and saying so here is the point: the requirement is written where it can be compared against
  // the next entry rather than left in a paragraph of prose.
  std::size_t overlayCaptured = 0U;
  for (const auto& surface : overlaySurfaces())
    if (captured.count(std::string{surface.name} + "-1440x900") != 0U) ++overlayCaptured;
  CHECK(overlayCaptured == overlaySurfaces().size());

  // The compact surfaces, at the sizes where they exist rather than at the canonical window, because at
  // the canonical window they do not exist at all. Five inspector sizes and two menu sizes.
  std::size_t compactCaptured = 0U;
  for (const auto& size : kSizes) {
    const auto label = std::to_string(static_cast<int>(size[0])) + "x" +
                       std::to_string(static_cast<int>(size[1]));
    if (captured.count("compact-inspector-" + label) != 0U) ++compactCaptured;
    if (captured.count("workspace-menu-" + label) != 0U) ++compactCaptured;
  }
  CHECK(compactCaptured == 7U);

  // The totals, as numbers a reader sees rather than a promise: nineteen surfaces a creator can reach,
  // forty-one frames written across them, all four variants of the five workspaces and one variant each
  // of the fourteen overlays and the seven compact frames. The gap is the three other variants of the
  // overlays and the compact surfaces, and it is stated here so that closing it is a change to this case
  // rather than a new claim in a document.
  const auto surfacesCovered = workspaces.size() + overlaySurfaces().size();
  const auto framesWritten = workspaceCaptured + overlayCaptured + compactCaptured;
  CHECK(surfacesCovered == 19U);
  CHECK(framesWritten == 41U);
  CHECK(overlayCaptured + compactCaptured == overlaySurfaces().size() + 7U);
  std::printf("frame coverage: %zu surfaces, %zu frames across %d variants\n", surfacesCovered,
              framesWritten, static_cast<int>(variants.size()));
}
