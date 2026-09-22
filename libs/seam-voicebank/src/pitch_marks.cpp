#include "seam/voicebank/pitch_marks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::voicebank {
namespace {

// Frames are analysis windows, not instants. A frame at origin s covers
// [s, s + frameSize), so only a frame whose origin is at or before a sample can
// say anything about it, and with a 2048-sample window on a 256-sample hop up to
// eight frames cover the same sample.
//
// This replaces a nearest-origin lookup over a voiced-only list. That version had
// two defects: it could return a frame whose window starts *after* the sample, and
// it ignored the frames between, so a voiced span, an unvoiced span and a second
// voiced span produced marks inside the unvoiced one. Ownership is the property
// callers need, so it is what this returns.
const PitchFrame* coveringVoicedFrame(std::span<const PitchFrame> frames,
                                      time::SampleFrame sourceFrame,
                                      std::size_t frameSize) noexcept {
  if (frames.empty()) return nullptr;
  const auto probe = static_cast<std::uint64_t>(sourceFrame);
  // Frames with origin <= probe, nearest first.
  auto cursor = std::upper_bound(frames.begin(), frames.end(), probe,
      [](std::uint64_t value, const PitchFrame& frame) { return value < frame.sourceFrame; });
  while (cursor != frames.begin()) {
    const auto& candidate = *std::prev(cursor);
    // Origins are ascending, so once a candidate's window ends at or before the
    // sample, every earlier frame does too and the search is finished.
    if (probe >= static_cast<std::uint64_t>(candidate.sourceFrame) + frameSize) return nullptr;
    return &candidate;
  }
  return nullptr;
}

// The earliest sample a later voiced frame could cover, used to skip an unvoiced
// span without giving up on the rest of the take.
const PitchFrame* nextCoveringCandidate(std::span<const PitchFrame> frames,
                                        std::uint64_t after) noexcept {
  const auto next = std::upper_bound(frames.begin(), frames.end(), after,
      [](std::uint64_t value, const PitchFrame& frame) { return value < frame.sourceFrame; });
  return next == frames.end() ? nullptr : &*next;
}

time::SampleFrame refinePeak(std::span<const float> samples,
                             time::SampleFrame predicted,
                             time::SampleFrame radius,
                             time::SampleFrame rangeStart,
                             time::SampleFrame rangeEnd) noexcept {
  const auto begin = std::max(rangeStart, predicted - radius);
  const auto end = std::min(rangeEnd - 1, predicted + radius);
  auto best = std::clamp(predicted, begin, end);
  auto bestValue = -std::numeric_limits<float>::infinity();
  for (auto frame = begin; frame <= end; ++frame) {
    const auto value = samples[static_cast<std::size_t>(frame)];
    if (std::isfinite(value) && value > bestValue) {
      bestValue = value;
      best = frame;
    }
  }
  return best;
}

}  // namespace

core::Result<void> validatePitchMarks(std::span<const PitchMark> marks,
                                      time::SampleFrame rangeStart,
                                      time::SampleFrame rangeEnd) {
  if (rangeStart < 0 || rangeEnd <= rangeStart) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Pitch mark validation range is invalid");
  }
  time::SampleFrame previous = rangeStart - 1;
  for (const auto& mark : marks) {
    if (mark.frame <= previous || mark.frame < rangeStart || mark.frame >= rangeEnd ||
        !std::isfinite(mark.confidence) || mark.confidence < 0.0F ||
        mark.confidence > 1.0F) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Pitch marks must be strictly increasing and inside the unit range");
    }
    previous = mark.frame;
  }
  return core::success();
}

core::Result<std::vector<PitchMark>> generatePitchMarks(
    std::span<const float> samples,
    std::uint32_t sampleRate,
    time::SampleFrame rangeStart,
    time::SampleFrame rangeEnd,
    PitchMarkGenerationConfig config, std::stop_token stopToken,
    PitchAnalysisLimits limits) {
  if (samples.empty() || rangeStart < 0 || rangeEnd <= rangeStart ||
      static_cast<std::uint64_t>(rangeEnd) > samples.size() ||
      sampleRate < 8000 || sampleRate > 384000 ||
      !std::isfinite(config.refinementRadiusPeriods) ||
      config.refinementRadiusPeriods <= 0.0 ||
      config.refinementRadiusPeriods > 1.0 ||
      !std::isfinite(config.minimumConfidence) ||
      config.minimumConfidence <= 0.0F || config.minimumConfidence >= 1.0F) {
    return core::failure<std::vector<PitchMark>>(
        core::ErrorCode::InvalidArgument,
        "Pitch mark generation input is invalid");
  }

  auto analysis = analyzePitch(samples, sampleRate, config.pitch, stopToken, limits);
  if (!analysis) return core::Result<std::vector<PitchMark>>{analysis.error()};
  std::vector<PitchFrame> voiced;
  voiced.reserve(analysis.value().size());
  for (const auto& frame : analysis.value())
    if (frame.voiced && frame.confidence >= config.minimumConfidence && std::isfinite(frame.f0Hz) && frame.f0Hz > 0.0)
      voiced.push_back(frame);
  // A mark claims that a glottal pulse belongs at that sample, and that claim is
  // only supported where the analysis found periodicity covering it. The previous
  // implementation searched a voiced-only list for the nearest origin, which had
  // two consequences: it could return a frame whose window starts after the sample,
  // and it ignored the frames in between, so a voiced span, a fricative and a
  // second voiced span produced marks inside the fricative. Measured on a
  // 200 Hz / noise / 220 Hz fixture at 48 kHz, 45 of 209 marks landed in the
  // unvoiced region. Those marks persist into the unit manifest and drive
  // loopStart, loopEnd and releaseStart, so the unit described an unvoiced span as
  // pitched. The renderers re-check voicing separately and would not have turned
  // that into voiced audio, which is why this survived: the audio was safe and the
  // description was wrong.
  const auto firstFrame = coveringVoicedFrame(voiced, rangeStart, config.pitch.frameSize);
  const auto* startFrame = firstFrame != nullptr
      ? firstFrame
      : nextCoveringCandidate(voiced, static_cast<std::uint64_t>(rangeStart));
  if (startFrame == nullptr) {
    return core::failure<std::vector<PitchMark>>(
        core::ErrorCode::NotFound,
        "No voiced pitch frames are available for pitch mark generation");
  }

  std::vector<PitchMark> marks;
  auto predicted = std::max<time::SampleFrame>(
      rangeStart,
      static_cast<time::SampleFrame>(startFrame->sourceFrame));
  constexpr std::size_t kMaximumMarks = 1'000'000;
  while (predicted < rangeEnd && marks.size() < kMaximumMarks) {
    if (stopToken.stop_requested()) return core::failure<std::vector<PitchMark>>(
        core::ErrorCode::Conflict, "Pitch mark generation cancelled");
    const auto* frame = coveringVoicedFrame(voiced, predicted, config.pitch.frameSize);
    if (frame == nullptr) {
      // This candidate is not covered by any voiced window, so no mark may be
      // placed here. Resume at the earliest sample a later voiced frame could
      // cover, rather than abandoning the rest of the take: a brief consonant or
      // breath inside a take must not cost every mark after it.
      const auto* candidate = nextCoveringCandidate(
          voiced, static_cast<std::uint64_t>(predicted));
      if (candidate == nullptr) break;
      const auto resume = static_cast<time::SampleFrame>(
          std::max<std::uint64_t>(std::uint64_t{candidate->sourceFrame},
                                  static_cast<std::uint64_t>(predicted) + 1U));
      // Always move strictly forward; a resume point at or behind the current
      // candidate would spin without emitting a mark.
      predicted = resume > predicted ? resume : predicted + 1;
      continue;
    }
    const auto period = static_cast<time::SampleFrame>(std::llround(
        static_cast<double>(sampleRate) / frame->f0Hz));
    if (period < 2 || period > static_cast<time::SampleFrame>(sampleRate)) break;
    const auto radius = std::max<time::SampleFrame>(
        1, static_cast<time::SampleFrame>(std::llround(
            static_cast<double>(period) * config.refinementRadiusPeriods)));
    // Peak refinement may only move the mark inside the voiced frame that owns it.
    // The frame is the evidence that a mark belongs at this position at all, so
    // letting the search wander into the next window would reintroduce exactly the
    // unvoiced marks this guard exists to prevent: the strongest sample a few
    // hundred frames later is a good pitch peak and the wrong place to put one.
    const auto ownedStart = static_cast<time::SampleFrame>(frame->sourceFrame);
    const auto ownedEnd = static_cast<time::SampleFrame>(
        static_cast<std::uint64_t>(frame->sourceFrame) + config.pitch.frameSize);
    auto refined = refinePeak(samples, predicted, radius,
                              std::max(rangeStart, ownedStart),
                              std::min(rangeEnd, ownedEnd));
    if (!marks.empty() && refined <= marks.back().frame) {
      refined = marks.back().frame + 1;
    }
    // If enforcing strict increase pushed the mark out of its frame, the frame
    // cannot host another mark and generation moves on.
    if (refined < ownedStart || refined >= ownedEnd) {
      predicted = refined;
      continue;
    }
    if (refined >= rangeEnd) break;
    marks.push_back(PitchMark{
        .frame = refined,
        .confidence = static_cast<float>(std::clamp(frame->confidence, 0.0, 1.0)),
        .locked = false,
    });
    predicted = refined + period;
  }
  if (marks.size() < 3U) {
    return core::failure<std::vector<PitchMark>>(
        core::ErrorCode::NotFound,
        "Pitch mark generation produced fewer than three marks");
  }
  const auto validation = validatePitchMarks(marks, rangeStart, rangeEnd);
  if (!validation) return core::Result<std::vector<PitchMark>>{validation.error()};
  return marks;
}

core::Result<void> PitchMarkEditor::add(std::vector<PitchMark>& marks,
                                        PitchMark mark,
                                        time::SampleFrame rangeStart,
                                        time::SampleFrame rangeEnd) const {
  if (!std::isfinite(mark.confidence) || mark.confidence < 0.0F ||
      mark.confidence > 1.0F) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Pitch mark confidence is invalid");
  }
  const auto iterator = std::lower_bound(
      marks.begin(), marks.end(), mark.frame,
      [](const PitchMark& value, time::SampleFrame frame) {
        return value.frame < frame;
      });
  if (iterator != marks.end() && iterator->frame == mark.frame) {
    return core::failure(core::ErrorCode::Conflict,
                         "A pitch mark already exists at this frame");
  }
  marks.insert(iterator, mark);
  const auto validation = validatePitchMarks(marks, rangeStart, rangeEnd);
  if (!validation) {
    std::erase_if(marks, [&mark](const PitchMark& value) {
      return value.frame == mark.frame;
    });
    return validation;
  }
  return core::success();
}

core::Result<void> PitchMarkEditor::move(std::vector<PitchMark>& marks,
                                         std::size_t index,
                                         time::SampleFrame frame,
                                         time::SampleFrame rangeStart,
                                         time::SampleFrame rangeEnd) const {
  if (index >= marks.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch mark index is outside the collection");
  }
  if (marks[index].locked) {
    return core::failure(core::ErrorCode::Conflict,
                         "Locked pitch marks cannot be moved");
  }
  const auto before = marks;
  marks[index].frame = frame;
  std::stable_sort(marks.begin(), marks.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.frame < rhs.frame; });
  const auto validation = validatePitchMarks(marks, rangeStart, rangeEnd);
  if (!validation) {
    marks = before;
    return validation;
  }
  return core::success();
}

core::Result<void> PitchMarkEditor::remove(std::vector<PitchMark>& marks,
                                           std::size_t index,
                                           time::SampleFrame rangeStart,
                                           time::SampleFrame rangeEnd) const {
  if (index >= marks.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch mark index is outside the collection");
  }
  if (marks[index].locked) {
    return core::failure(core::ErrorCode::Conflict,
                         "Locked pitch marks cannot be removed");
  }
  if (marks.size() <= 3U) {
    return core::failure(core::ErrorCode::Conflict,
                         "Classic PSOLA requires at least three pitch marks");
  }
  const auto removed = marks[index];
  marks.erase(marks.begin() + static_cast<std::ptrdiff_t>(index));
  const auto validation = validatePitchMarks(marks, rangeStart, rangeEnd);
  if (!validation) {
    marks.push_back(removed);
    std::stable_sort(marks.begin(), marks.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.frame < rhs.frame; });
    return validation;
  }
  return core::success();
}

core::Result<void> PitchMarkEditor::setLocked(std::vector<PitchMark>& marks,
                                              std::size_t index,
                                              bool locked) const {
  if (index >= marks.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch mark index is outside the collection");
  }
  marks[index].locked = locked;
  return core::success();
}

}  // namespace seam::voicebank
