#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/synthesis/source_phoneme_alignment.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace seam::synthesis {

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

  friend bool operator==(const UnitPlanEntry&, const UnitPlanEntry&) = default;
};

struct UnitPlan final {
  std::vector<UnitPlanEntry> entries;
  double totalScore{0.0};

  friend bool operator==(const UnitPlan&, const UnitPlan&) = default;
};

class UnitCandidateGenerator final {
public:
  [[nodiscard]] std::vector<UnitCandidate> generate(
      const voicebank::Manifest& manifest,
      const domain::VocalRegion& region,
      std::span<const domain::PhonemeToken> tokens,
      std::string_view style,
      std::span<const domain::UnitSelectionOverride> overrides = {},
      std::span<const SourceAlignmentEvidence> alignments = {},
      bool requireNucleusAlignment = false) const;
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
      bool requireNucleusAlignment = false) const;
};

}  // namespace seam::synthesis
