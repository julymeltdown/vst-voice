#include "seam/synthesis/unit_selection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

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
  const auto kindBonus = unit.phones.size() > 1 ?
      -2.0 * static_cast<double>(unit.phones.size() - 1U) : 0.0;
  const auto priorityBonus = -0.25 * static_cast<double>(unit.priority);
  const auto takePenalty = 0.001 * static_cast<double>(std::max(0, unit.take - 1));
  return static_cast<double>(pitchDistance) * 10.0 + kindBonus +
         priorityBonus + takePenalty;
}

}  // namespace

std::vector<UnitCandidate> UnitCandidateGenerator::generate(
    const voicebank::Manifest& manifest,
    const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens,
    std::string_view style) const {
  std::vector<UnitCandidate> candidates;
  for (std::size_t start = 0; start < tokens.size(); ++start) {
    const auto* note = noteFor(region, tokens[start]);
    if (note == nullptr) continue;
    for (const auto& unit : manifest.units) {
      if (!unit.enabled || unit.style != style || !phonesMatch(unit, tokens, start)) continue;
      candidates.push_back(UnitCandidate{
          .unitId = unit.id,
          .tokenStart = start,
          .tokenCount = unit.phones.size(),
          .score = candidateScore(unit, note->midiKey),
          .targetMidi = note->midiKey,
      });
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.tokenStart != rhs.tokenStart) return lhs.tokenStart < rhs.tokenStart;
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
    std::string_view style) const {
  if (tokens.empty()) {
    return core::failure<UnitPlan>(core::ErrorCode::InvalidArgument,
                                   "Unit selection requires phoneme tokens");
  }
  UnitCandidateGenerator generator;
  const auto candidates = generator.generate(manifest, region, tokens, style);
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
      const bool deterministicTie = tied &&
          (!states[next].candidate.has_value() ||
           candidate.tokenCount > states[next].candidate->tokenCount ||
           (candidate.tokenCount == states[next].candidate->tokenCount &&
            candidate.unitId < states[next].candidate->unitId));
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
    plan.entries.push_back(UnitPlanEntry{
        .unitId = candidate.unitId,
        .tokenStart = candidate.tokenStart,
        .tokenCount = candidate.tokenCount,
        .score = candidate.score,
        .targetMidi = candidate.targetMidi,
        .alternatives = std::move(alternatives),
    });
    cursor = state.previous;
  }
  std::reverse(plan.entries.begin(), plan.entries.end());
  return plan;
}

}  // namespace seam::synthesis
