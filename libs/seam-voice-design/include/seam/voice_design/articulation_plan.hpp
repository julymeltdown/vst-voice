#pragma once
#include "seam/voice_design/frication_source.hpp"
#include "seam/voice_design/plosive_source.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/transition_plan.hpp"

namespace seam::voice_design {
// Affricate is a released closure whose burst continues into a frication tail inside one
// bounded gesture. It is not a stop followed later by a fricative, and it is unvoiced today:
// a voiced affricate needs prevoiced closure plus voiced frication, and is refused rather than
// approximated with an unvoiced noise pair.
// Approximant is a voiced liquid or glide: the phonation continues through a resonance bank of
// its own and the gesture's defining sound is the bounded formant transition into its
// neighbouring vowel, not a noise source.
// Closure and Breath are declared event spans rather than articulated ones: a closure is exactly
// silent for the span its role resolves, and a breath is unvoiced broadband noise. Both keep the
// symbol that owns them, so a bank still holds one unit per symbol.
enum class ArticulationGestureKind { OralVowel, Frication, Nasal, Plosive, VoicedFrication, VoicedPlosive, Affricate, Approximant, VoicedAffricate, Closure, Breath };
[[nodiscard]] inline bool isVoicedGesture(ArticulationGestureKind kind) noexcept {
  return kind == ArticulationGestureKind::OralVowel || kind == ArticulationGestureKind::Nasal || kind == ArticulationGestureKind::VoicedFrication || kind == ArticulationGestureKind::VoicedPlosive || kind == ArticulationGestureKind::Approximant || kind == ArticulationGestureKind::VoicedAffricate;
}
[[nodiscard]] inline bool isNoiseGesture(ArticulationGestureKind kind) noexcept {
  return kind==ArticulationGestureKind::Frication || kind==ArticulationGestureKind::Plosive || kind==ArticulationGestureKind::VoicedFrication;
}
// Every gesture the aperiodic lane owns, which is the noise gestures plus the affricate whose
// closure, burst and tail are all unvoiced.
[[nodiscard]] inline bool isAperiodicGesture(ArticulationGestureKind kind) noexcept {
  return isNoiseGesture(kind) || kind == ArticulationGestureKind::Affricate ||
      kind == ArticulationGestureKind::VoicedAffricate || kind == ArticulationGestureKind::Breath;
}
struct FricationBinding final { std::string phone; FricationConfig source; std::optional<double> voicingGain{}; };
struct PlosiveBinding final { std::string phone; FricationConfig source; double burstMilliseconds{10.0}; std::optional<VoiceRecipe::VoicedClosure> voicedClosure{}; };
struct AffricateBinding final { std::string phone; FricationConfig burst; FricationConfig tail; double burstMilliseconds{10.0}; };
// A voiced affricate's release is the same closure and burst an unvoiced one uses, and its tail is
// voiced through the tract, so the binding carries the closure's voicing and the tail's gain.
struct VoicedAffricateBinding final {
  std::string phone;
  FricationConfig burst;
  FricationConfig tail;
  double burstMilliseconds{10.0};
  double closureVoicingGain{0.15}, closureLowpassHz{500.0};
  double tailVoicingGain{0.35};
};
struct ApproximantBinding final { std::string phone; double transitionMilliseconds{40.0}; };
// A closure binding declares that this phone's resolved span is silent. It carries no source,
// because inventing one would be exactly the substitution the event model refuses.
struct ClosureBinding final { std::string phone; };
struct BreathBinding final { std::string phone; FricationConfig source; };
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
  // Bounded formant transition for a voiced approximant, in frames. Zero means the generic
  // short crossfade a vowel-to-vowel change already uses.
  std::uint32_t transitionFrames{0U};
  // The resonance pose this gesture leaves in force from its start, which is what makes a
  // palatalized consonant palatalized rather than its base consonant sung with another label.
  // Empty for every gesture whose resonance is simply the pose named after its own phone, and
  // it is never a caller's choice: the frozen recipe has to declare the palatalized phone.
  std::optional<std::string> posePhone{};
};
// Immutable prepared gestures. Explicit bindings request a DSP source; they
// do not prove that its output realizes the named phone intelligibly.
class ArticulationPlan final {
public:
  static constexpr std::uint32_t algorithmRevision = 13U;
  // The engineering default, not a measured phonetic duration: how long a boundary that is not a
  // movement takes to blend one pose into the next. It is the window the renderer used to derive
  // for itself (sampleRate/50), now declared by the plan rather than guessed per gesture.
  static constexpr double kBoundaryCrossfadeMilliseconds{20.0};
  // Versions the transition plan: which boundaries produce a bounded acoustic overlap, which mode
  // each one uses, and what the composition says the layers do across it.
  static constexpr std::uint32_t kTransitionModelRevision{1U};
  // The shortest frication a released closure may continue into before the pair stops being an
  // affricate and becomes a stop with a separate fricative.
  static constexpr double kMinimumAffricateTailMilliseconds{20.0};
  // Versions the affricate composition rule itself: how a released closure, its burst and its
  // frication tail are split inside one gesture.
  static constexpr std::uint32_t kAffricateModelRevision{1U};
   // Versions the approximant rule: the transition length admitted for a liquid or glide gesture.
   static constexpr std::uint32_t kApproximantModelRevision{1U};
   // Versions the palatalized rule: a base consonant's release rendered through the palatal
   // resonance pose declared under the palatalized phone's own name.
  static constexpr std::uint32_t kPalatalizedModelRevision{1U};
  // Versions the voiced affricate rule: how a prevoiced closure, its burst and its voiced
  // frication tail are split inside one gesture.
  static constexpr std::uint32_t kVoicedAffricateModelRevision{1U};
  // Versions the event rules: which symbol a declared closure silences, and how a declared
  // breath's source fills the span its role resolves.
  static constexpr std::uint32_t kClosureEventModelRevision{1U};
  static constexpr std::uint32_t kBreathEventModelRevision{1U};
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
      std::span<const AffricateBinding> affricateBindings = {},
      std::span<const ApproximantBinding> approximantBindings = {},
      std::span<const std::string> palatalizedPhones = {},
      std::span<const VoicedAffricateBinding> voicedAffricateBindings = {},
      std::span<const ClosureBinding> closureBindings = {},
      std::span<const BreathBinding> breathBindings = {});
  [[nodiscard]] std::span<const ArticulationGesture> gestures() const noexcept { return gestures_; }
  // Every bounded acoustic overlap the renderer will perform, in frame order. The linguistic spans
  // above remain an ordered partition; this list never shortens, lengthens or reorders one.
  [[nodiscard]] std::span<const ArticulationTransition> transitions() const noexcept { return transitions_; }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] synthesis::PhraseFrameRange context() const noexcept { return context_; }
private:
  ArticulationPlan() = default;
  std::vector<ArticulationGesture> gestures_;
  std::vector<ArticulationTransition> transitions_;
  std::uint32_t sampleRate_{0U};
  synthesis::PhraseFrameRange context_;
};
}
