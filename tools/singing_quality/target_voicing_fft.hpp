#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace seam::synthesis::experimental::detail {
// Internal radix-2 transform. Callers validate their bounded power-of-two size.
inline void fft(std::vector<std::complex<double>>& values, bool inverse) {
  const auto size = values.size();
  for (std::size_t index = 1U, reversed = 0U; index < size; ++index) {
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0U; bit >>= 1U) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) std::swap(values[index], values[reversed]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    const auto angle = (inverse ? 1.0 : -1.0) * 2.0 * std::numbers::pi / static_cast<double>(length);
    const std::complex<double> step{std::cos(angle), std::sin(angle)};
    for (std::size_t start = 0U; start < size; start += length) {
      std::complex<double> phase{1.0, 0.0};
      const auto half = length / 2U;
      for (std::size_t offset = 0U; offset < half; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + half] * phase;
        values[start + offset] = even + odd;
        values[start + offset + half] = even - odd;
        phase *= step;
      }
    }
  }
  if (inverse) {
    const auto scale = 1.0 / static_cast<double>(size);
    for (auto& value : values) value *= scale;
  }
}
}
