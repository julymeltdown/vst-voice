#include "seam/synthesis/automatic_performance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace seam::synthesis {
namespace {

constexpr std::size_t kMaximumGeneratedNotes = 4096U;

std::int32_t seededCents(std::uint64_t seed, domain::NoteId id) noexcept {
  auto value = seed ^ (id.value() + 0x9e3779b97f4a7c15ULL +
                       (seed << 6U) + (seed >> 2U));
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31U;
  return static_cast<std::int32_t>(value % 9U) - 4;
}

template <typename T>
void appendPoint(std::vector<domain::PerformancePoint>& points,
                 time::Tick tick, T value) {
  if (!points.empty() && points.back().tick == tick) {
    points.back().value = static_cast<double>(value);
    return;
  }
  points.push_back({tick, static_cast<double>(value)});
}

}  // namespace

core::Result<domain::PerformanceTake> generateAutomaticPerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    const phonemizer::ResolvedPronunciation& pronunciation,
    AutomaticPerformanceRequest request, std::stop_token stopToken) {
  using Output = domain::PerformanceTake;
  if (stopToken.stop_requested()) {
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Automatic performance generation was cancelled");
  }
  const auto projectValid = project.validate();
  if (!projectValid) return core::Result<Output>{projectValid.error()};
  const auto regionValid = region.validate();
  if (!regionValid) return core::Result<Output>{regionValid.error()};
  if (!request.regionId.valid() || request.regionId != region.id ||
      request.capturedRevision != region.performance.revision) {
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Automatic performance request is stale or targets another region");
  }
  const auto pronunciationValid = pronunciation.identity.validate();
  if (!pronunciationValid) return core::Result<Output>{pronunciationValid.error()};
  if (request.pronunciation != pronunciation.identity) {
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Automatic performance request pronunciation no longer matches the region");
  }
  if (request.channels.empty() || request.channels.size() > 12U) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Automatic performance channel set is invalid");
  }
  for (std::size_t index = 0U; index < request.channels.size(); ++index) {
    const auto channel = request.channels[index];
    if (std::find(request.channels.begin(), request.channels.begin() +
                      static_cast<std::ptrdiff_t>(index), channel) !=
        request.channels.begin() + static_cast<std::ptrdiff_t>(index)) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                   "Automatic performance channels repeat");
    }
    if (channel != domain::PerformanceChannel::Pitch &&
        channel != domain::PerformanceChannel::Dynamics &&
        channel != domain::PerformanceChannel::Attack &&
        channel != domain::PerformanceChannel::Release) {
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "Automatic performance channel requires a qualified generator",
          std::string{domain::performanceChannelUnit(channel)});
    }
  }
  const auto rangeValid = request.range.validate();
  if (!rangeValid || request.range.endTick > region.durationTick) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Automatic performance range is outside the region");
  }
  if (region.notes.size() > kMaximumGeneratedNotes) {
    return core::failure<Output>(core::ErrorCode::Unsupported,
                                 "Automatic performance note count exceeds bounds");
  }

  Output result{
      .id = std::move(request.takeId),
      .sourceRegionId = region.id,
      .capturedRevision = request.capturedRevision,
      .resource = std::move(request.resource),
      .pronunciation = std::move(request.pronunciation),
      .generatorId = std::move(request.generatorId),
      .generatorVersion = std::move(request.generatorVersion),
      .seed = request.seed,
      .range = request.range,
      .state = domain::PerformanceProposalState::Proposed,
      .lanes = {},
  };

  const auto makeLane = [&](domain::PerformanceChannel channel) {
    domain::PerformanceLane lane{.channel = channel, .points = {}};
    const auto begin = request.range.startTick;
    const auto end = request.range.endTick;
    if (channel == domain::PerformanceChannel::Pitch) {
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        const auto tick = std::max(begin, note.startTick);
        const auto cents = std::clamp(
            static_cast<std::int32_t>(note.midiKey) * 100 +
                seededCents(request.seed, note.id),
            0, 12700);
        appendPoint(lane.points, tick, cents);
        const auto noteEnd = std::min(end, note.endTick());
        appendPoint(lane.points, noteEnd, cents);
      }
      if (lane.points.empty()) appendPoint(lane.points, begin, 0);
    } else if (channel == domain::PerformanceChannel::Dynamics) {
      appendPoint(lane.points, begin, region.dynamicsAutomation.valueAt(begin));
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        appendPoint(lane.points, std::clamp(note.startTick, begin, end),
                    region.dynamicsAutomation.valueAt(
                        std::clamp(note.startTick, begin, end)));
        appendPoint(lane.points, std::clamp(note.endTick(), begin, end),
                    region.dynamicsAutomation.valueAt(
                        std::clamp(note.endTick(), begin, end)));
      }
      appendPoint(lane.points, end, region.dynamicsAutomation.valueAt(end));
    } else if (channel == domain::PerformanceChannel::Attack) {
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        appendPoint(lane.points, std::max(begin, note.startTick),
                    note.articulation == domain::NoteArticulation::Staccato
                        ? 20.0
                        : 35.0);
      }
      if (lane.points.empty()) appendPoint(lane.points, begin, 0.0);
    } else {
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        appendPoint(lane.points, std::min(end, note.endTick()), 40.0);
      }
      if (lane.points.empty()) appendPoint(lane.points, begin, 0.0);
    }
    return lane;
  };

  result.lanes.reserve(request.channels.size());
  for (const auto channel : request.channels) {
    if (stopToken.stop_requested()) {
      return core::failure<Output>(core::ErrorCode::Conflict,
                                   "Automatic performance generation was cancelled");
    }
    result.lanes.push_back(makeLane(channel));
  }
  if (stopToken.stop_requested()) {
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Automatic performance generation was cancelled");
  }
  const auto valid = result.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  return result;
}

}  // namespace seam::synthesis
