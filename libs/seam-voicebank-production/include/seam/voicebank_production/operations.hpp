#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace seam::voicebank_production {

struct OperationRequest final {
  OperationKind kind{OperationKind::Downmix};
  std::uint16_t channelIndex{0U};
  std::uint32_t targetSampleRate{48000U};
  float targetPeak{0.9F};
  std::size_t startFrame{0U};
  std::size_t endFrame{0U};
};

[[nodiscard]] core::Result<voicebank::AudioBuffer> applyOperation(
    const voicebank::AudioBuffer& input, const OperationRequest& request);
[[nodiscard]] std::map<std::string, std::string, std::less<>>
operationParameters(const OperationRequest& request);

// The implementation identity this build records in a derived revision's
// "method" parameter for an operation kind, or empty when the operation is exact
// arithmetic with no algorithm choice (channel selection, gain, trim, segment).
[[nodiscard]] std::string_view currentOperationMethod(OperationKind kind) noexcept;

// Refuses a derived revision whose output came from an implementation this build
// no longer runs, so that output cannot silently stand in for what the current
// code would produce. The motivating case is "linear-v1" resampling, which folded
// energy above the new Nyquist back into the band: a 10 kHz tone resampled from
// 48 kHz to 16 kHz survived at full level near 6 kHz, as partials nobody sang.
// A revision that records no method for an operation that has one is refused
// too, because its provenance cannot be established.
//
// The raw take is preserved, so the remedy is to regenerate the derivative from
// it with the current operation. The old output stays in history; it is never
// relabelled as current.
[[nodiscard]] core::Result<void> requireCurrentOperation(const DerivedRevision& revision);

}
