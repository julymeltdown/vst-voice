#pragma once

#include "seam/phonemizer/phonemizer.hpp"

namespace seam::phonemizer {

class JapaneseKanaPhonemizer final : public IPhonemizer {
public:
  [[nodiscard]] domain::Language language() const noexcept override {
    return domain::Language::Japanese;
  }

  [[nodiscard]] Result phonemize(const domain::VocalRegion& region) const override;
};

}  // namespace seam::phonemizer
