#include "seam/synthesis/unit_selection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace seam::synthesis {
bool hasMultipleNuclei(std::span<const domain::PhonemeToken> tokens) {
  return std::count_if(tokens.begin(), tokens.end(), [](const auto& token) {
    return token.role == domain::PhonemeRole::Nucleus;
  }) > 1;
}
bool supportsExplicitPhonemeTiming(std::span<const domain::PhonemeToken> tokens) {
  if (tokens.empty()) return false;
  auto nucleus = std::find_if(tokens.begin(), tokens.end(), [](const auto& token) { return token.role == domain::PhonemeRole::Nucleus; });
  if (nucleus == tokens.end()) nucleus = tokens.begin();
  for (auto token = tokens.begin(); token != tokens.end(); ++token) {
    if (token->timing.startOffset && token != tokens.begin() && token != nucleus) return false;
    if (token->timing.endOffset && token != std::prev(tokens.end())) return false;
  }
  return true;
}

bool supportsAlignedPhonemeTiming(
    const voicebank::Unit& unit, std::span<const domain::PhonemeToken> tokens,
    std::span<const SourceAlignmentEvidence> alignments) {
  if (tokens.size() != unit.phones.size()) return false;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].symbol != unit.phones[i]) return false;
  }
  return std::any_of(alignments.begin(), alignments.end(), [&](const auto& evidence) {
    return evidence.alignment && evidence.alignment->unitId == unit.id &&
        evidence.alignment->validate(unit, evidence.verifiedAudioSha256, evidence.decodedFrames).hasValue();
  });
}

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
  const auto pitchDistance = std::abs(static_cast<double>(unit.rootMidi) - targetMidi);
  const auto kindBonus = unit.phones.size() > 1
      ? -2.0 * static_cast<double>(unit.phones.size() - 1U)
      : 0.0;
  const auto priorityBonus = -0.25 * static_cast<double>(unit.priority);
  const auto takePenalty = 0.001 * std::max(0.0, static_cast<double>(unit.take) - 1.0);
  return static_cast<double>(pitchDistance) * 10.0 + kindBonus +
         priorityBonus + takePenalty;
}

const domain::UnitSelectionOverride* overrideFor(
    std::span<const domain::UnitSelectionOverride> overrides,
    domain::PhonemeKey startKey) noexcept {
  const auto iterator = std::find_if(overrides.begin(), overrides.end(),
      [startKey](const auto& value) { return !value.unresolved && value.startKey == startKey; });
  return iterator == overrides.end() ? nullptr : &*iterator;
}

}  // namespace

core::Result<std::vector<UnitCandidate>> UnitCandidateGenerator::generate(
    const voicebank::Manifest& manifest,
    const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens,
    std::string_view style,
    std::span<const domain::UnitSelectionOverride> overrides,
    std::span<const SourceAlignmentEvidence> alignments,
    bool requireNucleusAlignment, UnitSelectionContext context) const {
  UnitSelectionBudget localBudget;
  auto& budget = context.budget ? *context.budget : localBudget;
  if (tokens.empty() || tokens.size() > kMaximumSelectionTokens || manifest.units.size() > 65536U ||
      overrides.size() > kMaximumSelectionTokens || alignments.size() > 65536U) {
    return core::failure<std::vector<UnitCandidate>>(core::ErrorCode::Unsupported,
        "Unit selection input exceeds bounded token/inventory limits");
  }
  if (overrides.empty() && !region.unitSelectionOverrides.empty()) overrides = region.unitSelectionOverrides;
  if (overrides.size() > kMaximumSelectionTokens) return core::failure<std::vector<UnitCandidate>>(
      core::ErrorCode::Unsupported, "Unit selection override budget exceeded");
  struct ForcedSpan { std::size_t start; std::size_t end; const domain::UnitSelectionOverride* edit; };
  std::vector<ForcedSpan> forced;
  for (const auto& edit : overrides) {
    if (edit.unresolved) continue;
    const auto valid = edit.validate();
    if (!valid) return core::Result<std::vector<UnitCandidate>>{valid.error()};
    const auto scan = budget.spend(SelectionWork::Matching, tokens.size(), context.stop);
    if (!scan) return core::Result<std::vector<UnitCandidate>>{scan.error()};
    const auto token = std::find_if(tokens.begin(), tokens.end(), [&](const auto& value) { return value.key == edit.startKey; });
    if (token == tokens.end()) continue;
    const auto start = static_cast<std::size_t>(token - tokens.begin());
    if (edit.tokenCount > tokens.size() - start) return core::failure<std::vector<UnitCandidate>>(
        core::ErrorCode::Conflict, "Forced unit crosses the selection context", edit.unitId);
    forced.push_back({start, start + edit.tokenCount, &edit});
  }
  std::sort(forced.begin(), forced.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  for (std::size_t i = 1; i < forced.size(); ++i) {
    if (forced[i].start < forced[i - 1U].end) return core::failure<std::vector<UnitCandidate>>(
        core::ErrorCode::Conflict, "Forced unit intervals overlap", forced[i].edit->unitId);
  }
  std::vector<UnitCandidate> candidates;
  std::map<std::string_view, bool> unitIds;
  for (const auto& unit : manifest.units) {
    if (unit.id.empty() || unit.id.size() > 512U || !unitIds.emplace(unit.id, true).second) {
      return core::failure<std::vector<UnitCandidate>>(core::ErrorCode::InvalidArgument,
          "Unit selection requires unique bounded unit IDs");
    }
  }
  for (std::size_t start = 0; start < tokens.size(); ++start) {
    const auto* note = noteFor(region, tokens[start]);
    if (note == nullptr) continue;
    const auto* explicitOverride = overrideFor(overrides, tokens[start].key);
    for (const auto& unit : manifest.units) {
      auto spent = budget.spend(SelectionWork::Matching, 1U, context.stop);
      if (!spent) return core::Result<std::vector<UnitCandidate>>{spent.error()};
      if (!unit.enabled || unit.style != style || unit.phones.empty() || unit.phones.size() > tokens.size() - start) continue;
      spent = budget.spend(SelectionWork::Matching, unit.phones.size() + forced.size(), context.stop);
      if (!spent) return core::Result<std::vector<UnitCandidate>>{spent.error()};
      if (!phonesMatch(unit, tokens, start)) continue;
      const auto end = start + unit.phones.size();
      if (std::any_of(forced.begin(), forced.end(), [&](const auto& span) {
        return start < span.end && end > span.start &&
            !(start == span.start && end == span.end && unit.id == span.edit->unitId);
      })) continue;
      const auto covered = tokens.subspan(start, unit.phones.size());
      spent = budget.spend(SelectionWork::Matching, covered.size() * (alignments.size() + 1U), context.stop);
      if (!spent) return core::Result<std::vector<UnitCandidate>>{spent.error()};
      if ((!supportsExplicitPhonemeTiming(covered) || (requireNucleusAlignment && hasMultipleNuclei(covered))) &&
          !supportsAlignedPhonemeTiming(unit, covered, alignments)) continue;
      if (explicitOverride != nullptr && unit.id != explicitOverride->unitId) continue;
      if (explicitOverride != nullptr &&
          unit.phones.size() != explicitOverride->tokenCount) {
        continue;
      }
      spent = budget.spend(SelectionWork::Candidates, 1U, context.stop);
      if (!spent) return core::Result<std::vector<UnitCandidate>>{spent.error()};
      spent = budget.spend(SelectionWork::MetadataBytes, unit.id.size(), context.stop);
      if (!spent) return core::Result<std::vector<UnitCandidate>>{spent.error()};
      candidates.push_back(UnitCandidate{
          .unitId = unit.id,
          .tokenStart = start,
          .tokenCount = unit.phones.size(),
          .score = candidateScore(unit, note->midiKey),
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
    std::span<const domain::UnitSelectionOverride> overrides,
    std::span<const SourceAlignmentEvidence> alignments,
    bool requireNucleusAlignment, UnitSelectionContext context) const {
  UnitSelectionBudget localBudget;
  if (!context.budget) context.budget = &localBudget;
  auto& budget = *context.budget;
  if (overrides.empty() && !region.unitSelectionOverrides.empty()) {
    overrides = region.unitSelectionOverrides;
  }
  if (tokens.empty() || tokens.size() > kMaximumSelectionTokens ||
      manifest.units.size() > 65536U || overrides.size() > kMaximumSelectionTokens) {
    return core::failure<UnitPlan>(core::ErrorCode::InvalidArgument,
                                   "Unit selection requires bounded phoneme tokens and inventory");
  }
  for (const auto& overrideValue : overrides) {
    const auto spent = budget.spend(SelectionWork::Matching, tokens.size() + manifest.units.size(), context.stop);
    if (!spent) return core::Result<UnitPlan>{spent.error()};
    if (overrideValue.unresolved) continue;
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
    if (unit && phonesMatch(*unit, tokens, start) && !supportsExplicitPhonemeTiming(tokens.subspan(start, unit->phones.size())) &&
        !supportsAlignedPhonemeTiming(*unit, tokens.subspan(start, unit->phones.size()), alignments)) {
      return core::failure<UnitPlan>(core::ErrorCode::Conflict,
          "Forced unit cannot represent an interior phoneme timing edit; select smaller units",
          overrideValue.startKey.toString() + " -> " + overrideValue.unitId);
    }
    if (unit == nullptr || !unit->enabled || unit->style != style ||
        unit->phones.size() != overrideValue.tokenCount ||
        !phonesMatch(*unit, tokens, start)) {
      return core::failure<UnitPlan>(
          core::ErrorCode::Conflict,
          "Explicit unit selection no longer matches the phoneme sequence",
          overrideValue.startKey.toString() + " -> " + overrideValue.unitId);
    }
    const auto covered = tokens.subspan(start, unit->phones.size());
    if (requireNucleusAlignment && hasMultipleNuclei(covered) &&
        !supportsAlignedPhonemeTiming(*unit, covered, alignments)) {
      return core::failure<UnitPlan>(core::ErrorCode::Conflict,
          "Forced multi-nucleus unit requires source alignment; author landmarks or select smaller units", unit->id);
    }
  }

  UnitCandidateGenerator generator;
  auto generated = generator.generate(manifest, region, tokens, style, overrides, alignments, requireNucleusAlignment, context);
  if (!generated) return core::Result<UnitPlan>{generated.error()};
  const auto& candidates = generated.value();
  const auto allocated = budget.spend(SelectionWork::States, candidates.size(), context.stop);
  if (!allocated) return core::Result<UnitPlan>{allocated.error()};
  constexpr auto none = std::numeric_limits<std::size_t>::max();
  struct State { double score{std::numeric_limits<double>::infinity()}; std::size_t previous{none}; double edge{0.0}; bool joined{false}; };
  std::vector<State> states(candidates.size());
  std::vector<std::vector<std::size_t>> byStart(tokens.size()), byEnd(tokens.size() + 1U);
  std::map<std::string_view, const UnitJoinAnalysis*> analysis;
  if (context.analysis.size() > 65536U) return core::failure<UnitPlan>(core::ErrorCode::Unsupported, "Too many join analyses");
  for (const auto& item : context.analysis) {
    const auto finite = [](const auto& feature) {
      return std::isfinite(feature.levelDb) && std::all_of(feature.correlation.begin(), feature.correlation.end(),
          [](double value) { return std::isfinite(value) && std::abs(value) <= 1.0; });
    };
    if (!finite(item.head) || !finite(item.tail) || !analysis.emplace(item.unitId, &item).second) {
      return core::failure<UnitPlan>(core::ErrorCode::InvalidArgument, "Invalid or duplicate source join evidence", item.unitId);
    }
  }
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    if (context.requireAcoustic && !analysis.contains(candidate.unitId)) return core::failure<UnitPlan>(
        core::ErrorCode::NotFound, "Missing acoustic evidence for eligible unit", candidate.unitId);
    byStart[candidate.tokenStart].push_back(i);
    auto& ending = byEnd[candidate.tokenStart + candidate.tokenCount];
    if (ending.size() >= kMaximumSelectionStatesAtBoundary) return core::failure<UnitPlan>(
        core::ErrorCode::Unsupported, "Unit selection per-boundary state budget exceeded");
    ending.push_back(i);
  }
  // One state per candidate OCCURRENCE, not per boundary or unit ID. The edge
  // uses only its two occurrences, so minimum predecessor-chain dominance is
  // exact. No beam, silent truncation or greedy predecessor collapse.
  // Exact score ties use canonical candidate index, then the already resolved
  // predecessor chain (reverse occurrence order); inventory order is irrelevant.
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    if (candidate.tokenStart == 0U) { states[i].score = candidate.score; continue; }
    for (const auto predecessor : byEnd[candidate.tokenStart]) {
      const auto spent = budget.spend(SelectionWork::Edges, 1U, context.stop);
      if (!spent) return core::Result<UnitPlan>{spent.error()};
      if (!std::isfinite(states[predecessor].score)) continue;
      const auto& previous = candidates[predecessor];
      double edge = 0.0;
      bool joined = false;
      const auto* previousNote = noteFor(region, tokens[candidate.tokenStart - 1U]);
      const auto* nextNote = noteFor(region, tokens[candidate.tokenStart]);
      if (context.requireAcoustic && previousNote && nextNote &&
          (previousNote == nextNote || previousNote->endTick() >= nextNote->startTick)) {
        const auto& tail = analysis.at(previous.unitId)->tail;
        const auto& head = analysis.at(candidate.unitId)->head;
        edge = 0.5 * std::min(24.0, std::abs(tail.levelDb - head.levelDb));
        for (std::size_t band = 0; band < tail.correlation.size(); ++band) {
          edge += std::abs(tail.correlation[band] - head.correlation[band]);
        }
        joined = true;
      }
      const auto score = states[predecessor].score + candidate.score + edge;
      if (score < states[i].score || (score == states[i].score && predecessor < states[i].previous)) {
        states[i] = State{score, predecessor, edge, joined};
      }
    }
  }
  std::size_t best = none;
  for (const auto i : byEnd.back()) {
    if (std::isfinite(states[i].score) && (best == none || states[i].score < states[best].score ||
        (states[i].score == states[best].score && i < best))) best = i;
  }
  if (best == none) return core::failure<UnitPlan>(core::ErrorCode::NotFound, "Voicebank cannot cover the phoneme sequence");
  UnitPlan plan;
  plan.totalScore = states[best].score;
  auto cursor = best;
  while (cursor != none) {
    const auto& state = states[cursor];
    const auto& candidate = candidates[cursor];
    std::vector<std::string> alternatives;
    for (const auto optionIndex : byStart[candidate.tokenStart]) {
      const auto& option = candidates[optionIndex];
      if (option.tokenCount == candidate.tokenCount && option.unitId != candidate.unitId) {
        const auto spent = budget.spend(SelectionWork::MetadataBytes, option.unitId.size(), context.stop);
        if (!spent) return core::Result<UnitPlan>{spent.error()};
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
          const auto spent = budget.spend(SelectionWork::Matching, 1U + unit.phones.size(), context.stop);
          if (!spent) return core::Result<UnitPlan>{spent.error()};
          if (!unit.enabled || unit.style != style || unit.id == candidate.unitId ||
              !phonesMatch(unit, tokens, candidate.tokenStart) ||
              unit.phones.size() != candidate.tokenCount) {
            continue;
          }
          const auto bytes = budget.spend(SelectionWork::MetadataBytes, unit.id.size(), context.stop);
          if (!bytes) return core::Result<UnitPlan>{bytes.error()};
          alternatives.push_back(unit.id);
        }
        std::sort(alternatives.begin(), alternatives.end());
        alternatives.erase(std::unique(alternatives.begin(), alternatives.end()),
                           alternatives.end());
      }
    }
    const auto rationaleBytes = budget.spend(SelectionWork::MetadataBytes,
        candidate.unitId.size() + 64U + (state.previous == none ? 0U : candidates[state.previous].unitId.size()), context.stop);
    if (!rationaleBytes) return core::Result<UnitPlan>{rationaleBytes.error()};
    plan.entries.push_back(UnitPlanEntry{
        .unitId = candidate.unitId,
        .tokenStart = candidate.tokenStart,
        .tokenCount = candidate.tokenCount,
        .score = candidate.score,
        .targetMidi = candidate.targetMidi,
        .forced = candidate.forced,
        .renderer = candidate.renderer,
        .alternatives = std::move(alternatives),
        .rationale = {.acoustic = context.requireAcoustic, .joined = state.joined,
            .predecessor = state.previous == none ? std::string{} : candidates[state.previous].unitId,
            .incomingCost = state.edge, .cumulativeCost = state.score},
    });
    cursor = state.previous;
  }
  std::reverse(plan.entries.begin(), plan.entries.end());
  return plan;
}

}  // namespace seam::synthesis
