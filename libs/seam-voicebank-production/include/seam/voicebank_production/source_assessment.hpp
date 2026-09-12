#pragma once
#include "seam/voicebank_production/project.hpp"

namespace seam::voicebank_production {
// Canonical identities for a reviewer decision, not an acoustic score or grant
// of source-use/redistribution rights. Annotation review is a separate workflow.
[[nodiscard]] std::string sourceQualityPolicyIdentity(const SourceStrategyAssessment& source);
[[nodiscard]] core::Result<std::string> sourceQualityMaterialIdentity(
    const VoicebankProductionProject& project, std::string_view strategyId);
[[nodiscard]] core::Result<void> requireCurrentSourceQualityAssessment(
    const VoicebankProductionProject& project, std::string_view strategyId);
[[nodiscard]] core::Result<void> validateSourceQualityReviewer(
    const VoicebankProductionProject& project, const SourceQualityAssessment& assessment);
}
