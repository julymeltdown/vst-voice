#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace seam::voicebank {

struct SpectrogramConfig final {
  std::size_t fftSize{512};
  std::size_t hopSize{128};
  float minimumDb{-90.0F};
  float maximumDb{-6.0F};
};

struct Spectrogram final {
  std::size_t columns{0};
  std::size_t bins{0};
  std::vector<float> decibels;

  [[nodiscard]] float at(std::size_t column, std::size_t bin) const noexcept {
    return decibels[column * bins + bin];
  }
};

[[nodiscard]] core::Result<Spectrogram> buildSpectrogram(
    std::span<const float> samples,
    SpectrogramConfig config = {});
[[nodiscard]] core::Result<void> writeSpectrogramPgm(
    const std::filesystem::path& path,
    const Spectrogram& spectrogram,
    float minimumDb = -90.0F,
    float maximumDb = -6.0F);

}  // namespace seam::voicebank
