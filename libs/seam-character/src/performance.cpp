#include "seam/character/performance.hpp"

#include <algorithm>
#include <cmath>

namespace seam::character {
namespace {

constexpr std::uint32_t kMaximumWindowFrames{48000U};
constexpr std::size_t kMaximumWindows{262144U};
constexpr std::uint32_t kMinimumSampleRate{8000U};
constexpr std::uint32_t kMaximumSampleRate{384000U};

core::Error invalid(std::string message) {
  return {core::ErrorCode::InvalidArgument, std::move(message)};
}

bool isDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
}

float clamped(float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

}  // namespace

std::string_view mouthShapeName(MouthShape shape) noexcept {
  switch (shape) {
    case MouthShape::Closed: return "closed";
    case MouthShape::Narrow: return "narrow";
    case MouthShape::Nasal: return "nasal";
    case MouthShape::Open: return "open";
    case MouthShape::Wide: return "wide";
    case MouthShape::Round: return "round";
  }
  return "closed";
}

MouthShape mouthShapeForCue(CueKind kind, std::string_view phone) noexcept {
  switch (kind) {
    case CueKind::Silence:
    case CueKind::Closure: return MouthShape::Closed;
    case CueKind::Nasal: return MouthShape::Nasal;
    case CueKind::Consonant: return MouthShape::Narrow;
    case CueKind::Vowel: break;
  }
  if (phone.empty()) return MouthShape::Open;
  // An engineering default for presentation only: the vowel's own opening is what the dock can
  // draw, and no claim is made that this is how the rendered phone is articulated.
  switch (phone.front()) {
    case 'i': case 'I': case 'e': case 'E': return MouthShape::Wide;
    case 'u': case 'U': case 'o': case 'O': return MouthShape::Round;
    default: return MouthShape::Open;
  }
}

core::Result<void> CharacterPerformanceSnapshot::validate() const {
  if (schemaVersion != 1)
    return core::failure(core::ErrorCode::Unsupported, "Character performance snapshot schema is unsupported");
  if (resourceId.empty() || resourceVersion.empty() || resourceContentHash.empty() || style.empty())
    return core::Result<void>{invalid("Character performance snapshot identity is incomplete")};
  if (!isDigest(resourceContentHash))
    return core::Result<void>{invalid("Character performance snapshot resource digest is malformed")};
  if (sampleRate < kMinimumSampleRate || sampleRate > kMaximumSampleRate)
    return core::Result<void>{invalid("Character performance snapshot sample rate is outside its bound")};
  if (end <= origin)
    return core::Result<void>{invalid("Character performance snapshot span is empty or inverted")};
  if (windowFrames == 0U || windowFrames > kMaximumWindowFrames)
    return core::Result<void>{invalid("Character performance snapshot window length is outside its bound")};
  if (energy.empty() || energy.size() != expression.size())
    return core::Result<void>{invalid("Character performance snapshot envelopes differ in length")};
  if (energy.size() > kMaximumWindows)
    return core::Result<void>{invalid("Character performance snapshot envelope exceeds its bound")};
  const auto expected = static_cast<std::size_t>(
      (end - origin + static_cast<time::SampleFrame>(windowFrames) - 1) /
      static_cast<time::SampleFrame>(windowFrames));
  if (energy.size() != expected)
    return core::Result<void>{invalid("Character performance snapshot envelope does not cover its span")};
  for (std::size_t index = 0U; index < energy.size(); ++index) {
    if (!std::isfinite(energy[index]) || !std::isfinite(expression[index]) ||
        energy[index] < 0.0F || energy[index] > 1.0F ||
        expression[index] < 0.0F || expression[index] > 1.0F)
      return core::Result<void>{invalid("Character performance snapshot envelope value is outside zero to one")};
  }
  time::SampleFrame previousEnd = origin;
  for (const auto& cue : cues) {
    if (cue.phone.empty())
      return core::Result<void>{invalid("Character performance cue has no phone")};
    if (cue.end <= cue.start || cue.start < previousEnd || cue.end > end)
      return core::Result<void>{invalid("Character performance cue span is empty, unordered or outside its phrase")};
    previousEnd = cue.end;
  }
  return core::success();
}

core::Result<CharacterPerformanceSnapshot> buildCharacterPerformanceSnapshot(
    const CharacterPerformanceRequest& request, std::uint32_t windowFrames, std::stop_token stop) {
  using Output = CharacterPerformanceSnapshot;
  const auto cancelled = [] {
    return core::failure<Output>(core::ErrorCode::Conflict, "Character performance snapshot build cancelled");
  };
  if (stop.stop_requested()) return cancelled();
  if (request.resourceId.empty() || request.resourceVersion.empty() ||
      request.resourceContentHash.empty() || request.style.empty() ||
      request.pronunciationIdentity.empty() || !isDigest(request.resourceContentHash))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance request identity is incomplete");
  if (request.end <= request.origin)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance request span is empty or inverted");
  if (request.sampleRate < kMinimumSampleRate || request.sampleRate > kMaximumSampleRate)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance request sample rate is outside its bound");
  if (windowFrames == 0U || windowFrames > kMaximumWindowFrames)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance window length is outside its bound");
  const auto span = request.end - request.origin;
  const auto windows = static_cast<std::size_t>(
      (span + static_cast<time::SampleFrame>(windowFrames) - 1) /
      static_cast<time::SampleFrame>(windowFrames));
  if (windows == 0U || windows > kMaximumWindows)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance envelope length is outside its bound");
  if (request.samples.size() < static_cast<std::size_t>(span))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance request has fewer samples than its span");
  if (!request.expressionEnvelope.empty() && request.expressionEnvelope.size() != windows)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Character performance expression envelope does not match the analysis window");
  Output result;
  result.resourceId = request.resourceId;
  result.resourceVersion = request.resourceVersion;
  result.resourceContentHash = request.resourceContentHash;
  result.style = request.style;
  result.pronunciationIdentity = request.pronunciationIdentity;
  result.renderRevision = request.renderRevision;
  result.sampleRate = request.sampleRate;
  result.origin = request.origin;
  result.end = request.end;
  result.windowFrames = windowFrames;
  result.energy.assign(windows, 0.0F);
  result.expression.assign(windows, 0.0F);
  result.expressionMeasured = !request.expressionEnvelope.empty();
  float peak = 0.0F;
  for (std::size_t window = 0U; window < windows; ++window) {
    if (stop.stop_requested()) return cancelled();
    const auto first = window * static_cast<std::size_t>(windowFrames);
    const auto last = std::min<std::size_t>(first + static_cast<std::size_t>(windowFrames),
        static_cast<std::size_t>(span));
    double sum = 0.0;
    for (std::size_t index = first; index < last; ++index) {
      const auto value = static_cast<double>(request.samples[index]);
      sum += value * value;
    }
    const auto count = last > first ? last - first : 1U;
    const auto rms = static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
    result.energy[window] = rms;
    peak = std::max(peak, rms);
  }
  for (std::size_t window = 0U; window < windows; ++window)
    result.energy[window] = peak > 0.0F ? clamped(result.energy[window] / peak) : 0.0F;
  if (result.expressionMeasured)
    for (std::size_t window = 0U; window < windows; ++window)
      result.expression[window] = clamped(request.expressionEnvelope[window]);
  for (const auto& cue : request.cues) {
    if (cue.phone.empty())
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Character performance cue has no phone");
    if (cue.end <= cue.start || cue.start < request.origin || cue.end > request.end ||
        (!result.cues.empty() && cue.start < result.cues.back().end))
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Character performance cue span is empty, unordered or outside its phrase");
    result.cues.push_back(PerformanceCue{cue.phone, cue.kind,
        mouthShapeForCue(cue.kind, cue.phone), cue.start, cue.end});
  }
  if (stop.stop_requested()) return cancelled();
  const auto valid = result.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  return core::success(std::move(result));
}

PerformanceBindingKey performanceBindingKey(const CharacterPerformanceSnapshot& snapshot) noexcept {
  return PerformanceBindingKey{snapshot.resourceId, snapshot.resourceVersion,
                               snapshot.resourceContentHash, snapshot.style,
                               snapshot.renderRevision};
}

CharacterPerformanceFrame characterPerformanceFrameAt(
    const CharacterPerformanceSnapshot& snapshot, time::SampleFrame playhead) noexcept {
  CharacterPerformanceFrame result;
  if (playhead < snapshot.origin || playhead >= snapshot.end || snapshot.energy.empty()) return result;
  const auto offset = playhead - snapshot.origin;
  auto window = static_cast<std::size_t>(offset / static_cast<time::SampleFrame>(snapshot.windowFrames));
  if (window >= snapshot.energy.size()) window = snapshot.energy.size() - 1U;
  result.energy = snapshot.energy[window];
  if (window < snapshot.expression.size()) result.expression = snapshot.expression[window];
  for (const auto& cue : snapshot.cues) {
    if (playhead >= cue.start && playhead < cue.end) { result.mouth = cue.mouth; break; }
  }
  result.performing = true;
  return result;
}

}  // namespace seam::character
