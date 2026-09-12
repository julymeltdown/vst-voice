#pragma once

#include "seam/voicebank_production/repository.hpp"

namespace seam::voicebank_production {

// Call before importing any staged audio or changing the project. This checks
// the repository-owned descriptor, not a digest supplied by StagedOperation.
// Generation, source authorization and parent checks remain commit's duties.
[[nodiscard]] core::Result<void> validateStagedOperation(
    const std::filesystem::path& root, const StagedOperation& staged);

}  // namespace seam::voicebank_production
