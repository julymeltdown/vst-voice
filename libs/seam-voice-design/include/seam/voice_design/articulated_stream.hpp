#pragma once
#include "seam/voice_design/frication_gesture_stream.hpp"
#include "seam/voice_design/procedural_renderer.hpp"

namespace seam::voice_design {
// Worker renderer for explicit, recipe-bound vowel/frication gestures.
// Production snapshots select it only after shared phrase/recipe validation.
class ArticulatedStream final {
public:
  [[nodiscard]] static core::Result<ArticulatedStream> createFromRecipe(
      const synthesis::ProceduralSingerResource& resource, synthesis::CompiledScorePerformance performance,
      std::span<const domain::PhonemeToken> phones, std::string style,
      std::size_t blockFrames = 512U, std::stop_token stop = {}, bool allowVoicedFrication = true, bool allowVoicedStops = true);
  [[nodiscard]] static core::Result<ArticulatedStream> create(
      const synthesis::ProceduralSingerResource& resource, synthesis::CompiledScorePerformance performance,
      ArticulationPlan plan, std::string style, std::size_t blockFrames = 512U,
      bool allowVoicedFrication = true, bool allowVoicedStops = true);
  [[nodiscard]] core::Result<synthesis::PhraseAudio> renderOwned(synthesis::PhraseFrameRange owned, std::stop_token stop = {});
  void reset();
  [[nodiscard]] time::SampleFrame position() const noexcept { return voice_->position(); }
  static constexpr std::uint32_t algorithmRevision = 12U;
private:
  ArticulatedStream() = default;
  std::shared_ptr<const ArticulationPlan> plan_;
  std::shared_ptr<const synthesis::CompiledScorePerformance> performance_;
  std::shared_ptr<const VoiceRecipe> recipe_;
  std::optional<PhonationSource> voice_;
  std::optional<VocalTract> tract_, initialTract_;
  std::optional<FricationGestureStream> frication_;
  std::optional<VoicedPlosiveSource> voicedStop_;
  std::string style_, initialPhone_, currentPhone_;
  std::size_t next_{0U}, blockFrames_{512U};
};
}
