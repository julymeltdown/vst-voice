#pragma once

#include "seam/domain/ids.hpp"
#include "seam/time/tick.hpp"
#include "seam/ui/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace seam::ui {

struct NoteVisualLayoutItem final {
  domain::NoteId noteId;
  std::uint8_t midiKey{60U};
  time::Tick start;
  time::Tick end;
  Rect timelineBounds;
};

struct NoteVisualLayout final {
  Rect paintBounds;
  Rect hitBounds;
  std::size_t groupIndex{0U};
  std::size_t groupMemberCount{1U};
  std::size_t bandIndex{0U};
  std::size_t visibleBandCount{1U};
  std::size_t hiddenMemberCount{0U};
  bool hiddenByDensity{false};
  bool drawsOverflowIndicator{false};

};

[[nodiscard]] std::vector<NoteVisualLayout> layoutNoteVisuals(
    const std::vector<NoteVisualLayoutItem>& items,
    double minimumHitWidth = 8.0) noexcept;

}
