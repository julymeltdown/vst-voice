#include "seam/synthesis/raw_renderer.hpp"
#include "seam/synthesis/seam_composer.hpp"
#include "seam/voicebank/pitch.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 48000;

std::vector<float> sourceAudio() {
  std::vector<float> samples(24000, 0.0F);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto phase = 2.0 * std::numbers::pi * 329.627556 *
                       static_cast<double>(index) / kSampleRate;
    samples[index] = static_cast<float>(0.32 * std::sin(phase) +
                                        0.08 * std::sin(phase * 2.0));
  }
  return samples;
}

seam::voicebank::Unit benchmarkUnit() {
  return seam::voicebank::Unit{
      .id = "benchmark.k-i",
      .alias = "k i",
      .phones = {"k", "i"},
      .kind = seam::voicebank::UnitKind::Cv,
      .audioPath = "benchmark.wav",
      .rootMidi = 64,
      .style = "original",
      .take = 1,
      .priority = 0,
      .gainDb = 0.0F,
      .renderer = seam::voicebank::RendererHint::Raw,
      .markers = seam::voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = 2600,
          .vowelOnset = 3600,
          .stableStart = 5200,
          .loopStart = 7200,
          .loopEnd = 16800,
          .releaseStart = 19800,
          .audioEnd = 24000,
      },
      .pitchMarks = {},
      .enabled = true,
  };
}

}  // namespace

int main() {
  const auto samples = sourceAudio();
  const seam::voicebank::AudioBuffer source{
      .sampleRate = kSampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  const auto unit = benchmarkUnit();
  seam::synthesis::RawLoopRenderer renderer;

  constexpr std::size_t renderIterations = 200;
  constexpr seam::time::SampleFrame outputFrames = 24000;
  std::size_t renderedSamples = 0;
  const auto renderStart = std::chrono::steady_clock::now();
  seam::synthesis::RenderedUnit lastRendered;
  for (std::size_t iteration = 0; iteration < renderIterations; ++iteration) {
    auto result = renderer.render(
        unit, source, kSampleRate, outputFrames,
        static_cast<std::int32_t>(64 + iteration % 5),
        seam::synthesis::RawRenderParameters{
            .loopPrint = 0.85F,
            .additionalGainDb = -2.0F,
        });
    if (!result) {
      std::cerr << result.error().message << '\n';
      return 1;
    }
    lastRendered = std::move(result).value();
    renderedSamples += lastRendered.samples.size();
  }
  const auto renderEnd = std::chrono::steady_clock::now();

  seam::synthesis::SeamComposer composer;
  std::vector<seam::synthesis::PlacedRenderedUnit> placements;
  for (int index = 0; index < 4; ++index) {
    placements.push_back(seam::synthesis::PlacedRenderedUnit{
        .destinationStart = static_cast<seam::time::SampleFrame>(index * 18000),
        .unit = lastRendered,
        .incomingBoundary = std::nullopt,
    });
  }
  constexpr std::size_t seamIterations = 200;
  std::size_t composedSamples = 0;
  const auto seamStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0; iteration < seamIterations; ++iteration) {
    auto result = composer.compose(
        placements, seam::synthesis::SeamSettings{
                        .seamAmount = static_cast<float>(iteration % 100) / 99.0F,
                        .sampleRate = kSampleRate,
                    });
    if (!result) {
      std::cerr << result.error().message << '\n';
      return 2;
    }
    composedSamples += result.value().samples.size();
  }
  const auto seamEnd = std::chrono::steady_clock::now();

  constexpr std::size_t pitchIterations = 5;
  std::size_t pitchFrames = 0;
  const auto pitchStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0; iteration < pitchIterations; ++iteration) {
    auto result = seam::voicebank::analyzePitch(samples, kSampleRate);
    if (!result) {
      std::cerr << result.error().message << '\n';
      return 3;
    }
    pitchFrames += result.value().size();
  }
  const auto pitchEnd = std::chrono::steady_clock::now();

  const auto renderMs = std::chrono::duration<double, std::milli>(
      renderEnd - renderStart).count();
  const auto seamMs = std::chrono::duration<double, std::milli>(
      seamEnd - seamStart).count();
  const auto pitchMs = std::chrono::duration<double, std::milli>(
      pitchEnd - pitchStart).count();
  const auto renderedAudioSeconds = static_cast<double>(renderedSamples) / kSampleRate;
  const auto realTimeMultiple = renderedAudioSeconds / (renderMs / 1000.0);

  std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"rawRenderIterations\": " << renderIterations << ",\n"
            << "  \"rawRenderMs\": " << renderMs << ",\n"
            << "  \"rawRenderedSamples\": " << renderedSamples << ",\n"
            << "  \"rawRealTimeMultiple\": " << realTimeMultiple << ",\n"
            << "  \"seamIterations\": " << seamIterations << ",\n"
            << "  \"seamComposeMs\": " << seamMs << ",\n"
            << "  \"seamComposedSamples\": " << composedSamples << ",\n"
            << "  \"pitchIterations\": " << pitchIterations << ",\n"
            << "  \"pitchAnalysisMs\": " << pitchMs << ",\n"
            << "  \"pitchFrames\": " << pitchFrames << "\n"
            << "}\n";
  return 0;
}
