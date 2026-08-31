#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/project.hpp"

#include <string>
#include <string_view>

namespace seam::voicebank_production {

[[nodiscard]] core::Result<void> validateProductionProject(
    const VoicebankProductionProject& project);
[[nodiscard]] std::string encodeProductionProject(
    const VoicebankProductionProject& project);
[[nodiscard]] core::Result<VoicebankProductionProject> decodeProductionProject(
    std::string_view json);

}
