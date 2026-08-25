#include "seam/ui/phoneme_lane_model.hpp"

#include <algorithm>
#include <cmath>

namespace seam::ui {
namespace {

double roleWeight(domain::PhonemeRole role) noexcept {
  switch (role) {
    case domain::PhonemeRole::Onset: return 0.32;
    case domain::PhonemeRole::Nucleus: return 1.0;
    case domain::PhonemeRole::Coda: return 0.36;
    case domain::PhonemeRole::Geminate: return 0.42;
    case domain::PhonemeRole::Breath: return 0.7;
    case domain::PhonemeRole::Silence: return 0.8;
  }
  return 1.0;
}

}  // namespace

void PhonemeLaneModel::rebuild(const PianoRollModel& pianoRoll,
                               const phonemizer::Result& phonemes,
                               double laneTop,
                               double laneHeight) {
  visuals_.clear();
  const auto notes = pianoRoll.visibleNotes();
  for (const auto& note : notes) {
    const auto tokens = phonemes.tokensForNote(note.noteId);
    if (tokens.empty()) {
      continue;
    }
    double totalWeight = 0.0;
    for (const auto& token : tokens) {
      totalWeight += roleWeight(token.role);
    }
    if (totalWeight <= 0.0) {
      continue;
    }

    double cursor = note.bounds.x;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
      const auto& token = tokens[index];
      const auto defaultWidth = note.bounds.width * roleWeight(token.role) / totalWeight;
      double x = cursor;
      double width = index + 1 == tokens.size()
                         ? note.bounds.right() - cursor
                         : defaultWidth;

      if (token.timing.startOffset.has_value()) {
        x = pianoRoll.pixelAtMicrosecondOffset(
            note.absoluteStart, *token.timing.startOffset);
      }
      if (token.timing.endOffset.has_value()) {
        const auto end = pianoRoll.pixelAtMicrosecondOffset(
            note.absoluteStart, *token.timing.endOffset);
        width = end - x;
      }
      if (!std::isfinite(x) || !std::isfinite(width)) {
        continue;
      }
      width = std::max(2.0, width);
      visuals_.push_back(PhonemeVisual{
          .key = token.key,
          .symbol = token.symbol,
          .role = token.role,
          .bounds = Rect{x, laneTop, width, laneHeight},
          .locked = token.locked,
          .timingOverridden = token.timing.startOffset.has_value() ||
                              token.timing.endOffset.has_value(),
      });
      cursor += defaultWidth;
    }
  }
}

std::optional<domain::PhonemeKey> PhonemeLaneModel::hitTest(Point point) const {
  for (auto iterator = visuals_.rbegin(); iterator != visuals_.rend(); ++iterator) {
    if (iterator->bounds.contains(point)) {
      return iterator->key;
    }
  }
  return std::nullopt;
}

std::optional<std::pair<domain::PhonemeKey, bool>>
PhonemeLaneModel::hitTestBoundary(Point point, double tolerance) const {
  if (!std::isfinite(tolerance) || tolerance <= 0.0) return std::nullopt;
  const auto limit = tolerance * tolerance;
  std::optional<std::pair<domain::PhonemeKey, bool>> result;
  double best = limit;
  for (const auto& visual : visuals_) {
    const auto left = visual.bounds.x;
    const auto right = visual.bounds.right();
    const auto leftDistance = (point.x - left) * (point.x - left);
    const auto rightDistance = (point.x - right) * (point.x - right);
    if (point.y < visual.bounds.y - tolerance ||
        point.y > visual.bounds.bottom() + tolerance) {
      continue;
    }
    if (leftDistance <= best) {
      best = leftDistance;
      result = std::pair{visual.key, true};
    }
    if (rightDistance <= best) {
      best = rightDistance;
      result = std::pair{visual.key, false};
    }
  }
  return result;
}

}  // namespace seam::ui
