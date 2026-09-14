#include "seam/rendering/render_performance.hpp"

#include "seam/core/sha256.hpp"
#include "seam/phonemizer/phonemizer.hpp"

#include <algorithm>
#include <string>

namespace seam::rendering {

namespace {

bool isDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
         });
}

}  // namespace

bool RenderedPerformanceIdentity::complete() const noexcept {
  return !resourceId.empty() && !resourceVersion.empty() && !style.empty() &&
         isDigest(resourceContentHash) && isDigest(pronunciationIdentity);
}

std::string renderedPronunciationIdentity(std::span<const std::string> phraseDigests) {
  if (phraseDigests.empty()) return {};
  // Each digest is framed by its own length so two different orders or splits can never hash to the
  // same identity by concatenation.
  std::string framed;
  for (const auto& digest : phraseDigests) {
    framed += std::to_string(digest.size());
    framed.push_back(':');
    framed += digest;
    framed.push_back(';');
  }
  return core::sha256Hex(framed);
}

RenderedCueKind renderedCueKindForSymbol(std::string_view symbol,
                                        domain::PhonemeRole role) noexcept {
  // The declared event symbols come first: they are the product's own vocabulary and they mean a
  // closed mouth regardless of how the inventory spells a consonant.
  if (symbol == "cl") return RenderedCueKind::Closure;
  if (symbol == "pau" || role == domain::PhonemeRole::Silence ||
      role == domain::PhonemeRole::Breath)
    return RenderedCueKind::Silence;
  if (phonemizer::isVowelSymbol(symbol)) return RenderedCueKind::Vowel;
  if (phonemizer::isNasalSymbol(symbol)) return RenderedCueKind::Nasal;
  return RenderedCueKind::Consonant;
}

core::Result<std::vector<RenderedCueSpan>> collectPhrasePerformanceCues(
    const synthesis::CompiledScorePerformance& performance,
    std::span<const domain::PhonemeToken> tokens, std::size_t maximumCues) {
  using Output = std::vector<RenderedCueSpan>;
  if (maximumCues == 0U || maximumCues > kMaximumRenderedCueSpans)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Presentation cue bound is outside its limit");
  const auto anchors = performance.phonemeTiming();
  if (anchors.size() > maximumCues)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "The rendered phrase declares more phone spans than a presentation holds");
  std::vector<const domain::PhonemeToken*> resolved;
  resolved.reserve(anchors.size());
  for (const auto& anchor : anchors) {
    const auto token = std::find_if(tokens.begin(), tokens.end(),
                                    [&anchor](const domain::PhonemeToken& candidate) {
                                      return candidate.key == anchor.key;
                                    });
    if (token == tokens.end())
      return core::failure<Output>(core::ErrorCode::InvariantViolation,
                                   "A phone timing anchor has no resolved phone in the render");
    resolved.push_back(&*token);
  }
  // The declared onset is the render's own decision; a policy that only inferred one still recorded
  // it, and the nucleus frame is the last honest fallback. A later phone that declares an onset
  // inside the span already in use does not steal it: the earlier onset stands, because the dock can
  // only be in one mouth at a time and the first declaration is the one the phrase started with.
  std::vector<time::SampleFrame> onsets;
  onsets.reserve(anchors.size());
  for (const auto& anchor : anchors) {
    const auto declared = anchor.explicitStartFrame.has_value() ? *anchor.explicitStartFrame
                        : anchor.inferredStartFrame.has_value() ? *anchor.inferredStartFrame
                                                                : anchor.nucleusFrame;
    onsets.push_back(onsets.empty() ? declared : std::max(declared, onsets.back()));
  }
  Output cues;
  cues.reserve(anchors.size());
  for (std::size_t index = 0U; index < anchors.size(); ++index) {
    const auto start = onsets[index];
    const auto end = index + 1U < anchors.size() ? onsets[index + 1U] : anchors[index].endFrame;
    if (end <= start) continue;
    cues.push_back(RenderedCueSpan{resolved[index]->symbol,
                                   renderedCueKindForSymbol(resolved[index]->symbol,
                                                            resolved[index]->role),
                                   start, end});
  }
  return core::success(std::move(cues));
}

}  // namespace seam::rendering
