#pragma once
// Frozen developer diagnostics shared by comparisons; not production voicing inference.
#include "test_framework.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace seam::synthesis::experimental::diagnostics {
inline double energy(std::span<const float> values) {
  double result = 0.0;
  for (const auto value : values) result += static_cast<double>(value) * value;
  return result;
}
inline double periodicity(std::span<const float> values, std::uint32_t rate, double hz) {
  double result = 0.0;
  // Off-grid periods, especially at 8 kHz, need a fractional-delay oracle;
  // rounding by half a sample can decorrelate the upper formants of the SOURCE.
  const auto exactPeriod = rate / hz;
  const auto integral = static_cast<std::size_t>(std::floor(exactPeriod));
  std::array<double, 33U> kernel{};
  double kernelSum = 0.0;
  for (int tap = -16; tap <= 16; ++tap) {
    const auto distance = static_cast<double>(tap) + exactPeriod - static_cast<double>(integral);
    const auto sinc = std::abs(distance) < 1.0e-12 ? 1.0 : std::sin(std::numbers::pi * distance) / (std::numbers::pi * distance);
    const auto window = 0.5 + 0.5 * std::cos(std::numbers::pi * static_cast<double>(tap) / 17.0);
    kernel[static_cast<std::size_t>(tap + 16)] = sinc * window;
    kernelSum += sinc * window;
  }
  double cross = 0.0, first = 0.0, second = 0.0;
  for (std::size_t i = integral + 16U; i + 16U < values.size(); ++i) {
    double delayed = 0.0;
    for (std::size_t tap = 0U; tap < kernel.size(); ++tap)
      delayed += values[i - integral - 16U + tap] * kernel[tap] / kernelSum;
    cross += values[i] * delayed;
    first += static_cast<double>(values[i]) * values[i]; second += delayed * delayed;
  }
  result = cross / std::max(1.0e-24, std::sqrt(first * second));
  // Also catch a remaining octave/subharmonic rather than accepting a changed
  // periodic carrier merely because its original-period correlation fell.
  for (const auto multiple : {0.5, 1.0, 2.0, 3.0}) {
    const auto period = static_cast<std::size_t>(std::llround(rate / hz * multiple));
    const auto tolerance = std::max<std::size_t>(1U, period / 50U);
    for (auto lag = period - tolerance; lag <= period + tolerance; ++lag) {
      cross = 0.0; first = 0.0; second = 0.0;
      for (std::size_t i = lag; i < values.size(); ++i) {
        cross += static_cast<double>(values[i]) * values[i - lag];
        first += static_cast<double>(values[i]) * values[i];
        second += static_cast<double>(values[i - lag]) * values[i - lag];
      }
      result = std::max(result, cross / std::max(1.0e-24, std::sqrt(first * second)));
    }
  }
  return result;
}
inline std::vector<float> vowel(std::uint32_t rate, double hz) {
  std::vector<float> result(rate / 2U);
  const std::array<std::pair<double, double>, 3U> formants{{{650.0, 100.0}, {1150.0, 180.0}, {2450.0, 220.0}}};
  for (std::size_t harmonic = 1U; hz * static_cast<double>(harmonic) < rate * 0.45; ++harmonic) {
    const auto frequency = hz * static_cast<double>(harmonic);
    double amplitude = 0.0;
    for (const auto& [center, width] : formants) amplitude += std::exp(-0.5 * std::pow((frequency - center) / width, 2.0));
    amplitude /= std::pow(static_cast<double>(harmonic), 0.4);
    for (std::size_t i = 0U; i < result.size(); ++i)
      result[i] += static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
  }
  float peak = 0.0F;
  for (const auto sample : result) peak = std::max(peak, std::abs(sample));
  for (std::size_t i = 0U; i < result.size(); ++i) {
    const auto motion = 0.75 + 0.25 * std::sin(2.0 * std::numbers::pi * 4.0 * static_cast<double>(i) / rate);
    result[i] *= static_cast<float>(0.45 * motion / peak);
  }
  return result;
}
inline std::array<double, 5U> bands(std::span<const float> values, std::uint32_t rate) {
  std::size_t size = 256U;
  while (size < static_cast<std::size_t>(rate) * 32U / 1000U && size < 8192U) size *= 2U;
  const auto spectrum = voicebank::buildSpectrogram(values,
      {.fftSize = size, .hopSize = size / 2U, .minimumDb = -150.0F, .maximumDb = 20.0F}); CHECK(spectrum);
  constexpr std::array<double, 4U> boundaries{400.0, 900.0, 1800.0, 3200.0};
  std::array<double, 5U> result{};
  double total = 0.0;
  for (std::size_t bin = 0U; bin < spectrum.value().bins; ++bin) {
    const auto hz = static_cast<double>(bin) * rate / static_cast<double>(size);
    const auto band = static_cast<std::size_t>(std::upper_bound(boundaries.begin(), boundaries.end(), hz) - boundaries.begin());
    for (std::size_t column = 0U; column < spectrum.value().columns; ++column) {
      const auto power = std::pow(10.0, spectrum.value().at(column, bin) / 10.0);
      result[band] += power; total += power;
    }
  }
  for (auto& value : result) value /= total;
  return result;
}
}
