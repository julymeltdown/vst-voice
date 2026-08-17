#include "seam/voicebank/content_identity.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string_view>

namespace seam::voicebank {
namespace {

void addField(core::Sha256& hash, std::string_view value) noexcept {
  const auto size = static_cast<std::uint64_t>(value.size());
  std::array<std::byte, sizeof(size)> encoded{};
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    encoded[index] = static_cast<std::byte>((size >> (index * 8U)) & 0xffU);
  }
  hash.update(encoded);
  hash.update(value);
}

}  // namespace

core::Result<std::string> computeVoicebankContentHash(
    const Manifest& manifest,
    const std::filesystem::path& bankRoot) {
  const auto validation = manifest.validate();
  if (!validation) return core::Result<std::string>{validation.error()};
  if (bankRoot.empty()) {
    return core::failure<std::string>(
        core::ErrorCode::InvalidArgument,
        "Voicebank content identity requires a non-empty bank root");
  }

  // Character bindings and display text are product presentation metadata. They
  // must not alter synthesis identity or force audio-cache invalidation.
  auto synthesisManifest = manifest;
  synthesisManifest.displayName = "project-seam-synthesis-identity";
  synthesisManifest.characterId.clear();
  synthesisManifest.characterVersion.clear();
  ManifestJsonCodec codec;
  auto encodedManifest = codec.encode(synthesisManifest);
  if (!encodedManifest) {
    return core::Result<std::string>{encodedManifest.error()};
  }

  std::set<std::string> uniqueAudioPaths;
  for (const auto& unit : manifest.units) {
    uniqueAudioPaths.insert(unit.audioPath.generic_string());
  }

  core::Sha256 hash;
  addField(hash, "project-seam-voicebank-content-v1");
  addField(hash, encodedManifest.value());
  for (const auto& relative : uniqueAudioPaths) {
    auto resolved = resolveBankAsset(bankRoot, std::filesystem::path{relative});
    if (!resolved) return core::Result<std::string>{resolved.error()};
    auto digest = core::sha256File(resolved.value(), kMaximumSupportedWavBytes);
    if (!digest) return core::Result<std::string>{digest.error()};
    addField(hash, relative);
    addField(hash, digest.value());
  }
  return hash.hexDigest();
}

}  // namespace seam::voicebank
