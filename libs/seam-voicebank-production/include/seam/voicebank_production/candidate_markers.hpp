#pragma once
#include "seam/voicebank_production/project.hpp"
#include "seam/voice_design/procedural_candidate.hpp"

namespace seam::voicebank_production {
struct ResolvedCandidateMarkers final {
  voice_design::ProceduralCandidate candidate;
  std::string revisionId;
};
// Effective manual bounds over immutable original lineage. No audio IO or approval.
[[nodiscard]] core::Result<ResolvedCandidateMarkers> resolveCandidateMarkers(
    const VoicebankProductionProject& project, std::string_view takeId);
}
