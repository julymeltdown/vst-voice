#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/synthesis/source_phoneme_alignment.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::synthesis {

inline constexpr std::uint32_t kUnitSelectionRevision = 2U;
inline constexpr std::size_t kMaximumSelectionTokens = 4096U;
inline constexpr std::size_t kMaximumSelectionStatesAtBoundary = 256U;
enum class SelectionWork : std::size_t { Matching, Candidates, States, Edges, Samples, MetadataBytes };
// Shared by both style arms, alignment discovery, enumeration and analysis.
// Limits may be reduced for a caller, never raised above the hard ceilings.
struct UnitSelectionBudget final {
  static constexpr std::array<std::size_t, 6> ceilings{
      8U * 1024U * 1024U, 65536U, 65536U, 4U * 1024U * 1024U,
      16U * 1024U * 1024U, 8U * 1024U * 1024U};
  std::array<std::size_t, 6> limits{ceilings};
  std::array<std::size_t, 6> used{};
  [[nodiscard]] core::Result<void> spend(SelectionWork work, std::size_t amount,
                                       std::stop_token stop = {});
};

struct SourceBoundaryFeatures final {
  double levelDb{-180.0};
  std::array<double, 4> correlation{};
  friend bool operator==(const SourceBoundaryFeatures&, const SourceBoundaryFeatures&) = default;
};
struct UnitJoinAnalysis final {
  std::string unitId;
  std::string audioSha256;
  SourceBoundaryFeatures head;
  SourceBoundaryFeatures tail;
};
// Source-domain proxy only: first/last 20 ms of the playable marker crop,
// mono arithmetic downmix, gain-aware RMS, normalized autocorrelation at
// 1/2/4/8 nominal 48 kHz frame delays (rounded at the source sample rate).
// Does not estimate the eventual pitch/time-mapped or seam-composed boundary.
[[nodiscard]] core::Result<UnitJoinAnalysis> analyzeUnitJoin(
    const voicebank::Unit& unit, const voicebank::AudioBuffer& audio,
    std::string_view verifiedAudioSha256, UnitSelectionBudget& budget,
    std::stop_token stop = {});

struct UnitSelectionContext final {
  std::span<const UnitJoinAnalysis> analysis{};
  bool requireAcoustic{false};
  UnitSelectionBudget* budget{nullptr};
  std::stop_token stop{};
};

struct UnitSelectionRationale final {
  bool acoustic{false};
  bool joined{false};
  std::string predecessor{};
  double incomingCost{0.0};
  double cumulativeCost{0.0};
  std::uint32_t revision{kUnitSelectionRevision};
  std::string evidenceHash{};
  friend bool operator==(const UnitSelectionRationale&, const UnitSelectionRationale&) = default;
};

// Current renderers expose a leading onset, first nucleus and final end only.
// Do not select a unit that would hide an explicitly edited interior boundary.
[[nodiscard]] bool supportsExplicitPhonemeTiming(std::span<const domain::PhonemeToken> tokens);
[[nodiscard]] bool hasMultipleNuclei(std::span<const domain::PhonemeToken> tokens);

// Borrowed evidence from the frozen resource owner, never an unchecked unit-ID flag.
struct SourceAlignmentEvidence final {
  const SourcePhonemeAlignment* alignment{nullptr};
  std::string_view verifiedAudioSha256;
  time::SampleFrame decodedFrames{0};
};
[[nodiscard]] bool supportsAlignedPhonemeTiming(
    const voicebank::Unit& unit, std::span<const domain::PhonemeToken> tokens,
    std::span<const SourceAlignmentEvidence> alignments);

struct UnitCandidate final {
  std::string unitId;
  std::size_t tokenStart{0};
  std::size_t tokenCount{0};
  double score{0.0};
  std::int32_t targetMidi{60};
  bool forced{false};
  domain::UnitRendererKind renderer{domain::UnitRendererKind::Inherit};

  friend bool operator==(const UnitCandidate&, const UnitCandidate&) = default;
};

struct UnitPlanEntry final {
  std::string unitId;
  std::size_t tokenStart{0};
  std::size_t tokenCount{0};
  double score{0.0};
  std::int32_t targetMidi{60};
  bool forced{false};
  domain::UnitRendererKind renderer{domain::UnitRendererKind::Inherit};
  std::vector<std::string> alternatives;
  UnitSelectionRationale rationale{};

  friend bool operator==(const UnitPlanEntry&, const UnitPlanEntry&) = default;
};

struct UnitPlan final {
  std::vector<UnitPlanEntry> entries;
  double totalScore{0.0};
  std::string selectionContextHash{};

  friend bool operator==(const UnitPlan&, const UnitPlan&) = default;
};

[[nodiscard]] std::string describeUnitSelection(const UnitPlanEntry& entry);

class UnitCandidateGenerator final {
public:
  [[nodiscard]] core::Result<std::vector<UnitCandidate>> generate(
      const voicebank::Manifest& manifest,
      const domain::VocalRegion& region,
      std::span<const domain::PhonemeToken> tokens,
      std::string_view style,
      std::span<const domain::UnitSelectionOverride> overrides = {},
      std::span<const SourceAlignmentEvidence> alignments = {},
      bool requireNucleusAlignment = false,
      UnitSelectionContext context = {}) const;
};

class DeterministicUnitSelector final {
public:
  [[nodiscard]] core::Result<UnitPlan> select(
      const voicebank::Manifest& manifest,
      const domain::VocalRegion& region,
      std::span<const domain::PhonemeToken> tokens,
      std::string_view style,
      std::span<const domain::UnitSelectionOverride> overrides = {},
      std::span<const SourceAlignmentEvidence> alignments = {},
      bool requireNucleusAlignment = false,
      UnitSelectionContext context = {}) const;
};

}  // namespace seam::synthesis
