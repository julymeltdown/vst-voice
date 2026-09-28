#pragma once
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/synthesis/singer_resource.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voice_design {
// The Designer's sustained-pose audition, shared so that what a producer hears, what a listening
// packet retains and what the committed fingerprints describe are one rendering: one second at
// 48 kHz of the recipe's compiled-F0 phonation through the pose's own tract, then the same safety
// limiter (a peak above 0.9 is scaled down to 0.9) and 5 ms edge ramps the Designer applies before
// playback. It is a synthesis preview, not a listening, intelligibility or identity judgement.
inline constexpr std::uint32_t kPoseAuditionSampleRate{48000U};
inline constexpr std::size_t kPoseAuditionFrames{48000U};
// Versions the audition definition itself: duration, score, limiter and ramps. Changing it is a
// deliberate act that invalidates every committed fingerprint.
inline constexpr std::uint32_t kPoseAuditionRevision{1U};
[[nodiscard]] core::Result<std::vector<float>> finalizeAuditionPcm(std::vector<float> samples,
    std::stop_token stopToken = {});
[[nodiscard]] core::Result<std::vector<float>> renderPoseAudition(const VoiceRecipe& recipe,
    std::string_view phone, std::string_view style, std::uint8_t midiKey, std::stop_token stopToken = {});

inline constexpr std::size_t kAuditionBandCount{12U};
// Reproducibility features of an audition's steady middle half. The digest covers the exact
// little-endian IEEE-754 Float32 bytes; the other features let a declared tolerance absorb
// floating-point differences while still catching a changed voice.
struct AuditionFingerprint final {
  std::string pcmSha256;
  std::uint64_t frames{0U};
  double peak{0.0}, rmsDbfs{0.0}, f0Hz{0.0}, periodicity{0.0}, spectralCentroidHz{0.0};
  // Share of the analysed power in each of twelve log-spaced bands from 80 Hz, in dB.
  std::array<double, kAuditionBandCount> bandLevelsDb{};
  friend bool operator==(const AuditionFingerprint&, const AuditionFingerprint&) = default;
};
// Declared engineering bounds for the same recipe, seed and runtime. They are not perceptual
// thresholds, and nothing here says they hold on another platform.
struct AuditionTolerance final {
  double peakRelative{1.0e-3}, rmsDb{0.01}, f0Cents{0.5}, periodicity{1.0e-3};
  double centroidRelative{1.0e-3}, bandLevelDb{0.05};
  friend bool operator==(const AuditionTolerance&, const AuditionTolerance&) = default;
};
// expectedF0Hz bounds the period search to seven semitones either side, which excludes octave
// errors without letting the estimate follow a different note.
[[nodiscard]] core::Result<AuditionFingerprint> fingerprintAudition(std::span<const float> samples,
    std::uint32_t sampleRate, double expectedF0Hz, std::stop_token stopToken = {});
// Success, or the first feature outside tolerance with both values and the bound.
[[nodiscard]] core::Result<void> compareAuditionFingerprint(const AuditionFingerprint& reference,
    const AuditionFingerprint& measured, const AuditionTolerance& tolerance);

// Click and discontinuity screening. Every 1 ms window's second-difference energy (a high-pass
// whose gain grows with frequency squared, so a step or an impulse dominates it) is divided by the
// larger of the medians of the twenty windows before it and the twenty after it, skipping the
// adjacent window on each side. An onset from silence or an offset into silence is compared with
// the side that holds the sound; a click inside a sustained sound exceeds both sides. This is a
// signal-processing screen, not a perceptual judgement.
struct DiscontinuityReport final {
  bool finite{true};
  double peak{0.0};
  double worstRatio{0.0};
  std::size_t worstFrame{0U};
};
[[nodiscard]] DiscontinuityReport measureDiscontinuity(std::span<const float> samples, std::uint32_t sampleRate);

struct AuditionCase final {
  std::string id, phone, style;
  std::uint8_t midiKey{60U};
  AuditionFingerprint fingerprint;
  friend bool operator==(const AuditionCase&, const AuditionCase&) = default;
};
// Committed evidence that one recipe and seed reproduce their auditions. The encoding always states
// that no listener reviewed the material and that no cross-platform tolerance has been measured;
// the decoder refuses a document that claims otherwise.
struct AuditionFingerprintManifest final {
  std::string recipeId, recipeHash, recipeSource, engineId;
  std::uint64_t seed{0U};
  std::uint32_t engineRevision{0U}, auditionRevision{kPoseAuditionRevision};
  AuditionTolerance tolerance;
  std::string measuredPlatform;
  std::vector<AuditionCase> cases;
  friend bool operator==(const AuditionFingerprintManifest&, const AuditionFingerprintManifest&) = default;
};
// Every oral vowel pose and the syllabic nasal N of every declared style, at each requested key.
[[nodiscard]] core::Result<std::vector<AuditionCase>> auditionCasesForRecipe(const VoiceRecipe& recipe,
    std::span<const std::uint8_t> midiKeys);
[[nodiscard]] core::Result<AuditionFingerprintManifest> buildAuditionFingerprintManifest(
    const synthesis::ProceduralSingerResource& resource, std::string recipeSource,
    std::span<const std::uint8_t> midiKeys, std::stop_token stopToken = {});
[[nodiscard]] std::string encodeAuditionFingerprintManifest(const AuditionFingerprintManifest& manifest);
[[nodiscard]] core::Result<AuditionFingerprintManifest> decodeAuditionFingerprintManifest(std::string_view json);
// Re-renders every committed case and compares it within the committed tolerance. A different
// recipe identity or seed is refused before anything is rendered: a changed voice needs its
// fingerprints regenerated on purpose, not a widened tolerance.
[[nodiscard]] core::Result<void> verifyAuditionFingerprints(const AuditionFingerprintManifest& committed,
    const synthesis::ProceduralSingerResource& resource, std::stop_token stopToken = {});
// The platform/toolchain identity recorded beside newly generated fingerprints.
[[nodiscard]] std::string auditionMeasurementPlatform();
}
