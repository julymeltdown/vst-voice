#include "seam/rendering/sample_rate_converter.hpp"

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
  const auto ratio = static_cast<long double>(sourceRate) / targetRate;
  for (std::size_t index = 0U; index < output.size(); ++index) {
    const auto position = static_cast<long double>(index) * ratio;
    const auto left = std::min<std::size_t>(
        static_cast<std::size_t>(position), source.size() - 1U);
    const auto right = std::min(left + 1U, source.size() - 1U);
    const auto fraction = static_cast<float>(position - static_cast<long double>(left));
    const auto interpolated = source[left] +
                               (source[right] - source[left]) * fraction;
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
  while (nextOutputIndex_ < outputLimit) {
    const auto position = static_cast<long double>(nextOutputIndex_) * ratio_;
    const auto left = static_cast<std::uint64_t>(position);
    if (left >= inputFrames_ || (!final && left + 1U >= inputFrames_)) break;
    const auto right = std::min(left + 1U, inputFrames_ - 1U);
    const auto fraction = static_cast<float>(position - static_cast<long double>(left));
    const auto interpolated = sampleAt(left) +
                               (sampleAt(right) - sampleAt(left)) * fraction;
    output.push_back(quality_ == SampleRateQuality::Final
                         ? std::clamp(interpolated, -1.0F, 1.0F)
                         : interpolated);
    ++nextOutputIndex_;
    const auto nextPosition =
        static_cast<long double>(nextOutputIndex_) * ratio_;
    const auto nextLeft = static_cast<std::uint64_t>(nextPosition);
    while (baseIndex_ < nextLeft && !pending_.empty()) {
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
