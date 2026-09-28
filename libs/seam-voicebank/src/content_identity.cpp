#include "seam/voicebank/content_identity.hpp"

#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
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

  // Per-unit sidecars that change how a unit renders are part of its synthesis
  // identity: source-phoneme alignments, and stored acoustic analyses, whose
  // measured voicing replaces the marker map's default in the renderers. Exact
  // bytes are hashed here; audio-bound semantic validation belongs to loading.
  // An absent directory or file contributes nothing, so a bank without sidecars
  // keeps its legacy identity, and one with alignments only keeps the identity it
  // had before analyses were hashed.
  std::uint64_t totalBytes = 0U;
  constexpr std::uint64_t maximumBytes = 64ULL * 1024ULL * 1024ULL;
  const auto addSidecars = [&](std::string_view directoryName, std::string_view noun,
                               std::string_view tag,
                               const std::function<std::string(std::string_view)>& pathFor)
      -> core::Result<void> {
    const auto directory = bankRoot / std::string{directoryName};
    std::error_code error;
    const auto directoryStatus = std::filesystem::symlink_status(directory, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && !std::filesystem::exists(directoryStatus))) return core::success();
    if (error || !std::filesystem::is_directory(directoryStatus) ||
        std::filesystem::is_symlink(directoryStatus)) {
      return core::failure(core::ErrorCode::Conflict,
          "Voicebank " + std::string{noun} + " directory must be a real directory",
          directory.string());
    }
    std::set<std::string> paths;
    for (const auto& unit : manifest.units) paths.insert(pathFor(unit.id));
    bool started = false;
    for (const auto& relative : paths) {
      const auto path = bankRoot / relative;
      error.clear();
      const auto status = std::filesystem::symlink_status(path, error);
      if (error == std::errc::no_such_file_or_directory ||
          (!error && !std::filesystem::exists(status))) continue;
      if (error) return core::failure(core::ErrorCode::IoError,
          "Cannot inspect voicebank " + std::string{noun}, path.string());
      const auto resolved = resolveBankAsset(bankRoot, relative);
      if (!resolved) return core::Result<void>{resolved.error()};
      const auto bytes = core::readFileBytesLimited(resolved.value(),
          std::min<std::uint64_t>(512ULL * 1024ULL, maximumBytes - totalBytes));
      if (!bytes) return core::Result<void>{bytes.error()};
      totalBytes += static_cast<std::uint64_t>(bytes.value().size());
      if (!started) {
        addField(hash, tag);
        started = true;
      }
      addField(hash, relative);
      addField(hash, core::sha256Hex(bytes.value()));
    }
    return core::success();
  };
  const auto alignments = addSidecars("alignments", "alignment",
      "source-phoneme-alignments-v1", [](std::string_view unitId) {
        return "alignments/" + core::sha256Hex(unitId) + ".json";
      });
  if (!alignments) return core::Result<std::string>{alignments.error()};
  const auto analyses = addSidecars("analysis", "acoustic analysis",
      "acoustic-analyses-v1", [](std::string_view unitId) {
        return acousticAnalysisSidecarPath(unitId);
      });
  if (!analyses) return core::Result<std::string>{analyses.error()};
  return hash.hexDigest();
}

}  // namespace seam::voicebank
