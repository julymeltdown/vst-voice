#include "seam/platform/audio_callback.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/synthesis/pitch_curve.hpp"
#include "seam/synthesis/spectral_classic.hpp"
#include "seam/synthesis/stretch_renderer.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 48000U;
constexpr seam::time::SampleFrame kOutputFrames = 48000;

std::vector<float> sourceAudio() {
  std::vector<float> samples(42000U, 0.0F);
  constexpr double fundamental = 246.9416506;
  for (std::size_t index = 0U; index < samples.size(); ++index) {
    const auto seconds = static_cast<double>(index) /
                         static_cast<double>(kSampleRate);
    const auto attack = std::min(1.0, static_cast<double>(index) / 320.0);
    const auto remaining = samples.size() - index - 1U;
    const auto release = std::min(1.0, static_cast<double>(remaining) / 960.0);
    const auto envelope = std::min(attack, release);
    const auto phase = 2.0 * std::numbers::pi * fundamental * seconds;
    samples[index] = static_cast<float>(
        envelope * (0.28 * std::sin(phase) +
                    0.10 * std::sin(phase * 2.0 + 0.17) +
                    0.04 * std::sin(phase * 3.0 + 0.39)));
  }
  return samples;
}

seam::voicebank::Unit benchmarkUnit() {
  return seam::voicebank::Unit{
      .id = "phase4.benchmark.b3",
      .alias = "a",
      .phones = {"a"},
      .kind = seam::voicebank::UnitKind::Sustain,
      .audioPath = "benchmark.wav",
      .rootMidi = 59,
      .style = "original",
      .take = 1,
      .priority = 0,
      .gainDb = -1.0F,
      .renderer = seam::voicebank::RendererHint::SpectralClassic,
      .markers = seam::voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = 2600,
          .vowelOnset = 3900,
          .stableStart = 5600,
          .loopStart = 7600,
          .loopEnd = 34000,
          .releaseStart = 37000,
          .audioEnd = 42000,
      },
      .pitchMarks = {},
      .enabled = true,
  };
}

std::filesystem::path cacheDirectory() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("project-seam-phase4-benchmark-" + std::to_string(stamp));
}

double milliseconds(std::chrono::steady_clock::time_point start,
                    std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace

int main() {
  const auto samples = sourceAudio();
  const seam::voicebank::AudioBuffer source{
      .sampleRate = kSampleRate,
      .channels = 1U,
      .interleaved = samples,
  };
  const auto unit = benchmarkUnit();
  const auto validation = unit.validate();
  if (!validation) {
    std::cerr << validation.error().message << '\n';
    return 1;
  }

  const seam::synthesis::PitchCurve pitchCurve{
      std::vector<seam::synthesis::PitchPoint>{
          {.frame = 0, .cents = -22.0F,
           .interpolation = seam::domain::CurveInterpolation::Smooth},
          {.frame = kOutputFrames / 2, .cents = 34.0F,
           .interpolation = seam::domain::CurveInterpolation::Smooth},
          {.frame = kOutputFrames - 1, .cents = 0.0F,
           .interpolation = seam::domain::CurveInterpolation::Linear},
      }};

  seam::synthesis::SpectralClassicRenderer spectral;
  constexpr std::size_t spectralIterations = 18U;
  std::size_t spectralSamples = 0U;
  seam::synthesis::RenderedUnit lastSpectral;
  const auto spectralStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0U; iteration < spectralIterations; ++iteration) {
    auto rendered = spectral.render(
        unit, source, kSampleRate, kOutputFrames,
        static_cast<std::int32_t>(64 + iteration % 3U),
        seam::synthesis::SpectralRenderParameters{
            .fftSize = 1024U,
            .hopSize = 256U,
            .formantFollow = 0.58F,
            .phaseReset = static_cast<float>(iteration % 10U) / 10.0F,
            .additionalGainDb = -2.0F,
            .pitchCurve = pitchCurve,
        });
    if (!rendered) {
      std::cerr << rendered.error().message << '\n';
      return 2;
    }
    lastSpectral = std::move(rendered).value();
    spectralSamples += lastSpectral.samples.size();
  }
  const auto spectralEnd = std::chrono::steady_clock::now();

  seam::synthesis::StretchUnitRenderer stretch;
  constexpr std::size_t stretchIterations = 80U;
  std::size_t stretchSamples = 0U;
  seam::synthesis::RenderedUnit lastStretch;
  const auto stretchStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0U; iteration < stretchIterations; ++iteration) {
    auto rendered = stretch.render(
        unit, source, kSampleRate,
        kOutputFrames + static_cast<seam::time::SampleFrame>((iteration % 4U) * 2400U),
        static_cast<std::int32_t>(62 + iteration % 5U),
        seam::synthesis::StretchRenderParameters{
            .grainSize = 1024U,
            .hopSize = 256U,
            .transientPreservation = 0.78F,
            .sourceDrift = 0.31F,
            .additionalGainDb = -2.0F,
            .pitchCurve = pitchCurve,
        });
    if (!rendered) {
      std::cerr << rendered.error().message << '\n';
      return 3;
    }
    lastStretch = std::move(rendered).value();
    stretchSamples += lastStretch.samples.size();
  }
  const auto stretchEnd = std::chrono::steady_clock::now();

  auto vocal = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = kSampleRate,
          .startFrame = 0,
          .samples = lastSpectral.samples,
      });
  auto backingSamples = lastSpectral.samples;
  for (std::size_t index = 0U; index < backingSamples.size(); ++index) {
    const auto seconds = static_cast<double>(index) /
                         static_cast<double>(kSampleRate);
    backingSamples[index] = static_cast<float>(
        0.04 * std::sin(2.0 * std::numbers::pi * 82.406889 * seconds));
  }
  auto backing = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = kSampleRate,
          .startFrame = 0,
          .samples = std::move(backingSamples),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(kSampleRate);
  const auto timelineResult = timeline->setClips({
      seam::rendering::PlaybackClip{
          .id = "voice", .pcm = vocal, .gain = 0.82F,
          .fadeInFrames = 64, .fadeOutFrames = 128, .enabled = true},
      seam::rendering::PlaybackClip{
          .id = "backing", .pcm = backing, .gain = 0.70F,
          .fadeInFrames = 128, .fadeOutFrames = 128, .enabled = true},
  });
  if (!timelineResult) {
    std::cerr << timelineResult.error().message << '\n';
    return 4;
  }

  seam::rendering::SpscAudioRingBuffer ring{32768U};
  seam::rendering::PlaybackFeeder feeder{ring, kSampleRate, 1024U};
  if (!feeder.setTimeline(timeline) ||
      !feeder.setLoop(seam::rendering::PlaybackLoop{
          .enabled = true, .startFrame = 0,
          .endFrame = static_cast<seam::time::SampleFrame>(vocal->samples.size())})) {
    std::cerr << "Unable to configure Phase 4 playback benchmark\n";
    return 5;
  }
  feeder.setPlaying(true);
  seam::platform::RingBufferAudioProcessor processor{ring};
  seam::platform::AudioCallbackSimulator callback{48000.0, 256U};
  constexpr std::size_t callbackIterations = 2400U;
  const auto playbackStart = std::chrono::steady_clock::now();
  for (std::size_t index = 0U; index < callbackIterations; ++index) {
    static_cast<void>(feeder.feedToWatermark(8192U));
    callback.run(processor, 1U);
  }
  const auto playbackEnd = std::chrono::steady_clock::now();
  const auto callbackStats = processor.stats();
  if (callbackStats.underflowFrames != 0U) {
    std::cerr << "Playback benchmark underflowed\n";
    return 6;
  }

  const auto cacheRoot = cacheDirectory();
  seam::rendering::PcmCache cache{
      cacheRoot,
      seam::rendering::PcmCacheLimits{
          .maximumMemoryBytes = 160000U,
          .maximumDiskBytes = 360000U,
          .maximumDiskEntries = 3U,
      }};
  constexpr std::size_t cacheEntries = 16U;
  const auto cacheStart = std::chrono::steady_clock::now();
  for (std::size_t entry = 0U; entry < cacheEntries; ++entry) {
    auto payload = lastStretch.samples;
    if (!payload.empty()) payload.front() = static_cast<float>(entry) / 1000.0F;
    const auto stored = cache.store(
        "phase4bench" + std::to_string(entry), seam::rendering::CachedPcm{
            .sampleRate = kSampleRate,
            .startFrame = static_cast<seam::time::SampleFrame>(entry * 128U),
            .samples = std::move(payload),
        });
    if (!stored) {
      std::cerr << stored.error().message << '\n';
      return 7;
    }
  }
  const auto cacheEnd = std::chrono::steady_clock::now();
  const auto usage = cache.usage();
  if (!usage) {
    std::cerr << usage.error().message << '\n';
    return 8;
  }
  const auto cacheStats = cache.stats();

  const auto spectralMs = milliseconds(spectralStart, spectralEnd);
  const auto stretchMs = milliseconds(stretchStart, stretchEnd);
  const auto playbackMs = milliseconds(playbackStart, playbackEnd);
  const auto cacheMs = milliseconds(cacheStart, cacheEnd);
  const auto spectralAudioSeconds = static_cast<double>(spectralSamples) /
                                     static_cast<double>(kSampleRate);
  const auto stretchAudioSeconds = static_cast<double>(stretchSamples) /
                                    static_cast<double>(kSampleRate);
  const auto callbackAudioSeconds =
      static_cast<double>(callbackStats.requestedFrames) /
      static_cast<double>(kSampleRate);

  std::error_code removeError;
  std::filesystem::remove_all(cacheRoot, removeError);

  std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"spectralIterations\": " << spectralIterations << ",\n"
            << "  \"spectralMs\": " << spectralMs << ",\n"
            << "  \"spectralRenderedSamples\": " << spectralSamples << ",\n"
            << "  \"spectralRealTimeMultiple\": "
            << spectralAudioSeconds / (spectralMs / 1000.0) << ",\n"
            << "  \"stretchIterations\": " << stretchIterations << ",\n"
            << "  \"stretchMs\": " << stretchMs << ",\n"
            << "  \"stretchRenderedSamples\": " << stretchSamples << ",\n"
            << "  \"stretchRealTimeMultiple\": "
            << stretchAudioSeconds / (stretchMs / 1000.0) << ",\n"
            << "  \"callbackIterations\": " << callbackIterations << ",\n"
            << "  \"callbackAudioSeconds\": " << callbackAudioSeconds << ",\n"
            << "  \"playbackMs\": " << playbackMs << ",\n"
            << "  \"playbackThroughputMultiple\": "
            << callbackAudioSeconds / (playbackMs / 1000.0) << ",\n"
            << "  \"callbackUnderflowFrames\": "
            << callbackStats.underflowFrames << ",\n"
            << "  \"cacheEntriesStored\": " << cacheEntries << ",\n"
            << "  \"cacheStoreMs\": " << cacheMs << ",\n"
            << "  \"cacheMemoryEntries\": " << usage.value().memoryEntries << ",\n"
            << "  \"cacheDiskEntries\": " << usage.value().diskEntries << ",\n"
            << "  \"cacheMemoryEvictions\": "
            << cacheStats.memoryEvictions << ",\n"
            << "  \"cacheDiskEvictions\": " << cacheStats.diskEvictions << "\n"
            << "}\n";
  return 0;
}
