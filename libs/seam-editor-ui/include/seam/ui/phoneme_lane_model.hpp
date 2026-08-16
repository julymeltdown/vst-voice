#pragma once

#include "seam/phonemizer/phonemizer.hpp"
#include "seam/ui/geometry.hpp"
#include "seam/ui/piano_roll_model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace seam::ui {

struct PhonemeVisual final {
  domain::PhonemeKey key;
  std::string symbol;
  domain::PhonemeRole role{domain::PhonemeRole::Nucleus};
  Rect bounds;
  bool locked{false};
  bool timingOverridden{false};
};

class PhonemeLaneModel final {
public:
  void rebuild(const PianoRollModel& pianoRoll,
               const phonemizer::Result& phonemes,
               double laneTop,
               double laneHeight);

  [[nodiscard]] const std::vector<PhonemeVisual>& visuals() const noexcept {
    return visuals_;
  }
  [[nodiscard]] std::optional<domain::PhonemeKey> hitTest(Point point) const;

private:
  std::vector<PhonemeVisual> visuals_;
};

}  // namespace seam::ui
