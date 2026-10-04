#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

// The sample encoding a render state was captured from. The state payload is
// always normalized float PCM, so this records the source encoding rather than
// a second copy of the audio. It exists because extracting a state must return
// the bit depth it was packed from: writing 16-bit unconditionally silently
// threw away 8 bits per sample from 24-bit renders and every mantissa bit from
// float32 renders while still reporting success.
enum class ClapSampleFormat : std::uint32_t {
  Pcm16 = 1U,
  Pcm24 = 2U,
  Float32 = 3U,
};

[[nodiscard]] constexpr bool isKnownClapSampleFormat(
    std::uint32_t value) noexcept {
  return value >= static_cast<std::uint32_t>(ClapSampleFormat::Pcm16) &&
         value <= static_cast<std::uint32_t>(ClapSampleFormat::Float32);
}

// Maps the stored header slot onto a format. Zero is not a format: it is the
// value every writer put in that reserved field before the format was recorded,
// so it is read as PCM16, which is the only depth extract could emit before this
// change. Any other unrecognized value is a corrupt or future format and is
// rejected rather than guessed at.
[[nodiscard]] constexpr std::optional<ClapSampleFormat>
clapSampleFormatFromStoredId(std::uint32_t value) noexcept {
  if (value == 0U) return ClapSampleFormat::Pcm16;
  if (!isKnownClapSampleFormat(value)) return std::nullopt;
  return static_cast<ClapSampleFormat>(value);
}

[[nodiscard]] constexpr std::string_view clapSampleFormatName(
    ClapSampleFormat format) noexcept {
  switch (format) {
    case ClapSampleFormat::Pcm16:
      return "pcm16";
    case ClapSampleFormat::Pcm24:
      return "pcm24";
    case ClapSampleFormat::Float32:
      return "float32";
  }
  return "pcm16";
}

struct PluginSession final {
  std::uint32_t sampleRate{48000U};
  std::uint8_t channelCount{2U};
  ClapSampleFormat sampleFormat{ClapSampleFormat::Pcm16};
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
[[nodiscard]] core::Result<double> parseMasterGainDb(std::string_view text);
[[nodiscard]] float gainFromDecibels(double decibels) noexcept;

}  // namespace seam::clap
