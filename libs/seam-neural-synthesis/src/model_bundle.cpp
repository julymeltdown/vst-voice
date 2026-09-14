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
  // The configuration declares which mel representation the pair shares, but it cannot say whether
  // the graph files agree with it. Reading them is what separates an admitted pair from two files
  // that are each individually plausible and disagree about the feature representation. The byte
  // bound here is the frozen asset's own admitted length: the payload never grew, and every other
  // graph bound is the parser's own.
  const auto graphLimits=[](const std::shared_ptr<const synthesis::FrozenSingerData>& asset) {
    GraphInspectionLimits limits;
    limits.maximumBytes=asset->bytes().size();
    limits.maximumInitializerBytes=static_cast<std::uint64_t>(asset->bytes().size());
    return limits;
  };
  auto acousticGraph=inspectNeuralGraph(acoustic->bytes(),graphLimits(acoustic));
  if (!acousticGraph) return core::Result<Output>{acousticGraph.error()};
  auto vocoderGraph=inspectNeuralGraph(vocoder->bytes(),graphLimits(vocoder));
  if (!vocoderGraph) return core::Result<Output>{vocoderGraph.error()};
  // The configured layout names which axis carries the mel features, and that axis has to be a
  // bounded size the two graphs agree on. A dynamic feature axis is refused: the vocoder would have
  // to accept whatever the acoustic graph happened to emit.
  const auto featureIndex=inspected.features.layout=="BTF" ? std::size_t{2U} : std::size_t{1U};
  const auto melBins=static_cast<std::int64_t>(inspected.features.bins);
  const auto isMel=[&](const GraphTensorContract& tensor) {
    return tensor.rank()==3U && isFloatTensorElementType(tensor.elementType) &&
        tensor.dimensions[featureIndex]==melBins;
  };
  const GraphTensorContract* acousticMel=nullptr;
  for (const auto& output:acousticGraph.value().outputs) if (isMel(output)) {
    if (acousticMel) return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural acoustic graph declares more than one mel output for the configured feature layout");
    acousticMel=&output;
  }
  const GraphTensorContract* vocoderMel=nullptr;
  for (const auto& input:vocoderGraph.value().inputs) if (isMel(input)) {
    if (vocoderMel) return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural vocoder graph declares more than one mel input for the configured feature layout");
    vocoderMel=&input;
  }
  if (!acousticMel) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural acoustic graph does not declare a bounded mel output for the configured feature layout");
  if (!vocoderMel) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural vocoder graph does not declare a bounded mel input for the configured feature layout");
  if (acousticMel->elementType!=vocoderMel->elementType)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural acoustic and vocoder graphs disagree about the mel element type");
  const auto* audio=vocoderGraph.value().findOutput(inspected.vocoderOutput);
  if (!audio) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural vocoder graph does not declare the configured output tensor");
  if (!isFloatTensorElementType(audio->elementType)) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural vocoder output is not a floating point tensor");
  if (audio->rank()==0U || audio->rank()>2U) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural vocoder output rank is not admitted");
  if (audio->rank()==2U && audio->dimensions.front()>1)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural vocoder output declares more than one channel");
  if (stop.stop_requested()) return cancelled();
  auto data=std::make_shared<Data>(std::move(bundle),inspected);
  data->acousticGraph=std::move(acousticGraph).value();
  data->vocoderGraph=std::move(vocoderGraph).value();
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
