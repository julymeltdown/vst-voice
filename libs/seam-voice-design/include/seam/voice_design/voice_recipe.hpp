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
  struct PlosivePose final {
    std::string phone, style;
    FricationConfig source;
    double burstMilliseconds{10.0};
    friend bool operator==(const PlosivePose&,const PlosivePose&)=default;
  };
  std::vector<PlosivePose> plosives;
  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const VoiceRecipe&, const VoiceRecipe&) = default;
};
[[nodiscard]] core::Result<std::string> encodeVoiceRecipe(const VoiceRecipe& recipe);
[[nodiscard]] std::int64_t voiceRecipeSchemaVersion(const VoiceRecipe& recipe) noexcept;
[[nodiscard]] core::Result<VoiceRecipe> decodeVoiceRecipe(std::string_view json);
}
