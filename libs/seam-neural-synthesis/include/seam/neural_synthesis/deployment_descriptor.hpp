#pragma once

#include "seam/distribution/signing.hpp"
#include "seam/neural_synthesis/neural_phrase_backend.hpp"

namespace seam::neural_synthesis {

struct NeuralDeploymentTarget final {
  std::string_view buildId;
  std::string_view platform;
  std::string_view surface;
  std::uint32_t protocolVersion{1U};
};

// Constructible only by validating a detached signature and the loaded surface's
// expected identity. The release trust owner supplies the key; banks cannot.
class VerifiedNeuralDeployment final {
public:
  [[nodiscard]] static core::Result<VerifiedNeuralDeployment> verify(
      std::string_view json,const distribution::Ed25519Signature& signature,
      const distribution::Ed25519PublicKey& trustedReleaseKey,
      const NeuralDeploymentTarget& expected);

  [[nodiscard]] core::Result<NeuralWorkerRunOptions> load(
      const void* moduleAnchor,std::stop_token stop = {}) const;
  [[nodiscard]] const std::string& contentHash() const noexcept { return contentHash_; }

private:
  VerifiedNeuralDeployment() = default;
  std::filesystem::path modulePath_;
  std::filesystem::path manifestPath_;
  std::string manifestHash_;
  std::string buildId_;
  std::string contentHash_;
  std::uint32_t protocolVersion_{1U};
};

}  // namespace seam::neural_synthesis
