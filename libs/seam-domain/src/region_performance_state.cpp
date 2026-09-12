#include "seam/domain/performance_intent.hpp"

#include "seam/domain/dynamics_automation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>

namespace seam::domain {
namespace {

bool identityText(const std::string& value, std::size_t maximum = 1024U) {
  return !value.empty() && value.size() <= maximum && fromUtf8(value) &&
      std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20U || character == 0x7fU;
      });
}

bool digest(const std::string& value) {
  return value.size() == 64U &&
      std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      });
}

bool valueInRange(PerformanceChannel channel, double value) {
  if (!std::isfinite(value)) return false;
  switch (channel) {
    case PerformanceChannel::Pitch: return value >= 0.0 && value <= 12700.0;
    case PerformanceChannel::Timing: return value >= -60000000.0 && value <= 60000000.0;
    case PerformanceChannel::Dynamics: return value >= 0.0 && value <= kMaximumDynamicsGain;
    case PerformanceChannel::Formant: return value >= -24.0 && value <= 24.0;
    case PerformanceChannel::Gender: return value >= -1.0 && value <= 1.0;
    case PerformanceChannel::Attack:
    case PerformanceChannel::Release: return value >= 0.0 && value <= 2000.0;
    case PerformanceChannel::Breathiness:
    case PerformanceChannel::Tension:
    case PerformanceChannel::Airiness:
    case PerformanceChannel::StyleBlend:
    case PerformanceChannel::Growl: return value >= 0.0 && value <= 1.0;
  }
  return false;
}

using NoteRanges = std::unordered_map<NoteId, PerformanceTimeRange>;

core::Result<PerformanceTimeRange> scopeRange(const PerformanceScope& scope,
                                              const NoteRanges& notes,
                                              time::Tick duration) {
  PerformanceTimeRange range;
  if (const auto* id = std::get_if<NoteId>(&scope)) {
    const auto found = notes.find(*id);
    if (found == notes.end()) {
      return core::failure<PerformanceTimeRange>(core::ErrorCode::InvariantViolation,
          "Current performance scope references a missing note");
    }
    range = found->second;
  } else {
    range = std::get<PerformanceTimeRange>(scope);
  }
  const auto valid = range.validate();
  if (!valid) return core::Result<PerformanceTimeRange>{valid.error()};
  if (range.endTick > duration) {
    return core::failure<PerformanceTimeRange>(core::ErrorCode::InvariantViolation,
        "Current performance scope extends beyond its region");
  }
  return range;
}

struct Interval final {
  PerformanceChannel channel;
  ManualPerformanceMode mode;
  PerformanceTimeRange range;
  NoteId noteId;
};

bool disjoint(std::vector<Interval> intervals) {
  const auto byStart = [](const auto& left, const auto& right) {
    return std::tie(left.channel, left.mode, left.range.startTick) <
           std::tie(right.channel, right.mode, right.range.startTick);
  };
  std::vector<Interval> ranges;
  std::set<std::tuple<PerformanceChannel, ManualPerformanceMode, NoteId>> noteKeys;
  for (const auto& interval : intervals) {
    if (interval.noteId.valid()) {
      if (!noteKeys.emplace(interval.channel, interval.mode, interval.noteId).second) return false;
    } else {
      ranges.push_back(interval);
    }
  }
  std::sort(ranges.begin(), ranges.end(), byStart);
  for (std::size_t index = 1U; index < ranges.size(); ++index) {
    const auto& previous = ranges[index - 1U];
    const auto& current = ranges[index];
    if (current.channel == previous.channel && current.mode == previous.mode &&
        current.range.startTick < previous.range.endTick) return false;
  }
  for (const auto& interval : intervals) {
    if (!interval.noteId.valid()) continue;
    const Interval endpoint{interval.channel, interval.mode,
                             {interval.range.endTick, interval.range.endTick}, {}};
    auto next = std::lower_bound(ranges.begin(), ranges.end(), endpoint, byStart);
    if (next == ranges.begin()) continue;
    const auto& preceding = *--next;
    if (preceding.channel == interval.channel && preceding.mode == interval.mode &&
        preceding.range.endTick > interval.range.startTick) return false;
  }
  return true;
}

}

core::Result<void> SingerResourceIdentity::validate() const {
  if ((kind != SingerResourceKind::Sample && kind != SingerResourceKind::Procedural &&
       kind != SingerResourceKind::Neural) || !identityText(id) ||
      !identityText(version, 128U) || !digest(contentHash)) {
    return core::failure(core::ErrorCode::InvariantViolation,
        "Performance resource identity requires a known kind and exact content digest");
  }
  return core::success();
}

core::Result<void> PronunciationIdentity::validate() const {
  if ((language != Language::Japanese && language != Language::English &&
       language != Language::Korean) || !identityText(resolverId) ||
      !identityText(resolverVersion, 128U) || !digest(resourceHash) ||
      !digest(inputHash) || !digest(sequenceHash)) {
    return core::failure(core::ErrorCode::InvariantViolation,
        "Pronunciation identity requires a language, resolver and exact resource/input/sequence digests");
  }
  return core::success();
}

std::string_view performanceChannelUnit(PerformanceChannel channel) noexcept {
  switch (channel) {
    case PerformanceChannel::Pitch: return "midi-cents";
    case PerformanceChannel::Timing: return "microseconds-offset";
    case PerformanceChannel::Dynamics: return "linear-gain";
    case PerformanceChannel::Formant: return "semitones-shift";
    case PerformanceChannel::Gender: return "bipolar";
    case PerformanceChannel::Attack:
    case PerformanceChannel::Release: return "milliseconds";
    case PerformanceChannel::Breathiness:
    case PerformanceChannel::Tension:
    case PerformanceChannel::Airiness:
    case PerformanceChannel::StyleBlend:
    case PerformanceChannel::Growl: return "normalized";
  }
  return {};
}

core::Result<void> PerformanceLane::validate(PerformanceTimeRange range) const {
  const auto validRange = range.validate();
  if (!validRange) return validRange;
  if (performanceChannelUnit(channel).empty() || points.empty() ||
      points.size() > kMaximumPerformanceLanePoints) {
    return core::failure(core::ErrorCode::InvariantViolation,
        "Performance lane requires a known channel and bounded nonempty points");
  }
  std::optional<time::Tick> previous;
  for (const auto& point : points) {
    if (point.tick < range.startTick || point.tick > range.endTick ||
        (previous.has_value() && point.tick <= *previous) ||
        (!point.value.has_value() && channel != PerformanceChannel::Pitch) ||
        (point.value.has_value() && !valueInRange(channel, *point.value))) {
      return core::failure(core::ErrorCode::InvariantViolation,
          "Performance points must be ordered, within their captured span and valid in channel units");
    }
    previous = point.tick;
  }
  return core::success();
}

core::Result<void> PerformanceTake::validate() const {
  if (!identityText(id, 128U) || !sourceRegionId.valid() ||
      !identityText(generatorId) || !identityText(generatorVersion, 128U) ||
      (state != PerformanceProposalState::Proposed && state != PerformanceProposalState::Rejected) ||
      lanes.empty() || lanes.size() > 12U) {
    return core::failure(core::ErrorCode::InvariantViolation,
        "Performance take identity, state or lane count is invalid");
  }
  const auto validResource = resource.validate();
  if (!validResource) return validResource;
  const auto validPronunciation = pronunciation.validate();
  if (!validPronunciation) return validPronunciation;
  const auto validRange = range.validate();
  if (!validRange) return validRange;
  std::array<bool, 12U> seen{};
  for (const auto& lane : lanes) {
    const auto valid = lane.validate(range);
    if (!valid) return valid;
    const auto index = static_cast<std::size_t>(lane.channel);
    if (seen[index]) {
      return core::failure(core::ErrorCode::InvariantViolation,
          "Performance take cannot contain duplicate channels");
    }
    seen[index] = true;
  }
  return core::success();
}

core::Result<void> RegionPerformanceState::validate(
    std::span<const Note> notes, time::Tick regionDuration) const {
  if (regionDuration <= time::Tick{0} || ownership.size() > kMaximumPerformanceOwnership ||
      takes.size() > kMaximumPerformanceTakes || accepted.size() > kMaximumPerformanceSelections) {
    return core::failure(core::ErrorCode::InvariantViolation,
        "Region performance collection or duration exceeds its bounds");
  }
  if (pronunciation.has_value()) {
    const auto valid = pronunciation->validate();
    if (!valid) return valid;
  }
  std::size_t pointCount = 0U;
  std::unordered_map<std::string, const PerformanceTake*> takeIndex;
  for (const auto& take : takes) {
    if (take.lanes.size() > 12U) {
      return core::failure(core::ErrorCode::InvariantViolation, "Performance take has too many lanes");
    }
    for (const auto& lane : take.lanes) {
      if (lane.points.size() > kMaximumPerformanceStatePoints - pointCount) {
        return core::failure(core::ErrorCode::InvariantViolation,
            "Region performance exceeds the aggregate point budget");
      }
      pointCount += lane.points.size();
    }
    const auto valid = take.validate();
    if (!valid) return valid;
    if (!takeIndex.emplace(take.id, &take).second) {
      return core::failure(core::ErrorCode::InvariantViolation, "Performance take IDs must be unique");
    }
  }
  if (ownership.empty() && accepted.empty()) return core::success();
  NoteRanges noteIndex;
  for (const auto& note : notes) {
    const auto valid = note.validate();
    if (!valid) return valid;
    if (!noteIndex.emplace(note.id, PerformanceTimeRange{note.startTick, note.endTick()}).second) {
      return core::failure(core::ErrorCode::InvariantViolation, "Performance note IDs must be unique");
    }
  }
  std::vector<Interval> manualIntervals;
  for (const auto& entry : ownership) {
    const auto valid = entry.validate();
    if (!valid) return valid;
    const auto range = scopeRange(entry.scope, noteIndex, regionDuration);
    if (!range) return core::Result<void>{range.error()};
    const auto* note = std::get_if<NoteId>(&entry.scope);
    manualIntervals.push_back({entry.channel, entry.mode, range.value(), note != nullptr ? *note : NoteId{}});
  }
  if (!disjoint(std::move(manualIntervals))) {
    return core::failure(core::ErrorCode::InvariantViolation, "Manual performance ownership overlaps ambiguously");
  }
  std::vector<Interval> acceptedIntervals;
  for (const auto& entry : accepted) {
    const auto found = takeIndex.find(entry.takeId);
    if (found == takeIndex.end() || found->second->state == PerformanceProposalState::Rejected) {
      return core::failure(core::ErrorCode::InvariantViolation, "Accepted performance references an absent or rejected take");
    }
    const auto& take = *found->second;
    if (std::none_of(take.lanes.begin(), take.lanes.end(), [&entry](const auto& lane) {
          return lane.channel == entry.channel;
        })) {
      return core::failure(core::ErrorCode::InvariantViolation, "Accepted performance channel has no take payload");
    }
    const auto range = scopeRange(entry.scope, noteIndex, regionDuration);
    if (!range) return core::Result<void>{range.error()};
    const auto offset = entry.sourceTickOffset.value();
    if ((offset > 0 && range.value().endTick.value() > std::numeric_limits<std::int64_t>::max() - offset) ||
        (offset < 0 && range.value().startTick.value() < std::numeric_limits<std::int64_t>::min() - offset) ||
        range.value().startTick + entry.sourceTickOffset < take.range.startTick ||
        range.value().endTick + entry.sourceTickOffset > take.range.endTick) {
      return core::failure(core::ErrorCode::InvariantViolation, "Accepted performance maps outside its captured source span");
    }
    const auto* note = std::get_if<NoteId>(&entry.scope);
    acceptedIntervals.push_back({entry.channel, ManualPerformanceMode::Replace, range.value(),
                                 note != nullptr ? *note : NoteId{}});
  }
  if (!disjoint(std::move(acceptedIntervals))) {
    return core::failure(core::ErrorCode::InvariantViolation, "Accepted performance selections overlap ambiguously");
  }
  return core::success();
}

bool RegionPerformanceState::permitsGenerated(PerformanceChannel channel, NoteId noteId,
                                              time::Tick tick, bool manualVibrato) const noexcept {
  if (tick < time::Tick{0} || performanceChannelUnit(channel).empty() ||
      (channel == PerformanceChannel::Pitch && manualVibrato)) return false;
  return std::none_of(ownership.begin(), ownership.end(), [&](const auto& entry) {
    return entry.channel == channel && entry.mode == ManualPerformanceMode::Replace &&
           entry.appliesTo(noteId, tick);
  });
}

core::Result<RegionPerformanceState> transformRegionPerformance(
    const RegionPerformanceState& state, std::span<const Note> sourceNotes,
    time::Tick sourceDuration, std::span<const PerformanceNoteRemap> noteMap,
    PerformanceTimeRange sourceWindow) {
  const auto valid = state.validate(sourceNotes, sourceDuration);
  if (!valid) return core::Result<RegionPerformanceState>{valid.error()};
  const auto validWindow = sourceWindow.validate();
  if (!validWindow) return core::Result<RegionPerformanceState>{validWindow.error()};
  if (sourceWindow.endTick > sourceDuration || noteMap.size() > sourceNotes.size()) {
    return core::failure<RegionPerformanceState>(core::ErrorCode::InvalidArgument,
        "Performance transformation exceeds its source region");
  }
  std::unordered_map<NoteId, const Note*> sourceIndex;
  for (const auto& note : sourceNotes) {
    const auto validNote = note.validate();
    if (!validNote) return core::Result<RegionPerformanceState>{validNote.error()};
    if (note.endTick() > sourceDuration || !sourceIndex.emplace(note.id, &note).second) {
      return core::failure<RegionPerformanceState>(core::ErrorCode::InvalidArgument,
          "Performance source notes must be unique and within the source region");
    }
  }
  std::unordered_map<NoteId, NoteId> mapped;
  std::set<NoteId> targets;
  std::vector<Note> targetNotes;
  for (const auto& pair : noteMap) {
    const auto source = sourceIndex.find(pair.source);
    if (source == sourceIndex.end() || !pair.target.valid() ||
        !mapped.emplace(pair.source, pair.target).second || !targets.insert(pair.target).second ||
        source->second->startTick < sourceWindow.startTick || source->second->endTick() > sourceWindow.endTick) {
      return core::failure<RegionPerformanceState>(core::ErrorCode::InvalidArgument,
          "Performance note mapping must name unique complete notes in the source window");
    }
    auto note = *source->second;
    note.id = pair.target;
    note.startTick = note.startTick - sourceWindow.startTick;
    targetNotes.push_back(std::move(note));
  }
  const auto transformScope = [&](const PerformanceScope& scope) -> std::optional<PerformanceScope> {
    if (const auto* note = std::get_if<NoteId>(&scope)) {
      const auto target = mapped.find(*note);
      if (target == mapped.end()) return std::nullopt;
      return target->second;
    }
    const auto range = std::get<PerformanceTimeRange>(scope);
    const auto start = std::max(range.startTick, sourceWindow.startTick);
    const auto end = std::min(range.endTick, sourceWindow.endTick);
    if (start >= end) return std::nullopt;
    return PerformanceTimeRange{start - sourceWindow.startTick, end - sourceWindow.startTick};
  };
  RegionPerformanceState result{.revision = state.revision, .takes = state.takes};
  for (const auto& entry : state.ownership) {
    const auto scope = transformScope(entry.scope);
    if (!scope.has_value()) continue;
    auto transformed = entry;
    transformed.scope = *scope;
    result.ownership.push_back(std::move(transformed));
  }
  for (const auto& entry : state.accepted) {
    const auto scope = transformScope(entry.scope);
    if (!scope.has_value()) continue;
    if (entry.sourceTickOffset.value() >
        std::numeric_limits<std::int64_t>::max() - sourceWindow.startTick.value()) {
      return core::failure<RegionPerformanceState>(core::ErrorCode::InvalidArgument,
          "Performance source offset overflows during transformation");
    }
    auto transformed = entry;
    transformed.scope = *scope;
    transformed.sourceTickOffset = entry.sourceTickOffset + sourceWindow.startTick;
    result.accepted.push_back(std::move(transformed));
  }
  const auto validResult = result.validate(targetNotes, sourceWindow.endTick - sourceWindow.startTick);
  if (!validResult) return core::Result<RegionPerformanceState>{validResult.error()};
  return result;
}

}
