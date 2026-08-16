#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/render_scheduler.hpp"
#include "seam/synthesis/classic_psola.hpp"
#include "seam/synthesis/seam_composer.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 48000;

std::vector<float> sourceAudio() {
  std::vector<float> samples(36000, 0.0F);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto phase = 2.0 * std::numbers::pi * 329.627556 *
                       static_cast<double>(index) /
                       static_cast<double>(kSampleRate);
    const auto envelope = std::min(
        std::min(1.0, static_cast<double>(index) / 256.0),
        std::min(1.0, static_cast<double>(samples.size() - index - 1U) / 512.0));
    samples[index] = static_cast<float>(
        envelope * (0.30 * std::sin(phase) + 0.08 * std::sin(phase * 2.0) +
                    0.03 * std::sin(phase * 3.0)));
  }
  return samples;
}

seam::voicebank::Unit psolaUnit() {
  seam::voicebank::Unit unit{
      .id = "benchmark.psola.e4",
      .alias = "i",
      .phones = {"i"},
      .kind = seam::voicebank::UnitKind::Sustain,
      .audioPath = "benchmark.wav",
      .rootMidi = 64,
      .style = "original",
      .take = 1,
      .priority = 0,
      .gainDb = -1.0F,
      .renderer = seam::voicebank::RendererHint::ClassicPsola,
      .markers = seam::voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = 2400,
          .vowelOnset = 3600,
          .stableStart = 5200,
          .loopStart = 6800,
          .loopEnd = 29200,
          .releaseStart = 31000,
          .audioEnd = 36000,
      },
      .pitchMarks = {},
      .enabled = true,
  };
  constexpr seam::time::SampleFrame period = 146;
  for (auto frame = seam::time::SampleFrame{5200}; frame < 31000;
       frame += period) {
    unit.pitchMarks.push_back(seam::voicebank::PitchMark{
        .frame = frame,
        .confidence = 0.98F,
        .locked = false,
    });
  }
  return unit;
}

std::filesystem::path cacheDirectory() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("project-seam-phase3-benchmark-" + std::to_string(stamp));
}

}  // namespace

int main() {
  const auto samples = sourceAudio();
  const seam::voicebank::AudioBuffer source{
      .sampleRate = kSampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  const auto unit = psolaUnit();
  const auto unitValidation = unit.validate();
  if (!unitValidation) {
    std::cerr << unitValidation.error().message << '\n';
    return 1;
  }

  seam::synthesis::ClassicPsolaRenderer psola;
  constexpr std::size_t psolaIterations = 120;
  constexpr seam::time::SampleFrame outputFrames = 36000;
  std::size_t psolaSamples = 0;
  seam::synthesis::RenderedUnit last;
  const auto psolaStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0; iteration < psolaIterations; ++iteration) {
    auto rendered = psola.render(
        unit, source, kSampleRate, outputFrames,
        static_cast<std::int32_t>(64 + iteration % 5U),
        seam::synthesis::PsolaRenderParameters{
            .sourcePitchResidual = 0.35F,
            .additionalGainDb = -2.0F,
            .pitchCurve = seam::synthesis::PitchCurve{
                std::vector<seam::synthesis::PitchPoint>{
                    {.frame = 0, .cents = -18.0F},
                    {.frame = outputFrames / 2, .cents = 12.0F,
                     .interpolation = seam::domain::CurveInterpolation::Smooth},
                    {.frame = outputFrames - 1, .cents = 0.0F},
                }},
        });
    if (!rendered) {
      std::cerr << rendered.error().message << '\n';
      return 2;
    }
    last = std::move(rendered).value();
    psolaSamples += last.samples.size();
  }
  const auto psolaEnd = std::chrono::steady_clock::now();

  std::vector<seam::synthesis::PlacedRenderedUnit> placements;
  placements.reserve(4);
  for (int index = 0; index < 4; ++index) {
    placements.push_back(seam::synthesis::PlacedRenderedUnit{
        .destinationStart = static_cast<seam::time::SampleFrame>(index * 27000),
        .unit = last,
        .incomingBoundary = index == 0
            ? std::optional<seam::synthesis::BoundarySeamSettings>{}
            : std::optional<seam::synthesis::BoundarySeamSettings>{
                  seam::synthesis::BoundarySeamSettings{
                      .seamAmount = 0.72F,
                      .curve = seam::domain::SeamCurve::HardCharacter,
                      .maxOverlapFrames = 9000,
                      .phaseReset = 0.65F,
                      .envelopeBlend = 0.25F,
                  }},
    });
  }
  seam::synthesis::SeamComposer composer;
  constexpr std::size_t seamIterations = 120;
  std::size_t seamSamples = 0;
  const auto seamStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0; iteration < seamIterations; ++iteration) {
    placements[1].incomingBoundary->phaseReset =
        static_cast<float>(iteration % 100U) / 99.0F;
    auto composed = composer.compose(
        placements, seam::synthesis::SeamSettings{
                        .seamAmount = 0.7F,
                        .curve = seam::domain::SeamCurve::HardCharacter,
                        .sampleRate = kSampleRate,
                    });
    if (!composed) {
      std::cerr << composed.error().message << '\n';
      return 3;
    }
    seamSamples += composed.value().samples.size();
  }
  const auto seamEnd = std::chrono::steady_clock::now();

  const auto cacheRoot = cacheDirectory();
  seam::rendering::PcmCache cache{cacheRoot};
  const seam::rendering::CachedPcm cachedPcm{
      .sampleRate = kSampleRate,
      .startFrame = -3600,
      .samples = last.samples,
  };
  if (!cache.store("phase3benchmark", cachedPcm)) {
    std::cerr << "Unable to initialize Phase 3 cache benchmark\n";
    return 4;
  }
  constexpr std::size_t cacheIterations = 80;
  const auto cacheStart = std::chrono::steady_clock::now();
  std::size_t cacheSamples = 0;
  for (std::size_t iteration = 0; iteration < cacheIterations; ++iteration) {
    cache.clearMemory();
    auto loaded = cache.load("phase3benchmark");
    if (!loaded) {
      std::cerr << loaded.error().message << '\n';
      return 5;
    }
    cacheSamples += loaded.value()->samples.size();
  }
  const auto cacheEnd = std::chrono::steady_clock::now();

  constexpr std::size_t schedulerTasks = 48;
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 4};
  const auto schedulerStart = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < schedulerTasks; ++index) {
    const auto key = "scheduler" + std::to_string(index);
    const auto submitted = scheduler.submit(seam::rendering::ScheduledRenderRequest{
        .phraseId = "phrase" + std::to_string(index),
        .cacheKey = key,
        .revision = static_cast<std::uint64_t>(index + 1U),
        .sampleRate = kSampleRate,
        .priority = index < 4U ? seam::rendering::RenderPriority::Playhead
                              : seam::rendering::RenderPriority::Background,
        .task = [index](std::stop_token token)
            -> seam::core::Result<seam::synthesis::PhraseAudio> {
          if (token.stop_requested()) {
            return seam::core::failure<seam::synthesis::PhraseAudio>(
                seam::core::ErrorCode::Conflict, "benchmark render cancelled");
          }
          return seam::synthesis::PhraseAudio{
              .startFrame = static_cast<seam::time::SampleFrame>(index * 128U),
              .samples = std::vector<float>(2400, static_cast<float>(index) / 1000.0F),
          };
        },
    });
    if (!submitted) {
      std::cerr << submitted.error().message << '\n';
      return 6;
    }
  }
  if (!scheduler.waitIdle(std::chrono::seconds{10})) {
    std::cerr << "Render scheduler benchmark timed out\n";
    return 7;
  }
  const auto schedulerEnd = std::chrono::steady_clock::now();
  const auto completions = scheduler.drainCompleted();
  if (completions.size() != schedulerTasks) {
    std::cerr << "Render scheduler benchmark completion count is invalid\n";
    return 8;
  }

  const auto psolaMs = std::chrono::duration<double, std::milli>(
      psolaEnd - psolaStart).count();
  const auto seamMs = std::chrono::duration<double, std::milli>(
      seamEnd - seamStart).count();
  const auto cacheMs = std::chrono::duration<double, std::milli>(
      cacheEnd - cacheStart).count();
  const auto schedulerMs = std::chrono::duration<double, std::milli>(
      schedulerEnd - schedulerStart).count();
  const auto psolaAudioSeconds = static_cast<double>(psolaSamples) /
                                 static_cast<double>(kSampleRate);
  const auto psolaRealtime = psolaAudioSeconds / (psolaMs / 1000.0);
  const auto schedulerStats = scheduler.stats();
  const auto cacheStats = cache.stats();

  std::error_code removeError;
  std::filesystem::remove_all(cacheRoot, removeError);

  std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"psolaIterations\": " << psolaIterations << ",\n"
            << "  \"psolaMs\": " << psolaMs << ",\n"
            << "  \"psolaRenderedSamples\": " << psolaSamples << ",\n"
            << "  \"psolaRealTimeMultiple\": " << psolaRealtime << ",\n"
            << "  \"seamIterations\": " << seamIterations << ",\n"
            << "  \"seamMs\": " << seamMs << ",\n"
            << "  \"seamComposedSamples\": " << seamSamples << ",\n"
            << "  \"cacheDiskReadIterations\": " << cacheIterations << ",\n"
            << "  \"cacheDiskReadMs\": " << cacheMs << ",\n"
            << "  \"cacheReadSamples\": " << cacheSamples << ",\n"
            << "  \"cacheDiskHits\": " << cacheStats.diskHits << ",\n"
            << "  \"schedulerTasks\": " << schedulerTasks << ",\n"
            << "  \"schedulerMs\": " << schedulerMs << ",\n"
            << "  \"schedulerCompleted\": " << schedulerStats.completed << "\n"
            << "}\n";
  return 0;
}
