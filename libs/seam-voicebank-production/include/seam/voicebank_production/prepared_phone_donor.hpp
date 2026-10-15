#pragma once
#include "seam/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::voicebank_production {
struct SourceIntakeFileSnapshot final {
  std::string relativePath, sha256;
  std::vector<std::byte> bytes;
};
// Exact packet/plan/notices for one selected, unassigned English speech donor.
// This verifies the evidence bytes and their declared audio identity, not audio
// bytes, factual annotations, rights or approval. Suitable for retained evidence
// whose original donor WAV is deliberately not redistributed in a package.
struct PreparedPhoneDonorEvidence final {
  SourceIntakeFileSnapshot packet, plan, license, attribution;
  std::string donorId, proposalJson, derivedAudioSha256;
};
// One bounded read/parse of shared metadata, with declared PCM consistency
// checked for every donor. Contains no audio measurements or held file bytes.
// Cache only within one verification operation, keyed by packetSha256.
struct PreparedPhoneDonorPacketFacts final {
  std::string packetSha256, intakePlanSha256, originalLicenseSha256, attributionSha256;
  std::map<std::string, std::string> derivedAudioSha256ByDonor;
};
[[nodiscard]] core::Result<PreparedPhoneDonorPacketFacts> loadPreparedPhoneDonorPacketFacts(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::stop_token stopToken = {});
// Owns the selected audio too, with facts measured from those exact WAV bytes.
// Future import must consume/retain these bytes, not reopen preparation paths.
struct PreparedPhoneDonorSnapshot final {
  PreparedPhoneDonorEvidence evidence;
  SourceIntakeFileSnapshot audio;
  std::string pcmSha256;
  std::uint32_t sampleRate{0U};
  std::uint64_t frames{0U};
  bool nonzeroPcm{false};
};
[[nodiscard]] core::Result<PreparedPhoneDonorEvidence> loadPreparedPhoneDonorEvidence(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::string donorId, std::stop_token stopToken = {});
// expectedPacketSha256 must come from the retained trusted handoff. Other
// donors' audio is not read; use the Python whole-packet preflight separately.
[[nodiscard]] core::Result<PreparedPhoneDonorSnapshot> loadPreparedPhoneDonor(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::string donorId, std::stop_token stopToken = {});
// Revalidates owned packet/plan/notices/audio without any filesystem reads.
// Cached proposal/fact fields are re-derived. Both expected identities come
// from the caller's retained request, rather than mutable snapshot fields.
[[nodiscard]] core::Result<PreparedPhoneDonorSnapshot> validatePreparedPhoneDonorSnapshot(
    PreparedPhoneDonorSnapshot snapshot, std::string expectedPacketSha256,
    std::string expectedDonorId, std::stop_token stopToken = {});
}
