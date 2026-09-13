#pragma once
#include "seam/neural_synthesis/model_contract.hpp"
#include "seam/synthesis/neural_bundle.hpp"
#include <filesystem>

namespace seam::neural_synthesis {
struct MelFeatureSpec final {
  // B/T/F mean batch/time/features. Stored mel = multiplier * encoded
  // amplitude + offset; amplitudeScale chooses linear, ln or log10 encoding.
  std::uint32_t sampleRate, hopSize, bins;
  std::string layout, amplitudeScale;
  double multiplier, offset, minimumHz, maximumHz;
  // Zero/empty means unspecified legacy v1 metadata, not an inferred default.
  std::uint32_t fftSize{0}, windowSize{0};
  std::string melFrequencyScale{};
  friend bool operator==(const MelFeatureSpec&,const MelFeatureSpec&)=default;
};
// Verified declarations/vocabulary, NOT an executable admission handle. Actual
// graph input/output shapes and operators still need independent inspection.
struct NeuralBundleMetadata final {
  ModelContract model;
  NeuralVocabulary vocabulary;
  MelFeatureSpec features;
  std::uint32_t configurationVersion{1};
  std::string stepsLayout{"scalar"};
  std::string vocoderOutput{"audio"};
};
[[nodiscard]] core::Result<NeuralBundleMetadata> inspectNeuralBundleMetadata(
    const synthesis::FrozenNeuralBundle& bundle, std::stop_token stop = {});
// Read manifest.json and its flat named assets, then freeze exact verified bytes.
// Payload bounds are not a peak-memory ceiling; no graph execution is admitted.
[[nodiscard]] core::Result<synthesis::FrozenNeuralBundle> loadNeuralBundleDirectory(
    const std::filesystem::path& directory,domain::SingerResourceIdentity identity,
    std::size_t maximumTotalBytes,std::stop_token stop = {});
}
