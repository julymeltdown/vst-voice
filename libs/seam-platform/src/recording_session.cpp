#include "seam/platform/recording_session.hpp"

#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <limits>

namespace seam::platform {

RecordingSession::RecordingSession(std::uint32_t sampleRate,
                                   std::size_t maximumSeconds)
    : sampleRate_(sampleRate) {
  if (sampleRate_ < 8000U || sampleRate_ > 384000U || maximumSeconds == 0U ||
      maximumSeconds > 3600U ||
      maximumSeconds > std::numeric_limits<std::size_t>::max() / sampleRate_) {
    sampleRate_ = 48000U;
    maximumSeconds = 300U;
  }
  buffer_.assign(static_cast<std::size_t>(sampleRate_) * maximumSeconds, 0.0F);
}

core::Result<void> RecordingSession::arm() noexcept {
  if (buffer_.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Recording session has no capture buffer");
  }
  writeIndex_.store(0U, std::memory_order_release);
  overflowed_.store(false, std::memory_order_release);
  armed_.store(true, std::memory_order_release);
  return core::success();
}

void RecordingSession::stop() noexcept {
  armed_.store(false, std::memory_order_release);
}

void RecordingSession::clear() noexcept {
  stop();
  writeIndex_.store(0U, std::memory_order_release);
  overflowed_.store(false, std::memory_order_release);
}

void RecordingSession::process(AudioInputProcessContext context) noexcept {
  if (!armed_.load(std::memory_order_acquire) || context.mono.empty()) return;
  auto offset = writeIndex_.load(std::memory_order_relaxed);
  if (offset >= buffer_.size()) {
    overflowed_.store(true, std::memory_order_release);
    armed_.store(false, std::memory_order_release);
    return;
  }
  const auto writable = std::min(context.mono.size(), buffer_.size() - offset);
  std::copy_n(context.mono.begin(), writable, buffer_.begin() +
              static_cast<std::ptrdiff_t>(offset));
  offset += writable;
  writeIndex_.store(offset, std::memory_order_release);
  if (writable != context.mono.size()) {
    overflowed_.store(true, std::memory_order_release);
    armed_.store(false, std::memory_order_release);
  }
}

std::span<const float> RecordingSession::samples() const noexcept {
  return std::span<const float>{buffer_.data(), recordedFrames()};
}

core::Result<void> RecordingSession::exportWav(
    const std::filesystem::path& path) const {
  const auto data = samples();
  if (data.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Recording session contains no audio");
  }
  return voicebank::writeMonoPcm16Wav(path, sampleRate_, data);
}

}  // namespace seam::platform
