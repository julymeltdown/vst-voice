#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace seam::voicebank {

struct PitchMarkGenerationConfig final {
  PitchConfig pitch{};
  double refinementRadiusPeriods{0.25};
  float minimumConfidence{0.35F};
};

[[nodiscard]] core::Result<std::vector<PitchMark>> generatePitchMarks(
    std::span<const float> samples,
    std::uint32_t sampleRate,
    time::SampleFrame rangeStart,
    time::SampleFrame rangeEnd,
    PitchMarkGenerationConfig config = {});

[[nodiscard]] core::Result<void> validatePitchMarks(
    std::span<const PitchMark> marks,
    time::SampleFrame rangeStart,
    time::SampleFrame rangeEnd);

class PitchMarkEditor final {
public:
  [[nodiscard]] core::Result<void> add(std::vector<PitchMark>& marks,
                                       PitchMark mark,
                                       time::SampleFrame rangeStart,
                                       time::SampleFrame rangeEnd) const;
  [[nodiscard]] core::Result<void> move(std::vector<PitchMark>& marks,
                                        std::size_t index,
                                        time::SampleFrame frame,
                                        time::SampleFrame rangeStart,
                                        time::SampleFrame rangeEnd) const;
  [[nodiscard]] core::Result<void> remove(std::vector<PitchMark>& marks,
                                          std::size_t index,
                                          time::SampleFrame rangeStart,
                                          time::SampleFrame rangeEnd) const;
  [[nodiscard]] core::Result<void> setLocked(std::vector<PitchMark>& marks,
                                             std::size_t index,
                                             bool locked) const;
};

}  // namespace seam::voicebank
