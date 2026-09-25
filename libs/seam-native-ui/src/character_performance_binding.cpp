#include "seam/native_ui/character_performance_binding.hpp"

#include <vector>

namespace seam::native_ui {

character::CueKind characterCueKind(rendering::RenderedCueKind kind) noexcept {
  switch (kind) {
    case rendering::RenderedCueKind::Vowel: return character::CueKind::Vowel;
    case rendering::RenderedCueKind::Nasal: return character::CueKind::Nasal;
    case rendering::RenderedCueKind::Closure: return character::CueKind::Closure;
    case rendering::RenderedCueKind::Silence: return character::CueKind::Silence;
    case rendering::RenderedCueKind::Consonant: break;
  }
  return character::CueKind::Consonant;
}

core::Result<character::CharacterPerformanceSnapshot>
buildPublishedCharacterPerformance(const CharacterPerformanceBindingRequest& request) {
  using Output = character::CharacterPerformanceSnapshot;
  if (request.cues.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A performance has no rendered phone spans to draw");
  if (request.channelCount == 0U || request.channelCount > 8U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A published mix has an unsupported channel count");
  const auto channels = static_cast<std::size_t>(request.channelCount);
  const auto origin = request.cues.front().startFrame;
  const auto end = request.cues.back().endFrame;
  if (request.interleavedStartFrame < 0 ||
      origin < request.interleavedStartFrame || end <= origin)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A performance span is empty or outside its audio source");
  if (end - origin > kMaximumCharacterPerformanceFrames)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A performance span exceeds what one presentation may cover");
  for (std::size_t index = 0U; index < request.cues.size(); ++index) {
    const auto& cue = request.cues[index];
    if (cue.symbol.empty() || cue.endFrame <= cue.startFrame || cue.startFrame < origin ||
        cue.endFrame > end || (index > 0U && cue.startFrame < request.cues[index - 1U].endFrame))
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                   "A rendered phone span is empty, unordered or outside its phrase");
  }
  if (static_cast<std::uint64_t>(end - request.interleavedStartFrame) >
      request.interleaved.size() / channels)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "The published singer audio is shorter than its phrase");
  const auto frames = static_cast<std::size_t>(end - origin);
  std::vector<float> mono(frames, 0.0F);
  const auto firstSample = static_cast<std::size_t>(origin - request.interleavedStartFrame) * channels;
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    double sum = 0.0;
    for (std::size_t channel = 0U; channel < channels; ++channel)
      sum += static_cast<double>(request.interleaved[firstSample + frame * channels + channel]);
    mono[frame] = static_cast<float>(sum / static_cast<double>(channels));
  }
  std::vector<character::PerformanceCueInput> cues;
  cues.reserve(request.cues.size());
  for (const auto& cue : request.cues)
    cues.push_back(character::PerformanceCueInput{cue.symbol, characterCueKind(cue.kind),
                                                  cue.startFrame, cue.endFrame});
  character::CharacterPerformanceRequest build;
  build.resourceId = request.resourceId;
  build.resourceVersion = request.resourceVersion;
  build.resourceContentHash = request.resourceContentHash;
  build.resourceKind = request.resourceKind;
  build.style = request.style;
  build.pronunciationIdentity = request.pronunciationIdentity;
  build.renderRevision = request.renderRevision;
  build.scorePitchRange = request.scorePitchRange;
  build.origin = origin;
  build.end = end;
  build.sampleRate = request.sampleRate;
  build.samples = mono;
  build.cues = cues;
  build.expressionEnvelope = request.expressionEnvelope;
  return character::buildCharacterPerformanceSnapshot(build, request.windowFrames);
}

}  // namespace seam::native_ui
