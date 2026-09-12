#include "seam/synthesis/singer_resource.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>

namespace seam::synthesis {
namespace {
constexpr std::size_t kMaximumModelBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumPatchBytes = 4U * 1024U * 1024U;

core::Result<void> validateIdentity(const domain::SingerResourceIdentity& identity,
    domain::SingerResourceKind kind) {
  const auto valid = identity.validate();
  if (!valid) return valid;
  if (identity.kind != kind) return core::failure(core::ErrorCode::Conflict,
      "Singer resource identity does not match its typed payload");
  return core::success();
}
core::Result<void> validateData(const domain::SingerResourceIdentity& identity,
    domain::SingerResourceKind kind, const std::shared_ptr<const FrozenSingerData>& data,
    std::size_t limit) {
  const auto valid = validateIdentity(identity, kind);
  if (!valid) return valid;
  if (!data || data->bytes().empty() || data->bytes().size() > limit || data->sha256() != identity.contentHash) {
    return core::failure(core::ErrorCode::Conflict, "Singer resource payload is missing or differs from its identity");
  }
  return core::success();
}
}

core::Result<FrozenSingerData> FrozenSingerData::freeze(std::span<const std::byte> bytes,
    std::string_view expectedSha256, std::size_t maximumBytes, std::stop_token stopToken) {
  if (maximumBytes == 0U || maximumBytes > kMaximumModelBytes || bytes.empty() || bytes.size() > maximumBytes ||
      expectedSha256.size() != 64U || !std::all_of(expectedSha256.begin(), expectedSha256.end(),
          [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
    return core::failure<FrozenSingerData>(core::ErrorCode::InvalidArgument, "Frozen singer data exceeds bounds or has an invalid digest");
  }
  if (stopToken.stop_requested()) return core::failure<FrozenSingerData>(core::ErrorCode::Conflict, "Singer resource freezing cancelled");
  // Hash owned bytes, not a separately reopened path or borrowed mutable alias.
  std::vector<std::byte> frozen(bytes.begin(), bytes.end());
  core::Sha256 hash;
  constexpr std::size_t chunkBytes = 65536U;
  for (std::size_t offset = 0U; offset < frozen.size(); offset += chunkBytes) {
    if (stopToken.stop_requested()) return core::failure<FrozenSingerData>(core::ErrorCode::Conflict, "Singer resource freezing cancelled");
    hash.update(std::span<const std::byte>{frozen}.subspan(offset, std::min(chunkBytes, frozen.size() - offset)));
  }
  auto digest = hash.hexDigest();
  if (digest != expectedSha256) return core::failure<FrozenSingerData>(core::ErrorCode::Conflict, "Singer resource content hash mismatch");
  return FrozenSingerData{std::move(frozen), std::move(digest)};
}

core::Result<void> ProceduralSingerResource::validate() const {
  return validateData(identity, domain::SingerResourceKind::Procedural, patch, kMaximumPatchBytes);
}
core::Result<void> NeuralSingerResource::validate() const {
  return validateData(identity, domain::SingerResourceKind::Neural, model, kMaximumModelBytes);
}
core::Result<ProceduralSingerResource> freezeProceduralResource(domain::SingerResourceIdentity identity,
    std::span<const std::byte> patch, std::stop_token stopToken) {
  const auto valid = validateIdentity(identity, domain::SingerResourceKind::Procedural);
  if (!valid) return core::Result<ProceduralSingerResource>{valid.error()};
  auto data = FrozenSingerData::freeze(patch, identity.contentHash, kMaximumPatchBytes, stopToken);
  if (!data) return core::Result<ProceduralSingerResource>{data.error()};
  return ProceduralSingerResource{std::move(identity), std::make_shared<const FrozenSingerData>(std::move(data).value())};
}
core::Result<NeuralSingerResource> freezeNeuralResource(domain::SingerResourceIdentity identity,
    std::span<const std::byte> model, std::stop_token stopToken) {
  const auto valid = validateIdentity(identity, domain::SingerResourceKind::Neural);
  if (!valid) return core::Result<NeuralSingerResource>{valid.error()};
  auto data = FrozenSingerData::freeze(model, identity.contentHash, kMaximumModelBytes, stopToken);
  if (!data) return core::Result<NeuralSingerResource>{data.error()};
  return NeuralSingerResource{std::move(identity), std::make_shared<const FrozenSingerData>(std::move(data).value())};
}
}
