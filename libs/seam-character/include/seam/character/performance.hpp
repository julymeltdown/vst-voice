#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::character {

// The mouth shapes a presentation can draw. This is a fixed presentation vocabulary, not a phonetic
// transcription: a shape says what the dock is able to show, and the phone-to-shape mapping below
// is an engineering default the artwork and the plan can revisit.
enum class MouthShape { Closed, Narrow, Nasal, Open, Wide, Round };

[[nodiscard]] std::string_view mouthShapeName(MouthShape shape) noexcept;

// What the phrase contains over one span. The phone the render used is kept beside the shape, so a
// presentation can show the real cue instead of a shape with no provenance.
enum class CueKind { Vowel, Consonant, Nasal, Closure, Silence };

struct PerformanceCueInput final {
  std::string phone;
  CueKind kind{CueKind::Consonant};
  time::SampleFrame start{0}, end{0};
};

struct PerformanceCue final {
  std::string phone;
  CueKind kind{CueKind::Consonant};
  MouthShape mouth{MouthShape::Closed};
  time::SampleFrame start{0}, end{0};
  friend bool operator==(const PerformanceCue&, const PerformanceCue&) = default;
};

// One immutable performance read model, produced from the same successful phrase result that is
// audible. It carries no file paths, no portraits and no decoded audio: the presentation maps a
// playhead into bounded envelopes and draws. Energy and expression are normalized to 0..1 over a
// fixed window, and expressionMeasured says whether an envelope was supplied or the snapshot is
// honestly reporting zeros.
struct CharacterPerformanceSnapshot final {
  std::int32_t schemaVersion{1};
  std::string resourceId, resourceVersion, resourceContentHash, style;
  std::string pronunciationIdentity;
  std::uint64_t renderRevision{0};
  time::SampleFrame origin{0}, end{0};
  std::uint32_t windowFrames{240U};
  std::vector<PerformanceCue> cues;
  std::vector<float> energy, expression;
  bool expressionMeasured{false};
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] std::size_t windowCount() const noexcept { return energy.size(); }
};

struct CharacterPerformanceRequest final {
  std::string resourceId, resourceVersion, resourceContentHash, style;
  std::string pronunciationIdentity;
  std::uint64_t renderRevision{0};
  time::SampleFrame origin{0}, end{0};
  std::uint32_t sampleRate{48000U};
  // The audible material of the phrase result this snapshot describes, read once during
  // construction. Only the bounded envelopes survive in the snapshot.
  std::span<const float> samples;
  std::span<const PerformanceCueInput> cues;
  // Optional, one value per analysis window. An envelope of the wrong length is refused rather than
  // stretched, because a presentation that draws the wrong window is worse than drawing none.
  std::span<const float> expressionEnvelope;
};

[[nodiscard]] core::Result<CharacterPerformanceSnapshot> buildCharacterPerformanceSnapshot(
    const CharacterPerformanceRequest& request, std::uint32_t windowFrames = 240U,
    std::stop_token stop = {});

// Which singer a performance belongs to. Two resources, two styles or two render revisions are two
// singers as far as a presentation is concerned: a mouth that was showing the previous singer's
// phrase must not keep moving for a phrase that has not arrived yet.
struct PerformanceBindingKey final {
  std::string resourceId, resourceVersion, resourceContentHash, style;
  std::uint64_t renderRevision{0};
  friend bool operator==(const PerformanceBindingKey&, const PerformanceBindingKey&) = default;
};

[[nodiscard]] PerformanceBindingKey performanceBindingKey(
    const CharacterPerformanceSnapshot& snapshot) noexcept;

// The bounded read model one playhead position produces. performing is true only inside the
// snapshot's own span; outside it the mouth is closed and both envelopes are zero, while whatever
// operational state the dock is showing stays untouched. This is a pure function of the snapshot
// and the playhead, so stop, seek and loop are the caller's arithmetic over one immutable model.
struct CharacterPerformanceFrame final {
  MouthShape mouth{MouthShape::Closed};
  float energy{0.0F}, expression{0.0F};
  bool performing{false};
  friend bool operator==(const CharacterPerformanceFrame&, const CharacterPerformanceFrame&) = default;
};

[[nodiscard]] CharacterPerformanceFrame characterPerformanceFrameAt(
    const CharacterPerformanceSnapshot& snapshot, time::SampleFrame playhead) noexcept;

[[nodiscard]] MouthShape mouthShapeForCue(CueKind kind, std::string_view phone) noexcept;

}  // namespace seam::character
