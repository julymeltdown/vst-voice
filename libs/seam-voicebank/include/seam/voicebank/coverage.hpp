#pragma once

#include "seam/domain/project.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace seam::voicebank {

enum class CoverageIssueKind {
  MissingUnit,
  DisabledUnit,
  UnsupportedPitchRange,
  UnsupportedStyle,
};

struct UnitKindInventory final {
  UnitKind kind{UnitKind::Special};
  std::size_t enabled{0U};
  std::size_t disabled{0U};
};

struct VoicebankInventory final {
  domain::Language language{domain::Language::Unspecified};
  std::vector<std::string> styles;
  std::vector<std::int32_t> rootPitchLayers;
  std::vector<UnitKindInventory> unitKinds;
  std::vector<std::string> phoneSequences;
  std::size_t enabledUnitCount{0U};
  std::size_t disabledUnitCount{0U};
  bool hasSustain{false};
  bool hasRelease{false};
  bool hasBreath{false};
};

struct CoverageIssue final {
  CoverageIssueKind kind{CoverageIssueKind::MissingUnit};
  domain::TrackId trackId;
  domain::RegionId regionId;
  domain::PhonemeKey phonemeKey;
  std::string symbol;
  std::int32_t targetMidi{60};
  std::string requestedStyle;
  std::vector<std::string> relatedUnitIds;
  std::string diagnostic;
};

struct CoverageSummary final {
  std::size_t totalPhonemes{0U};
  std::size_t coveredPhonemes{0U};
  std::size_t missingUnitCount{0U};
  std::size_t disabledUnitCount{0U};
  std::size_t unsupportedPitchRangeCount{0U};
  std::size_t unsupportedStyleCount{0U};
};

struct VoicebankCoverageReport final {
  VoicebankInventory inventory;
  CoverageSummary summary;
  std::vector<CoverageIssue> issues;

  [[nodiscard]] bool complete() const noexcept {
    return issues.empty() && summary.coveredPhonemes == summary.totalPhonemes;
  }
};

class VoicebankCoverageAnalyzer final {
public:
  [[nodiscard]] static VoicebankInventory inventory(const Manifest& manifest);

  [[nodiscard]] static VoicebankCoverageReport analyzeRegion(
      const Manifest& manifest,
      domain::TrackId trackId,
      const domain::VocalRegion& region,
      std::span<const domain::PhonemeToken> tokens,
      std::string_view style,
      std::int32_t maximumPitchDistanceSemitones = 12);
};

[[nodiscard]] std::string_view coverageIssueKindName(
    CoverageIssueKind kind) noexcept;

}  // namespace seam::voicebank
