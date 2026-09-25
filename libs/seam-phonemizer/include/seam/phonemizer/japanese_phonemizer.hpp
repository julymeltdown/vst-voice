#pragma once

#include "seam/phonemizer/phonemizer.hpp"
#include <stop_token>
#include <limits>

namespace seam::phonemizer {

// Sorted unique symbols emitted by the built-in Japanese kana adapter, plus the
// explicit event symbols accepted by Japanese phone hints.
[[nodiscard]] const std::vector<std::string>& japanesePhoneSymbols();
[[nodiscard]] core::Result<std::vector<std::string>> parseJapanesePhoneHint(std::string_view text);

class JapaneseKanaPhonemizer final : public IPhonemizer {
public:
  [[nodiscard]] domain::Language language() const noexcept override {
    return domain::Language::Japanese;
  }

  [[nodiscard]] Result phonemize(const domain::VocalRegion& region) const override;
  [[nodiscard]] core::Result<Result> phonemize(const domain::VocalRegion& region, std::stop_token stop,
      std::size_t maximumTokens = std::numeric_limits<std::size_t>::max()) const;
};

}  // namespace seam::phonemizer
