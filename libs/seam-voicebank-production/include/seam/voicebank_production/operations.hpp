#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

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

}
