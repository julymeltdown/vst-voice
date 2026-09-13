#pragma once
#include "seam/voice_design/frication_source.hpp"
#include "seam/voice_design/plosive_source.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/voice_design/recipe_resource.hpp"

namespace seam::voice_design {
// Affricate is a released closure whose burst continues into a frication tail inside one
// bounded gesture. It is not a stop followed later by a fricative, and it is unvoiced today:
// a voiced affricate needs prevoiced closure plus voiced frication, and is refused rather than
// approximated with an unvoiced noise pair.
enum class ArticulationGestureKind { OralVowel, Frication, Nasal, Plosive, VoicedFrication, VoicedPlosive, Affricate };
[[nodiscard]] inline bool isVoicedGesture(ArticulationGestureKind kind) noexcept {
  return kind == ArticulationGestureKind::OralVowel || kind == ArticulationGestureKind::Nasal || kind == ArticulationGestureKind::VoicedFrication || kind == ArticulationGestureKind::VoicedPlosive;
}
[[nodiscard]] inline bool isNoiseGesture(ArticulationGestureKind kind) noexcept {
  return kind==ArticulationGestureKind::Frication || kind==ArticulationGestureKind::Plosive || kind==ArticulationGestureKind::VoicedFrication;
}
// Every gesture the aperiodic lane owns, which is the noise gestures plus the affricate whose
// closure, burst and tail are all unvoiced.
[[nodiscard]] inline bool isAperiodicGesture(ArticulationGestureKind kind) noexcept {
  return isNoiseGesture(kind) || kind == ArticulationGestureKind::Affricate;
}
struct FricationBinding final { std::string phone; FricationConfig source; std::optional<double> voicingGain{}; };
struct PlosiveBinding final { std::string phone; FricationConfig source; double burstMilliseconds{10.0}; std::optional<VoiceRecipe::VoicedClosure> voicedClosure{}; };
struct AffricateBinding final { std::string phone; FricationConfig burst; FricationConfig tail; double burstMilliseconds{10.0}; };
struct AffricateConfig final {
  PlosiveConfig release;
  FricationConfig tail;
  std::uint32_t tailFrames{0U};
  friend bool operator==(const AffricateConfig&, const AffricateConfig&) = default;
};
struct ArticulationGesture final {
  ArticulationGestureKind kind;
  domain::PhonemeKey key;
  std::string phone;
  synthesis::PhraseFrameRange span;
  std::optional<FricationConfig> frication;
  std::optional<PlosiveConfig> plosive{};
  std::optional<double> voicingGain{};
  std::optional<VoicedPlosiveConfig> voicedPlosive{};
  std::optional<AffricateConfig> affricate{};
};
// Immutable prepared gestures. Explicit bindings request a DSP source; they
// do not prove that its output realizes the named phone intelligibly.
class ArticulationPlan final {
public:
  static constexpr std::uint32_t algorithmRevision = 10U;
  // The shortest frication a released closure may continue into before the pair stops being an
  // affricate and becomes a stop with a separate fricative.
  static constexpr double kMinimumAffricateTailMilliseconds{20.0};
  // Versions the affricate composition rule itself: how a released closure, its burst and its
  // frication tail are split inside one gesture.
  static constexpr std::uint32_t kAffricateModelRevision{1U};
  // Worker preparation from immutable recipe data; never invents onset timing
  // or borrows source settings from another style. Not acoustic qualification.
  [[nodiscard]] static core::Result<ArticulationPlan> compileRecipe(
      const synthesis::ProceduralSingerResource& resource,
      const synthesis::CompiledScorePerformance& performance,
      std::span<const domain::PhonemeToken> phones, std::string_view style,
      std::stop_token stop = {}, bool allowVoicedFrication = true, bool allowVoicedStops = true);
  [[nodiscard]] static core::Result<ArticulationPlan> compile(
      std::span<const domain::PhonemeToken> phones,
      std::span<const synthesis::PhonemeTimingAnchor> timing,
      std::span<const FricationBinding> bindings, std::uint32_t sampleRate,
      synthesis::PhraseFrameRange context, std::span<const std::string> nasalBindings = {},
      std::span<const PlosiveBinding> plosiveBindings = {},
      std::span<const AffricateBinding> affricateBindings = {});
  [[nodiscard]] std::span<const ArticulationGesture> gestures() const noexcept { return gestures_; }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] synthesis::PhraseFrameRange context() const noexcept { return context_; }
private:
  ArticulationPlan() = default;
  std::vector<ArticulationGesture> gestures_;
  std::uint32_t sampleRate_{0U};
  synthesis::PhraseFrameRange context_;
};
}
