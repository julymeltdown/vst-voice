#pragma once

#include "seam/core/result.hpp"
#include "seam/live_voice/live_resources.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace seam::live_voice {

struct ArticulationRequest final {
  std::optional<std::string_view> previousVowel;
  std::string_view targetVowel{"a"};
  std::int16_t key{60};
  std::int16_t previousKey{60};
  bool legato{false};
};

struct SegmentSelection final {
  const LiveUnitAudio* unit{nullptr};
  std::uint32_t startFrame{0U};
  std::uint32_t endFrame{0U};
};

struct ArticulationPlan final {
  SegmentSelection attack;
  std::optional<SegmentSelection> transition;
  SegmentSelection sustain;
  std::optional<SegmentSelection> release;
  bool usedTransitionFallback{false};
  std::string diagnostic;
};

class ArticulationPlanner final {
 public:
  [[nodiscard]] core::Result<ArticulationPlan> plan(
      const LiveVoicebankResources& resources,
      const ArticulationRequest& request) const;
};

}
