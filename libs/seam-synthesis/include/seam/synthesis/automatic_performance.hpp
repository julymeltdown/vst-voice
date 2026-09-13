#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/performance_intent.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>
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

// Identity of the production proposal backend. A proposal must never be
// mistakable for the deterministic reference backend above, so this backend stamps
// its own identity and refuses a request that names a different generator or an
// unknown version of itself.
inline constexpr std::string_view kPhraseAwareGeneratorId{"seam-phrase-proposal"};
inline constexpr std::string_view kPhraseAwareGeneratorVersion{"1"};

// Production proposal backend: derives expressive shaping from the compiled score
// -- phrase position, note-to-note leaps, articulation and the phoneme roles the
// region actually resolves to -- and modulates the region's own dynamics
// automation instead of replacing it. The result is a Proposed take like any other,
// so it still needs the shared acceptance command, and a user's manual edits and
// accepted takes stay authoritative. This claims structural shaping, not taste:
// language/style-conditioned quality remains M3.P1/M6 work, and no measured
// quality threshold is asserted here.
[[nodiscard]] core::Result<domain::PerformanceTake> generatePhraseAwarePerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    const phonemizer::ResolvedPronunciation& pronunciation,
    AutomaticPerformanceRequest request, std::stop_token stopToken = {});

}  // namespace seam::synthesis
