#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <string>
#include <vector>

namespace seam::neural_synthesis {

struct WorkerProtocolLimits final {
  std::size_t maximumFrameBytes{64U * 1024U * 1024U};
  std::size_t maximumMetadataBytes{64U * 1024U};
  std::size_t maximumModelIdBytes{256U};
  std::size_t maximumHashBytes{64U};
  std::uint64_t maximumFrames{4U * 1024U * 1024U};
  std::uint32_t maximumSampleRate{384'000U};
  std::uint8_t maximumChannels{8U};
};

struct NeuralPhonemeSpan final {
  std::uint32_t tokenId{0U};
  std::uint64_t startFrame{0U}, endFrame{0U};
  friend bool operator==(const NeuralPhonemeSpan&,const NeuralPhonemeSpan&)=default;
};
// Vocabulary-bound phonetic conditioning for the next request protocol.
// Frame units are output PCM frames, not feature hops. Silence is an explicit
// vocabulary token; gaps are never filled by guessing pronunciation.
struct PhoneticConditioning final {
  std::string vocabularyHash;
  std::vector<NeuralPhonemeSpan> spans;
  [[nodiscard]] core::Result<void> validate(std::uint64_t frameCount,
      std::uint32_t vocabularySize,const WorkerProtocolLimits& limits = {}) const;
  friend bool operator==(const PhoneticConditioning&,const PhoneticConditioning&)=default;
};

struct NeuralRequest final {
  std::uint64_t requestId{0U};
  std::string modelId;
  std::string modelVersion;
  std::string modelContentHash;
  std::string pronunciationHash;
  std::uint32_t sampleRate{48'000U};
  std::uint8_t channels{1U};
  std::uint64_t frameCount{0U};
  std::vector<float> f0Hz;
  std::vector<float> dynamics;
  std::vector<float> breathiness;
  std::optional<PhoneticConditioning> conditioning{};
  std::uint32_t vocabularySize{0U};
  // Nonempty selects metadata v3. Identity of the canonical frozen bundle;
  // v1/v2 retain their original semantics and cannot carry this field.
  std::string bundleContentHash{};

  [[nodiscard]] core::Result<void> validate(
      const WorkerProtocolLimits& limits = {}) const;
  friend bool operator==(const NeuralRequest&, const NeuralRequest&) = default;
};

struct NeuralResponse final {
  std::uint64_t requestId{0U};
  std::string backendId;
  std::string modelContentHash;
  std::uint32_t sampleRate{48'000U};
  std::uint8_t channels{1U};
  std::uint64_t frameCount{0U};
  std::vector<float> pcm;
  // SHA-256 of the complete canonical request frame; required for conditioned responses.
  std::string requestContentHash{};
  std::string bundleContentHash{};

  [[nodiscard]] core::Result<void> validate(
      const WorkerProtocolLimits& limits = {}) const;
  friend bool operator==(const NeuralResponse&, const NeuralResponse&) = default;
};

// Frames are length-prefixed and contain bounded JSON metadata followed by a
// little-endian Float32 payload.  Decoders validate every length before
// allocating or interpreting model output.
[[nodiscard]] core::Result<std::vector<std::byte>> encodeRequest(
    const NeuralRequest& request, WorkerProtocolLimits limits = {});
[[nodiscard]] core::Result<NeuralRequest> decodeRequest(
    std::span<const std::byte> frame, WorkerProtocolLimits limits = {});
[[nodiscard]] core::Result<std::vector<std::byte>> encodeResponse(
    const NeuralResponse& response, WorkerProtocolLimits limits = {});
[[nodiscard]] core::Result<NeuralResponse> decodeResponse(
    std::span<const std::byte> frame, WorkerProtocolLimits limits = {});

}  // namespace seam::neural_synthesis
