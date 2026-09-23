#include "seam/rendering/sample_rate_converter.hpp"
#include "seam/core/resample.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::rendering {

core::Result<std::vector<float>> SampleRateConverter::convert(
    std::span<const float> source, std::uint32_t sourceRate,
    std::uint32_t targetRate, SampleRateQuality quality) {
  if (sourceRate < 8000U || sourceRate > 384000U || targetRate < 8000U ||
      targetRate > 384000U) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::InvalidArgument,
        "Sample-rate conversion rate is outside supported bounds");
  }
  if (source.empty()) return std::vector<float>{};
  if (sourceRate == targetRate) return std::vector<float>{source.begin(), source.end()};
  const auto outputSize = static_cast<std::size_t>(std::llround(
      static_cast<long double>(source.size()) * targetRate / sourceRate));
  if (outputSize == 0U) return std::vector<float>{};
  std::vector<float> output(outputSize, 0.0F);
  // `bandRatio` is output samples per input sample, which is the direction the
  // shared kernel expects. The previous code passed its inverse, which put the
  // cutoff below the true Nyquist on an upsample and simply interpolated on a
  // downsample.
  const auto bandRatio = static_cast<double>(targetRate) /
                         static_cast<double>(sourceRate);
  for (std::size_t index = 0U; index < output.size(); ++index) {
    const auto position = static_cast<double>(index) / bandRatio;
    const auto interpolated = static_cast<float>(core::bandLimitedSampleAt(
        source, position, bandRatio, 1U, 0U));
    output[index] = quality == SampleRateQuality::Final
                        ? std::clamp(interpolated, -1.0F, 1.0F)
                        : interpolated;
  }
  return output;
}

core::Result<void> StreamingSampleRateConverter::validate() const {
  if (sourceRate_ < 8000U || sourceRate_ > 384000U || targetRate_ < 8000U ||
      targetRate_ > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Sample-rate conversion rate is outside supported bounds");
  }
  return core::success();
}

std::vector<float> StreamingSampleRateConverter::emit(bool final) {
  std::vector<float> output;
  if (sourceRate_ == targetRate_) {
    if (!final) {
      output.assign(pending_.begin(), pending_.end());
      baseIndex_ += pending_.size();
      inputFrames_ = baseIndex_;
      nextOutputIndex_ = inputFrames_;
      pending_.clear();
    }
    return output;
  }

  if (ratio_ == 0.0L) {
    ratio_ = static_cast<long double>(sourceRate_) /
             static_cast<long double>(targetRate_);
  }
  const auto outputLimit = final
                               ? static_cast<std::uint64_t>(std::llround(
                                     static_cast<long double>(inputFrames_) *
                                     targetRate_ / sourceRate_))
                               : std::numeric_limits<std::uint64_t>::max();
  // The kernel reaches halfWidth source samples either side of the interpolated
  // position, so the stream must hold that much history and wait for that much
  // lookahead. `sampleAt` above is retained for the identity path only; the
  // band-limited path needs edge handling, so it clamps explicitly.
  const auto bandRatio = static_cast<double>(targetRate_) /
                         static_cast<double>(sourceRate_);
  const auto halfWidth = static_cast<std::uint64_t>(
      core::resampleKernelHalfWidthSamples(bandRatio));
  const auto sampleClamped = [this](std::int64_t index) noexcept {
    const auto frames = static_cast<std::int64_t>(inputFrames_);
    if (frames <= 0) return 0.0F;
    const auto clamped = std::clamp<std::int64_t>(index, 0, frames - 1);
    const auto asIndex = static_cast<std::uint64_t>(clamped);
    // The retention rule below guarantees this index is still buffered.
    if (asIndex < baseIndex_ ||
        asIndex - baseIndex_ >= static_cast<std::uint64_t>(pending_.size())) {
      return 0.0F;
    }
    return pending_[static_cast<std::size_t>(asIndex - baseIndex_)];
  };
  while (nextOutputIndex_ < outputLimit) {
    // Output index to input position. `ratio_` holds input samples per output
    // sample, the inverse of `bandRatio`, so dividing by bandRatio is the same
    // conversion stated in the direction the kernel expects.
    const auto position = static_cast<double>(nextOutputIndex_) / bandRatio;
    const auto centreSigned = static_cast<std::int64_t>(std::floor(position));
    if (centreSigned < 0) break;
    const auto centre = static_cast<std::uint64_t>(centreSigned);
    if (centre >= inputFrames_) break;
    // Without `final`, emit only once the whole kernel fits inside the input
    // received so far. Emitting earlier would clamp against the last buffered
    // sample and fabricate a signal boundary that is not the recording's real end.
    if (!final && centre + halfWidth >= inputFrames_) break;
    const auto read = [&sampleClamped](std::int64_t index) {
      return static_cast<double>(sampleClamped(index));
    };
    const auto interpolated = static_cast<float>(core::bandLimitedSampleAt(
        read, position, bandRatio,
        static_cast<std::int64_t>(inputFrames_)));
    output.push_back(quality_ == SampleRateQuality::Final
                         ? std::clamp(interpolated, -1.0F, 1.0F)
                         : interpolated);
    ++nextOutputIndex_;
    // Retain halfWidth samples of history behind the next position; everything
    // before that can no longer contribute to any future output.
    const auto nextPosition = static_cast<double>(nextOutputIndex_) / bandRatio;
    const auto nextCentre = static_cast<std::uint64_t>(std::floor(nextPosition));
    const auto keepFrom = nextCentre > halfWidth ? nextCentre - halfWidth : 0U;
    while (baseIndex_ < keepFrom && !pending_.empty()) {
      pending_.pop_front();
      ++baseIndex_;
    }
  }
  return output;
}

core::Result<std::vector<float>> StreamingSampleRateConverter::append(
    std::span<const float> source) {
  const auto valid = validate();
  if (!valid) return core::Result<std::vector<float>>{valid.error()};
  if (finished_) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::InvalidState,
        "Streaming sample-rate converter has already been finalized");
  }
  pending_.insert(pending_.end(), source.begin(), source.end());
  inputFrames_ += source.size();
  return emit(false);
}

core::Result<std::vector<float>> StreamingSampleRateConverter::finish() {
  const auto valid = validate();
  if (!valid) return core::Result<std::vector<float>>{valid.error()};
  if (finished_) return std::vector<float>{};
  finished_ = true;
  return emit(true);
}

}
