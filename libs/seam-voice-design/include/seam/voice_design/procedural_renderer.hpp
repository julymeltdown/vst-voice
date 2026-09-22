#pragma once
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/synthesis/phrase_backend.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/voice_design/articulation_plan.hpp"

namespace seam::voice_design {
inline constexpr std::uint32_t kSustainedPoseRendererRevision = 14U;
[[nodiscard]] bool requiresArticulation(std::span<const domain::PhonemeToken> phonemes) noexcept;
[[nodiscard]] core::Result<void> validateProceduralPhrase(
    const domain::VocalRegion& region, std::span<const domain::PhonemeToken> phonemes);
[[nodiscard]] core::Result<void> validateVowelTiming(const synthesis::CompiledScorePerformance& performance);
[[nodiscard]] core::Result<std::string> validateSustainedVowelPhrase(
    const domain::VocalRegion& region, std::span<const domain::PhonemeToken> phonemes);
[[nodiscard]] core::Result<void> validateVowelRecipePoses(const VoiceRecipe& recipe,
    std::span<const domain::PhonemeToken> phonemes, std::string_view style, std::uint32_t sampleRate);
// Planned source gestures, not measured acoustic/F0 boundaries or approval.
// Spans use absolute frames and are clipped to the returned PCM window;
// clipped starts must not be interpreted as new phoneme onsets.
// Markers preserve the planner's exact gesture identity; adding a source kind
// must never fall through to an unrelated vowel/noise label.
using ProceduralGestureKind = ArticulationGestureKind;
struct ProceduralPhoneMarker final {
  domain::PhonemeKey key;
  std::string phone;
  synthesis::PhraseFrameRange ownedSpan;
  bool startClipped{false}, endClipped{false};
  ProceduralGestureKind kind{ProceduralGestureKind::OralVowel};
  // True when the gesture is a palatalized consonant whose source is its base consonant's and
  // whose resonance is the pose named after the gesture's own phone.
  bool palatalized{false};
  friend bool operator==(const ProceduralPhoneMarker&, const ProceduralPhoneMarker&) = default;
};
struct SustainedPoseResult final {
  synthesis::PhraseAudio audio;
  domain::SingerResourceIdentity resource;
  // Initial pose, not a claim that all owned frames use this phone.
  std::string posePhone, style;
  std::uint32_t algorithmRevision{kSustainedPoseRendererRevision};
  std::size_t processedFrames{0U};
  std::vector<ProceduralPhoneMarker> markers{};
};
// Single-owner sequential stream. Copies are independent DSP checkpoints;
// immutable musical data remains shared. No realtime allocation guarantee.
class SustainedPoseStream final {
public:
  [[nodiscard]] static core::Result<SustainedPoseStream> create(
      const synthesis::ProceduralSingerResource& resource, const synthesis::CompiledScorePerformance& performance,
      std::string_view posePhone, std::string_view style, synthesis::PhraseFrameRange context,
      std::size_t blockFrames = 512U, std::stop_token stopToken = {});
  [[nodiscard]] core::Result<SustainedPoseResult> renderOwned(synthesis::PhraseFrameRange owned,
      std::stop_token stopToken = {});
  [[nodiscard]] core::Result<void> configureVowels(const domain::VocalRegion& region,
      std::span<const domain::PhonemeToken> phonemes);
  void reset();
  [[nodiscard]] time::SampleFrame position() const noexcept { return source_ ? source_->position() : context_.start; }
private:
  SustainedPoseStream() = default;
  std::optional<PhonationSource> source_;
  std::optional<VocalTract> tract_;
  std::optional<VocalTract> initialTract_;
  std::shared_ptr<const VoiceRecipe> recipe_;
  struct PoseEvent { time::SampleFrame frame; std::string phone; std::size_t frames; };
  std::shared_ptr<const std::vector<PoseEvent>> events_;
  std::size_t nextEvent_{0U};
  struct ActiveSpan { time::SampleFrame start, end; bool fadeIn, fadeOut; domain::PhonemeKey key; std::string phone; };
  std::shared_ptr<const std::vector<ActiveSpan>> activeSpans_;
  std::size_t activeSpan_{0U};
  std::shared_ptr<const synthesis::CompiledScorePerformance> performance_;
  domain::SingerResourceIdentity resource_;
  std::string phone_, style_;
  synthesis::PhraseFrameRange context_;
  std::size_t blockFrames_{512U};
};
// Single-pose audition wrapper. Scheduled oral-vowel sequences use
// configureVowels on the stream; neither path synthesizes consonants.
[[nodiscard]] core::Result<SustainedPoseResult> renderSustainedPose(
    const synthesis::ProceduralSingerResource& resource,
    const synthesis::CompiledScorePerformance& performance,
    std::string_view posePhone, std::string_view style,
    synthesis::PhraseOutputContract output, std::size_t blockFrames = 512U,
    std::stop_token stopToken = {});
}
