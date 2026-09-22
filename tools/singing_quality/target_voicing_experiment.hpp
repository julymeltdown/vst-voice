#pragma once

#include "seam/synthesis/performance_compiler.hpp"
#include "seam/synthesis/source_target_map.hpp"
#include <string_view>

namespace seam::synthesis::experimental {
// Developer-only, DEFAULT OFF. The frozen speech assessment fails; this API is
// not linked into normal rendering and must not be used as a supported fallback.
// Convert active-note null-pitch frames to envelope-shaped aperiodic excitation.
// The caller supplies complete unit carrier PCM AFTER DC/fades, BEFORE performance
// gain. Numeric pitch and internal null-span carrier filling remain the backend's
// responsibility; canonical null intent is never replaced with a frequency.
// Ineligible samples are never written (relative to THIS carrier, not another
// render). Explicitly unvoiced source-map spans bypass conversion. Scratch is
// O(FFT), at most 16384 frames; owned chunks must crop the complete context.
// streamId must be a stable resource/unit identity, not a job/chunk/snapshot hash.
// On failure discard the caller-owned buffer; no partial result is publishable.
[[nodiscard]] core::Result<void> applySampleTargetVoicing(std::span<float> carrier,
    const CompiledScorePerformance& performance, time::SampleFrame origin,
    double carrierFrequencyHz, std::string_view streamId,
    const SourceTargetMap* sourceMap = nullptr, std::stop_token stopToken = {});
}
