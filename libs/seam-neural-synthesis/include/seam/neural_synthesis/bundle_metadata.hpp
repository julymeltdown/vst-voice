#pragma once
#include "seam/neural_synthesis/model_contract.hpp"
#include "seam/synthesis/neural_bundle.hpp"

namespace seam::neural_synthesis {
struct MelFeatureSpec final {
  // B/T/F mean batch/time/features. Stored mel = multiplier * encoded
  // amplitude + offset; amplitudeScale chooses linear, ln or log10 encoding.
  std::uint32_t sampleRate, hopSize, bins;
  std::string layout, amplitudeScale;
  double multiplier, offset, minimumHz, maximumHz;
  friend bool operator==(const MelFeatureSpec&,const MelFeatureSpec&)=default;
};
// Verified declarations/vocabulary, NOT an executable admission handle. Actual
// graph input/output shapes and operators still need independent inspection.
struct NeuralBundleMetadata final {
  ModelContract model;
  NeuralVocabulary vocabulary;
  MelFeatureSpec features;
};
[[nodiscard]] core::Result<NeuralBundleMetadata> inspectNeuralBundleMetadata(
    const synthesis::FrozenNeuralBundle& bundle, std::stop_token stop = {});
}
