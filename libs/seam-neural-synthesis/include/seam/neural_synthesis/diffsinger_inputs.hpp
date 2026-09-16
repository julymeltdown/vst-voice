#pragma once

#include "seam/neural_synthesis/model_contract.hpp"

#include <stop_token>

namespace seam::neural_synthesis {

// Algorithm revision of the conversion below. Any change to token/duration
// quantization, F0 ownership or padding changes rendered audio, so render
// identity must include this value.
inline constexpr std::uint32_t kDiffSingerInputRevision = 2U;

// Batch size is one. tokens/durations are int64 [1, N], f0Hz is float32
// [1, T], and steps is an int64 scalar for the inspected acoustic exporter.
struct DiffSingerAcousticInputs final {
  std::vector<std::int64_t> tokens;
  std::vector<std::int64_t> durations;
  std::vector<float> f0Hz;
  std::vector<float> breathiness;
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

// Mono profile output: validate the entire padded vocoder buffer, trim only the
// final partial hop, then apply sample-domain dynamics exactly once. Reject
// nonfinite/out-of-range output instead of silently clipping it.
[[nodiscard]] core::Result<std::vector<float>> finalizeDiffSingerAudio(
    const NeuralRequest& request,const ModelContract& model,
    std::span<const float> paddedAudio,std::stop_token stop = {});

// Worker-side completion: produces final gained PCM and binds it to the exact
// canonical request. The receiving backend must not apply dynamics again.
[[nodiscard]] core::Result<NeuralResponse> finalizeDiffSingerResponse(
    const NeuralRequest& request,const ModelContract& model,
    std::span<const float> paddedAudio,std::string backendId,std::stop_token stop = {});

}  // namespace seam::neural_synthesis
