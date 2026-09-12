#pragma once

#include "seam/voicebank_production/project.hpp"
#include <filesystem>

namespace seam::voicebank_production::source_internal {
[[nodiscard]] core::Result<void> verifySnapshots(const std::filesystem::path& root,
                                               const VoicebankProductionProject& project);
[[nodiscard]] core::Result<void> preserveBindings(const VoicebankProductionProject& current,
                                                const VoicebankProductionProject& proposed);
}  // namespace seam::voicebank_production::source_internal
