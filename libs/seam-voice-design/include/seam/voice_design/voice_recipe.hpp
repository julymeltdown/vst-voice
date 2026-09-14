#pragma once
#include "seam/core/result.hpp"
#include "seam/voice_design/frication_config.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seam::voice_design {
struct Phonation final {
  double openQuotient{0.6}, spectralTiltDbPerOctave{-12.0}, aspiration{0.05};
  friend bool operator==(const Phonation&, const Phonation&) = default;
};
struct ResonanceBand final {
  double frequencyHz{500.0}, bandwidthHz{80.0}, gainDb{0.0};
  friend bool operator==(const ResonanceBand&, const ResonanceBand&) = default;
};
struct NasalResonance final {
  double resonanceHz{250.0}, resonanceBandwidthHz{90.0};
  double antiresonanceHz{1000.0}, antiresonanceBandwidthHz{120.0};
  friend bool operator==(const NasalResonance&, const NasalResonance&) = default;
};
struct VoicePose final {
  std::string phone, style;
  double nasalCoupling{0.0};
  std::vector<ResonanceBand> formants;
  // Explicit model opt-in. Legacy coupling-only patches are not reinterpreted.
  std::optional<NasalResonance> nasal{};
  friend bool operator==(const VoicePose&, const VoicePose&) = default;
};
struct Modulation final {
  double jitterCents{0.0}, shimmerAmount{0.0}, rateHz{0.0};
  friend bool operator==(const Modulation&, const Modulation&) = default;
};
// Draft design-time data only: no score F0, executable script, source approval
// or installed-bank mutation. Runtime capabilities are a separate contract.
struct VoiceRecipe final {
  std::string id;
  std::string engineId{"seam.source-filter.v1"};
  std::uint64_t seed{0U};
  Phonation phonation;
  Modulation modulation;
  std::vector<VoicePose> poses;
  struct FricationPose final {
    std::string phone, style;
    FricationConfig source;
    // Explicit voiced-noise opt-in; requires a same-phone/style resonance pose.
    // Absent retains the legacy unvoiced source contract.
    std::optional<double> voicingGain{};
    friend bool operator==(const FricationPose&, const FricationPose&) = default;
  };
  std::vector<FricationPose> frications;
  struct VoicedClosure final {
    double gain{0.15}, lowpassHz{500.0};
    friend bool operator==(const VoicedClosure&,const VoicedClosure&)=default;
  };
  struct PlosivePose final {
    std::string phone, style;
    FricationConfig source;
    double burstMilliseconds{10.0};
    // Schema-six explicit opt-in. Absent retains unvoiced p/t/k semantics.
    std::optional<VoicedClosure> voicedClosure{};
    friend bool operator==(const PlosivePose&,const PlosivePose&)=default;
  };
  std::vector<PlosivePose> plosives;
  // Schema-seven explicit opt-in: a released closure whose burst is followed by a frication
  // tail, which is what makes an affricate different from a stop plus a later fricative.
  // Absent poses keep their previous meaning, so an old recipe never becomes an affricate by
  // virtue of naming a phone that a newer build would like to interpret.
  struct AffricatePose final {
    std::string phone, style;
    FricationConfig burst;
    FricationConfig tail;
    double burstMilliseconds{10.0};
    friend bool operator==(const AffricatePose&, const AffricatePose&) = default;
  };
  std::vector<AffricatePose> affricates;
  // Schema-ten explicit opt-in: a voiced affricate is a prevoiced closure, its release burst and a
  // voiced frication tail. It needs a same-phone resonance pose because the tail is voiced through
  // the tract, and it is refused rather than approximated when any of its three parts is missing.
  struct VoicedAffricatePose final {
    std::string phone, style;
    FricationConfig burst;
    FricationConfig tail;
    double burstMilliseconds{10.0};
    double closureVoicingGain{0.15}, closureLowpassHz{500.0};
    double tailVoicingGain{0.35};
    friend bool operator==(const VoicedAffricatePose&, const VoicedAffricatePose&) = default;
  };
  std::vector<VoicedAffricatePose> voicedAffricates;
  // Schema-eight explicit opt-in: a voiced approximant (liquid or glide) whose resonance comes
  // from the same-phone resonance pose and whose defining gesture is the bounded formant
  // transition into the neighbouring vowel. Absent poses keep their previous meaning, so an
  // unused consonant resonance bank in an older recipe does not become a sung glide.
  struct ApproximantPose final {
    std::string phone, style;
    double transitionMilliseconds{40.0};
    friend bool operator==(const ApproximantPose&, const ApproximantPose&) = default;
  };
  std::vector<ApproximantPose> approximants;
  // Schema-nine explicit opt-in: a palatalized consonant borrows its base consonant's release
  // source and is coloured by the resonance pose declared under its own phone name, so きゃ is
  // not か sung longer. Absent poses keep their previous meaning, so a recipe that merely names
  // a palatalized symbol never becomes one by virtue of the newer build reading it.
  struct PalatalizedPose final {
    std::string phone, style, basePhone;
    friend bool operator==(const PalatalizedPose&, const PalatalizedPose&) = default;
  };
  std::vector<PalatalizedPose> palatalized;
  // Schema-eleven explicit opt-in: an event phone is a declared span rather than a recorded
  // articulation. A closure (the moraic obstruent, an explicit closure, a pause or a glottal
  // occlusion) is exactly silent for the span its role resolves, and a breath is unvoiced
  // broadband noise from its declared source. Both keep their own symbol, so a bank still has a
  // unit per symbol; neither is inferred from a symbol a newer build happens to recognise.
  struct ClosurePose final {
    std::string phone, style;
    friend bool operator==(const ClosurePose&, const ClosurePose&) = default;
  };
  std::vector<ClosurePose> closures;
  struct BreathPose final {
    std::string phone, style;
    FricationConfig source;
    friend bool operator==(const BreathPose&, const BreathPose&) = default;
  };
  std::vector<BreathPose> breaths;
  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const VoiceRecipe&, const VoiceRecipe&) = default;
};
[[nodiscard]] core::Result<std::string> encodeVoiceRecipe(const VoiceRecipe& recipe);
[[nodiscard]] std::int64_t voiceRecipeSchemaVersion(const VoiceRecipe& recipe) noexcept;
[[nodiscard]] core::Result<VoiceRecipe> decodeVoiceRecipe(std::string_view json);
}
