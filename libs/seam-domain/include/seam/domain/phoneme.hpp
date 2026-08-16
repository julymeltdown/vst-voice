#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"
#include "seam/time/tick.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace seam::domain {

enum class PhonemeRole {
  Onset,
  Nucleus,
  Coda,
  Geminate,
  Breath,
  Silence,
};

struct PhonemeKey final {
  NoteId noteId;
  std::uint16_t ordinal{0};

  [[nodiscard]] std::string toString() const;

  friend bool operator==(const PhonemeKey&, const PhonemeKey&) = default;
  friend auto operator<=>(const PhonemeKey&, const PhonemeKey&) = default;
};

struct PhonemeTiming final {
  std::optional<time::Microseconds> startOffset;
  std::optional<time::Microseconds> endOffset;

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const PhonemeTiming&, const PhonemeTiming&) = default;
};

struct PhonemeOverride final {
  PhonemeKey key;
  std::optional<std::string> symbol;
  PhonemeTiming timing;
  bool locked{false};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const PhonemeOverride&, const PhonemeOverride&) = default;
};

struct PhonemeToken final {
  PhonemeKey key;
  std::string symbol;
  PhonemeRole role{PhonemeRole::Nucleus};
  bool voiced{true};
  PhonemeTiming timing;
  bool locked{false};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const PhonemeToken&, const PhonemeToken&) = default;
};

[[nodiscard]] std::string_view phonemeRoleName(PhonemeRole role) noexcept;
[[nodiscard]] PhonemeRole parsePhonemeRole(std::string_view value) noexcept;

}  // namespace seam::domain
