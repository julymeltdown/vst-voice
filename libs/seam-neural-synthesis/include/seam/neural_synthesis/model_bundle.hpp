#pragma once

#include "seam/neural_synthesis/bundle_metadata.hpp"

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

namespace seam::neural_synthesis {

// Identity of one prepared execution. The bundle digest is the canonical
// manifest digest that also bounds its assets; the steps value is part of the
// identity because it changes the rendered result for identical input.
struct NeuralExecutionIdentity final {
  std::string modelId, modelVersion, bundleContentHash;
  std::uint32_t configurationVersion{0U};
  std::int64_t inferenceSteps{0};
  friend bool operator==(const NeuralExecutionIdentity&,const NeuralExecutionIdentity&)=default;
};

// Immutable, application-built preparation of one neural model bundle.
//
// Admission verifies the frozen manifest, both graphs, the vocabulary and the
// declared execution identity once, outside the audio callback. Every phrase
// then shares the same immutable backing instead of copying a model per
// snapshot. This is not operator-level graph admission and not an OS sandbox:
// the selected first-party child re-admits the exact bytes it executes.
class AdmittedNeuralBundle final {
public:
  [[nodiscard]] static core::Result<AdmittedNeuralBundle> admit(
      synthesis::FrozenNeuralBundle bundle,std::uint64_t maximumFrames,
      std::int64_t inferenceSteps,std::stop_token stop = {});
  [[nodiscard]] bool valid() const noexcept {return static_cast<bool>(data_);}
  [[nodiscard]] const synthesis::FrozenNeuralBundle& bundle() const noexcept {return data_->bundle;}
  [[nodiscard]] const NeuralBundleMetadata& metadata() const noexcept {return data_->metadata;}
  [[nodiscard]] const NeuralExecutionIdentity& execution() const noexcept {return data_->execution;}
  // Retained admitted bytes, valid only while this handle reports valid().
  [[nodiscard]] const std::shared_ptr<const synthesis::FrozenSingerData>& acoustic() const noexcept {return data_->acoustic;}
  [[nodiscard]] const std::shared_ptr<const synthesis::FrozenSingerData>& vocoder() const noexcept {return data_->vocoder;}
  [[nodiscard]] const std::shared_ptr<const synthesis::FrozenSingerData>& vocabulary() const noexcept {return data_->vocabulary;}
private:
  struct Data final {
    Data(synthesis::FrozenNeuralBundle value,NeuralBundleMetadata inspected)
        :bundle(std::move(value)),metadata(std::move(inspected)) {}
    synthesis::FrozenNeuralBundle bundle;
    NeuralBundleMetadata metadata;
    NeuralExecutionIdentity execution;
    std::shared_ptr<const synthesis::FrozenSingerData> acoustic,vocoder,vocabulary;
  };
  explicit AdmittedNeuralBundle(std::shared_ptr<const Data> data):data_(std::move(data)) {}
  std::shared_ptr<const Data> data_;
};

}  // namespace seam::neural_synthesis
