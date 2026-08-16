#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace seam::voicebank {

struct WaveformBucket final {
  float minimum{0.0F};
  float maximum{0.0F};
  float rms{0.0F};

  friend bool operator==(const WaveformBucket&, const WaveformBucket&) = default;
};

struct WaveformLevel final {
  std::size_t framesPerBucket{1};
  std::vector<WaveformBucket> buckets;
};

class WaveformPyramid final {
public:
  [[nodiscard]] static core::Result<WaveformPyramid> build(
      std::span<const float> samples,
      std::size_t baseFramesPerBucket = 64,
      std::size_t maximumLevels = 16);

  [[nodiscard]] const std::vector<WaveformLevel>& levels() const noexcept { return levels_; }
  [[nodiscard]] const WaveformLevel& levelFor(double framesPerPixel) const noexcept;

private:
  std::vector<WaveformLevel> levels_;
};

[[nodiscard]] core::Result<void> writeWaveformSvg(
    const std::filesystem::path& path,
    const WaveformLevel& level,
    double width,
    double height,
    std::string_view title);

}  // namespace seam::voicebank
