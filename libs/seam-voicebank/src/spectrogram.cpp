#include "seam/voicebank/spectrogram.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <numbers>

namespace seam::voicebank {
namespace {

bool powerOfTwo(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1U)) == 0;
}

void fft(std::vector<std::complex<double>>& values) {
  const auto size = values.size();
  for (std::size_t index = 1, reversed = 0; index < size; ++index) {
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0U; bit >>= 1U) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) std::swap(values[index], values[reversed]);
  }
  for (std::size_t length = 2; length <= size; length <<= 1U) {
    const auto angle = -2.0 * std::numbers::pi / static_cast<double>(length);
    const std::complex<double> step(std::cos(angle), std::sin(angle));
    for (std::size_t start = 0; start < size; start += length) {
      std::complex<double> phase(1.0, 0.0);
      const auto half = length / 2U;
      for (std::size_t offset = 0; offset < half; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + half] * phase;
        values[start + offset] = even + odd;
        values[start + offset + half] = even - odd;
        phase *= step;
      }
    }
  }
}

}  // namespace

core::Result<Spectrogram> buildSpectrogram(std::span<const float> samples,
                                           SpectrogramConfig config) {
  if (samples.empty() || !powerOfTwo(config.fftSize) || config.fftSize < 64 ||
      config.fftSize > 8192 || config.hopSize == 0 ||
      config.hopSize > config.fftSize || config.minimumDb >= config.maximumDb) {
    return core::failure<Spectrogram>(core::ErrorCode::InvalidArgument,
                                      "Spectrogram configuration is invalid");
  }
  const auto columns = samples.size() <= config.fftSize
                           ? 1U
                           : 1U + (samples.size() - config.fftSize) / config.hopSize;
  const auto bins = config.fftSize / 2U + 1U;
  Spectrogram result;
  result.columns = columns;
  result.bins = bins;
  result.decibels.resize(columns * bins, config.minimumDb);
  std::vector<std::complex<double>> spectrum(config.fftSize);

  for (std::size_t column = 0; column < columns; ++column) {
    const auto start = column * config.hopSize;
    for (std::size_t index = 0; index < config.fftSize; ++index) {
      const auto sourceIndex = start + index;
      const auto sample = sourceIndex < samples.size() && std::isfinite(samples[sourceIndex])
                              ? static_cast<double>(samples[sourceIndex])
                              : 0.0;
      const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                         static_cast<double>(config.fftSize - 1U);
      const auto window = 0.5 - 0.5 * std::cos(phase);
      spectrum[index] = std::complex<double>(sample * window, 0.0);
    }
    fft(spectrum);
    for (std::size_t bin = 0; bin < bins; ++bin) {
      const auto magnitude = std::abs(spectrum[bin]) /
                             static_cast<double>(config.fftSize);
      const auto db = 20.0 * std::log10(std::max(magnitude, 1.0e-12));
      result.decibels[column * bins + bin] = static_cast<float>(
          std::clamp(db, static_cast<double>(config.minimumDb),
                    static_cast<double>(config.maximumDb)));
    }
  }
  return result;
}

core::Result<void> writeSpectrogramPgm(const std::filesystem::path& path,
                                       const Spectrogram& spectrogram,
                                       float minimumDb,
                                       float maximumDb) {
  if (spectrogram.columns == 0 || spectrogram.bins == 0 ||
      spectrogram.decibels.size() != spectrogram.columns * spectrogram.bins ||
      minimumDb >= maximumDb) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Spectrogram image arguments are invalid");
  }
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create spectrogram output directory",
                           error.message());
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to create spectrogram image",
                         path.string());
  }
  stream << "P5\n" << spectrogram.columns << ' ' << spectrogram.bins << "\n255\n";
  for (std::size_t outputRow = 0; outputRow < spectrogram.bins; ++outputRow) {
    const auto bin = spectrogram.bins - 1U - outputRow;
    for (std::size_t column = 0; column < spectrogram.columns; ++column) {
      const auto db = spectrogram.at(column, bin);
      const auto normalized = std::clamp(
          (db - minimumDb) / (maximumDb - minimumDb), 0.0F, 1.0F);
      const auto value = static_cast<unsigned char>(
          std::lround(static_cast<double>(normalized) * 255.0));
      stream.put(static_cast<char>(value));
    }
  }
  stream.flush();
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to write spectrogram image",
                         path.string());
  }
  return core::success();
}

}  // namespace seam::voicebank
