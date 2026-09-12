#include "seam/synthesis/neural_bundle.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace seam::synthesis {
namespace {
constexpr std::size_t maximumBundleBytes=512U*1024U*1024U;
constexpr std::array roles{"acoustic","vocoder","vocabulary","configuration","variance","tensor"};
}
core::Result<std::string> FrozenNeuralBundle::manifest(
    std::span<const NeuralBundleAssetInput> assets,std::size_t maximumTotalBytes) {
  const auto invalid=[] {return core::failure<std::string>(core::ErrorCode::InvalidArgument,"Neural bundle assets, roles or byte bounds are invalid");};
  if (assets.size()<4U || assets.size()>32U || maximumTotalBytes==0U || maximumTotalBytes>maximumBundleBytes) return invalid();
  std::array<unsigned,6> counts{};
  std::set<std::string> names;
  std::size_t total=0;
  std::vector<const NeuralBundleAssetInput*> ordered;
  for (const auto& asset:assets) {
    const auto role=static_cast<std::size_t>(asset.role);
    if (role>=roles.size() || asset.name.empty() || asset.name.size()>64U || asset.name.front()=='.' ||
        !std::all_of(asset.name.begin(),asset.name.end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.';}) ||
        !names.insert(asset.name).second || asset.sha256.size()!=64U ||
        !std::all_of(asset.sha256.begin(),asset.sha256.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}) ||
        asset.bytes.empty() || asset.bytes.size()>256U*1024U*1024U || asset.bytes.size()>maximumTotalBytes-total)
      return invalid();
    if ((asset.role==NeuralAssetRole::Vocabulary || asset.role==NeuralAssetRole::Configuration) && asset.bytes.size()>4U*1024U*1024U) return invalid();
    total+=asset.bytes.size(); ++counts[role]; ordered.push_back(&asset);
  }
  for (std::size_t role=0;role<4U;++role) if (counts[role]!=1U) return invalid();
  std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->name<b->name;});
  using J=formats::JsonValue;
  J::Array rows;
  for (const auto* asset:ordered) rows.emplace_back(J::Object{{"role",roles[static_cast<std::size_t>(asset->role)]},
      {"name",asset->name},{"sha256",asset->sha256},{"bytes",static_cast<std::int64_t>(asset->bytes.size())}});
  return formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-data-bundle"},
      {"schemaVersion",std::int64_t{1}},{"assets",std::move(rows)}}});
}
core::Result<FrozenNeuralBundle> FrozenNeuralBundle::freeze(domain::SingerResourceIdentity identity,
    std::span<const NeuralBundleAssetInput> assets,std::size_t maximumTotalBytes,std::stop_token stop) {
  const auto cancelled=[] {return core::failure<FrozenNeuralBundle>(core::ErrorCode::Conflict,"Neural bundle freezing cancelled");};
  if (stop.stop_requested()) return cancelled();
  const auto valid=identity.validate();
  if (!valid) return core::Result<FrozenNeuralBundle>{valid.error()};
  if (identity.kind!=domain::SingerResourceKind::Neural) return core::failure<FrozenNeuralBundle>(core::ErrorCode::Conflict,"Neural bundle identity has the wrong kind");
  const auto encoded=manifest(assets,maximumTotalBytes);
  if (!encoded) return core::Result<FrozenNeuralBundle>{encoded.error()};
  auto frozenManifest=FrozenSingerData::freeze(std::as_bytes(std::span{encoded.value().data(),encoded.value().size()}),identity.contentHash,32768U,stop);
  if (!frozenManifest) return core::Result<FrozenNeuralBundle>{frozenManifest.error()};
  auto data=std::make_shared<Data>(); data->identity=std::move(identity);
  data->manifest=std::make_shared<const FrozenSingerData>(std::move(frozenManifest.value()));
  for (const auto& asset:assets) {
    auto frozen=FrozenSingerData::freeze(asset.bytes,asset.sha256,256U*1024U*1024U,stop);
    if (!frozen) return core::Result<FrozenNeuralBundle>{frozen.error()};
    data->assets.push_back({asset.role,asset.name,std::make_shared<const FrozenSingerData>(std::move(frozen.value()))});
  }
  std::sort(data->assets.begin(),data->assets.end(),[](const auto& a,const auto& b){return a.name<b.name;});
  if (stop.stop_requested()) return cancelled();
  return FrozenNeuralBundle{std::move(data)};
}
}
