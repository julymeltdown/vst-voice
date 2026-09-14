#pragma once

#include "seam/domain/project.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"
#include <array>
#include <optional>
#include <span>
#include <stop_token>

namespace seam::synthesis {
inline constexpr std::uint32_t kPerformanceCompilerRevision = 11U;
inline constexpr std::size_t kMaximumScoreVoiceAllocationNotes = 4096U;

struct ScoreVoicePlan final {
  std::vector<std::vector<domain::NoteId>> voices;
  friend bool operator==(const ScoreVoicePlan&, const ScoreVoicePlan&) = default;
};
// Score overlap only: source preutterance/coarticulation do not create voices.
[[nodiscard]] core::Result<ScoreVoicePlan> allocateScoreVoices(
    const domain::VocalRegion& region, std::size_t maximumVoices = 16U);

struct ScorePerformanceSample final {
  // Score fallback is not a phonetic voicing decision. Accepted null pitch can
  // suppress frequency while keeping noteId; silence has no active note.
  std::optional<domain::NoteId> noteId;
  std::optional<double> scoreFrequencyHz;
  double vibratoCents{0.0};
  float dynamicsGain{1.0F};
  // The vocal-tract envelope shift for this frame, in semitones. The excitation is untouched, so this
  // moves the resonances without moving the melody.
  float formantSemitones{0.0F};
  // The periodic/aperiodic balance of the excitation for this frame, normalized to the channel's own
  // range. It is not a gain: zero is the recipe's own source and one is the breathiest setting the
  // channel admits, so a consumer that treats this as loudness would be wrong by construction.
  float breathiness{0.0F};
  // The source's own spectral tilt change for this frame, normalized to the channel's range. Like
  // breathiness it is not a gain: zero is the recipe's own source and one is the most pressed setting
  // the channel admits, so a consumer that treated it as loudness would be wrong by construction.
  float tension{0.0F};
  float articulationGain{1.0F};
  // Accepted amplitude attack; absent/manual replacement retains the neutral
  // envelope. Continuations expose intent but do not restart the attack.
  std::optional<double> attackMilliseconds;
  std::optional<double> releaseMilliseconds;
  bool reattack{true};
  domain::NoteArticulation articulation{domain::NoteArticulation::Normal};
  // Inspection-only selected dynamics before manual replacement, never a
  // fallback value. Ordinary at(), rests and missing selections omit it.
  std::optional<float> selectedGeneratedDynamicsGain{};
};

struct ScoreNoteSpan final {
  domain::NoteId id;
  time::SampleFrame startFrame{0};
  time::SampleFrame endFrame{0};
  std::uint8_t midiKey{60};
  domain::NoteVibrato vibrato;
  domain::NoteArticulation articulation{domain::NoteArticulation::Normal};
  time::SampleFrame gateEndFrame{0};
  time::SampleFrame releaseStartFrame{0};
  bool reattack{true};
  std::optional<std::uint8_t> transitionFromMidi{};
  time::SampleFrame transitionEndFrame{0};
};

// Bounded immutable score evaluator; no per-frame song arrays or worker phase.
// Phonetic voicing and articulation gates remain separate integration work;
// scoreFrequencyHz must not be presented as complete singer F0.
class CompiledScorePerformance final {
public:
  [[nodiscard]] ScorePerformanceSample at(time::SampleFrame absoluteFrame) const noexcept;
  [[nodiscard]] ScorePerformanceSample inspectAt(time::SampleFrame absoluteFrame) const noexcept;
  [[nodiscard]] std::span<const ScoreNoteSpan> notes() const noexcept { return notes_; }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] std::span<const PhonemeTimingAnchor> phonemeTiming() const noexcept { return phonemeTiming_; }
private:
  [[nodiscard]] ScorePerformanceSample evaluate(time::SampleFrame absoluteFrame, bool inspect) const noexcept;
  friend core::Result<CompiledScorePerformance> compileScorePerformance(
      const domain::Project&, const domain::VocalRegion&, std::uint32_t,
      std::span<const domain::PhonemeToken>, PhonemeTimingPolicy);
  std::vector<ScoreNoteSpan> notes_;
  std::vector<PhonemeTimingAnchor> phonemeTiming_;
  time::TempoMap tempo_;
  time::Tick regionStart_;
  std::uint32_t sampleRate_{48000U};
  domain::PitchAutomation pitch_;
  domain::DynamicsAutomation dynamics_;
  domain::FormantAutomation formant_;
  domain::BreathinessAutomation breathiness_;
  domain::TensionAutomation tension_;
  domain::RegionPerformanceState performance_;
  struct FrameScope final {
    std::optional<domain::NoteId> noteId;
    time::SampleFrame start{0};
    time::SampleFrame end{0};
    time::Tick startTick;
    time::Tick endTick;
    [[nodiscard]] bool contains(domain::NoteId id, time::SampleFrame frame) const noexcept {
      return (!noteId || *noteId == id) && frame >= start && frame < end;
    }
  };
  std::vector<FrameScope> ownershipScopes_;
  std::vector<FrameScope> acceptedScopes_;
  // Separate ownership modes may overlap, but each channel/mode's validated
  // intervals are disjoint after monophonic voice projection.
  std::array<std::vector<std::size_t>, 24U> ownershipIndex_;
  std::array<std::vector<std::size_t>, 12U> acceptedIndex_;
  std::vector<std::pair<std::size_t, std::size_t>> acceptedLanes_;
};

[[nodiscard]] core::Result<CompiledScorePerformance> compileScorePerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    std::uint32_t sampleRate,
    std::span<const domain::PhonemeToken> phonemes = {},
    PhonemeTimingPolicy policy = PhonemeTimingPolicy::SourceDependent);
[[nodiscard]] core::Result<void> applyCompiledPerformanceGain(std::span<float> samples,
    const CompiledScorePerformance& performance, time::SampleFrame origin,
    std::stop_token stopToken = {});
struct CompiledVoicePerformance final {
  std::vector<domain::NoteId> noteIds;
  CompiledScorePerformance performance;
};
// Validated, bounded region projections retaining original note IDs/ticks.
[[nodiscard]] core::Result<std::vector<domain::VocalRegion>> projectScoreVoices(
    const domain::Project& project, const domain::VocalRegion& region,
    std::uint32_t sampleRate, std::span<const domain::PhonemeToken> phonemes = {},
    std::size_t maximumVoices = 16U);
// Multi-voice inputs with active unit/seam edits require the complete original
// phoneme sequence. Dependencies crossing allocated voices fail with Conflict;
// unresolved edits stay inactive. This API compiles intent, not mixed audio.
// Without independentPhonemes, supplied multi-voice phonemes must match the
// current Japanese resolver; each
// projected voice is then resolved in its own context, as in region rendering.
// Omitting both phoneme inputs requests score-only evaluation, without
// phonetic continuation.
// Language adapters may supply one complete token sequence per allocated voice
// in allocation order. Note identities, coverage and aggregate bounds validate
// before compilation; these explicit sequences are not re-phonemized.
[[nodiscard]] core::Result<std::vector<CompiledVoicePerformance>> compileScoreVoices(
    const domain::Project& project, const domain::VocalRegion& region,
    std::uint32_t sampleRate, std::span<const domain::PhonemeToken> phonemes = {},
    std::size_t maximumVoices = 16U,
    std::span<const std::vector<domain::PhonemeToken>> independentPhonemes = {});
}
