#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace seam::voicebank {

// Automated signal checks for one take under the QC policy that applies to its
// unit. The result is technical evidence about exact bytes. It never approves a
// take musically: passing every applicable check still leaves listening and
// marker review to a person, and a format pass is not a quality judgement.
//
// Policy version 1 derives the policy from the canonical coverage key:
//   breath:*                         -> Breath   (audible, mostly unpitched)
//   special:<only br>                -> Breath
//   special:<only pau/sil>           -> Pause    (quiet)
//   special:<only cl/R/glottal>      -> Closure  (quiet gap before a release)
//   every other canonical key        -> Voiced   (audible, pitched at the root)
// A special key that mixes these phone classes is refused rather than guessed.
enum class TakeQcPolicy { Voiced, Breath, Closure, Pause };

enum class TakeCheckOutcome { Pass, Fail, Inapplicable };

enum class TakeCheck : std::size_t {
  Format,
  Finite,
  Clipping,
  DcOffset,
  SignalPresent,
  Quiet,
  Unvoiced,
  RootPitch,
};

inline constexpr std::size_t kTakeCheckCount = 8U;
inline constexpr std::array<TakeCheck, kTakeCheckCount> kTakeChecks{
    TakeCheck::Format, TakeCheck::Finite, TakeCheck::Clipping,
    TakeCheck::DcOffset, TakeCheck::SignalPresent, TakeCheck::Quiet,
    TakeCheck::Unvoiced, TakeCheck::RootPitch};

inline constexpr std::string_view kTakeInspectorId = "seam.take-inspector";
// The measurement algorithm. A new value means old receipts are not current.
inline constexpr std::string_view kTakeInspectorVersion = "2";
// The applicability matrix and thresholds below.
inline constexpr std::int64_t kTakeQcPolicyVersion = 1;

inline constexpr std::uint32_t kTakeSampleRate = 48000U;
inline constexpr double kTakeSignalFloorRms = 1.0e-4;      // -80 dBFS
inline constexpr double kTakeQuietCeilingRms = 3.0e-3;     // about -50 dBFS
inline constexpr double kTakeQuietCeilingPeak = 3.0e-2;    // about -30 dBFS
inline constexpr double kTakeMaximumDcOffset = 0.01;
inline constexpr float kTakeClipLevel = 0.9999F;
inline constexpr double kTakeMaximumVoicedShare = 0.5;     // breath
inline constexpr double kTakeRootPitchToleranceCents = 80.0;
// Analysis windows quieter than this are left out of the voiced share.
inline constexpr double kTakeEnergeticWindowRms = 1.0e-3;  // -60 dBFS
inline constexpr std::uint64_t kTakePitchMinimumFrames = 2048U;

using TakeCheckOutcomes = std::array<TakeCheckOutcome, kTakeCheckCount>;

// Every value that decides an outcome, so a stored receipt can be re-evaluated.
struct TakeMeasurements final {
  std::uint32_t sampleRate{0U};
  std::uint16_t channels{0U};
  std::uint16_t bitsPerSample{0U};
  std::uint64_t frameCount{0U};
  std::uint64_t nonFiniteSamples{0U};
  std::uint64_t clippedSamples{0U};
  float peak{0.0F};
  double rms{0.0};
  double dcOffset{0.0};
  // Voiced policy only.
  std::optional<std::int32_t> expectedRootMidi;
  std::optional<std::int32_t> analyzedRootMidi;
  std::optional<double> rootPitchDeviationCents;
  // Breath policy only: voiced share of the analysis windows that carry energy.
  std::optional<double> voicedShare;

  friend bool operator==(const TakeMeasurements&, const TakeMeasurements&) = default;
};

struct TakeInspection final {
  TakeQcPolicy policy{TakeQcPolicy::Voiced};
  std::string sourceSha256;
  std::uint64_t byteSize{0U};
  TakeMeasurements measurements;
  TakeCheckOutcomes checks{};

  [[nodiscard]] TakeCheckOutcome outcome(TakeCheck check) const noexcept {
    return checks[static_cast<std::size_t>(check)];
  }
  // True when no applicable check failed. Technical evidence, not approval.
  [[nodiscard]] bool accepted() const noexcept;
};

struct TakeInspectionRequest final {
  TakeQcPolicy policy{TakeQcPolicy::Voiced};
  // Required for Voiced, refused for every other policy.
  std::optional<std::int32_t> expectedRootMidi;
  std::uint64_t maximumBytes{kMaximumSupportedWavBytes};
};

[[nodiscard]] core::Result<TakeQcPolicy> takeQcPolicyForCoverageKey(
    std::string_view coverageKey);
[[nodiscard]] bool takeCheckApplies(TakeQcPolicy policy, TakeCheck check) noexcept;
// Pure policy-version-1 evaluation of recorded measurements.
[[nodiscard]] TakeCheckOutcomes evaluateTakeChecks(
    TakeQcPolicy policy, const TakeMeasurements& measurements) noexcept;
[[nodiscard]] bool takeChecksPassed(const TakeCheckOutcomes& checks) noexcept;

// Hashes the file before and after decoding, so the measurements describe
// exactly the bytes named by sourceSha256. Failures carry bounded diagnostics.
[[nodiscard]] core::Result<TakeInspection> inspectTake(
    const std::filesystem::path& path, const TakeInspectionRequest& request,
    std::stop_token stopToken = {});

[[nodiscard]] std::string_view takeQcPolicyName(TakeQcPolicy policy) noexcept;
[[nodiscard]] std::optional<TakeQcPolicy> parseTakeQcPolicy(std::string_view value) noexcept;
[[nodiscard]] std::string_view takeCheckName(TakeCheck check) noexcept;
[[nodiscard]] std::string_view takeCheckOutcomeName(TakeCheckOutcome outcome) noexcept;
[[nodiscard]] std::optional<TakeCheckOutcome> parseTakeCheckOutcome(std::string_view value) noexcept;

}  // namespace seam::voicebank
