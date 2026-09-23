#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank {

// Measured acoustic analysis, as a stored and versioned contract.
//
// Why this exists as its own artifact rather than as a call: the analyser was
// already good enough to decide voicing, but every caller re-ran it and threw
// the result away. Two consequences. First, the renderers and the producer QC
// could disagree about where voicing begins, because each derived it separately
// from pitch marks and markers. Second, nothing recorded which algorithm
// produced a conclusion, so improving the analyser silently reinterpreted banks
// whose stored descriptions had come from the older one.
//
// This type is what makes "regenerate derivatives after algorithm changes" a
// checkable statement instead of an intention: the algorithm identity is part of
// the stored record, and validation fails when it no longer matches the code.

// Identity of the analysis implementation. Bump the version whenever the
// analyser answers could change, and every stored analysis becomes stale in a
// way that is detected rather than assumed away.
inline constexpr std::string_view kAcousticAnalysisAlgorithmId =
    "seam.pitch.fft-autocorrelation";
inline constexpr std::string_view kAcousticAnalysisAlgorithmVersion = "1";
inline constexpr std::string_view kAcousticAnalysisFormatId =
    "com.project-seam.acoustic-analysis";

// The one analysis configuration the product means when it says "the analysis".
//
// Three sites need to agree on this: the producer that writes a draft, the QC
// that reviews it, and anything that re-measures an installed bank. When each
// wrote its own constants they agreed only by convention, and the way that fails
// is not a compile error -- it is a bank whose stored analysis and freshly
// measured analysis disagree because one of the three changed. Three separate
// resamplers in this repository drifted that exact way.
inline constexpr std::size_t kProducerFrameSize = 2048U;
inline constexpr std::size_t kProducerHopSize = 256U;
inline constexpr double kProducerMinimumHz = 60.0;
inline constexpr double kProducerMaximumHz = 1200.0;
inline constexpr double kProducerVoicingThreshold = 0.32;

[[nodiscard]] PitchConfig producerPitchConfig() noexcept;
// Butterfly budget for one take of `frames` samples: both transforms of a
// 2*frameSize FFT per analysed frame.
[[nodiscard]] std::uint64_t producerAnalysisWork(std::size_t frames) noexcept;
// Analysis limits that admit exactly one take of `frames` samples under that
// budget, so a caller cannot silently exceed it.
[[nodiscard]] PitchAnalysisLimits producerPitchLimits(std::size_t frames) noexcept;
[[nodiscard]] PitchMarkGenerationConfig producerPitchMarkConfig() noexcept;

// One contiguous run of samples with a single voicing conclusion. Spans are
// merged from per-frame decisions, so their count tracks how often the source
// actually alternates rather than how long it is.
struct AcousticVoicingSpan final {
  time::SampleFrame start{0};
  time::SampleFrame end{0};  // exclusive
  bool voiced{false};
  // Median voiced fundamental across the span, in Hz. Zero when unvoiced: an
  // unvoiced span has no fundamental to report, and filling one in would be
  // fabricating a measurement.
  double f0Hz{0.0};
  // Mean analysis confidence over the span, in [0, 1]. Low values mean the
  // analyser was unsure; consumers must not read a low-confidence span as a
  // measurement. It is a proposal, not a label.
  double confidence{0.0};

  friend bool operator==(const AcousticVoicingSpan&, const AcousticVoicingSpan&) = default;
};

struct AcousticAnalysisLimits final {
  std::size_t maximumSpans{4096U};
  PitchAnalysisLimits pitch{};
};

struct AcousticAnalysis final {
  static constexpr std::int32_t kSchemaVersion = 1;

  std::string unitId;
  // Digest of the exact encoded audio these conclusions were measured from.
  std::string audioSha256;
  std::uint32_t sampleRate{0};
  time::SampleFrame decodedFrames{0};
  std::string algorithmId;
  std::string algorithmVersion;
  std::vector<AcousticVoicingSpan> spans;

  // True when this record was produced by the analysis implementation currently
  // compiled in. False means the derivatives must be regenerated before use.
  [[nodiscard]] bool currentAlgorithm() const noexcept;
  [[nodiscard]] const AcousticVoicingSpan* spanAt(time::SampleFrame frame) const noexcept;

  friend bool operator==(const AcousticAnalysis&, const AcousticAnalysis&) = default;
};

// Measures one unit audio and binds the result to those exact bytes.
// verifiedAudioSha256 must be the digest of the encoded audio the samples were
// decoded from, so the record cannot be detached from its source.
//
// There is deliberately no configuration parameter. The record carries an
// algorithm identity, so a caller able to pass a different configuration would be
// able to store numbers that claim to be the canonical analysis while being a
// different measurement -- the same defect as an analysis that does not say which
// audio it came from. One entry point means the identity is true by construction.
[[nodiscard]] core::Result<AcousticAnalysis> analyzeUnitAcoustics(
    std::span<const float> samples, std::uint32_t sampleRate, const Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames,
    AcousticAnalysisLimits limits = {}, std::stop_token stopToken = {});

// Structural plus binding validation: unit identity, digest match, decoded
// bounds, ordered non-overlapping full-coverage spans, finite values in range,
// and the algorithm identity matching the current implementation.
[[nodiscard]] core::Result<void> validateAcousticAnalysis(
    const AcousticAnalysis& analysis, const Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames);

[[nodiscard]] core::Result<std::string> encodeAcousticAnalysis(
    const AcousticAnalysis& analysis, const Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames);

[[nodiscard]] core::Result<AcousticAnalysis> decodeAcousticAnalysis(
    std::string_view json, const Unit& unit, std::string_view verifiedAudioSha256,
    time::SampleFrame decodedFrames);

// Bank-relative location of a unit stored analysis. Keyed by the unit ID digest
// so an ID is never interpreted as a path, matching the alignment sidecar
// convention.
[[nodiscard]] std::string acousticAnalysisSidecarPath(std::string_view unitId);

// Voicing at a sample, or nullopt outside the analysed range or when the frame
// falls in no span. Callers must handle the unknown case rather than assuming.
[[nodiscard]] std::optional<bool> acousticVoicedAt(
    const AcousticAnalysis& analysis, time::SampleFrame frame) noexcept;

}  // namespace seam::voicebank
