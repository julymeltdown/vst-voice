#pragma once

#include "seam/voicebank_production/candidate_publication.hpp"
#include <memory>

namespace seam::voicebank_production::candidate_publication_internal {

struct OriginAttribution final {
  std::string actor;
  std::uint64_t generation;
  std::string journalSha256;
};

[[nodiscard]] bool isDigest(std::string_view value);
[[nodiscard]] core::Result<void> cancelled(std::stop_token stop);
[[nodiscard]] core::Result<std::filesystem::path> realDirectory(const std::filesystem::path& path);
// Shared create-new directory transaction primitives. Neither grants review or
// release qualification; draft builders use the same no-overwrite publication.
[[nodiscard]] core::Result<void> absentDestination(const std::filesystem::path& path);
[[nodiscard]] core::Result<void> syncDirectory(const std::filesystem::path& path);
struct NativeDirectoryIdentity;
struct DirectoryIdentity final {
  // Keeping the native object open prevents identity reuse while a transaction
  // is active. This is object identity, not merely a canonical pathname.
  std::shared_ptr<const NativeDirectoryIdentity> native;
};
[[nodiscard]] core::Result<DirectoryIdentity> captureDirectoryIdentity(const std::filesystem::path& path);
[[nodiscard]] core::Result<void> validateDirectoryIdentity(const std::filesystem::path& path, const DirectoryIdentity& identity);
void cleanupOwnedDirectory(const std::filesystem::path& stage,
    const DirectoryIdentity& parentIdentity, const DirectoryIdentity& stageIdentity) noexcept;
[[nodiscard]] core::Result<void> publishNewDirectory(const std::filesystem::path& stage,
    const std::filesystem::path& destination, const DirectoryIdentity& parentIdentity,
    const DirectoryIdentity& stageIdentity);
[[nodiscard]] core::Result<std::string> boundedManifest(const voicebank::Manifest& manifest);
[[nodiscard]] std::string coverageKey(const voicebank::Unit& unit);
[[nodiscard]] std::string reviewBasis(const VoicebankProductionProject& project, std::string_view takeId = {});
[[nodiscard]] core::Result<const TakeRecord*> selectedTake(const VoicebankProductionProject& project,
    const SampleCandidateUnitBinding& binding);
[[nodiscard]] core::Result<std::map<std::string, OriginAttribution>> collectOriginAttribution(
    const std::filesystem::path& workspace, const VoicebankProductionProject& project,
    const std::vector<SampleCandidateUnitBinding>& bindings, std::stop_token stop);
[[nodiscard]] core::Result<void> validateIndependentReviewer(const VoicebankProductionProject& project,
    const SampleCandidateUnitBinding& binding, std::string_view reviewerId, const OriginAttribution& origin);
[[nodiscard]] core::Result<void> validateReviewedBinding(const VoicebankProductionProject& project,
    const voicebank::Manifest& manifest, const SampleCandidateUnitBinding& binding);

}  // namespace seam::voicebank_production::candidate_publication_internal
