#pragma once

#include "seam/core/result.hpp"

#include <string>

namespace seam::domain {

enum class VoiceStyleOrigin {
  Unselected,
  Explicit,
  SoleDeclaredStyle,
  LegacyNeedsExactBankResolution,
  LegacyManifestFirst,
};

struct VoiceStyleSelection final {
  VoiceStyleOrigin origin{VoiceStyleOrigin::Unselected};
  std::string styleId;

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const VoiceStyleSelection&, const VoiceStyleSelection&) = default;
};

}
