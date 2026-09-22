#pragma once

#include "seam/synthesis/performance_compiler.hpp"
#include "seam/synthesis/phrase_renderer.hpp"

namespace seam::rendering {

// Ordered, linear PCM crossfade, not a phase-reconstructed timbre morph.
// Identity includes this revision. Both complete rendered arms are checked
// before any owned-output crop; decisions cannot depend on chunk boundaries.
inline constexpr std::uint32_t kStyleBlendRevision = 1U;
inline constexpr std::size_t kMaximumStyleBlendFrames = 32U * 1024U * 1024U;

struct StyleBlendReport final {
  std::size_t comparedWindows{0U};
  double minimumCorrelation{1.0};
};
struct StyleBlendAudio final {
  synthesis::PhraseAudio audio;
  StyleBlendReport compatibility;
};

[[nodiscard]] core::Result<void> validateStyleBlendTiming(
    const synthesis::TimingPlan& primary, const synthesis::TimingPlan& secondary);

// Verifies finite bounded audio and local cancellation risk for the whole
// pair, even at endpoints. It never shifts, normalizes or flips either arm.
// 50 ms windows, half-window hops, absolute frame grid; active windows with
// correlation below -0.5 reject. This bounds a signal defect, not human quality.
[[nodiscard]] core::Result<StyleBlendAudio> crossfadeStylePair(
    const synthesis::PhraseAudio& primary, const synthesis::PhraseAudio& secondary,
    const synthesis::CompiledScorePerformance& performance, std::stop_token stop = {});

}
