#pragma once

#include "seam/core/result.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/voicebank/wav.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace seam::platform {

class RecordingSession final : public IAudioInputProcessor {
public:
  RecordingSession(std::uint32_t sampleRate = 48000U,
                   std::size_t maximumSeconds = 300U);

  [[nodiscard]] core::Result<void> arm() noexcept;
  void stop() noexcept;
  void clear() noexcept;
  void process(AudioInputProcessContext context) noexcept override;

  [[nodiscard]] bool armed() const noexcept {
    return armed_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::size_t recordedFrames() const noexcept {
    return writeIndex_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] bool overflowed() const noexcept {
    return overflowed_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::span<const float> samples() const noexcept;
  [[nodiscard]] core::Result<void> exportWav(
      const std::filesystem::path& path,
      voicebank::WavSampleFormat format = voicebank::WavSampleFormat::Pcm16,
      bool replaceExisting = true) const;

private:
  std::uint32_t sampleRate_{48000U};
  std::vector<float> buffer_;
  std::atomic<std::size_t> writeIndex_{0U};
  std::atomic<bool> armed_{false};
  std::atomic<bool> overflowed_{false};
};

}  // namespace seam::platform
