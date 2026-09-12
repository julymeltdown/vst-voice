#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/performance_intent.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::synthesis {

// A generator request is immutable input for a proposal job. It does not
// mutate the region or accept the resulting take; callers must deliver the
// returned proposal through AddPerformanceProposalCommand.
struct AutomaticPerformanceRequest final {
  std::string takeId;
  domain::RegionId regionId;
  domain::PerformanceRevision capturedRevision;
  domain::SingerResourceIdentity resource;
  domain::PronunciationIdentity pronunciation;
  std::string generatorId{"seam-deterministic-performance"};
  std::string generatorVersion{"1"};
  std::uint64_t seed{0U};
  domain::PerformanceTimeRange range;
  std::vector<domain::PerformanceChannel> channels{
      domain::PerformanceChannel::Pitch,
      domain::PerformanceChannel::Dynamics,
      domain::PerformanceChannel::Attack,
      domain::PerformanceChannel::Release};
};

// Deterministic contract backend used to exercise the proposal lifecycle
// before a qualified neural model is available. Its output is deliberately a
// Proposed take and carries the exact resource/pronunciation/revision
// identities supplied by the caller.
[[nodiscard]] core::Result<domain::PerformanceTake>
generateAutomaticPerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    const phonemizer::ResolvedPronunciation& pronunciation,
    AutomaticPerformanceRequest request,
    std::stop_token stopToken = {});

}  // namespace seam::synthesis
