#pragma once

#include "seam/phonemizer/phonemizer.hpp"

#include <limits>
#include <stop_token>

namespace seam::phonemizer {

// Inventory revision 2 distinguishes eo/o, eu/u, ae/e and ch/cch/chh.
// Generated codas retain Coda roles. The rule adapter has no morphological
// lexicon; lexical exceptions and native-speaker qualification remain needed.

[[nodiscard]] core::Result<std::vector<std::string>> parseKoreanPhoneHint(
    std::string_view text);

class KoreanHangulPhonemizer final : public IPhonemizer {
public:
  [[nodiscard]] domain::Language language() const noexcept override {
    return domain::Language::Korean;
  }

  [[nodiscard]] Result phonemize(const domain::VocalRegion& region) const override;
  [[nodiscard]] core::Result<Result> phonemize(
      const domain::VocalRegion& region, std::stop_token stop,
      std::size_t maximumTokens = std::numeric_limits<std::size_t>::max()) const;
};

}  // namespace seam::phonemizer
