#include "seam/platform/output_level_meter.hpp"

#include <algorithm>
#include <cmath>

namespace seam::platform {
namespace {

// Keeps an infinite sample finite in the published peak (+24 dBFS is far past any clip).
constexpr float kPeakCeiling = 16.0F;

template <typename Sample>
float blockPeak(const Sample* samples, std::size_t frames) noexcept {
  float peak = 0.0F;
  if (samples == nullptr) return peak;
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const auto magnitude = static_cast<float>(std::fabs(samples[frame]));
    // NaN compares false and is skipped; infinity reaches the ceiling and clips.
    if (magnitude > peak) peak = std::min(magnitude, kPeakCeiling);
  }
  return peak;
}

}  // namespace

void OutputLevelMeter::raise(std::size_t channel, float peak) noexcept {
  auto& window = windowPeak_[channel];
  auto current = window.load(std::memory_order_relaxed);
  while (peak > current &&
         !window.compare_exchange_weak(current, peak, std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
  }
}

void OutputLevelMeter::finishBlock(std::size_t channels, bool clipped) noexcept {
  // A block that carries fewer channels than an earlier one did leaves nothing measured for the
  // channels it did not carry; without this a narrower block would keep showing an older, wider
  // block's peak.
  for (std::size_t channel = channels; channel < kMaxChannels; ++channel)
    windowPeak_[channel].store(0.0F, std::memory_order_relaxed);
  if (clipped) clipped_.store(true, std::memory_order_relaxed);
  channels_.store(static_cast<std::uint32_t>(channels), std::memory_order_relaxed);
  blocks_.fetch_add(1U, std::memory_order_release);
}

void OutputLevelMeter::measure(const AudioProcessContext& context) noexcept {
  const auto channels = std::min(context.outputChannelCount(), kMaxChannels);
  bool clipped = false;
  for (std::size_t channel = 0U; channel < channels; ++channel) {
    const auto output = context.output(channel);
    const auto peak = blockPeak(output.data(), std::min(context.frameCount, output.size()));
    clipped = clipped || peak >= kClipThreshold;
    raise(channel, peak);
  }
  finishBlock(channels, clipped);
}

void OutputLevelMeter::measure(std::span<const float* const> channels,
                               std::size_t frames) noexcept {
  const auto count = std::min(channels.size(), kMaxChannels);
  bool clipped = false;
  for (std::size_t channel = 0U; channel < count; ++channel) {
    const auto peak = blockPeak(channels[channel], frames);
    clipped = clipped || peak >= kClipThreshold;
    raise(channel, peak);
  }
  finishBlock(count, clipped);
}

void OutputLevelMeter::measure(std::span<const double* const> channels,
                               std::size_t frames) noexcept {
  const auto count = std::min(channels.size(), kMaxChannels);
  bool clipped = false;
  for (std::size_t channel = 0U; channel < count; ++channel) {
    const auto peak = blockPeak(channels[channel], frames);
    clipped = clipped || peak >= kClipThreshold;
    raise(channel, peak);
  }
  finishBlock(count, clipped);
}

void OutputLevelMeter::forget() noexcept {
  live_ = false;
  shownChannels_ = 0U;
  level_.fill(0.0F);
  hold_.fill(0.0F);
  // Whatever the audio thread measured before this point belongs to a run that is over.
  for (auto& window : windowPeak_) window.store(0.0F, std::memory_order_relaxed);
  seenBlocks_ = blocks_.load(std::memory_order_acquire);
}

std::optional<OutputLevelReading> OutputLevelMeter::read(
    bool sourceRunning, std::chrono::steady_clock::time_point now) {
  if (!sourceRunning) {
    forget();
    return std::nullopt;
  }
  const auto blocks = blocks_.load(std::memory_order_acquire);
  const auto fresh = blocks != seenBlocks_;
  if (fresh) {
    seenBlocks_ = blocks;
    lastBlockAt_ = now;
  } else if (!live_ || now - lastBlockAt_ > ballistics_.staleAfter) {
    // Nothing measured since the source started, or the callback stopped arriving.
    forget();
    return std::nullopt;
  }
  const auto channels =
      std::min<std::size_t>(channels_.load(std::memory_order_relaxed), kMaxChannels);
  if (channels == 0U) {
    forget();
    return std::nullopt;
  }
  if (!live_ || channels != shownChannels_) {
    level_.fill(0.0F);
    hold_.fill(0.0F);
    holdSince_.fill(now);
    lastReadAt_ = now;
    shownChannels_ = channels;
    live_ = true;
  }
  const auto elapsed = std::clamp(
      std::chrono::duration<double>(now - lastReadAt_).count(), 0.0, 1.0);
  lastReadAt_ = now;
  const auto decay = static_cast<float>(
      std::pow(10.0, -ballistics_.decayDbPerSecond * elapsed / 20.0));

  OutputLevelReading reading;
  reading.peak.resize(channels);
  reading.hold.resize(channels);
  for (std::size_t channel = 0U; channel < channels; ++channel) {
    const auto measured = fresh ? windowPeak_[channel].exchange(0.0F, std::memory_order_relaxed)
                                : 0.0F;
    level_[channel] = std::max(measured, level_[channel] * decay);
    if (measured >= hold_[channel]) {
      hold_[channel] = measured;
      holdSince_[channel] = now;
    } else if (now - holdSince_[channel] > ballistics_.hold) {
      hold_[channel] = std::max(level_[channel], hold_[channel] * decay);
    }
    reading.peak[channel] = level_[channel];
    reading.hold[channel] = hold_[channel];
  }
  reading.clipped = clipped_.load(std::memory_order_relaxed);
  return reading;
}

}  // namespace seam::platform
