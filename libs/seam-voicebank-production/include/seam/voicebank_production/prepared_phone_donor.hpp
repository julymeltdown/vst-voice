#pragma once
#include "seam/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::voicebank_production {
struct SourceIntakeFileSnapshot final {
  std::string relativePath, sha256;
  std::vector<std::byte> bytes;
};
// Owned snapshots for one selected, unassigned English speech donor. This is
// byte verification only: no project, actor, rights, QC or bank approval. Future
// import must consume/retain these bytes, not reopen the preparation paths.
struct PreparedPhoneDonorSnapshot final {
  SourceIntakeFileSnapshot packet, plan, license, attribution, audio;
  std::string donorId, proposalJson, pcmSha256;
  std::uint32_t sampleRate{0U};
  std::uint64_t frames{0U};
  bool nonzeroPcm{false};
};
// expectedPacketSha256 must come from the retained trusted handoff. Other
// donors' audio is not read; use the Python whole-packet preflight separately.
[[nodiscard]] core::Result<PreparedPhoneDonorSnapshot> loadPreparedPhoneDonor(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::string donorId, std::stop_token stopToken = {});
}
