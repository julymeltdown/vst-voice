#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/rendering/playback_feeder_service.hpp"
#include "seam/text/text_engine.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <algorithm>
#include <array>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if !defined(_WIN32)
#include <cstdlib>
#endif

namespace {
using namespace std::chrono_literals;

seam::domain::Project makeProject(seam::application::ProjectFactory& factory,
                                  seam::domain::RegionId& regionId) {
  auto project = factory.createProject("Phase 5 benchmark");
  const auto track = factory.addVocalTrack(project, "Voice");
  regionId = factory.addRegion(project, track, "Dense",
                               seam::time::Tick{0}, seam::time::Tick{1200000});
  auto* region = project.findRegion(regionId);
  region->lyrics.reserve(10000U);
  region->notes.reserve(10000U);
  for (std::size_t index = 0U; index < 10000U; ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(index) * 120},
        seam::time::Tick{96},
        static_cast<std::uint8_t>(48U + index % 24U), U"a",
        seam::domain::Language::English);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  return project;
}

std::shared_ptr<const seam::rendering::PlaybackTimeline> makeTimeline() {
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = 48000U,
          .startFrame = 0,
          .samples = std::vector<float>(48000U * 4U, 0.1F),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(48000U);
  const auto added = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "benchmark",
      .pcm = std::move(pcm),
  });
  return added ? timeline : nullptr;
}
// The editor surface, as a host draws it: the SING shell, or the notice where it cannot present.
void paintEditor(seam::native_ui::design::SingShell& shell,
                 seam::native_ui::NativeEditorController& controller,
                 seam::native_ui::RasterCanvas& canvas) {
  if (!shell.prepareFrame(controller, canvas.logicalWidth(), canvas.logicalHeight()) ||
      !shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick()))
    seam::native_ui::paintEditorUnavailable(canvas);
}


// ---- Design shell frame pipeline (redesign plan section 10) ------------------------------------
//
// Every case paints the SING shell over the same 10,000-note project at 1440x900 logical points on a
// 2x surface (2880x1800 pixels), with the look's glows on (Standard contrast). A sample is the
// shell's own frame work, prepareFrame plus paint; deriving the scene state is host work and is
// reported separately. Each case fails above its p95 budget.

struct Quantiles final {
  double p50{0.0};
  double p95{0.0};
  double max{0.0};
};

Quantiles quantiles(std::vector<double> samples) {
  if (samples.empty()) return {};
  std::sort(samples.begin(), samples.end());
  const auto at = [&samples](double p) {
    const auto index = static_cast<std::size_t>(
        std::ceil(static_cast<double>(samples.size()) * p)) - 1U;
    return samples[std::min(samples.size() - 1U, index)];
  };
  return {at(0.50), at(0.95), samples.back()};
}

std::string machineName() {
#if defined(__APPLE__)
  const auto read = [](const char* key) {
    std::size_t size = 0U;
    if (sysctlbyname(key, nullptr, &size, nullptr, 0U) != 0 || size == 0U) return std::string{};
    std::string value(size, '\0');
    if (sysctlbyname(key, value.data(), &size, nullptr, 0U) != 0) return std::string{};
    while (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
  };
  return read("machdep.cpu.brand_string") + " / " + read("hw.model");
#else
  return "unknown";
#endif
}

double loadAverage() {
#if !defined(_WIN32)
  double load[1]{0.0};
  return getloadavg(load, 1) == 1 ? load[0] : -1.0;
#else
  return -1.0;
#endif
}

struct DesignCase final {
  std::string name;
  std::string mode;
  double budgetMs{0.0};
  std::string invalidation;
  std::vector<double> samples;
  std::vector<double> stateSamples;
  std::string layers;  // which layers the last sampled frame rasterized, when known
  std::size_t visibleNotes{0U};

  [[nodiscard]] bool pass() const { return !samples.empty() && quantiles(samples).p95 <= budgetMs; }
};

struct DesignRig final {
  seam::application::ProjectFactory factory{9100U};
  seam::domain::RegionId regionId;
  seam::application::EditorSession session;
  seam::native_ui::NativeEditorController controller;
  seam::native_ui::design::SingShell shell;
  seam::native_ui::PixelSurface surface{2880U, 1800U};
  seam::native_ui::RasterCanvas canvas{surface, 2.0, nullptr};
  seam::time::Tick playhead{0};
  std::uint64_t frameIndex{0U};

  explicit DesignRig(seam::native_ui::design::DesignMode mode)
      : session(makeProject(factory, regionId)), controller(session, factory, regionId) {
    controller.resize(1440.0, 900.0);
    shell.activate(seam::native_ui::design::locateDesignAssets(),
                   seam::native_ui::design::DesignPreferences{.mode = mode});
    // As the AppKit window and the CLAP view do: the surface keeps the previous frame.
    shell.setRetainedSurface(true);
  }

  // One frame: derive host state separately, then time the shell's only preparation and paint.
  double frame(DesignCase& record, bool playing) {
    const auto stateStart = std::chrono::steady_clock::now();
    controller.setPlaying(playing);
    controller.setPlayheadTick(playhead);
    auto state = controller.sceneState();
    const auto stateEnd = std::chrono::steady_clock::now();
    if (playing) {
      // A measured stereo level that moves every frame, as the audio thread reports it.
      const auto phase = static_cast<double>(frameIndex) * 0.37;
      const auto left = static_cast<float>(0.35 + 0.3 * std::sin(phase));
      const auto right = static_cast<float>(0.30 + 0.3 * std::cos(phase));
      state.outputLevel = seam::native_ui::EditorSceneState::OutputLevel{
          .peak = {left, right}, .hold = {0.7F, 0.66F}, .bus = "Master", .clipped = false};
    }
    ++frameIndex;
    const auto paintStart = std::chrono::steady_clock::now();
    if (!shell.prepareFrame(controller, 1440.0, 900.0)) return -1.0;
    state.playheadPixel = controller.pianoRoll().timeline().tickToPixel(playhead);
    const auto painted = shell.paint(canvas, controller, state, playhead);
    const auto paintEnd = std::chrono::steady_clock::now();
    if (!painted) return -1.0;
    record.stateSamples.push_back(
        std::chrono::duration<double, std::milli>(stateEnd - stateStart).count());
    return std::chrono::duration<double, std::milli>(paintEnd - paintStart).count();
  }
};

std::string layerSummary(const seam::native_ui::design::SingShell& shell) {
  const auto& layers = shell.lastFrameLayers();
  std::string text;
  static constexpr std::array<const char*, 4U> kNames{"L0", "L1", "L2", "L3"};
  for (std::size_t i = 0U; i < layers.size(); ++i)
    if (layers[i]) text += std::string{text.empty() ? "" : "+"} + kNames[i];
  return text.empty() ? "none" : text;
}

struct DesignReport final {
  bool available{false};
  bool pass{true};
  std::string json;
};

DesignReport runDesignShellBenchmark() {
  using seam::native_ui::design::DesignMode;
  DesignReport report;
  if (!seam::native_ui::paint::vectorBackendAvailable()) {
    report.json = "{\"available\": false, \"reason\": \"no vector backend on this platform\"}";
    return report;
  }
  report.available = true;
  // SEAM_BENCHMARK_CASE limits the run to one case name (profiling); SEAM_BENCHMARK_SAMPLES
  // overrides the sample count. Neither is set for evidence runs.
  const char* onlyCase = std::getenv("SEAM_BENCHMARK_CASE");
  const char* samplesOverride = std::getenv("SEAM_BENCHMARK_SAMPLES");
  constexpr std::size_t kWarmup = 5U;
  const std::size_t kSamples =
      samplesOverride != nullptr ? static_cast<std::size_t>(std::max(1, std::atoi(samplesOverride)))
                                 : 120U;
  constexpr double kCacheBudgetBytes = 80.0 * 1024.0 * 1024.0;
  const auto loadBefore = loadAverage();
  std::vector<DesignCase> cases;
  std::size_t cacheBytes = 0U;

  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    const std::string modeName = mode == DesignMode::Emo ? "emo" : "scene";
    DesignRig rig{mode};
    auto& model = rig.controller.pianoRoll();
    const auto baseOrigin = seam::time::Tick{960 * 8};
    const auto run = [&](DesignCase record, bool playing,
                         const std::function<void(std::size_t)>& step) {
      if (onlyCase != nullptr && record.name != onlyCase) return;
      for (std::size_t i = 0U; i < kWarmup + kSamples; ++i) {
        step(i);
        const auto ms = rig.frame(record, playing);
        if (ms < 0.0) {
          record.samples.clear();
          break;
        }
        if (i >= kWarmup) record.samples.push_back(ms);
      }
      record.layers = layerSummary(rig.shell);
      record.visibleNotes = model.visibleNotes().size();
      cacheBytes = std::max(cacheBytes, rig.shell.layerCacheBytes());
      cases.push_back(std::move(record));
    };
    const auto resetView = [&](double pixelsPerQuarter) {
      model.timeline().setPixelsPerQuarter(pixelsPerQuarter);
      model.timeline().setOriginTick(baseOrigin);
      model.rebuildIndex();
    };

    // Cold: every layer including L0's background painter, as on the first paint.
    resetView(100.0);
    run(DesignCase{.name = "cold-full-frame", .mode = modeName, .budgetMs = 14.0,
                   .invalidation = "L0 background painter plus all upper layers"}, false,
        [&](std::size_t) { rig.shell.invalidateBackgroundLayers(); });
    // A full upper-layer composition can reuse L0's retained background pixels.
    run(DesignCase{.name = "retained-background-invalidation", .mode = modeName,
                   .budgetMs = 14.0,
                   .invalidation = "L0 snapshot retained; upper layers recomposed"}, false,
        [&](std::size_t) { rig.shell.invalidateLayers(); });
    // Scroll and zoom: the grid and the notes move, the background stays.
    run(DesignCase{.name = "scroll-zoom", .mode = modeName, .budgetMs = 8.0}, false,
        [&](std::size_t i) {
          model.timeline().setPixelsPerQuarter(i % 8U == 7U ? 110.0 : 100.0);
          model.timeline().setOriginTick(baseOrigin + seam::time::Tick{
              static_cast<std::int64_t>(i % 64U) * 48});
          model.rebuildIndex();
        });
    // Playback: the playhead, the output meter and the singer's ring move; nothing else does.
    resetView(100.0);
    run(DesignCase{.name = "playback", .mode = modeName, .budgetMs = 3.0}, true,
        [&](std::size_t i) {
          rig.playhead = baseOrigin + seam::time::Tick{static_cast<std::int64_t>(i) * 40};
        });
    // The 10,000-note case: a zoomed-out grid full of notes whose content changes every frame
    // (the selection moves), so the notes, their glows and the lyrics are repainted each time.
    resetView(25.0);
    rig.playhead = seam::time::Tick{0};
    run(DesignCase{.name = "dense-10000-notes", .mode = modeName, .budgetMs = 8.0}, false,
        [&](std::size_t i) {
          const auto x = static_cast<double>(40 + (i % 16U) * 60);
          model.selectInBox({x, 0.0, 50.0, 2000.0});
        });
  }
  const auto loadAfter = loadAverage();

  std::ostringstream out;
  out << "{\n    \"available\": true,\n"
      << "    \"machine\": \"" << machineName() << "\",\n"
      << "    \"loadAverageBefore\": " << loadBefore << ",\n"
      << "    \"loadAverageAfter\": " << loadAfter << ",\n"
      << "    \"logicalSize\": [1440, 900],\n    \"scale\": 2,\n"
      << "    \"projectNotes\": 10000,\n    \"samplesPerCase\": " << kSamples << ",\n"
      << "    \"timingScope\": \"SingShell::prepareFrame + SingShell::paint wall time\",\n"
      << "    \"cases\": [\n";
  for (std::size_t i = 0U; i < cases.size(); ++i) {
    const auto& c = cases[i];
    const auto q = quantiles(c.samples);
    const auto state = quantiles(c.stateSamples);
    report.pass = report.pass && c.pass();
    out << "      {\"case\": \"" << c.name << "\", \"mode\": \"" << c.mode << "\", \"p50Ms\": "
        << q.p50 << ", \"p95Ms\": " << q.p95 << ", \"maxMs\": " << q.max
        << ", \"budgetP95Ms\": " << c.budgetMs << ", \"invalidation\": \"" << c.invalidation
        << "\", \"pass\": " << (c.pass() ? "true" : "false")
        << ", \"lastFrameLayers\": \"" << c.layers << "\", \"visibleNotes\": " << c.visibleNotes
        << ", \"hostStateP95Ms\": " << state.p95 << "}" << (i + 1U < cases.size() ? "," : "")
        << "\n";
  }
  const auto cachePass = static_cast<double>(cacheBytes) <= kCacheBudgetBytes;
  report.pass = report.pass && cachePass;
  out << "    ],\n    \"layerCacheBytes\": " << cacheBytes
      << ",\n    \"layerCacheBudgetBytes\": " << static_cast<std::uint64_t>(kCacheBudgetBytes)
      << ",\n    \"layerCachePass\": " << (cachePass ? "true" : "false")
      << ",\n    \"pass\": " << (report.pass ? "true" : "false") << "\n  }";
  report.json = out.str();
  return report;
}
}  // namespace

int main() {
  if (std::getenv("SEAM_BENCHMARK_DESIGN_ONLY") != nullptr) {
    const auto design = runDesignShellBenchmark();
    std::cout << "{\n  \"designShell\": " << design.json << "\n}\n";
    return design.pass ? 0 : 1;
  }
  seam::application::ProjectFactory factory{9000U};
  seam::domain::RegionId regionId;
  seam::application::EditorSession session{makeProject(factory, regionId)};
  seam::native_ui::NativeEditorController controller{session, factory, regionId};
  controller.resize(1280.0, 720.0);
  seam::native_ui::design::SingShell shell;
  shell.activate(seam::native_ui::design::locateDesignAssets(),
                 seam::native_ui::design::DesignPreferences{});
  seam::native_ui::PixelSurface surface{1280U, 720U};
  auto textEngineResult = seam::text::TextEngine::createSystem();
  auto* textEngine = textEngineResult ? textEngineResult.value().get() : nullptr;
  seam::native_ui::RasterCanvas canvas{surface, 1.0, textEngine};

  constexpr std::size_t paintIterations = 60U;
  std::vector<double> paintSamples;
  paintSamples.reserve(paintIterations);
  for (std::size_t iteration = 0U; iteration < paintIterations; ++iteration) {
    const auto paintStart = std::chrono::steady_clock::now();
    paintEditor(shell, controller, canvas);
    paintSamples.push_back(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - paintStart).count());
  }

  seam::rendering::SpscAudioRingBuffer ring{32768U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 512U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 16384U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  const auto timeline = makeTimeline();
  if (timeline == nullptr || !service.setTimeline(timeline) ||
      !service.setLoop(seam::rendering::PlaybackLoop{
          .enabled = true, .startFrame = 0, .endFrame = timeline->endFrame()}) ||
      !service.start() || !service.setPlaying(true)) {
    return 1;
  }
  std::this_thread::sleep_for(20ms);
  if (!device->open(seam::platform::AudioDeviceConfig{
                        .sampleRate = 48000U,
                        .blockFrames = 128U,
                        .outputChannels = 2U,
                        .applicationName = "SEAM benchmark",
                        .streamName = "benchmark",
                    },
                    processor) ||
      !device->start()) {
    service.stop();
    return 1;
  }
  std::this_thread::sleep_for(250ms);
  device->stop();
  service.stop();

  const auto totalPaintMs = std::accumulate(paintSamples.begin(), paintSamples.end(), 0.0);
  std::sort(paintSamples.begin(), paintSamples.end());
  const auto p95Index = std::min(
      paintSamples.size() - 1U,
      static_cast<std::size_t>(std::ceil(
          static_cast<double>(paintSamples.size()) * 0.95)) - 1U);
  const auto p50Index = std::min(
      paintSamples.size() - 1U,
      static_cast<std::size_t>(std::ceil(
          static_cast<double>(paintSamples.size()) * 0.50)) - 1U);
  const auto averagePaintMs = totalPaintMs / static_cast<double>(paintIterations);
  const auto textCache = textEngine != nullptr
                             ? textEngine->cacheStats()
                             : seam::text::TextCacheStats{};
  constexpr double paintBudgetMs = 16.7;
  const auto paintBudgetPass = paintSamples[p95Index] < paintBudgetMs;
  const auto audio = device->stats();
  const auto feederStats = service.stats();
  const auto callback = processor.stats();
  const auto design = runDesignShellBenchmark();
  std::cout << "{\n"
            << "  \"phase\": \"5.0\",\n"
            << "  \"projectNotes\": 10000,\n"
            << "  \"visibleNotes\": "
            << controller.pianoRoll().visibleNotes().size() << ",\n"
            << "  \"paintIterations\": " << paintIterations << ",\n"
            << "  \"averagePaintMs\": " << averagePaintMs << ",\n"
            << "  \"p50PaintMs\": " << paintSamples[p50Index] << ",\n"
            << "  \"p95PaintMs\": " << paintSamples[p95Index] << ",\n"
            << "  \"paintBudgetMs\": " << paintBudgetMs << ",\n"
            << "  \"paintBudgetPass\": "
            << (paintBudgetPass ? "true" : "false") << ",\n"
            << "  \"textCacheEntries\": " << textCache.entries << ",\n"
            << "  \"textCacheHits\": " << textCache.hits << ",\n"
            << "  \"textCacheMisses\": " << textCache.misses << ",\n"
            << "  \"surfaceChecksum\": " << surface.checksum() << ",\n"
            << "  \"audioCallbacks\": " << audio.callbacks << ",\n"
            << "  \"feederFrames\": " << feederStats.framesFed << ",\n"
            << "  \"deliveredFrames\": " << callback.deliveredFrames << ",\n"
            << "  \"underflowFrames\": " << callback.underflowFrames << ",\n"
            << "  \"designShell\": " << design.json << "\n"
            << "}\n";
  return callback.deliveredFrames > 0U && paintBudgetPass && design.pass ? 0 : 1;
}
