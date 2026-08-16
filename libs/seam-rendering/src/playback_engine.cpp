#include "seam/rendering/playback_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::rendering {
namespace {

float edgeGain(const PlaybackClip& clip,
               time::SampleFrame clipRelativeFrame,
               time::SampleFrame clipFrames) noexcept {
  float value = 1.0F;
  if (clip.fadeInFrames > 0 && clipRelativeFrame < clip.fadeInFrames) {
    value *= std::clamp(
        static_cast<float>(clipRelativeFrame + 1) /
            static_cast<float>(clip.fadeInFrames + 1),
        0.0F, 1.0F);
  }
  if (clip.fadeOutFrames > 0) {
    const auto remaining = clipFrames - clipRelativeFrame;
    if (remaining <= clip.fadeOutFrames) {
      value *= std::clamp(
          static_cast<float>(remaining) /
              static_cast<float>(clip.fadeOutFrames + 1),
          0.0F, 1.0F);
    }
  }
  return value;
}

}  // namespace

core::Result<void> PlaybackTimeline::validateClip(const PlaybackClip& clip) const {
  if (clip.id.empty() || clip.pcm == nullptr || clip.pcm->samples.empty() ||
      clip.pcm->sampleRate != sampleRate_ || !std::isfinite(clip.gain) ||
      std::abs(clip.gain) > 16.0F || clip.fadeInFrames < 0 ||
      clip.fadeOutFrames < 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Playback clip is invalid", clip.id);
  }
  return core::success();
}

core::Result<void> PlaybackTimeline::setClips(std::vector<PlaybackClip> clips) {
  if (sampleRate_ < 8000U || sampleRate_ > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Playback timeline sample rate is invalid");
  }
  for (const auto& clip : clips) {
    const auto validation = validateClip(clip);
    if (!validation) return validation;
  }
  std::stable_sort(clips.begin(), clips.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.pcm->startFrame == rhs.pcm->startFrame) return lhs.id < rhs.id;
    return lhs.pcm->startFrame < rhs.pcm->startFrame;
  });
  clips_ = std::move(clips);
  return core::success();
}

core::Result<void> PlaybackTimeline::addClip(PlaybackClip clip) {
  const auto validation = validateClip(clip);
  if (!validation) return validation;
  clips_.push_back(std::move(clip));
  std::stable_sort(clips_.begin(), clips_.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.pcm->startFrame == rhs.pcm->startFrame) return lhs.id < rhs.id;
    return lhs.pcm->startFrame < rhs.pcm->startFrame;
  });
  return core::success();
}

time::SampleFrame PlaybackTimeline::startFrame() const noexcept {
  if (clips_.empty()) return 0;
  auto result = std::numeric_limits<time::SampleFrame>::max();
  for (const auto& clip : clips_) {
    if (clip.enabled && clip.pcm != nullptr) {
      result = std::min(result, clip.pcm->startFrame);
    }
  }
  return result == std::numeric_limits<time::SampleFrame>::max() ? 0 : result;
}

time::SampleFrame PlaybackTimeline::endFrame() const noexcept {
  time::SampleFrame result = 0;
  for (const auto& clip : clips_) {
    if (!clip.enabled || clip.pcm == nullptr) continue;
    const auto clipFrames = static_cast<time::SampleFrame>(clip.pcm->samples.size());
    result = std::max(result, clip.pcm->startFrame + clipFrames);
  }
  return result;
}

void PlaybackTimeline::mix(time::SampleFrame startFrame,
                           std::span<float> output) const noexcept {
  std::fill(output.begin(), output.end(), 0.0F);
  const auto outputEnd = startFrame + static_cast<time::SampleFrame>(output.size());
  for (const auto& clip : clips_) {
    if (!clip.enabled || clip.pcm == nullptr || clip.pcm->samples.empty()) continue;
    const auto clipFrames = static_cast<time::SampleFrame>(clip.pcm->samples.size());
    const auto clipStart = clip.pcm->startFrame;
    const auto clipEnd = clipStart + clipFrames;
    const auto overlapStart = std::max(startFrame, clipStart);
    const auto overlapEnd = std::min(outputEnd, clipEnd);
    if (overlapStart >= overlapEnd) continue;
    for (auto frame = overlapStart; frame < overlapEnd; ++frame) {
      const auto outputIndex = static_cast<std::size_t>(frame - startFrame);
      const auto clipIndex = frame - clipStart;
      const auto sample = clip.pcm->samples[static_cast<std::size_t>(clipIndex)];
      output[outputIndex] += sample * clip.gain *
                             edgeGain(clip, clipIndex, clipFrames);
    }
  }
  for (auto& sample : output) {
    sample = std::isfinite(sample) ? std::clamp(sample, -4.0F, 4.0F) : 0.0F;
  }
}

PlaybackFeeder::PlaybackFeeder(SpscAudioRingBuffer& ring,
                               std::uint32_t sampleRate,
                               std::size_t blockFrames)
    : ring_(ring),
      sampleRate_(sampleRate),
      scratch_(std::max<std::size_t>(1U, blockFrames), 0.0F) {}

core::Result<void> PlaybackFeeder::setTimeline(
    std::shared_ptr<const PlaybackTimeline> timeline) {
  if (timeline == nullptr || timeline->sampleRate() != sampleRate_) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Playback timeline does not match feeder sample rate");
  }
  timeline_ = std::move(timeline);
  return core::success();
}

core::Result<void> PlaybackFeeder::setLoop(PlaybackLoop loop) {
  if (loop.enabled && loop.endFrame <= loop.startFrame) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Playback loop end must be after loop start");
  }
  loop_ = loop;
  if (loop_.enabled && playhead_ >= loop_.endFrame) playhead_ = loop_.startFrame;
  return core::success();
}

void PlaybackFeeder::seek(time::SampleFrame frame) noexcept {
  playhead_ = frame;
  ring_.clear();
  ++stats_.seeks;
}

void PlaybackFeeder::mixWithLoop(std::span<float> output) noexcept {
  std::fill(output.begin(), output.end(), 0.0F);
  if (timeline_ == nullptr || !playing_) return;
  std::size_t cursor = 0U;
  while (cursor < output.size()) {
    if (loop_.enabled && playhead_ >= loop_.endFrame) {
      playhead_ = loop_.startFrame;
      ++stats_.loopWraps;
    }
    auto count = output.size() - cursor;
    if (loop_.enabled) {
      const auto untilLoopEnd = loop_.endFrame - playhead_;
      if (untilLoopEnd <= 0) continue;
      count = std::min(count, static_cast<std::size_t>(untilLoopEnd));
    }
    timeline_->mix(playhead_, output.subspan(cursor, count));
    playhead_ += static_cast<time::SampleFrame>(count);
    cursor += count;
    if (!loop_.enabled && playhead_ >= timeline_->endFrame()) {
      playing_ = false;
      if (cursor < output.size()) {
        std::fill(output.begin() + static_cast<std::ptrdiff_t>(cursor),
                  output.end(), 0.0F);
      }
      break;
    }
  }
}

std::size_t PlaybackFeeder::feedOnce() noexcept {
  ++stats_.feedCalls;
  if (ring_.availableWrite() == 0U) {
    ++stats_.ringFullEvents;
    return 0U;
  }
  const auto requested = std::min(scratch_.size(), ring_.availableWrite());
  auto block = std::span<float>{scratch_}.first(requested);
  mixWithLoop(block);
  stats_.framesMixed += requested;
  const auto written = ring_.write(block);
  stats_.framesWritten += written;
  if (written < requested) ++stats_.ringFullEvents;
  return written;
}

std::size_t PlaybackFeeder::feedToWatermark(std::size_t targetFrames) noexcept {
  const auto target = std::min(targetFrames, ring_.capacity());
  std::size_t total = 0U;
  while (ring_.availableRead() < target && ring_.availableWrite() > 0U) {
    const auto written = feedOnce();
    total += written;
    if (written == 0U) break;
  }
  return total;
}

}  // namespace seam::rendering
