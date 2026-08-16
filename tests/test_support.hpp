#pragma once

#include "seam/voicebank/voicebank.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace seam::test::support {

inline std::filesystem::path temporaryDirectory(std::string_view name) {
  static std::atomic<std::uint64_t> counter{0};
  const auto path = std::filesystem::temp_directory_path() /
                    ("project-seam-" + std::string{name} + "-" +
                     std::to_string(counter.fetch_add(1)));
  std::error_code error;
  std::filesystem::remove_all(path, error);
  error.clear();
  std::filesystem::create_directories(path, error);
  if (error) {
    throw std::runtime_error("Unable to create test directory: " + error.message());
  }
  return path;
}

inline std::vector<float> sineWave(std::uint32_t sampleRate,
                                   double frequencyHz,
                                   double seconds,
                                   float amplitude = 0.35F,
                                   double phase = 0.0) {
  const auto count = static_cast<std::size_t>(
      std::llround(static_cast<double>(sampleRate) * seconds));
  std::vector<float> samples(count, 0.0F);
  const auto increment = 2.0 * std::numbers::pi * frequencyHz /
                         static_cast<double>(sampleRate);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto envelopeIn = std::min(1.0, static_cast<double>(index) / 128.0);
    const auto remaining = samples.size() - index - 1U;
    const auto envelopeOut = std::min(1.0, static_cast<double>(remaining) / 128.0);
    samples[index] = amplitude * static_cast<float>(
        std::sin(phase + increment * static_cast<double>(index)) *
        std::min(envelopeIn, envelopeOut));
  }
  return samples;
}

inline voicebank::Unit makeUnit(std::string id,
                                std::vector<std::string> phones,
                                std::filesystem::path audioPath,
                                std::int32_t rootMidi = 69,
                                voicebank::UnitKind kind = voicebank::UnitKind::Cv,
                                std::size_t totalFrames = 24000) {
  const auto frame = static_cast<time::SampleFrame>(totalFrames);
  return voicebank::Unit{
      .id = std::move(id),
      .alias = phones.empty() ? "unit" : phones.front(),
      .phones = std::move(phones),
      .kind = kind,
      .audioPath = std::move(audioPath),
      .rootMidi = rootMidi,
      .style = "original",
      .take = 1,
      .priority = 0,
      .gainDb = 0.0F,
      .renderer = voicebank::RendererHint::Raw,
      .markers = voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = std::min<time::SampleFrame>(2400, frame / 5),
          .vowelOnset = std::min<time::SampleFrame>(3600, frame / 4),
          .stableStart = std::min<time::SampleFrame>(4800, frame / 3),
          .loopStart = std::min<time::SampleFrame>(7200, frame / 2),
          .loopEnd = std::min<time::SampleFrame>(16800, frame * 4 / 5),
          .releaseStart = std::min<time::SampleFrame>(19200, frame * 9 / 10),
          .audioEnd = frame,
      },
      .pitchMarks = {},
      .enabled = true,
  };
}

inline voicebank::Manifest makeManifest(std::vector<voicebank::Unit> units,
                                        std::uint32_t sampleRate = 48000) {
  return voicebank::Manifest{
      .id = "test.voicebank",
      .version = "1.0.0",
      .displayName = "Project SEAM Test Voicebank",
      .language = domain::Language::Japanese,
      .expectedSampleRate = sampleRate,
      .styles = {"original"},
      .units = std::move(units),
  };
}

}  // namespace seam::test::support
