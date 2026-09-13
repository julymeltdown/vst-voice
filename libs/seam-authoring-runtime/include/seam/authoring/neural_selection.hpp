#pragma once

#include "seam/authoring/neural_phrase_runner.hpp"
#include "seam/authoring/neural_resource_registry.hpp"
#include "seam/core/result.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/domain/project.hpp"
#include "seam/neural_synthesis/deployment_descriptor.hpp"
#include "seam/rendering/project_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>

namespace seam::authoring {

// What one surface is allowed to run, declared by that surface. A project or
// voicebank never supplies any of this: it only names a model bundle identity
// that must already be installed. The signed descriptor, the release key, the
// helper budgets and the execution provenance all come from the surface's own
// deployment resources, exactly as the installer takes its trust roots from the
// application rather than from an installed bank.
struct NeuralSelectionSurface final {
  // Absolute path to the surface's own signed deployment descriptor. It is read
  // with the same bound the verifier enforces, so a large file cannot be used to
  // make selection allocate.
  std::filesystem::path deploymentDescriptor;
  distribution::Ed25519Signature deploymentSignature{};
  distribution::Ed25519PublicKey trustedReleaseKey{};
  // Expected deployment target. The protocol version is not configurable: the
  // bundle launch contract is the only admitted production contract.
  std::string buildId, platform, surface;
  const void* moduleAnchor{nullptr};
  // Declared provenance for prepared snapshots. The application states which
  // worker, runtime and provider it ships; both are hashed into the render cache
  // identity and published, so they must never be inferred from bank metadata.
  rendering::NeuralRenderProvenance provenance{};
  std::size_t maximumBundleBytes{0U};
  std::uint64_t maximumFrames{0U};
  std::int64_t inferenceSteps{0};
  // Best-effort process limits for the selected helper. The signed package owns
  // the helper path and its expected digest, but it deliberately states no
  // resource limits: the surface selects measured budgets, so a bank cannot
  // raise them.
  std::size_t maximumResidentBytes{0U};
  std::chrono::milliseconds maximumCpuTime{0};
  std::chrono::milliseconds helperTimeout{0};
  std::string silencePhone{"SP"};
};

// Application-owned neural selection for the ordinary render path.
//
// One service is created per surface: it verifies the surface's own deployment
// once, resolves the helper once, and then turns each saved neural resource
// reference into the source the renderer and export owners already consume. It
// refuses an unknown identity instead of substituting a different voice, and it
// never edits a project.
class NeuralSelectionService final {
public:
  [[nodiscard]] static core::Result<NeuralSelectionService> create(
      NeuralSelectionSurface surface,std::stop_token stop = {});

  // Resolve one saved reference through the installed-resource index, admit its
  // bundle and prepare the runner. Cancellation before construction leaves no
  // runner and no admitted handle.
  [[nodiscard]] core::Result<rendering::TrackNeuralSource> select(
      domain::TrackId trackId,const domain::NeuralResourceReference& saved,
      const NeuralResourceRegistry& registry,std::stop_token stop = {}) const;

  // Digest of the verified deployment descriptor that selected this helper.
  [[nodiscard]] const std::string& deploymentContentHash() const noexcept {return deploymentHash_;}
  [[nodiscard]] const std::string& buildId() const noexcept {return surface_.buildId;}
  [[nodiscard]] const neural_synthesis::NeuralWorkerRunOptions& worker() const noexcept {return worker_;}

private:
  NeuralSelectionService(NeuralSelectionSurface surface,
      neural_synthesis::NeuralWorkerRunOptions worker,std::string deploymentHash)
      :surface_(std::move(surface)),worker_(std::move(worker)),deploymentHash_(std::move(deploymentHash)) {}

  NeuralSelectionSurface surface_;
  neural_synthesis::NeuralWorkerRunOptions worker_;
  std::string deploymentHash_;
};

}  // namespace seam::authoring
