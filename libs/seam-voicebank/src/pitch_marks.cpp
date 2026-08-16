#include "seam/voicebank/pitch_marks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::voicebank {
namespace {

const PitchFrame* nearestVoicedFrame(std::span<const PitchFrame> frames,
                                     time::SampleFrame sourceFrame,
                                     float minimumConfidence) noexcept {
  const PitchFrame* best = nullptr;
  auto bestDistance = std::numeric_limits<std::uint64_t>::max();
  for (const auto& frame : frames) {
    if (!frame.voiced || frame.confidence < minimumConfidence ||
        !std::isfinite(frame.f0Hz) || frame.f0Hz <= 0.0) {
      continue;
    }
    const auto center = static_cast<time::SampleFrame>(frame.sourceFrame);
    const auto distance = center >= sourceFrame
        ? static_cast<std::uint64_t>(center - sourceFrame)
        : static_cast<std::uint64_t>(sourceFrame - center);
    if (distance < bestDistance) {
      best = &frame;
      bestDistance = distance;
    }
  }
  return best;
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
    PitchMarkGenerationConfig config) {
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

  auto analysis = analyzePitch(samples, sampleRate, config.pitch);
  if (!analysis) return core::Result<std::vector<PitchMark>>{analysis.error()};
  const auto* firstFrame = nearestVoicedFrame(analysis.value(), rangeStart,
                                               config.minimumConfidence);
  if (firstFrame == nullptr) {
    return core::failure<std::vector<PitchMark>>(
        core::ErrorCode::NotFound,
        "No voiced pitch frames are available for pitch mark generation");
  }

  std::vector<PitchMark> marks;
  auto predicted = std::max<time::SampleFrame>(
      rangeStart,
      static_cast<time::SampleFrame>(firstFrame->sourceFrame +
                                     config.pitch.frameSize / 2U));
  constexpr std::size_t kMaximumMarks = 1'000'000;
  while (predicted < rangeEnd && marks.size() < kMaximumMarks) {
    const auto* frame = nearestVoicedFrame(analysis.value(), predicted,
                                           config.minimumConfidence);
    if (frame == nullptr) break;
    const auto period = static_cast<time::SampleFrame>(std::llround(
        static_cast<double>(sampleRate) / frame->f0Hz));
    if (period < 2 || period > static_cast<time::SampleFrame>(sampleRate)) break;
    const auto radius = std::max<time::SampleFrame>(
        1, static_cast<time::SampleFrame>(std::llround(
            static_cast<double>(period) * config.refinementRadiusPeriods)));
    auto refined = refinePeak(samples, predicted, radius, rangeStart, rangeEnd);
    if (!marks.empty() && refined <= marks.back().frame) {
      refined = marks.back().frame + 1;
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
