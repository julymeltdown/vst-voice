#include "seam/live_voice/articulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <vector>

namespace seam::live_voice {
namespace {

bool hasPhone(const LiveUnitAudio& unit, std::string_view phone) {
  return std::find(unit.phones.begin(), unit.phones.end(), phone) !=
         unit.phones.end();
}

bool endsWithPhone(const LiveUnitAudio& unit, std::string_view phone) {
  return !unit.phones.empty() && unit.phones.back() == phone;
}

bool startsWithPhone(const LiveUnitAudio& unit, std::string_view phone) {
  return !unit.phones.empty() && unit.phones.front() == phone;
}

int rolePriority(const LiveUnitAudio& unit, LiveSegmentRole preferred) noexcept {
  if (unit.role == preferred) return 0;
  if (preferred == LiveSegmentRole::Attack &&
      (unit.kind == voicebank::UnitKind::Glottal ||
       unit.kind == voicebank::UnitKind::Special)) {
    return 1;
  }
  if (preferred == LiveSegmentRole::Sustain &&
      (unit.kind == voicebank::UnitKind::Cv ||
       unit.kind == voicebank::UnitKind::Vcv)) {
    return 2;
  }
  return 10;
}

auto rank(const LiveUnitAudio& unit, LiveSegmentRole preferred,
          std::string_view target, std::int16_t key) {
  const auto exact = endsWithPhone(unit, target) || startsWithPhone(unit, target);
  const auto phonePenalty = exact ? 0 : (hasPhone(unit, target) ? 1 : 2);
  return std::tuple{
      rolePriority(unit, preferred), phonePenalty,
      std::abs(unit.rootMidi - static_cast<std::int32_t>(key)),
      -unit.priority, unit.take, unit.unitId};
}

const LiveUnitAudio* choose(const LiveVoicebankResources& resources,
                            LiveSegmentRole preferred, std::string_view target,
                            std::int16_t key) {
  const LiveUnitAudio* selected = nullptr;
  for (const auto& unit : resources.units) {
    if (!hasPhone(unit, target)) continue;
    if (selected == nullptr || rank(unit, preferred, target, key) <
                                  rank(*selected, preferred, target, key)) {
      selected = &unit;
    }
  }
  return selected;
}

const LiveUnitAudio* chooseTransition(const LiveVoicebankResources& resources,
                                      std::string_view previous,
                                      std::string_view target,
                                      std::int16_t key) {
  const LiveUnitAudio* selected = nullptr;
  for (const auto& unit : resources.units) {
    if (unit.role != LiveSegmentRole::Transition ||
        !startsWithPhone(unit, previous) || !endsWithPhone(unit, target)) {
      continue;
    }
    if (selected == nullptr || rank(unit, LiveSegmentRole::Transition, target,
                                    key) <
                                  rank(*selected, LiveSegmentRole::Transition,
                                       target, key)) {
      selected = &unit;
    }
  }
  return selected;
}

SegmentSelection attackSelection(const LiveUnitAudio* unit) noexcept {
  if (unit == nullptr) return {};
  return SegmentSelection{unit, unit->sourceStart,
                          std::max(unit->sourceStart, unit->stableStart)};
}

SegmentSelection sustainSelection(const LiveUnitAudio* unit) noexcept {
  if (unit == nullptr) return {};
  return SegmentSelection{unit, unit->loopStart, unit->loopEnd};
}

SegmentSelection transitionSelection(const LiveUnitAudio* unit) noexcept {
  if (unit == nullptr) return {};
  return SegmentSelection{unit, unit->sourceStart, unit->sourceEnd};
}

SegmentSelection releaseSelection(const LiveUnitAudio* unit) noexcept {
  if (unit == nullptr) return {};
  return SegmentSelection{unit, unit->releaseStart, unit->sourceEnd};
}

}

core::Result<ArticulationPlan> ArticulationPlanner::plan(
    const LiveVoicebankResources& resources,
    const ArticulationRequest& request) const {
  if (request.targetVowel.empty()) {
    return core::failure<ArticulationPlan>(
        core::ErrorCode::InvalidArgument,
        "Articulation target vowel must not be empty");
  }
  const auto* sustain = choose(resources, LiveSegmentRole::Sustain,
                                request.targetVowel, request.key);
  if (sustain == nullptr || sustain->loopEnd <= sustain->loopStart) {
    return core::failure<ArticulationPlan>(
        core::ErrorCode::NotFound,
        "No valid sustain unit covers the requested vowel",
        std::string{request.targetVowel});
  }
  auto* attack = choose(resources, LiveSegmentRole::Attack,
                        request.targetVowel, request.key);
  if (attack == nullptr) attack = sustain;

  ArticulationPlan result;
  result.attack = attackSelection(attack);
  result.sustain = sustainSelection(sustain);
  if (request.legato && request.previousVowel.has_value()) {
    const auto* transition = chooseTransition(resources, *request.previousVowel,
                                               request.targetVowel,
                                               request.key);
    if (transition != nullptr) {
      result.transition = transitionSelection(transition);
    } else {
      result.usedTransitionFallback = true;
    }
  }
  if (const auto* release = choose(resources, LiveSegmentRole::Release,
                                    request.targetVowel, request.key);
      release != nullptr) {
    result.release = releaseSelection(release);
  }
  result.diagnostic = result.usedTransitionFallback
                          ? "transition-fallback"
                          : "exact-articulation";
  return result;
}

}
