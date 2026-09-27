// The SING shell's frame pipeline (redesign plan section 10): layers recorded once per frame, cached
// snapshots reused while their drawing is unchanged, the dynamic layer redrawn into the damaged
// rectangles of a retained surface, and the damage the presenter is told to invalidate.
//
// The claim under test is that caching is invisible: a frame composed from cached layers and partial
// rectangles has exactly the bytes of the same frame composed from nothing, in both looks and at 1x
// and 2x, and every pixel that changed between two frames lies inside the reported damage.
#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/paint/display_list.hpp"
#include "seam/native_ui/paint/layer_cache.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

namespace {

using namespace seam;
using native_ui::FrameDamage;
using native_ui::PixelSurface;
using native_ui::RasterCanvas;
using native_ui::design::Contrast;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::SingShell;
using native_ui::paint::Layer;

const std::filesystem::path& designAssetRoot() {
  static const std::filesystem::path root{"assets/ui-design"};
  return root;
}

std::chrono::steady_clock::time_point at(double seconds) {
  return std::chrono::steady_clock::time_point{} +
         std::chrono::duration_cast<std::chrono::steady_clock::duration>(
             std::chrono::duration<double>(seconds));
}

constexpr double kWidth = 1440.0;
constexpr double kHeight = 900.0;

// One session and controller, painted by two shells fed identical inputs: one keeps its layers and
// its retained surface, the other forgets its layers before every frame and paints a fresh surface.
struct Pipeline final {
  application::ProjectFactory factory{9900U};
  domain::RegionId regionId{};
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell cached;
  SingShell reference;
  double scale;
  PixelSurface retained;
  PixelSurface previous;
  std::chrono::steady_clock::time_point now{at(10.0)};

  Pipeline(DesignMode mode, double backingScale, Contrast contrast = Contrast::Standard)
      : session{makeProject()}, controller{session, factory, regionId}, scale{backingScale},
        retained{static_cast<std::uint32_t>(kWidth * backingScale),
                 static_cast<std::uint32_t>(kHeight * backingScale)} {
    controller.resize(kWidth, kHeight);
    for (auto* shell : {&cached, &reference}) {
      shell->activate(designAssetRoot(), DesignPreferences{.mode = mode, .contrast = contrast});
      shell->setUiClock([this] { return now; });
    }
    cached.setRetainedSurface(true);
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Frame pipeline");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    const auto track = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{96000});
    auto* region = project.findRegion(regionId);
    for (int i = 0; i < 48; ++i) {
      auto [lyric, note] = factory.makeNote(time::Tick{480 + i * 480}, time::Tick{400},
                                            static_cast<std::uint8_t>(60 + (i * 5) % 14), U"la",
                                            domain::Language::English);
      region->lyrics.push_back(std::move(lyric));
      region->notes.push_back(std::move(note));
    }
    region->sortNotes();
    return project;
  }

  // The scene a host derives, with the per-frame inputs a test sets.
  struct Inputs final {
    double playheadPixel{120.0};
    float level{0.0F};
    bool hover{false};
    bool box{false};
  };

  native_ui::EditorSceneState scene(const Inputs& in) {
    auto state = controller.sceneState();
    state.playheadPixel = in.playheadPixel;
    // Playing from the first frame: starting playback also swaps the play button for stop, which
    // is content, and the steps below isolate what moves while it plays.
    state.playing = true;
    if (in.level > 0.0F)
      state.outputLevel = native_ui::EditorSceneState::OutputLevel{
          .peak = {in.level, in.level * 0.8F}, .hold = {0.8F, 0.7F}, .bus = "Master"};
    const auto notes = controller.pianoRoll().visibleNotes();
    if (in.hover && notes.size() > 2U) state.hoveredNote = notes[2].noteId;
    if (in.box) state.boxSelection = ui::Rect{300.0, 260.0, 180.0, 90.0};
    return state;
  }

  struct Result final {
    bool identical{false};
    bool covered{false};
    FrameDamage damage;
    std::array<bool, 4U> layers{};
  };

  Result frame(const Inputs& in) {
    Result result;
    previous = retained;
    CHECK(cached.prepareFrame(controller, kWidth, kHeight));
    const auto state = scene(in);
    RasterCanvas canvas{retained, scale};
    CHECK(cached.paint(canvas, controller, state, time::Tick{0}));
    result.damage = cached.lastFrameDamage();
    result.layers = cached.lastFrameLayers();

    CHECK(reference.prepareFrame(controller, kWidth, kHeight));
    reference.invalidateLayers();
    PixelSurface fresh{retained.width(), retained.height()};
    RasterCanvas freshCanvas{fresh, scale};
    CHECK(reference.paint(freshCanvas, controller, scene(in), time::Tick{0}));
    result.identical = fresh.pixels().size() == retained.pixels().size() &&
                       std::equal(fresh.pixels().begin(), fresh.pixels().end(),
                                  retained.pixels().begin());
    if (!result.identical) {
      std::size_t differing = 0U;
      for (std::size_t i = 0U; i < fresh.pixels().size(); ++i)
        if (fresh.pixels()[i] != retained.pixels()[i]) ++differing;
      std::cerr << "cached frame differs from a full composition in " << differing << " pixels\n";
    }
    result.covered = damageCovers(result.damage);
    return result;
  }

  // Every pixel that changed since the previous frame lies inside the reported damage.
  bool damageCovers(const FrameDamage& damage) const {
    if (damage.full) return true;
    const auto w = retained.width();
    for (std::uint32_t y = 0U; y < retained.height(); ++y) {
      for (std::uint32_t x = 0U; x < w; ++x) {
        const auto i = static_cast<std::size_t>(y) * w + x;
        if (previous.pixels()[i] == retained.pixels()[i]) continue;
        const ui::Point p{(x + 0.5) / scale, (y + 0.5) / scale};
        bool inside = false;
        for (const auto& r : damage.rects) inside = inside || r.contains(p);
        if (!inside) {
          std::cerr << "pixel " << x << "," << y << " changed outside the damage\n";
          return false;
        }
      }
    }
    return true;
  }
};

bool onlyDynamic(const std::array<bool, 4U>& layers) {
  return !layers[0] && !layers[1] && !layers[2];
}

double damagedArea(const FrameDamage& damage) {
  double area = 0.0;
  for (const auto& r : damage.rects) area += r.width * r.height;
  return area;
}

void runPipeline(DesignMode mode, double scale, Contrast contrast = Contrast::Standard) {
  Pipeline p{mode, scale, contrast};
  Pipeline::Inputs in;
  // A first frame is composed from nothing and damages everything.
  auto r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.damage.full);
  CHECK(r.layers[0] && r.layers[1] && r.layers[2] && r.layers[3]);
  // Let the Stage figure's fade settle: its opacity belongs to the grid layer (plan: "stage state").
  p.now = at(20.0);
  r = p.frame(in);
  CHECK(r.identical);

  // Playback: the playhead and the output meter move. Only the dynamic layer is drawn, and only
  // into small rectangles.
  in.level = 0.4F;
  in.playheadPixel = 180.0;
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(onlyDynamic(r.layers));
  CHECK(!r.damage.full);
  CHECK(!r.damage.rects.empty());
  CHECK(damagedArea(r.damage) < kWidth * kHeight * 0.25);
  for (int step = 0; step < 3; ++step) {
    in.playheadPixel += 7.5;
    in.level = 0.25F + 0.2F * static_cast<float>(step);
    r = p.frame(in);
    CHECK(r.identical);
    CHECK(r.covered);
    CHECK(onlyDynamic(r.layers));
    CHECK(!r.damage.full);
  }

  // An unchanged frame damages nothing and draws nothing.
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.damage.empty());
  CHECK(onlyDynamic(r.layers));

  // Hover brightens one note without repainting the content layer.
  in.hover = true;
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(onlyDynamic(r.layers));
  CHECK(!r.damage.full);

  // The idle breath moves the singer's ring and avatar.
  p.now = at(20.6);
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(onlyDynamic(r.layers));

  // A box selection gesture is a dynamic item too.
  in.box = true;
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(onlyDynamic(r.layers));
  in.box = false;

  // A selection changes the content layer; the grid stays cached. Only the notes whose look changed
  // (and what they overlap) are redrawn, so the damage is theirs, not the surface.
  p.controller.pianoRoll().selectInBox({0.0, 0.0, 400.0, 2000.0});
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(!r.damage.full);
  CHECK(damagedArea(r.damage) < kWidth * kHeight * 0.5);
  CHECK(!r.layers[0]);
  CHECK(!r.layers[1]);
  CHECK(r.layers[2]);
  // Selecting other notes damages the notes that changed on both sides.
  p.controller.pianoRoll().selectInBox({500.0, 0.0, 200.0, 2000.0});
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(!r.layers[1]);
  CHECK(r.layers[2]);

  // Scrolling and zooming repaint the grid and the content over the cached background.
  auto& timeline = p.controller.pianoRoll().timeline();
  timeline.setOriginTick(time::Tick{960});
  p.controller.pianoRoll().rebuildIndex();
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(!r.layers[0]);
  CHECK(r.layers[1]);
  CHECK(r.layers[2]);
  timeline.setPixelsPerQuarter(timeline.pixelsPerQuarter() * 1.25);
  p.controller.pianoRoll().rebuildIndex();
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(r.layers[1]);

  // Playback over the scrolled view is dynamic-only again.
  in.playheadPixel += 30.0;
  r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.covered);
  CHECK(onlyDynamic(r.layers));
}

}  // namespace

TEST_CASE("cached and partial SING frames equal a full composition in both looks at 1x") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  runPipeline(DesignMode::Emo, 1.0);
  runPipeline(DesignMode::Scene, 1.0);
}

TEST_CASE("cached and partial SING frames equal a full composition in both looks at 2x") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  runPipeline(DesignMode::Emo, 2.0);
  runPipeline(DesignMode::Scene, 2.0);
}

TEST_CASE("cached and partial High Contrast frames equal a full composition in both looks") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  // High Contrast records without glow; the layers and the damage behave exactly as they do in the
  // standard contrast.
  runPipeline(DesignMode::Emo, 1.0, Contrast::High);
  runPipeline(DesignMode::Scene, 1.0, Contrast::High);
}

TEST_CASE("a mode switch or a resize recomposes every layer and damages everything") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  Pipeline p{DesignMode::Emo, 1.0};
  Pipeline::Inputs in;
  static_cast<void>(p.frame(in));
  static_cast<void>(p.frame(in));
  p.cached.setMode(DesignMode::Scene, false);
  p.reference.setMode(DesignMode::Scene, false);
  auto r = p.frame(in);
  CHECK(r.identical);
  CHECK(r.damage.full);
  CHECK(r.layers[0]);
  // A surface the shell did not compose last is written whole, even with a retained presenter.
  PixelSurface other{p.retained.width(), p.retained.height()};
  RasterCanvas canvas{other, 1.0};
  CHECK(p.cached.prepareFrame(p.controller, kWidth, kHeight));
  CHECK(p.cached.paint(canvas, p.controller, p.scene(in), time::Tick{0}));
  CHECK(std::equal(other.pixels().begin(), other.pixels().end(), p.retained.pixels().begin()));
}

TEST_CASE("the layer cache stays within the plan's 80 MB at 1440x900 on a 2x display") {
  if (!native_ui::paint::vectorBackendAvailable()) return;
  if (!std::filesystem::is_directory(designAssetRoot())) return;
  Pipeline p{DesignMode::Emo, 2.0};
  static_cast<void>(p.frame({}));
  CHECK(p.cached.layerCacheBytes() > 0U);
  CHECK(p.cached.layerCacheBytes() <= 80U * 1024U * 1024U);
}

TEST_CASE("a recorded layer hashes only its own drawing and replays like direct drawing") {
  using native_ui::paint::LayerScope;
  using native_ui::paint::Path;
  using native_ui::paint::RecordingCanvas;
  using native_ui::paint::StrokeStyle;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto measure = [](std::string_view, const native_ui::paint::TextStyle&) { return 10.0; };
  const auto draw = [](native_ui::paint::Canvas2D& c, double contentX) {
    c.save();
    c.clipRect({0.0, 0.0, 200.0, 150.0});
    {
      const LayerScope grid{c, Layer::Grid};
      c.stroke(Path::rect({10.0, 10.0, 180.0, 130.0}), native_ui::Color{80, 80, 90, 255},
               StrokeStyle{1.0});
    }
    c.setGlow(native_ui::Color{255, 0, 80, 200}, 6.0);
    c.fill(Path::roundedRect({contentX, 40.0, 60.0, 20.0}, 6.0), native_ui::Color{200, 60, 90, 255});
    c.clearGlow();
    {
      const LayerScope head{c, Layer::Dynamic, "playhead"};
      Path line;
      line.moveTo({120.0, 0.0}).lineTo({120.0, 150.0});
      c.stroke(line, native_ui::Color{255, 200, 0, 255}, StrokeStyle{1.4});
    }
    c.restore();
  };
  RecordingCanvas a{200.0, 150.0, 1.0, measure};
  RecordingCanvas b{200.0, 150.0, 1.0, measure};
  draw(a, 30.0);
  draw(b, 34.0);
  // Moving the content changes the content layer's hash and nothing else's.
  CHECK(a.layerHash(Layer::Grid) == b.layerHash(Layer::Grid));
  CHECK(a.layerHash(Layer::Dynamic) == b.layerHash(Layer::Dynamic));
  CHECK(a.layerHash(Layer::Content) != b.layerHash(Layer::Content));
  const auto items = a.items(Layer::Dynamic);
  CHECK(items.size() == 1U);
  if (!items.empty()) {
    CHECK(items.front().name == "playhead");
    CHECK(items.front().bounds.x <= 119.3);
    CHECK(items.front().bounds.right() >= 120.7);
    CHECK(items.front().bounds.width < 8.0);
  }
  // Replaying the layers in order equals drawing the same calls in that order directly.
  PixelSurface replayed{200U, 150U};
  PixelSurface direct{200U, 150U};
  replayed.clear({12, 10, 14, 255});
  direct.clear({12, 10, 14, 255});
  {
    auto canvas = native_ui::paint::makeCanvas(replayed, 1.0);
    RasterCanvas raster{replayed, 1.0};
    for (const auto layer : {Layer::Grid, Layer::Content, Layer::Dynamic})
      a.replay(layer, *canvas, raster);
    canvas->flush();
  }
  {
    auto canvas = native_ui::paint::makeCanvas(direct, 1.0);
    draw(*canvas, 30.0);  // a direct canvas ignores the layer scopes and draws in call order,
    canvas->flush();      // which here is already layer order
  }
  CHECK(replayed.checksum() == direct.checksum());
}

TEST_CASE("a large glow blurred at half resolution keeps the full-resolution shadow's energy") {
  using native_ui::paint::Path;
  using native_ui::paint::StrokeStyle;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  constexpr double kScale = 2.0;
  constexpr std::uint32_t kW = 700U;
  constexpr std::uint32_t kH = 300U;
  const native_ui::Color background{11, 10, 12, 255};
  // A pitch-curve-like stroke long enough to take the half-resolution path.
  Path curve;
  curve.moveTo({20.0, 80.0});
  for (int i = 1; i <= 64; ++i)
    curve.lineTo({20.0 + i * 5.0, 80.0 + 40.0 * std::sin(i * 0.3)});
  const auto draw = [&](PixelSurface& surface) {
    surface.clear(background);
    auto c = native_ui::paint::makeCanvas(surface, kScale);
    CHECK(c != nullptr);
    if (c == nullptr) return;
    c->save();
    c->setGlow(native_ui::Color{209, 20, 58, 230}, 7.0);
    c->stroke(curve, native_ui::Color{242, 238, 234, 255}, StrokeStyle{2.0});
    c->restore();
    c->flush();
  };
  PixelSurface half{kW, kH};
  PixelSurface full{kW, kH};
  draw(half);
  {
    const native_ui::paint::ScopedFullResolutionGlow direct;
    draw(full);
  }
  // The glow's energy: red above the background, summed.
  const auto energy = [&](const PixelSurface& s) {
    double total = 0.0;
    for (const auto p : s.pixels()) total += std::max(0.0, ((p >> 16U) & 0xFFU) - 11.0);
    return total;
  };
  const auto halfEnergy = energy(half);
  const auto fullEnergy = energy(full);
  std::cout << "[glow-energy] half=" << halfEnergy << " full=" << fullEnergy << '\n';
  CHECK(fullEnergy > 0.0);
  CHECK(std::abs(halfEnergy - fullEnergy) <= 0.10 * fullEnergy);
  double difference = 0.0;
  for (std::size_t i = 0U; i < half.pixels().size(); ++i)
    difference += std::abs(static_cast<double>((half.pixels()[i] >> 16U) & 0xFFU) -
                           static_cast<double>((full.pixels()[i] >> 16U) & 0xFFU));
  const auto meanDifference = difference / static_cast<double>(half.pixels().size());
  std::cout << "[glow-mean-red-difference] " << meanDifference << '\n';
  CHECK(meanDifference < 2.0);
}
