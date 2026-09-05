#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <variant>

namespace seam::domain {

enum class PerformanceChannel {
  Pitch, Timing, Dynamics, Breathiness, Tension, Airiness,
  Formant, Gender, StyleBlend, Growl, Attack, Release,
};

enum class ManualPerformanceMode { Replace, PitchOffset };

struct PerformanceRevision final {
  std::uint64_t musical{0U};
  std::uint64_t pronunciation{0U};
  std::uint64_t ownership{0U};

  friend bool operator==(const PerformanceRevision&, const PerformanceRevision&) = default;
};

struct PerformanceTimeRange final {
  time::Tick startTick;
  time::Tick endTick;

  [[nodiscard]] bool contains(time::Tick tick) const noexcept;
  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const PerformanceTimeRange&, const PerformanceTimeRange&) = default;
};

using PerformanceScope = std::variant<NoteId, PerformanceTimeRange>;

struct ManualPerformanceOwnership final {
  PerformanceChannel channel{PerformanceChannel::Pitch};
  PerformanceScope scope{NoteId{}};
  ManualPerformanceMode mode{ManualPerformanceMode::Replace};
  PerformanceRevision revision;

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] bool appliesTo(NoteId noteId, time::Tick tick) const noexcept;

  friend bool operator==(const ManualPerformanceOwnership&,
                         const ManualPerformanceOwnership&) = default;
};

[[nodiscard]] core::Result<void> validatePerformanceAcceptanceRevision(
    PerformanceRevision captured, PerformanceRevision current);

}
