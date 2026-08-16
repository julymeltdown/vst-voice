#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace seam::synthesis {

enum class TimingIssueCode {
  MissingNote,
  NegativePreutterance,
  ShortDestination,
  UnitOverlap,
};

struct TimingIssue final {
  TimingIssueCode code{TimingIssueCode::ShortDestination};
  std::string unitId;
  std::string message;
};

struct TimedUnitPlacement final {
  std::string unitId;
  std::size_t tokenStart{0};
  std::size_t tokenCount{0};
  std::int32_t targetMidi{60};
  time::SampleFrame noteOn{0};
  time::SampleFrame destinationStart{0};
  time::SampleFrame destinationEnd{0};
  time::SampleFrame desiredVowelOnset{0};
};

struct TimingPlan final {
  std::vector<TimedUnitPlacement> placements;
  std::vector<TimingIssue> issues;
  time::SampleFrame startFrame{0};
  time::SampleFrame endFrame{0};
};

class TimingSolver final {
public:
  [[nodiscard]] core::Result<TimingPlan> solve(
      const domain::Project& project,
      const domain::VocalRegion& region,
      std::span<const domain::PhonemeToken> tokens,
      const UnitPlan& unitPlan,
      const voicebank::Manifest& manifest,
      std::uint32_t outputSampleRate) const;
};

}  // namespace seam::synthesis
