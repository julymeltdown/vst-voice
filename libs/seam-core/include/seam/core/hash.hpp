#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace seam::core {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

inline std::uint64_t fnv1a(std::span<const std::byte> bytes,
                           std::uint64_t seed = kFnvOffsetBasis) noexcept {
  auto hash = seed;
  for (const auto byte : bytes) {
    hash ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(byte));
    hash *= kFnvPrime;
  }
  return hash;
}

inline std::uint64_t fnv1a(std::string_view text,
                           std::uint64_t seed = kFnvOffsetBasis) noexcept {
  auto hash = seed;
  for (const auto character : text) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
    hash *= kFnvPrime;
  }
  return hash;
}

inline std::uint64_t hashCombine(std::uint64_t current, std::uint64_t value) noexcept {
  current ^= value + 0x9e3779b97f4a7c15ull + (current << 6u) + (current >> 2u);
  return current;
}

}  // namespace seam::core
