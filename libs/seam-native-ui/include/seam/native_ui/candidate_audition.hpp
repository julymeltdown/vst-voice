#pragma once

#include "seam/core/result.hpp"
#include "seam/platform/audio_callback.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace seam::native_ui {

// Prepared on the owner thread and kept alive until the device has stopped.
// The callback owns its cursor: no allocation, locking, IO, or ownership swaps.
class CandidateAuditionProcessor final : public platform::IAudioProcessor {
public:
  static core::Result<std::unique_ptr<CandidateAuditionProcessor>> create(
      std::shared_ptr<const voicebank::AudioBuffer> audio,
      std::size_t begin, std::size_t end, float gain) {
    if (!audio || audio->channels != 1U || audio->sampleRate < 8000U || audio->sampleRate > 384000U ||
        begin >= end || end > audio->interleaved.size() || audio->interleaved.size() > 32U * 1024U * 1024U ||
        !std::isfinite(gain) || gain <= 0.0F || gain > 0.25F) {
      return core::failure<std::unique_ptr<CandidateAuditionProcessor>>(
          core::ErrorCode::InvalidArgument, "Candidate audition input is invalid");
    }
    return std::unique_ptr<CandidateAuditionProcessor>{new CandidateAuditionProcessor(std::move(audio), begin, end, gain)};
  }

  void process(platform::AudioProcessContext context) noexcept override {
    const auto channels = context.outputChannelCount();
    bool valid = channels > 0U && channels <= 2U && context.sampleRate == audio_->sampleRate;
    for (std::size_t channel = 0U; channel < channels; ++channel) {
      auto output = context.output(channel);
      std::fill(output.begin(), output.end(), 0.0F);
      valid = valid && output.size() >= context.frameCount;
    }
    if (!valid) { failed_.store(true, std::memory_order_release); finished_.store(true, std::memory_order_release); return; }
    if (finished_.load(std::memory_order_relaxed)) return;
    const auto count = std::min(context.frameCount, end_ - cursor_);
    const auto fade = std::min<std::size_t>(audio_->sampleRate / 200U, (end_ - begin_) / 2U);
    float peak = 0.0F;
    for (std::size_t index = 0U; index < count; ++index) {
      const auto position = cursor_ + index;
      const auto sample = audio_->interleaved[position];
      const auto envelope = fade == 0U ? 1.0 : std::min({1.0,
          static_cast<double>(position - begin_) / static_cast<double>(fade),
          static_cast<double>(end_ - 1U - position) / static_cast<double>(fade)});
      const auto value = std::isfinite(sample)
          ? static_cast<float>(std::clamp(static_cast<double>(sample) * gain_ * envelope, -1.0, 1.0)) : 0.0F;
      peak = std::max(peak, std::abs(value));
      for (std::size_t channel = 0U; channel < channels; ++channel) context.output(channel)[index] = value;
    }
    cursor_ += count;
    blockPeak_.store(peak, std::memory_order_relaxed);
    if (cursor_ == end_) finished_.store(true, std::memory_order_release);
  }
  [[nodiscard]] bool finished() const noexcept { return finished_.load(std::memory_order_acquire); }
  [[nodiscard]] bool failed() const noexcept { return failed_.load(std::memory_order_acquire); }
  // The absolute peak of the samples the last callback wrote to the output (0 before the first).
  [[nodiscard]] float blockPeak() const noexcept { return blockPeak_.load(std::memory_order_relaxed); }

private:
  CandidateAuditionProcessor(std::shared_ptr<const voicebank::AudioBuffer> audio,
      std::size_t begin, std::size_t end, float gain)
      : audio_(std::move(audio)), begin_(begin), end_(end), cursor_(begin), gain_(gain) {}
  std::shared_ptr<const voicebank::AudioBuffer> audio_;
  std::size_t begin_, end_, cursor_;
  float gain_;
  std::atomic<bool> finished_{false}, failed_{false};
  std::atomic<float> blockPeak_{0.0F};
};
}
