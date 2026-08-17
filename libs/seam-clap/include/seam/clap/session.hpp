#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace seam::clap {

inline constexpr std::uint32_t kStateFormatVersion = 1U;
inline constexpr std::uint32_t kMinimumSampleRate = 8000U;
inline constexpr std::uint32_t kMaximumSampleRate = 192000U;
inline constexpr std::uint8_t kMaximumChannels = 8U;
inline constexpr std::uint64_t kMaximumStateBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t kMaximumDurationSeconds = 600U;
inline constexpr double kMinimumMasterGainDb = -60.0;
inline constexpr double kMaximumMasterGainDb = 6.0;
inline constexpr double kDefaultMasterGainDb = 0.0;
inline constexpr std::uint32_t kMasterGainParamId = 0x534D4701U;

struct PluginSession final {
  std::uint32_t sampleRate{48000U};
  std::uint8_t channelCount{2U};
  double masterGainDb{kDefaultMasterGainDb};
  std::string title{"Untitled SEAM Render"};
  std::vector<float> interleavedSamples;

  [[nodiscard]] std::uint64_t frameCount() const noexcept {
    return channelCount == 0U ? 0U : interleavedSamples.size() / channelCount;
  }
  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const PluginSession&, const PluginSession&) = default;
};

[[nodiscard]] core::Result<PluginSession> resampleSession(
    const PluginSession& source, std::uint32_t targetSampleRate);
[[nodiscard]] core::Result<PluginSession> makeDiagnosticSession(
    std::uint32_t sampleRate, std::uint8_t channels,
    double durationSeconds = 2.0);
[[nodiscard]] float gainFromDecibels(double decibels) noexcept;

}  // namespace seam::clap
