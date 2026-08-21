#include "seam/authoring/bank_reference_registry.hpp"

namespace seam::authoring {

core::Result<void> BankReferenceRegistry::registerCandidate(
    const voicebank::VoicebankCandidate& candidate) {
  if (candidate.manifest.id.empty() || candidate.manifest.version.empty() ||
      candidate.contentHash.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Voicebank reference requires ID, version, and content hash");
  }
  const BankReferenceKey key{candidate.manifest.id, candidate.manifest.version};
  const auto [iterator, inserted] =
      references_.emplace(key, candidate.contentHash);
  if (!inserted && iterator->second != candidate.contentHash) {
    return core::failure(core::ErrorCode::Conflict,
                         "One voicebank ID and version maps to conflicting content hashes",
                         candidate.manifest.id + " " + candidate.manifest.version);
  }
  return core::success();
}

bool BankReferenceRegistry::contains(std::string_view id,
                                     std::string_view version,
                                     std::string_view contentHash) const noexcept {
  const auto iterator = references_.find(
      BankReferenceKey{std::string{id}, std::string{version}});
  return iterator != references_.end() && iterator->second == contentHash;
}

std::string BankReferenceRegistry::contentHash(std::string_view id,
                                               std::string_view version) const {
  const auto iterator = references_.find(
      BankReferenceKey{std::string{id}, std::string{version}});
  return iterator == references_.end() ? std::string{} : iterator->second;
}

}
