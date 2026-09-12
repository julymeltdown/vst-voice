#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::synthesis {
namespace {

core::Result<time::SampleFrame> scaleMarkerFrames(time::SampleFrame frames,
                                                std::uint32_t sourceRate,
                                                std::uint32_t targetRate) {
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<time::SampleFrame>::max());
  const auto value = static_cast<std::uint64_t>(frames);
  const auto quotient = value / sourceRate;
  const auto remainder = value % sourceRate;
  // Rates are bounded before this call; the remainder product fits uint64_t.
  const auto tail = (remainder * targetRate + sourceRate / 2U) / sourceRate;
  if (frames < 0 || quotient > (maximum - tail) / targetRate) {
    return core::failure<time::SampleFrame>(core::ErrorCode::InvalidArgument, "Source marker conversion exceeds frame range");
  }
  return static_cast<time::SampleFrame>(quotient * targetRate + tail);
}

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
    std::uint32_t outputSampleRate,
    std::span<const SourceAlignmentEvidence> alignments, bool allowShortTransitionMapping) const {
  if (outputSampleRate < 8000 || outputSampleRate > 384000 || unitPlan.entries.empty() || unitPlan.entries.size() > tokens.size() ||
      manifest.expectedSampleRate < 8000U || manifest.expectedSampleRate > 384000U) {
    return core::failure<TimingPlan>(core::ErrorCode::InvalidArgument,
                                     "Timing solver input is invalid");
  }
  TimingPlan result;
  const auto anchors = compilePhonemeTimingPlan(project, region, tokens, outputSampleRate);
  if (!anchors) return core::Result<TimingPlan>{anchors.error()};
  result.startFrame = std::numeric_limits<time::SampleFrame>::max();
  result.endFrame = std::numeric_limits<time::SampleFrame>::min();

  std::size_t nextToken = 0U;
  for (const auto& entry : unitPlan.entries) {
    if (entry.tokenCount == 0U || entry.tokenStart >= tokens.size() || entry.tokenCount > tokens.size() - entry.tokenStart) {
      return core::failure<TimingPlan>(core::ErrorCode::InvalidArgument, "Unit timing span is outside the phoneme sequence");
    }
    if (entry.tokenStart != nextToken) return core::failure<TimingPlan>(core::ErrorCode::InvalidArgument,
        "Unit timing plan must cover phonemes once in sequence");
    nextToken += entry.tokenCount;
    const auto* unit = manifest.findUnit(entry.unitId);
    const auto covered = tokens.subspan(entry.tokenStart, entry.tokenCount);
    if (!supportsExplicitPhonemeTiming(covered) &&
        !(unit && supportsAlignedPhonemeTiming(*unit, covered, alignments))) {
      return core::failure<TimingPlan>(core::ErrorCode::Conflict,
          "Selected unit cannot represent an interior phoneme timing edit", entry.unitId);
    }
    if (unit == nullptr) {
      return core::failure<TimingPlan>(core::ErrorCode::NotFound,
                                       "Unit plan references a missing voicebank unit",
                                       entry.unitId);
    }
    const auto validUnit = unit->validate();
    if (!validUnit) return core::Result<TimingPlan>{validUnit.error()};
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

    auto nucleus = entry.tokenStart;
    for (auto i = entry.tokenStart; i < entry.tokenStart + entry.tokenCount; ++i) {
      if (tokens[i].role == domain::PhonemeRole::Nucleus) { nucleus = i; break; }
    }
    const auto noteOn = tokens[nucleus].role == domain::PhonemeRole::Nucleus
        ? anchors.value()[nucleus].nucleusFrame
        : anchors.value()[nucleus].explicitStartFrame.value_or(anchors.value()[nucleus].nucleusFrame);
    const auto noteEnd = anchors.value()[entry.tokenStart + entry.tokenCount - 1U].endFrame;
    const auto vowelOffsetSource = unit->markers.vowelOnset - unit->markers.audioOffset;
    const auto scaledVowel = scaleMarkerFrames(vowelOffsetSource, manifest.expectedSampleRate, outputSampleRate);
    if (!scaledVowel) return core::Result<TimingPlan>{scaledVowel.error()};
    const auto vowelOffset = scaledVowel.value();
    const auto minimumLengthSource = unit->markers.stableStart - unit->markers.audioOffset;
    const auto scaledMinimum = scaleMarkerFrames(minimumLengthSource, manifest.expectedSampleRate, outputSampleRate);
    if (!scaledMinimum) return core::Result<TimingPlan>{scaledMinimum.error()};
    const auto minimumLength = scaledMinimum.value();

    if (noteOn < std::numeric_limits<time::SampleFrame>::min() + vowelOffset) {
      return core::failure<TimingPlan>(core::ErrorCode::InvalidArgument, "Preutterance start exceeds frame range", entry.unitId);
    }
    auto destinationStart = noteOn - vowelOffset;
    const bool explicitOnset = nucleus != entry.tokenStart && tokens[entry.tokenStart].timing.startOffset.has_value();
    if (explicitOnset) {
      destinationStart = *anchors.value()[entry.tokenStart].explicitStartFrame;
      if (destinationStart >= noteOn) return core::failure<TimingPlan>(core::ErrorCode::Conflict,
          "Explicit consonant start must precede its vowel", tokens[entry.tokenStart].key.toString());
    }
    const auto destinationEnd = noteEnd;
    if (destinationEnd <= destinationStart ||
        static_cast<std::uint64_t>(destinationEnd) - static_cast<std::uint64_t>(destinationStart) > 100'000'000ULL) {
      return core::failure<TimingPlan>(core::ErrorCode::Unsupported, "Timed unit exceeds supported render length", entry.unitId);
    }
    if (destinationStart < 0) {
      result.issues.push_back(TimingIssue{
          .code = TimingIssueCode::NegativePreutterance,
          .unitId = entry.unitId,
          .message = "Unit preutterance begins before project frame zero",
      });
    }
    // Consonant retiming must neither borrow time from the required voiced
    // transition nor make a feasible shortened onset look too short.
    const auto postVowelMinimum = std::max<time::SampleFrame>(0, minimumLength - vowelOffset);
    const bool shortTransition = noteOn > std::numeric_limits<time::SampleFrame>::max() - postVowelMinimum ||
        noteEnd <= noteOn + postVowelMinimum;
    const auto nuclei = std::count_if(covered.begin(), covered.end(), [](const auto& token) { return token.role == domain::PhonemeRole::Nucleus; });
    const bool simpleUnit = (unit->kind == voicebank::UnitKind::Cv && unit->phones.size() == 2U) ||
        (unit->kind == voicebank::UnitKind::Sustain && unit->phones.size() == 1U);
    if (shortTransition && (!allowShortTransitionMapping || !simpleUnit || nuclei != 1 ||
        noteEnd <= noteOn || static_cast<std::uint64_t>(noteEnd) - static_cast<std::uint64_t>(noteOn) < 3U)) {
      return core::failure<TimingPlan>(core::ErrorCode::Conflict,
          "Phoneme span is too short for the selected unit transition; lengthen the note or choose a shorter unit",
          entry.unitId + " at " + tokens[entry.tokenStart].key.toString());
    }
    result.placements.push_back(TimedUnitPlacement{
        .unitId = entry.unitId,
        .startKey = tokens[entry.tokenStart].key,
        .tokenStart = entry.tokenStart,
        .tokenCount = entry.tokenCount,
        .targetMidi = entry.targetMidi,
        .sourceStartTick = firstNote->startTick,
        .sourceEndTick = lastNote->endTick(),
        .noteOn = noteOn,
        .destinationStart = destinationStart,
        .destinationEnd = destinationEnd,
        .desiredVowelOnset = noteOn,
        .explicitOnsetStart = explicitOnset,
        .compressShortTransition = shortTransition,
        .phonemeTargets = std::vector<PhonemeTimingAnchor>(
            anchors.value().begin() + static_cast<std::ptrdiff_t>(entry.tokenStart),
            anchors.value().begin() + static_cast<std::ptrdiff_t>(entry.tokenStart + entry.tokenCount)),
    });
    result.startFrame = std::min(result.startFrame, destinationStart);
    result.endFrame = std::max(result.endFrame, destinationEnd);
  }

  if (nextToken != tokens.size()) return core::failure<TimingPlan>(core::ErrorCode::InvalidArgument,
      "Unit timing plan leaves phonemes uncovered");
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
