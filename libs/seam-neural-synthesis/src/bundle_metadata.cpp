#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <cmath>

namespace seam::neural_synthesis {
namespace {
using J=formats::JsonValue;
bool fields(const J& value,std::initializer_list<const char*> names) {
  return value.isObject() && value.asObject().size()==names.size() &&
      std::all_of(names.begin(),names.end(),[&](auto name){return value.find(name)!=nullptr;});
}
core::Result<MelFeatureSpec> feature(const J& value) {
  const auto bad=[] {return core::failure<MelFeatureSpec>(core::ErrorCode::InvalidArgument,"Neural mel feature declaration is invalid");};
  if (!fields(value,{"sampleRate","hopSize","bins","layout","amplitudeScale","multiplier","offset","minimumHz","maximumHz"})) return bad();
  for (const auto* key:{"sampleRate","hopSize","bins"}) if (!value.find(key)->isInteger()) return bad();
  for (const auto* key:{"layout","amplitudeScale"}) if (!value.find(key)->isString()) return bad();
  for (const auto* key:{"multiplier","offset","minimumHz","maximumHz"}) if (!value.find(key)->isNumber() || !std::isfinite(value.find(key)->asNumber())) return bad();
  const auto rate=value.find("sampleRate")->asInt64(),hop=value.find("hopSize")->asInt64(),bins=value.find("bins")->asInt64();
  if (rate<8000 || rate>384000 || hop<1 || hop>8192 || bins<1 || bins>512) return bad();
  MelFeatureSpec result{static_cast<std::uint32_t>(rate),static_cast<std::uint32_t>(hop),static_cast<std::uint32_t>(bins),
      value.find("layout")->asString(),value.find("amplitudeScale")->asString(),value.find("multiplier")->asNumber(),
      value.find("offset")->asNumber(),value.find("minimumHz")->asNumber(),value.find("maximumHz")->asNumber()};
  if ((result.layout!="BTF" && result.layout!="BFT") ||
      (result.amplitudeScale!="linear-amplitude" && result.amplitudeScale!="ln-amplitude" && result.amplitudeScale!="log10-amplitude") ||
      result.multiplier<=0 || result.multiplier>1000 || std::abs(result.offset)>1000 ||
      result.minimumHz<0 || result.maximumHz<=result.minimumHz || result.maximumHz>static_cast<double>(rate)/2.0) return bad();
  return result;
}
std::string_view bytes(const synthesis::FrozenSingerData& data) {
  return {reinterpret_cast<const char*>(data.bytes().data()),data.bytes().size()};
}
}
core::Result<NeuralBundleMetadata> inspectNeuralBundleMetadata(const synthesis::FrozenNeuralBundle& bundle,std::stop_token stop) {
  using Output=NeuralBundleMetadata;
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,"Neural bundle metadata inspection cancelled");};
  if (stop.stop_requested()) return cancelled();
  if (!bundle.valid()) return core::failure<Output>(core::ErrorCode::InvalidArgument,"Neural bundle handle is empty");
  const synthesis::FrozenSingerData* configuration=nullptr;
  const synthesis::FrozenSingerData* vocabulary=nullptr;
  for (const auto& asset:bundle.assets()) {
    if (asset.role==synthesis::NeuralAssetRole::Configuration) configuration=asset.data.get();
    if (asset.role==synthesis::NeuralAssetRole::Vocabulary) vocabulary=asset.data.get();
  }
  if (!configuration || !vocabulary) return core::failure<Output>(core::ErrorCode::InvalidArgument,"Neural bundle metadata assets are missing");
  const auto parsed=formats::parseJson(bytes(*configuration),{.maximumInputBytes=4U*1024U*1024U,.maximumDepth=3U,
      .maximumNodes=128U,.maximumStringBytes=128U,.maximumCollectionEntries=16U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& root=parsed.value();
  if (!fields(root,{"formatId","schemaVersion","maximumFrames","acousticFeatures","vocoderFeatures"}) ||
      !root.find("formatId")->isString() || root.find("formatId")->asString()!="com.project-seam.neural-bundle-configuration" ||
      !root.find("schemaVersion")->isInteger() || root.find("schemaVersion")->asInt64()!=1 ||
      !root.find("maximumFrames")->isInteger() || root.find("maximumFrames")->asInt64()<1 || root.find("maximumFrames")->asInt64()>4LL*1024LL*1024LL)
    return core::failure<Output>(core::ErrorCode::ParseError,"Neural bundle configuration shape or frame bound is invalid");
  const auto acoustic=feature(*root.find("acousticFeatures")),vocoder=feature(*root.find("vocoderFeatures"));
  if (!acoustic) return core::Result<Output>{acoustic.error()};
  if (!vocoder) return core::Result<Output>{vocoder.error()};
  if (acoustic.value()!=vocoder.value()) return core::failure<Output>(core::ErrorCode::Conflict,"Acoustic and vocoder mel declarations are incompatible");
  ModelContract model;
  model.modelId=bundle.identity().id; model.modelVersion=bundle.identity().version; model.modelContentHash=bundle.identity().contentHash;
  model.vocabularyHash=vocabulary->sha256(); model.sampleRate=acoustic.value().sampleRate; model.hopSize=acoustic.value().hopSize;
  model.maximumFrames=static_cast<std::uint64_t>(root.find("maximumFrames")->asInt64());
  const auto valid=model.validate(); if (!valid) return core::Result<Output>{valid.error()};
  auto decoded=NeuralVocabulary::decode(bytes(*vocabulary),model);
  if (!decoded) return core::Result<Output>{decoded.error()};
  if (stop.stop_requested()) return cancelled();
  return Output{std::move(model),std::move(decoded.value()),acoustic.value()};
}
}
