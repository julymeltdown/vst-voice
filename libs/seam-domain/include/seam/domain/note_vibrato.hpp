#pragma once

#include "seam/core/result.hpp"

namespace seam::domain {

struct NoteVibrato final {
  bool enabled{false};
  float startFraction{0.65F};
  float fadeInFraction{0.1F};
  float fadeOutFraction{0.1F};
  float depthCents{50.0F};
  float periodMilliseconds{180.0F};
  float phaseTurns{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const NoteVibrato&, const NoteVibrato&) = default;
};

}
