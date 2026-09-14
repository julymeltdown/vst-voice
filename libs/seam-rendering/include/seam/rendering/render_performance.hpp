#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/phoneme.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seam::rendering {

// What a presentation can draw over one span of a phrase that was actually rendered. This is the
// dock's own vocabulary, not a phonetic transcription: a vowel the inventory writes "a" and one it
// writes "i" are both a vowel here, and which mouth shape that becomes is the character layer's
// business.
enum class RenderedCueKind { Vowel, Consonant, Nasal, Closure, Silence };

struct RenderedCueSpan final {
  std::string symbol;
  RenderedCueKind kind{RenderedCueKind::Consonant};
  time::SampleFrame startFrame{0};
  time::SampleFrame endFrame{0};
  friend bool operator==(const RenderedCueSpan&, const RenderedCueSpan&) = default;
};

// A phrase that declares more phone spans than this is refused rather than truncated: a dock that
// silently draws an abbreviated phrase is worse than one that draws nothing.
inline constexpr std::size_t kMaximumRenderedCueSpans = 65536U;

// The product's own closure and pause symbols, plus role and symbol classification, decide the kind.
[[nodiscard]] RenderedCueKind renderedCueKindForSymbol(
    std::string_view symbol, domain::PhonemeRole role) noexcept;

// The ordered partition the dock can draw, projected from the compiled plan in absolute project
// frames. The projection is needed because a compiled plan keeps richer declarations than a
// single-mouth presentation can show: a phone may declare an onset inside the previous phone's
// recorded span, and a phone with no declared onset falls back to its own nucleus. So each phone
// starts at the later of its declared onset and the onset already in use, ends where the next phone
// starts, and the last phone ends where its own recorded span ends; a declaration that cannot own a
// single frame is not drawn. The raw declarations stay in the compiled performance for any caller
// that needs them. Frames are the same coordinates as the published mix, so a caller maps a
// transport position onto them without re-deriving any timing. An anchor with no resolved phone, an
// empty recorded span and an unbounded request are refused by cause.
[[nodiscard]] core::Result<std::vector<RenderedCueSpan>> collectPhrasePerformanceCues(
    const synthesis::CompiledScorePerformance& performance,
    std::span<const domain::PhonemeToken> tokens,
    std::size_t maximumCues = kMaximumRenderedCueSpans);

}  // namespace seam::rendering
