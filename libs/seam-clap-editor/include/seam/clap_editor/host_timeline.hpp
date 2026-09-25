#pragma once

#include "seam/domain/project.hpp"

#include <cstdint>

namespace seam::clap_editor {

struct HostTimelineState final {
  bool playing{true};
  bool hasSeconds{false};
  double seconds{0.0};
  bool hasBeats{false};
  double beats{0.0};
  bool hasTempo{false};
  double tempo{120.0};
  // CLAP tempo_inc is the per-sample tempo slope until the next time-info event.
  // The offline map is piecewise constant today, so preserve and reject ramps.
  bool hasTempoRamp{false};
  bool captureIncomplete{false};
  bool loopActive{false};
  bool loopHasSeconds{false};
  double loopStartSeconds{0.0};
  double loopEndSeconds{0.0};
  bool loopHasBeats{false};
  double loopStartBeats{0.0};
  double loopEndBeats{0.0};
  bool hasTimeSignature{false};
  std::uint16_t numerator{4U};
  std::uint16_t denominator{4U};
};

struct HostFramePosition final {
  bool audible{false};
  std::uint64_t sourceFrame{0U};
  double hostSeconds{0.0};
};

class HostTimelineMapper final {
public:
  [[nodiscard]] static HostFramePosition map(
      const HostTimelineState& state,
      double projectOffsetSeconds,
      double defaultTempo,
      double sampleRate,
      std::uint32_t frameOffset = 0U) noexcept;
  [[nodiscard]] static HostFramePosition map(
      const HostTimelineState& state,
      const domain::Project& project,
      double sampleRate,
      std::uint32_t frameOffset = 0U) noexcept;
};

// Allocation-free cursor for mapping one process block with sample-offset transport
// corrections. Callers apply sorted host events before mapping each corresponding frame.
class HostTimelineBlockCursor final {
public:
  explicit HostTimelineBlockCursor(HostTimelineState blockStart) noexcept
      : state_(blockStart) {}

  void observe(const HostTimelineState& state, std::uint32_t sampleOffset) noexcept;
  void markIncomplete(std::uint32_t sampleOffset) noexcept;
  [[nodiscard]] HostFramePosition mapAt(
      std::uint32_t sampleOffset,
      double projectOffsetSeconds,
      double defaultTempo,
      double sampleRate) const noexcept;

private:
  HostTimelineState state_{};
  std::uint32_t anchorOffset_{0U};
  bool reliable_{true};
};

}  // namespace seam::clap_editor
