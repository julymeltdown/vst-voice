#pragma once

#include "seam/core/result.hpp"

#include <string>
#include <optional>

namespace seam::domain {

enum class VoiceStyleOrigin {
  Unselected,
  Explicit,
  SoleDeclaredStyle,
  LegacyNeedsExactBankResolution,
  LegacyManifestFirst,
};

// Ordered, same-bank pair: zero is styleId and one is targetStyleId. The
// amount is the track default; accepted regional StyleBlend lanes may override
// it. Declaring a pair is intent, not proof of acoustic compatibility.
struct VoiceStyleBlend final {
  std::string targetStyleId;
  float amount{0.0F};
  friend bool operator==(const VoiceStyleBlend&, const VoiceStyleBlend&) = default;
};

struct VoiceStyleSelection final {
  VoiceStyleOrigin origin{VoiceStyleOrigin::Unselected};
  std::string styleId;
  std::optional<VoiceStyleBlend> blend{};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const VoiceStyleSelection&, const VoiceStyleSelection&) = default;
};

}
