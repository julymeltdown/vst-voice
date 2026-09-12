#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace seam::phase12c {

struct LiveVoicebankResource;

// Identity captured by a canonical Phase 12C runner.  This is evidence
// metadata, not a release authorization; the external validator and the
// full-scope gate still decide whether the record is admissible.
struct CanonicalEvidenceIdentity final {
  std::string pluginSha256;
  std::string voicebankId;
  std::string voicebankVersion;
  std::string voicebankTreeSha256;
  std::string sourceCommit;
  std::string buildId;
  std::string voicebankContentHash;
  std::string style;
  friend bool operator==(const CanonicalEvidenceIdentity&, const CanonicalEvidenceIdentity&) = default;
};

[[nodiscard]] core::Result<CanonicalEvidenceIdentity> loadCanonicalEvidenceIdentity(
    const std::filesystem::path& plugin,
    const std::filesystem::path& bank,
    bool developmentFixture = false,
    std::string style = {});

[[nodiscard]] core::Result<std::shared_ptr<const LiveVoicebankResource>>
loadCanonicalVoicebankResource(const std::filesystem::path& bank);

}  // namespace seam::phase12c
