#include "seam/neural_synthesis/model_bundle.hpp"

namespace seam::neural_synthesis {
namespace {
std::shared_ptr<const synthesis::FrozenSingerData> find(const synthesis::FrozenNeuralBundle& bundle,
    synthesis::NeuralAssetRole role) {
  for (const auto& asset:bundle.assets()) if (asset.role==role) return asset.data;
  return {};
}
}

core::Result<AdmittedNeuralBundle> AdmittedNeuralBundle::admit(
    synthesis::FrozenNeuralBundle bundle,std::uint64_t maximumFrames,
    std::int64_t inferenceSteps,std::stop_token stop) {
  using Output=AdmittedNeuralBundle;
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural model bundle admission cancelled");};
  if (stop.stop_requested()) return cancelled();
  if (!bundle.valid()) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Neural model bundle handle is empty");
  // The caller's prepared frame budget must cover the declared model bound.
  if (maximumFrames==0U || maximumFrames>4U*1024U*1024U || inferenceSteps<1 || inferenceSteps>1000)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Neural model bundle requires a bounded frame budget and 1 to 1000 inference steps");
  const auto acoustic=find(bundle,synthesis::NeuralAssetRole::Acoustic);
  const auto vocoder=find(bundle,synthesis::NeuralAssetRole::Vocoder);
  const auto vocabularyAsset=find(bundle,synthesis::NeuralAssetRole::Vocabulary);
  if (!acoustic || !vocoder || !vocabularyAsset || acoustic->bytes().empty() ||
      vocoder->bytes().empty() || vocabularyAsset->bytes().empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Neural model bundle is missing an admitted graph or vocabulary payload");
  const auto metadata=inspectNeuralBundleMetadata(bundle,stop);
  if (!metadata) return core::Result<Output>{metadata.error()};
  const auto& inspected=metadata.value();
  // Configuration schema v1 declares no steps layout and no vocoder output name.
  // Executing it would silently assume defaults that were never admitted.
  if (inspected.configurationVersion<2U)
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "Neural bundle configuration does not declare an executable steps layout and output name");
  if (inspected.model.maximumFrames>maximumFrames)
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "Neural bundle declares more frames than the prepared execution budget");
  if (inspected.model.vocabularyHash!=vocabularyAsset->sha256())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural bundle vocabulary differs from the declared model vocabulary");
  if (stop.stop_requested()) return cancelled();
  auto data=std::make_shared<Data>(std::move(bundle),inspected);
  data->execution=NeuralExecutionIdentity{.modelId=inspected.model.modelId,
      .modelVersion=inspected.model.modelVersion,
      .bundleContentHash=inspected.model.modelContentHash,
      .configurationVersion=inspected.configurationVersion,
      .inferenceSteps=inferenceSteps};
  // Retain the immutable frozen backing itself. Copying a frozen payload would
  // duplicate every model byte for each prepared handle.
  data->acoustic=acoustic;
  data->vocoder=vocoder;
  data->vocabulary=vocabularyAsset;
  return core::success(Output{std::move(data)});
}

}  // namespace seam::neural_synthesis
