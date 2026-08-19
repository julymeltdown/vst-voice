#include "seam/voicebank/coverage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>

namespace seam::voicebank {
namespace {

constexpr std::array<UnitKind, 10U> kKinds{
    UnitKind::Cv, UnitKind::Vcv, UnitKind::Vc, UnitKind::Vv, UnitKind::Cc,
    UnitKind::Sustain, UnitKind::Release, UnitKind::Breath,
    UnitKind::Glottal, UnitKind::Special};

std::string phoneSequence(const Unit& unit) {
  std::string result;
  for (std::size_t index = 0; index < unit.phones.size(); ++index) {
    if (index != 0U) result.push_back(' ');
    result += unit.phones[index];
  }
  return result;
}

bool phonesMatch(const Unit& unit,
                 std::span<const domain::PhonemeToken> tokens,
                 std::size_t start) {
  if (unit.phones.empty() || start + unit.phones.size() > tokens.size()) {
    return false;
  }
  for (std::size_t index = 0; index < unit.phones.size(); ++index) {
    if (unit.phones[index] != tokens[start + index].symbol) return false;
  }
  return true;
}

const domain::Note* noteFor(const domain::VocalRegion& region,
                            const domain::PhonemeToken& token) noexcept {
  return region.findNote(token.key.noteId);
}

std::string diagnosticFor(CoverageIssueKind kind,
                          std::string_view symbol,
                          std::string_view style,
                          std::int32_t targetMidi,
                          std::int32_t maximumPitchDistanceSemitones) {
  std::ostringstream stream;
  switch (kind) {
    case CoverageIssueKind::MissingUnit:
      stream << "No voicebank unit can cover phoneme '" << symbol << "'";
      break;
    case CoverageIssueKind::DisabledUnit:
      stream << "Matching voicebank unit is disabled for phoneme '" << symbol
             << "'";
      break;
    case CoverageIssueKind::UnsupportedPitchRange:
      stream << "Matching unit is outside the supported pitch distance for MIDI "
             << targetMidi << " (maximum " << maximumPitchDistanceSemitones
             << " semitones)";
      break;
    case CoverageIssueKind::UnsupportedStyle:
      stream << "Matching unit does not support requested style '" << style
             << "'";
      break;
  }
  return stream.str();
}

}  // namespace

VoicebankInventory VoicebankCoverageAnalyzer::inventory(
    const Manifest& manifest) {
  VoicebankInventory result;
  result.language = manifest.language;
  result.styles = manifest.styles;
  std::sort(result.styles.begin(), result.styles.end());
  result.styles.erase(std::unique(result.styles.begin(), result.styles.end()),
                      result.styles.end());

  std::set<std::int32_t> rootPitches;
  std::set<std::string> sequences;
  for (const auto& kind : kKinds) {
    result.unitKinds.push_back(UnitKindInventory{.kind = kind});
  }
  for (const auto& unit : manifest.units) {
    rootPitches.insert(unit.rootMidi);
    sequences.insert(phoneSequence(unit));
    auto* kind = &*std::find_if(
        result.unitKinds.begin(), result.unitKinds.end(),
        [&unit](const UnitKindInventory& value) { return value.kind == unit.kind; });
    if (unit.enabled) {
      ++result.enabledUnitCount;
      ++kind->enabled;
    } else {
      ++result.disabledUnitCount;
      ++kind->disabled;
    }
    result.hasSustain = result.hasSustain || unit.kind == UnitKind::Sustain;
    result.hasRelease = result.hasRelease || unit.kind == UnitKind::Release;
    result.hasBreath = result.hasBreath || unit.kind == UnitKind::Breath;
  }
  result.rootPitchLayers.assign(rootPitches.begin(), rootPitches.end());
  result.phoneSequences.assign(sequences.begin(), sequences.end());
  return result;
}

VoicebankCoverageReport VoicebankCoverageAnalyzer::analyzeRegion(
    const Manifest& manifest,
    domain::TrackId trackId,
    const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens,
    std::string_view style,
    std::int32_t maximumPitchDistanceSemitones) {
  VoicebankCoverageReport report;
  report.inventory = inventory(manifest);
  report.summary.totalPhonemes = tokens.size();
  if (tokens.empty()) return report;

  const auto pitchLimit = std::max(0, maximumPitchDistanceSemitones);
  std::vector<bool> covered(tokens.size(), false);

  for (std::size_t start = 0; start < tokens.size(); ++start) {
    const auto* note = noteFor(region, tokens[start]);
    if (note == nullptr) continue;
    for (const auto& unit : manifest.units) {
      if (!unit.enabled || unit.style != style ||
          !phonesMatch(unit, tokens, start) ||
          std::abs(unit.rootMidi - static_cast<std::int32_t>(note->midiKey)) >
              pitchLimit) {
        continue;
      }
      for (std::size_t offset = 0; offset < unit.phones.size(); ++offset) {
        covered[start + offset] = true;
      }
    }
  }

  report.summary.coveredPhonemes = static_cast<std::size_t>(
      std::count(covered.begin(), covered.end(), true));
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (covered[index]) continue;
    const auto* note = noteFor(region, tokens[index]);
    const auto targetMidi = note == nullptr
                                ? 60
                                : static_cast<std::int32_t>(note->midiKey);
    bool hasDisabled = false;
    bool hasOtherStyle = false;
    bool hasPitchMismatch = false;
    std::vector<std::string> related;
    for (const auto& unit : manifest.units) {
      if (!phonesMatch(unit, tokens, index)) continue;
      related.push_back(unit.id);
      if (!unit.enabled) {
        hasDisabled = true;
      } else if (unit.style != style) {
        hasOtherStyle = true;
      } else if (std::abs(unit.rootMidi - targetMidi) > pitchLimit) {
        hasPitchMismatch = true;
      }
    }
    std::sort(related.begin(), related.end());
    related.erase(std::unique(related.begin(), related.end()), related.end());

    CoverageIssueKind kind = CoverageIssueKind::MissingUnit;
    if (hasDisabled) {
      kind = CoverageIssueKind::DisabledUnit;
      ++report.summary.disabledUnitCount;
    } else if (hasOtherStyle) {
      kind = CoverageIssueKind::UnsupportedStyle;
      ++report.summary.unsupportedStyleCount;
    } else if (hasPitchMismatch) {
      kind = CoverageIssueKind::UnsupportedPitchRange;
      ++report.summary.unsupportedPitchRangeCount;
    } else {
      ++report.summary.missingUnitCount;
    }
    report.issues.push_back(CoverageIssue{
        .kind = kind,
        .trackId = trackId,
        .regionId = region.id,
        .phonemeKey = tokens[index].key,
        .symbol = tokens[index].symbol,
        .targetMidi = targetMidi,
        .requestedStyle = std::string{style},
        .relatedUnitIds = std::move(related),
        .diagnostic = diagnosticFor(kind, tokens[index].symbol, style,
                                    targetMidi, pitchLimit),
    });
  }
  return report;
}

std::string_view coverageIssueKindName(CoverageIssueKind kind) noexcept {
  switch (kind) {
    case CoverageIssueKind::MissingUnit: return "missing-unit";
    case CoverageIssueKind::DisabledUnit: return "disabled-unit";
    case CoverageIssueKind::UnsupportedPitchRange:
      return "unsupported-pitch-range";
    case CoverageIssueKind::UnsupportedStyle: return "unsupported-style";
  }
  return "unknown";
}

}  // namespace seam::voicebank
