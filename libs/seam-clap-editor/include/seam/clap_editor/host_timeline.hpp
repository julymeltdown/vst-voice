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

}  // namespace seam::clap_editor
