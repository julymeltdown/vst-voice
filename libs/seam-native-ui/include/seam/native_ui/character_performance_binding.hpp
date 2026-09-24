#pragma once

#include "seam/character/performance.hpp"
#include "seam/core/result.hpp"
#include "seam/rendering/render_performance.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace seam::native_ui {

// Everything here comes from the same published render: the selected vocal
// region's PCM, its phone partition, and the identity of its singer. The
// project master is deliberately not the mouth source because another track
// may overlap the singer the dock follows.
struct CharacterPerformanceBindingRequest final {
  std::string resourceId, resourceVersion, resourceContentHash, style;
  std::string pronunciationIdentity;
  std::uint64_t renderRevision{0};
  std::uint32_t sampleRate{48000U};
  std::uint8_t channelCount{2U};
  // The published interleaved source for this singer. Its first sample is at
  // interleavedStartFrame, so a clipped region need not allocate leading silence.
  time::SampleFrame interleavedStartFrame{0};
  std::span<const float> interleaved;
  // The active region's phone partition, in the same absolute frames.
  std::span<const rendering::RenderedCueSpan> cues;
  // Optional, one value per analysis window.
  std::span<const float> expressionEnvelope;
  std::uint32_t windowFrames{240U};
};

// How much mix one performance may cover. A longer span is refused by cause instead of summarised:
// a dock that draws a shortened phrase is worse than one that draws nothing.
inline constexpr std::int64_t kMaximumCharacterPerformanceFrames = 48000LL * 600LL;

[[nodiscard]] character::CueKind characterCueKind(rendering::RenderedCueKind kind) noexcept;

// The bounded read model for the span the cues cover. The mix is mixed down to mono over exactly
// that span and nothing else, so the envelope the dock draws belongs to the phrase, not to the
// silence a track happens to sit in.
[[nodiscard]] core::Result<character::CharacterPerformanceSnapshot>
buildPublishedCharacterPerformance(const CharacterPerformanceBindingRequest& request);

}  // namespace seam::native_ui
