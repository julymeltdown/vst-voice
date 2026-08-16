#include "seam/synthesis/timing_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::synthesis {
namespace {

const domain::Note* noteAt(const domain::VocalRegion& region,
                           std::span<const domain::PhonemeToken> tokens,
                           std::size_t index) noexcept {
  if (index >= tokens.size()) return nullptr;
  return region.findNote(tokens[index].key.noteId);
}

}  // namespace

core::Result<TimingPlan> TimingSolver::solve(
    const domain::Project& project,
    const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens,
    const UnitPlan& unitPlan,
    const voicebank::Manifest& manifest,
    std::uint32_t outputSampleRate) const {
  if (outputSampleRate < 8000 || outputSampleRate > 384000 || unitPlan.entries.empty()) {
    return core::failure<TimingPlan>(core::ErrorCode::InvalidArgument,
                                     "Timing solver input is invalid");
  }
  TimingPlan result;
  result.startFrame = std::numeric_limits<time::SampleFrame>::max();
  result.endFrame = std::numeric_limits<time::SampleFrame>::min();

  for (const auto& entry : unitPlan.entries) {
    const auto* unit = manifest.findUnit(entry.unitId);
    if (unit == nullptr) {
      return core::failure<TimingPlan>(core::ErrorCode::NotFound,
                                       "Unit plan references a missing voicebank unit",
                                       entry.unitId);
    }
    const auto* firstNote = noteAt(region, tokens, entry.tokenStart);
    const auto* lastNote = noteAt(region, tokens,
                                  entry.tokenStart + entry.tokenCount - 1U);
    if (firstNote == nullptr || lastNote == nullptr) {
      result.issues.push_back(TimingIssue{
          .code = TimingIssueCode::MissingNote,
          .unitId = entry.unitId,
          .message = "Selected unit references a missing note",
      });
      continue;
    }

    const auto absoluteStart = region.startTick + firstNote->startTick;
    const auto absoluteEnd = region.startTick + lastNote->endTick();
    const auto noteOn = project.tempoMap().sampleFrameAt(
        absoluteStart, static_cast<double>(outputSampleRate));
    const auto noteEnd = project.tempoMap().sampleFrameAt(
        absoluteEnd, static_cast<double>(outputSampleRate));
    const auto sampleRateRatio = static_cast<double>(outputSampleRate) /
                                 static_cast<double>(manifest.expectedSampleRate);
    const auto vowelOffsetSource = unit->markers.vowelOnset - unit->markers.audioOffset;
    const auto vowelOffset = static_cast<time::SampleFrame>(std::llround(
        static_cast<double>(vowelOffsetSource) * sampleRateRatio));
    const auto minimumLengthSource = unit->markers.stableStart - unit->markers.audioOffset;
    const auto minimumLength = static_cast<time::SampleFrame>(std::llround(
        static_cast<double>(minimumLengthSource) * sampleRateRatio));

    auto destinationStart = noteOn - vowelOffset;
    auto destinationEnd = std::max(noteEnd, destinationStart + minimumLength + 1);
    if (destinationStart < 0) {
      result.issues.push_back(TimingIssue{
          .code = TimingIssueCode::NegativePreutterance,
          .unitId = entry.unitId,
          .message = "Unit preutterance begins before project frame zero",
      });
    }
    if (noteEnd - destinationStart < minimumLength) {
      result.issues.push_back(TimingIssue{
          .code = TimingIssueCode::ShortDestination,
          .unitId = entry.unitId,
          .message = "Destination is shorter than the unit transition region",
      });
    }
    result.placements.push_back(TimedUnitPlacement{
        .unitId = entry.unitId,
        .tokenStart = entry.tokenStart,
        .tokenCount = entry.tokenCount,
        .targetMidi = entry.targetMidi,
        .noteOn = noteOn,
        .destinationStart = destinationStart,
        .destinationEnd = destinationEnd,
        .desiredVowelOnset = noteOn,
    });
    result.startFrame = std::min(result.startFrame, destinationStart);
    result.endFrame = std::max(result.endFrame, destinationEnd);
  }

  if (result.placements.empty()) {
    return core::failure<TimingPlan>(core::ErrorCode::NotFound,
                                     "Timing solver produced no placements");
  }
  std::stable_sort(result.placements.begin(), result.placements.end(),
      [](const auto& lhs, const auto& rhs) {
        if (lhs.destinationStart == rhs.destinationStart) return lhs.unitId < rhs.unitId;
        return lhs.destinationStart < rhs.destinationStart;
      });
  for (std::size_t index = 1; index < result.placements.size(); ++index) {
    if (result.placements[index].destinationStart <
        result.placements[index - 1U].destinationEnd) {
      result.issues.push_back(TimingIssue{
          .code = TimingIssueCode::UnitOverlap,
          .unitId = result.placements[index].unitId,
          .message = "Adjacent voice units overlap and will be composed by the seam engine",
      });
    }
  }
  return result;
}

}  // namespace seam::synthesis
