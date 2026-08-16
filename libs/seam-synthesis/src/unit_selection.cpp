#include "seam/synthesis/unit_selection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace seam::synthesis {
namespace {

const domain::Note* noteFor(const domain::VocalRegion& region,
                            const domain::PhonemeToken& token) noexcept {
  return region.findNote(token.key.noteId);
}

bool phonesMatch(const voicebank::Unit& unit,
                 std::span<const domain::PhonemeToken> tokens,
                 std::size_t start) {
  if (unit.phones.empty() || start + unit.phones.size() > tokens.size()) return false;
  for (std::size_t index = 0; index < unit.phones.size(); ++index) {
    if (unit.phones[index] != tokens[start + index].symbol) return false;
  }
  return true;
}

double candidateScore(const voicebank::Unit& unit, std::int32_t targetMidi) noexcept {
  const auto pitchDistance = std::abs(unit.rootMidi - targetMidi);
  const auto kindBonus = unit.phones.size() > 1
      ? -2.0 * static_cast<double>(unit.phones.size() - 1U)
      : 0.0;
  const auto priorityBonus = -0.25 * static_cast<double>(unit.priority);
  const auto takePenalty = 0.001 * static_cast<double>(std::max(0, unit.take - 1));
  return static_cast<double>(pitchDistance) * 10.0 + kindBonus +
         priorityBonus + takePenalty;
}

const domain::UnitSelectionOverride* overrideFor(
    std::span<const domain::UnitSelectionOverride> overrides,
    domain::PhonemeKey startKey) noexcept {
  const auto iterator = std::find_if(overrides.begin(), overrides.end(),
      [startKey](const auto& value) { return value.startKey == startKey; });
  return iterator == overrides.end() ? nullptr : &*iterator;
}

}  // namespace

std::vector<UnitCandidate> UnitCandidateGenerator::generate(
    const voicebank::Manifest& manifest,
    const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens,
    std::string_view style,
    std::span<const domain::UnitSelectionOverride> overrides) const {
  std::vector<UnitCandidate> candidates;
  for (std::size_t start = 0; start < tokens.size(); ++start) {
    const auto* note = noteFor(region, tokens[start]);
    if (note == nullptr) continue;
    const auto* explicitOverride = overrideFor(overrides, tokens[start].key);
    for (const auto& unit : manifest.units) {
      if (!unit.enabled || unit.style != style || !phonesMatch(unit, tokens, start)) continue;
      if (explicitOverride != nullptr && unit.id != explicitOverride->unitId) continue;
      if (explicitOverride != nullptr &&
          unit.phones.size() != explicitOverride->tokenCount) {
        continue;
      }
      candidates.push_back(UnitCandidate{
          .unitId = unit.id,
          .tokenStart = start,
          .tokenCount = unit.phones.size(),
          .score = explicitOverride != nullptr ? -1'000'000.0
                                                : candidateScore(unit, note->midiKey),
          .targetMidi = note->midiKey,
          .forced = explicitOverride != nullptr,
          .renderer = explicitOverride != nullptr
              ? explicitOverride->renderer
              : domain::UnitRendererKind::Inherit,
      });
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.tokenStart != rhs.tokenStart) return lhs.tokenStart < rhs.tokenStart;
    if (lhs.forced != rhs.forced) return lhs.forced;
    if (lhs.score != rhs.score) return lhs.score < rhs.score;
    if (lhs.tokenCount != rhs.tokenCount) return lhs.tokenCount > rhs.tokenCount;
    return lhs.unitId < rhs.unitId;
  });
  return candidates;
}

core::Result<UnitPlan> DeterministicUnitSelector::select(
    const voicebank::Manifest& manifest,
    const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens,
    std::string_view style,
    std::span<const domain::UnitSelectionOverride> overrides) const {
  if (overrides.empty() && !region.unitSelectionOverrides.empty()) {
    overrides = region.unitSelectionOverrides;
  }
  if (tokens.empty()) {
    return core::failure<UnitPlan>(core::ErrorCode::InvalidArgument,
                                   "Unit selection requires phoneme tokens");
  }
  for (const auto& overrideValue : overrides) {
    const auto validation = overrideValue.validate();
    if (!validation) return core::Result<UnitPlan>{validation.error()};
    const auto token = std::find_if(tokens.begin(), tokens.end(),
        [&overrideValue](const auto& value) {
          return value.key == overrideValue.startKey;
        });
    if (token == tokens.end()) {
      // Overrides outside the currently rendered phrase are intentionally ignored.
      continue;
    }
    const auto* unit = manifest.findUnit(overrideValue.unitId);
    const auto start = static_cast<std::size_t>(std::distance(tokens.begin(), token));
    if (unit == nullptr || !unit->enabled || unit->style != style ||
        unit->phones.size() != overrideValue.tokenCount ||
        !phonesMatch(*unit, tokens, start)) {
      return core::failure<UnitPlan>(
          core::ErrorCode::Conflict,
          "Explicit unit selection no longer matches the phoneme sequence",
          overrideValue.startKey.toString() + " -> " + overrideValue.unitId);
    }
  }

  UnitCandidateGenerator generator;
  const auto candidates = generator.generate(manifest, region, tokens, style, overrides);
  std::vector<std::vector<UnitCandidate>> byStart(tokens.size());
  for (const auto& candidate : candidates) {
    byStart[candidate.tokenStart].push_back(candidate);
  }

  struct State final {
    double score{std::numeric_limits<double>::infinity()};
    std::size_t previous{0};
    std::optional<UnitCandidate> candidate;
  };
  std::vector<State> states(tokens.size() + 1U);
  states[0].score = 0.0;
  for (std::size_t position = 0; position < tokens.size(); ++position) {
    if (!std::isfinite(states[position].score)) continue;
    for (const auto& candidate : byStart[position]) {
      const auto next = position + candidate.tokenCount;
      if (next > tokens.size()) continue;
      const auto score = states[position].score + candidate.score;
      const bool better = score < states[next].score - 1.0e-9;
      const bool tied = std::abs(score - states[next].score) <= 1.0e-9;
      bool deterministicTie = false;
      if (tied) {
        if (!states[next].candidate.has_value()) {
          deterministicTie = true;
        } else if (candidate.forced != states[next].candidate->forced) {
          deterministicTie = candidate.forced;
        } else {
          deterministicTie =
              candidate.tokenCount > states[next].candidate->tokenCount ||
              (candidate.tokenCount == states[next].candidate->tokenCount &&
               candidate.unitId < states[next].candidate->unitId);
        }
      }
      if (better || deterministicTie) {
        states[next].score = score;
        states[next].previous = position;
        states[next].candidate = candidate;
      }
    }
  }
  if (!states.back().candidate.has_value()) {
    std::size_t firstUncovered = 0;
    while (firstUncovered < tokens.size() && std::isfinite(states[firstUncovered].score)) {
      ++firstUncovered;
    }
    return core::failure<UnitPlan>(core::ErrorCode::NotFound,
                                   "Voicebank cannot cover the phoneme sequence",
                                   firstUncovered < tokens.size()
                                       ? tokens[firstUncovered].key.toString()
                                       : "end");
  }

  UnitPlan plan;
  plan.totalScore = states.back().score;
  std::size_t cursor = tokens.size();
  while (cursor > 0) {
    const auto& state = states[cursor];
    if (!state.candidate.has_value()) {
      return core::failure<UnitPlan>(core::ErrorCode::Internal,
                                     "Unit selector backtracking failed");
    }
    const auto& candidate = *state.candidate;
    std::vector<std::string> alternatives;
    for (const auto& option : byStart[candidate.tokenStart]) {
      if (option.tokenCount == candidate.tokenCount && option.unitId != candidate.unitId) {
        alternatives.push_back(option.unitId);
      }
    }
    // The complete candidate list is useful to the Unit Lane even when an explicit
    // override is active. Reconstruct it from the manifest without allowing it to
    // influence selection.
    if (candidate.forced) {
      const auto* note = noteFor(region, tokens[candidate.tokenStart]);
      if (note != nullptr) {
        for (const auto& unit : manifest.units) {
          if (!unit.enabled || unit.style != style || unit.id == candidate.unitId ||
              !phonesMatch(unit, tokens, candidate.tokenStart) ||
              unit.phones.size() != candidate.tokenCount) {
            continue;
          }
          alternatives.push_back(unit.id);
        }
        std::sort(alternatives.begin(), alternatives.end());
        alternatives.erase(std::unique(alternatives.begin(), alternatives.end()),
                           alternatives.end());
      }
    }
    plan.entries.push_back(UnitPlanEntry{
        .unitId = candidate.unitId,
        .tokenStart = candidate.tokenStart,
        .tokenCount = candidate.tokenCount,
        .score = candidate.score,
        .targetMidi = candidate.targetMidi,
        .forced = candidate.forced,
        .renderer = candidate.renderer,
        .alternatives = std::move(alternatives),
    });
    cursor = state.previous;
  }
  std::reverse(plan.entries.begin(), plan.entries.end());
  return plan;
}

}  // namespace seam::synthesis
