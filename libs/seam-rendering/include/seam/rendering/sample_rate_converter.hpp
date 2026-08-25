#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace seam::rendering {

enum class SampleRateQuality { Preview, Final };

class SampleRateConverter final {
public:
  [[nodiscard]] static core::Result<std::vector<float>> convert(
      std::span<const float> source, std::uint32_t sourceRate,
      std::uint32_t targetRate,
      SampleRateQuality quality = SampleRateQuality::Preview);
};

class StreamingSampleRateConverter final {
public:
  StreamingSampleRateConverter(std::uint32_t sourceRate,
                               std::uint32_t targetRate,
                               SampleRateQuality quality =
                                   SampleRateQuality::Preview)
      : sourceRate_(sourceRate), targetRate_(targetRate), quality_(quality) {}

  [[nodiscard]] core::Result<std::vector<float>> append(
      std::span<const float> source);
  [[nodiscard]] core::Result<std::vector<float>> finish();

private:
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] std::vector<float> emit(bool final);
  [[nodiscard]] float sampleAt(std::uint64_t index) const noexcept {
    return pending_[static_cast<std::size_t>(index - baseIndex_)];
  }

  std::uint32_t sourceRate_{0U};
  std::uint32_t targetRate_{0U};
  SampleRateQuality quality_{SampleRateQuality::Preview};
  long double ratio_{0.0L};
  std::uint64_t inputFrames_{0U};
  std::uint64_t nextOutputIndex_{0U};
  std::uint64_t baseIndex_{0U};
  std::deque<float> pending_;
  bool finished_{false};
};

}
