#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace seam::core {

// A small deterministic FNV-1a hash used for cache keys and stable UI IDs.
// It is intentionally not a cryptographic integrity primitive.
class StableHash64 final {
public:
  constexpr StableHash64() noexcept = default;

  void addBytes(std::span<const std::byte> bytes) noexcept {
    for (const auto value : bytes) {
      state_ ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(value));
      state_ *= kPrime;
    }
  }

  void addString(std::string_view value) noexcept {
    addUnsigned(static_cast<std::uint64_t>(value.size()));
    addBytes(std::as_bytes(std::span{value.data(), value.size()}));
  }

  void add(bool value) noexcept {
    addUnsigned(value ? 1U : 0U);
  }

  template <typename Integer>
    requires(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>)
  void add(Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    addUnsigned(static_cast<std::uint64_t>(static_cast<Unsigned>(value)));
  }

  template <typename Enumeration>
    requires std::is_enum_v<Enumeration>
  void add(Enumeration value) noexcept {
    using Raw = std::underlying_type_t<Enumeration>;
    using Unsigned = std::make_unsigned_t<Raw>;
    addUnsigned(static_cast<std::uint64_t>(static_cast<Unsigned>(value)));
  }

  void add(float value) noexcept {
    addUnsigned(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value)));
  }

  void add(double value) noexcept {
    addUnsigned(std::bit_cast<std::uint64_t>(value));
  }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return state_; }

  [[nodiscard]] std::string hex() const {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << state_;
    return stream.str();
  }

private:
  void addUnsigned(std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
      const auto byte = static_cast<unsigned char>((value >> (index * 8U)) & 0xffU);
      state_ ^= static_cast<std::uint64_t>(byte);
      state_ *= kPrime;
    }
  }

  static constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  static constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t state_{kOffset};
};

inline bool isStableHashKey(std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U) return false;
  for (const auto character : value) {
    const bool digit = character >= '0' && character <= '9';
    const bool lower = character >= 'a' && character <= 'f';
    if (!digit && !lower) return false;
  }
  return true;
}

}  // namespace seam::core
