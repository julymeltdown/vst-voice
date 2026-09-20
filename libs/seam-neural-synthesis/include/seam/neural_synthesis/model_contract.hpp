#pragma once

#include "seam/core/result.hpp"
#include "seam/neural_synthesis/worker_protocol.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <map>

namespace seam::neural_synthesis {

// Data-only importer for the pinned DiffSinger phone-to-ID JSON format.
// Preserves trained positive IDs and merged aliases; never fills ID gaps.
[[nodiscard]] core::Result<std::string> convertDiffSingerVocabulary(std::string_view json);

struct ModelContract final {
  std::string modelId;
  std::string modelVersion;
  std::string modelContentHash;
  std::string vocabularyHash;
  std::uint32_t sampleRate{48'000U};
  std::uint32_t hopSize{256U};
  std::uint8_t outputChannels{1U};
  std::uint64_t maximumFrames{4U * 1024U * 1024U};
  std::size_t maximumModelBytes{512U * 1024U * 1024U};
  // Measured per-phone defaults for the aperiodicity conditioning channel,
  // keyed by vocabulary phone symbol. They are a property of the deployed
  // model, not of the score: the bundle configuration carries them so a render
  // without drawn automation still feeds the channel the voice was trained
  // with. An empty map means the model declares no defaults, which is also
  // the only correct state for a graph that cannot consume the channel.
  std::map<std::string,float,std::less<>> breathinessDefaults;

  [[nodiscard]] core::Result<void> validate(
      const WorkerProtocolLimits& limits = {}) const;
  [[nodiscard]] core::Result<void> validateRequest(
      const NeuralRequest& request,
      const WorkerProtocolLimits& limits = {}) const;
  [[nodiscard]] core::Result<void> validatePhoneticConditioning(
      const PhoneticConditioning& conditioning,std::uint64_t frameCount,
      std::uint32_t vocabularySize,const WorkerProtocolLimits& limits = {}) const;
};

class NeuralVocabulary final {
public:
  [[nodiscard]] static core::Result<NeuralVocabulary> decode(
      std::string_view json,const ModelContract& model);
  [[nodiscard]] core::Result<std::uint32_t> tokenId(std::string_view phone) const;
  [[nodiscard]] core::Result<PhoneticConditioning> conditionScore(
      std::span<const domain::PhonemeToken> phones,std::span<const synthesis::PhonemeTimingAnchor> timing,
      time::SampleFrame origin,time::SampleFrame end,std::string_view silencePhone,
      const WorkerProtocolLimits& limits = {}) const;
  [[nodiscard]] std::uint32_t size() const noexcept { return vocabularySize_; }
  [[nodiscard]] const std::string& contentHash() const noexcept { return contentHash_; }
private:
  NeuralVocabulary() = default;
  std::string contentHash_;
  std::uint32_t vocabularySize_{0};
  std::map<std::string,std::uint32_t,std::less<>> tokens_;
};

}  // namespace seam::neural_synthesis
