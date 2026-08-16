#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace seam::voicebank {

inline constexpr std::uint64_t kMaximumSupportedWavBytes =
    512ULL * 1024ULL * 1024ULL;

struct AudioBuffer final {
  std::uint32_t sampleRate{0};
  std::uint16_t channels{0};
  std::vector<float> interleaved;

  [[nodiscard]] std::size_t frameCount() const noexcept {
    return channels == 0 ? 0 : interleaved.size() / channels;
  }
  [[nodiscard]] std::vector<float> monoMix() const;
};

struct AudioStatistics final {
  float peak{0.0F};
  double rms{0.0};
  double dcOffset{0.0};
  std::size_t clippedSamples{0};
};

[[nodiscard]] core::Result<AudioBuffer> readWav(
    std::span<const std::byte> bytes,
    std::string_view sourceLabel = {});
[[nodiscard]] core::Result<AudioBuffer> readWav(const std::filesystem::path& path);
[[nodiscard]] core::Result<void> writePcm16Wav(
    const std::filesystem::path& path,
    std::uint32_t sampleRate,
    std::uint16_t channels,
    std::span<const float> interleaved);
[[nodiscard]] core::Result<void> writeMonoPcm16Wav(
    const std::filesystem::path& path,
    std::uint32_t sampleRate,
    std::span<const float> samples);
[[nodiscard]] AudioStatistics analyzeAudio(std::span<const float> samples) noexcept;

}  // namespace seam::voicebank
