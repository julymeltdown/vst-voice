#include "seam/authoring/neural_selection.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/neural_synthesis/model_bundle.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace seam::authoring {
namespace {

// The deployment descriptor is verified from these bytes; the same bound is used
// here so a large or unreadable file fails as a read error rather than as a
// schema error after allocation.
constexpr std::size_t kMaximumDescriptorBytes{16U*1024U};
constexpr std::size_t kMaximumBundleBytesLimit{512U*1024U*1024U};
constexpr std::uint64_t kMaximumFramesLimit{4U*1024U*1024U};
constexpr std::int64_t kMaximumInferenceSteps{1000};

bool printable(std::string_view value,std::size_t limit) noexcept {
  if (value.empty() || value.size()>limit) return false;
  return std::all_of(value.begin(),value.end(),[](char character) {
    return static_cast<unsigned char>(character)>=32U && static_cast<unsigned char>(character)<127U;
  });
}

}  // namespace

core::Result<NeuralSelectionService> NeuralSelectionService::create(
    NeuralSelectionSurface surface,std::stop_token stop) {
  using Output=NeuralSelectionService;
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural selection was cancelled");};
  if (stop.stop_requested()) return cancelled();
  if (surface.deploymentDescriptor.empty() || !surface.deploymentDescriptor.is_absolute() ||
      surface.moduleAnchor==nullptr || !printable(surface.buildId,256U) ||
      !printable(surface.platform,64U) || !printable(surface.surface,64U) ||
      surface.maximumBundleBytes==0U || surface.maximumBundleBytes>kMaximumBundleBytesLimit ||
      surface.maximumFrames==0U || surface.maximumFrames>kMaximumFramesLimit ||
      surface.inferenceSteps<1 || surface.inferenceSteps>kMaximumInferenceSteps ||
      surface.maximumResidentBytes==0U || surface.maximumCpuTime.count()<=0 ||
      surface.helperTimeout.count()<=0 ||
      surface.silencePhone.empty() || surface.silencePhone.size()>64U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Neural selection requires an absolute deployment descriptor, a module anchor "
        "and explicit bounded bundle, frame and process budgets");
  const auto provenance=surface.provenance.validate();
  if (!provenance) return core::Result<Output>{provenance.error()};
  const auto descriptor=core::readTextFileLimited(surface.deploymentDescriptor,kMaximumDescriptorBytes);
  if (!descriptor) return core::Result<Output>{descriptor.error()};
  if (stop.stop_requested()) return cancelled();
  const auto verified=neural_synthesis::VerifiedNeuralDeployment::verify(descriptor.value(),
      surface.deploymentSignature,surface.trustedReleaseKey,
      neural_synthesis::NeuralDeploymentTarget{surface.buildId,surface.platform,surface.surface,3U});
  if (!verified) return core::Result<Output>{verified.error()};
  // Loading re-derives the helper path from the loaded module and checks the
  // signed manifest digest, so a bank cannot redirect execution even if it could
  // name a path.
  const auto loaded=verified.value().load(surface.moduleAnchor,stop);
  if (!loaded) return core::Result<Output>{loaded.error()};
  // The package declares the helper and its digest; the surface declares the
  // process budgets, because the package format deliberately carries none.
  auto worker=loaded.value();
  worker.maximumResidentBytes=surface.maximumResidentBytes;
  worker.maximumCpuTime=surface.maximumCpuTime;
  worker.timeout=surface.helperTimeout;
  if (stop.stop_requested()) return cancelled();
  return core::success(Output{std::move(surface),std::move(worker),verified.value().contentHash()});
}

core::Result<rendering::TrackNeuralSource> NeuralSelectionService::select(
    domain::TrackId trackId,const domain::NeuralResourceReference& saved,
    const NeuralResourceRegistry& registry,std::stop_token stop) const {
  using Output=rendering::TrackNeuralSource;
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural selection was cancelled");};
  if (stop.stop_requested()) return cancelled();
  const auto directory=registry.resolve(saved);
  if (!directory) return core::Result<Output>{directory.error()};
  if (stop.stop_requested()) return cancelled();
  auto frozen=neural_synthesis::loadNeuralBundleDirectory(directory.value(),saved.resource,
      surface_.maximumBundleBytes,stop);
  if (!frozen) return core::Result<Output>{frozen.error()};
  auto admitted=neural_synthesis::AdmittedNeuralBundle::admit(std::move(frozen).value(),
      surface_.maximumFrames,surface_.inferenceSteps,stop);
  if (!admitted) return core::Result<Output>{admitted.error()};
  auto bundle=std::make_shared<const neural_synthesis::AdmittedNeuralBundle>(std::move(admitted).value());
  // The renderer and export owners already refuse a singer that differs from the
  // saved selection; refusing here as well keeps that failure at the selection
  // boundary instead of in the middle of a render.
  const auto& execution=bundle->execution();
  if (execution.modelId!=saved.resource.id || execution.modelVersion!=saved.resource.version ||
      execution.bundleContentHash!=saved.resource.contentHash)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Installed neural resource differs from the saved selection",trackId.toString());
  if (stop.stop_requested()) return cancelled();
  NeuralPhraseRunnerOptions options{};
  options.bundleDirectory=directory.value();
  options.maximumBundleBytes=surface_.maximumBundleBytes;
  options.worker=worker_;
  options.silencePhone=surface_.silencePhone;
  auto runner=AuthoringNeuralPhraseRunner::create(std::move(options),stop);
  if (!runner) return core::Result<Output>{runner.error()};
  return core::success(Output{trackId,bundle,surface_.provenance,
      std::make_shared<const AuthoringNeuralPhraseRunner>(std::move(runner).value())});
}

}  // namespace seam::authoring
