#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace seam::voicebank {

inline constexpr std::uint64_t kMaximumSupportedWavBytes =
    512ULL * 1024ULL * 1024ULL;

struct WavReadLimits final {
  // Defaults retain the existing 512 MiB encoded-payload/eight-channel
  // admission. Bounded callers should set their smaller allocation budget.
  std::uint64_t maximumFrames{kMaximumSupportedWavBytes};
  std::uint16_t maximumChannels{8U};
  std::uint64_t maximumDecodedSamples{kMaximumSupportedWavBytes};
};

struct AudioBuffer final {
  std::uint32_t sampleRate{0};
  std::uint16_t channels{0};
  std::uint16_t bitsPerSample{0};
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

enum class WavSampleFormat { Pcm16, Pcm24, Float32 };

struct WavOutputFormat final {
  std::uint32_t sampleRate{48000U};
  std::uint16_t channels{2U};
  WavSampleFormat sampleFormat{WavSampleFormat::Pcm16};
};

class WavStreamWriter final {
public:
  static core::Result<std::unique_ptr<WavStreamWriter>> create(
      const std::filesystem::path& path, WavOutputFormat format);
  ~WavStreamWriter();

  WavStreamWriter(const WavStreamWriter&) = delete;
  WavStreamWriter& operator=(const WavStreamWriter&) = delete;

  [[nodiscard]] core::Result<void> writeFrames(
      std::span<const float> interleaved);
  [[nodiscard]] core::Result<void> finalize();
  [[nodiscard]] std::uint64_t framesWritten() const noexcept {
    return framesWritten_;
  }

private:
  WavStreamWriter(std::filesystem::path path, WavOutputFormat format);
  core::Result<void> writeHeader();
  core::Result<void> writeSample(float sample);

  std::filesystem::path path_;
  WavOutputFormat format_;
  std::ofstream* stream_{nullptr};
  std::unique_ptr<std::ofstream> ownedStream_;
  std::uint64_t dataBytes_{0U};
  std::uint64_t framesWritten_{0U};
  bool finalized_{false};
};

[[nodiscard]] core::Result<void> writeWav(
    const std::filesystem::path& path, WavOutputFormat format,
    std::span<const float> interleaved);

[[nodiscard]] core::Result<AudioBuffer> readWav(
    std::span<const std::byte> bytes,
    std::string_view sourceLabel = {});
[[nodiscard]] core::Result<AudioBuffer> readWav(const std::filesystem::path& path);
// The existing overloads retain source and binary call compatibility. Limits
// below are checked against validated WAV metadata before allocating PCM;
// cancellation never returns a partially decoded AudioBuffer.
[[nodiscard]] core::Result<AudioBuffer> readWav(
    std::span<const std::byte> bytes, std::string_view sourceLabel,
    WavReadLimits limits, std::stop_token stopToken = {});
[[nodiscard]] core::Result<AudioBuffer> readWav(
    const std::filesystem::path& path, WavReadLimits limits,
    std::stop_token stopToken = {});
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
