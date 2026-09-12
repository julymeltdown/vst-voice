#include "seam/voicebank/coverage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>
#include <unordered_map>
#include <optional>

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

std::string diagnosticFor(CoverageIssueKind kind,
                          std::string_view symbol,
                          std::string_view style,
                          std::int32_t targetMidi,
                          std::int32_t maximumPitchDistanceSemitones) {
  std::ostringstream stream;
  switch (kind) {
    case CoverageIssueKind::SequenceConflict:
      stream << "Matching units overlap; no non-overlapping unit sequence covers phoneme '" << symbol << "' in the selected cover";
      break;
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
  std::unordered_map<domain::NoteId, std::int32_t> notePitches;
  for (const auto& note : region.notes) notePitches.emplace(note.id, note.midiKey);
  std::vector<std::optional<std::int32_t>> pitches;
  pitches.reserve(tokens.size());
  for (const auto& token : tokens) {
    const auto note = notePitches.find(token.key.noteId);
    pitches.push_back(note == notePitches.end() ? std::nullopt : std::optional<std::int32_t>{note->second});
  }
  std::vector<std::size_t> best(tokens.size() + 1U, 0U), chosenLength(tokens.size(), 0U);
  std::vector<const Unit*> witness(tokens.size(), nullptr), pitchWitness(tokens.size(), nullptr), orphanWitness(tokens.size(), nullptr);
  std::vector<const Unit*> disabledWitness(tokens.size(), nullptr), styleWitness(tokens.size(), nullptr);
  // Maximum non-overlapping coverage, allowing gaps for useful diagnostics.
  // Prefer a longer unit at the earliest position on equal covered counts.
  for (std::size_t start = tokens.size(); start-- > 0U;) {
    best[start] = best[start + 1U];
    for (const auto& unit : manifest.units) {
      if (!phonesMatch(unit, tokens, start)) continue;
      if (!unit.enabled || unit.style != style) {
        auto& destination = !unit.enabled ? disabledWitness : styleWitness;
        for (std::size_t offset = 0; offset < unit.phones.size(); ++offset)
          if (!destination[start + offset]) destination[start + offset] = &unit;
        continue;
      }
      bool owners = true, pitch = true;
      for (std::size_t offset = 0; offset < unit.phones.size(); ++offset) {
        const auto value = pitches[start + offset];
        owners = owners && value.has_value();
        if (value && std::abs(static_cast<std::int64_t>(unit.rootMidi) - *value) > pitchLimit) pitch = false;
      }
      if (!owners) {
        for (std::size_t offset = 0; offset < unit.phones.size(); ++offset)
          if (!orphanWitness[start + offset]) orphanWitness[start + offset] = &unit;
        continue;
      }
      if (!pitch) {
        for (std::size_t offset = 0; offset < unit.phones.size(); ++offset)
          if (!pitchWitness[start + offset]) pitchWitness[start + offset] = &unit;
        continue;
      }
      for (std::size_t offset = 0; offset < unit.phones.size(); ++offset)
        if (!witness[start + offset]) witness[start + offset] = &unit;
      const auto score = unit.phones.size() + best[start + unit.phones.size()];
      if (score > best[start] || (score == best[start] && unit.phones.size() > chosenLength[start])) {
        best[start] = score; chosenLength[start] = unit.phones.size();
      }
    }
  }
  for (std::size_t start = 0U; start < tokens.size();) {
    if (chosenLength[start] == 0U) { ++start; continue; }
    const auto end = start + chosenLength[start];
    for (; start < end; ++start) covered[start] = true;
  }
  report.summary.coveredPhonemes = static_cast<std::size_t>(
      std::count(covered.begin(), covered.end(), true));
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (covered[index]) continue;
    const auto targetMidi = pitches[index].value_or(60);
    std::vector<std::string> related;

    CoverageIssueKind kind = CoverageIssueKind::MissingUnit;
    if (witness[index]) {
      kind = CoverageIssueKind::SequenceConflict; ++report.summary.sequenceConflictCount;
      related = {witness[index]->id};
    } else if (pitchWitness[index]) {
      kind = CoverageIssueKind::UnsupportedPitchRange; ++report.summary.unsupportedPitchRangeCount;
      related = {pitchWitness[index]->id};
    } else if (orphanWitness[index] || !pitches[index]) {
      ++report.summary.missingUnitCount;
      if (orphanWitness[index]) related = {orphanWitness[index]->id};
    } else if (disabledWitness[index]) {
      kind = CoverageIssueKind::DisabledUnit;
      ++report.summary.disabledUnitCount;
      related = {disabledWitness[index]->id};
    } else if (styleWitness[index]) {
      kind = CoverageIssueKind::UnsupportedStyle;
      ++report.summary.unsupportedStyleCount;
      related = {styleWitness[index]->id};
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
        .diagnostic = !pitches[index] ? "Phoneme references a missing note; coverage cannot be established" :
            pitchWitness[index] && !witness[index] ? "Matching unit span includes a note outside the supported pitch distance" :
            orphanWitness[index] && !witness[index] ? "Matching unit span references a missing note" :
            diagnosticFor(kind, tokens[index].symbol, style, targetMidi, pitchLimit),
    });
  }
  return report;
}

std::string_view coverageIssueKindName(CoverageIssueKind kind) noexcept {
  switch (kind) {
    case CoverageIssueKind::SequenceConflict: return "sequence-conflict";
    case CoverageIssueKind::MissingUnit: return "missing-unit";
    case CoverageIssueKind::DisabledUnit: return "disabled-unit";
    case CoverageIssueKind::UnsupportedPitchRange:
      return "unsupported-pitch-range";
    case CoverageIssueKind::UnsupportedStyle: return "unsupported-style";
  }
  return "unknown";
}

}  // namespace seam::voicebank
