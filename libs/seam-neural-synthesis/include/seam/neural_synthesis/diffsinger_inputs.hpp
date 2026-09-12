#pragma once

#include "seam/neural_synthesis/model_contract.hpp"

#include <stop_token>

namespace seam::neural_synthesis {

// Batch size is one. tokens/durations are int64 [1, N], f0Hz is float32
// [1, T], and steps is an int64 scalar for the inspected acoustic exporter.
struct DiffSingerAcousticInputs final {
  std::vector<std::int64_t> tokens;
  std::vector<std::int64_t> durations;
  std::vector<float> f0Hz;
  std::int64_t steps{0};
  std::uint64_t outputSampleFrames{0};
  std::uint64_t paddedSampleFrames{0};
};

// This prepares common acoustic inputs only. The worker must additionally
// validate the actual graph's input/output schema and any enabled conditioning.
// Dynamics are the request's sample-domain output gain, not model energy input.
[[nodiscard]] core::Result<DiffSingerAcousticInputs> prepareDiffSingerAcousticInputs(
    const NeuralRequest& request,const ModelContract& model,
    const NeuralVocabulary& vocabulary,std::int64_t steps,std::stop_token stop = {});

}  // namespace seam::neural_synthesis
