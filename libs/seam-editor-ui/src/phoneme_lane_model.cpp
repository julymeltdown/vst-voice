#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

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
  const auto* region = pianoRoll.project().findRegion(pianoRoll.regionId());
  if (!region) return;
  constexpr std::uint32_t displayRate = 48000U;
  const auto& tracks = pianoRoll.project().vocalTracks();
  const auto owner = std::find_if(tracks.begin(), tracks.end(), [&](const auto& track) { return track.findRegion(region->id) != nullptr; });
  const bool procedural = owner != tracks.end() && owner->proceduralRecipe.has_value();
  const auto timing = synthesis::compilePhonemeTimingPlan(pianoRoll.project(), *region, phonemes.tokens, displayRate,
      procedural ? synthesis::PhonemeTimingPolicy::ProceduralInNote : synthesis::PhonemeTimingPolicy::SourceDependent);
  std::map<domain::PhonemeKey, const synthesis::PhonemeTimingAnchor*> anchors;
  std::set<domain::PhonemeKey> inferredNuclei;
  if (timing) for (const auto& anchor : timing.value()) {
    anchors.emplace(anchor.key, &anchor);
    if (anchor.inferredStartFrame && anchor.nucleusKey) inferredNuclei.insert(*anchor.nucleusKey);
  }
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
      double end = index + 1 == tokens.size()
                       ? note.bounds.right()
                       : cursor + defaultWidth;
      const bool secondary = token.role == domain::PhonemeRole::Onset || token.role == domain::PhonemeRole::Coda || token.role == domain::PhonemeRole::Geminate;
      bool estimated = false;
      bool inferred = false;
      bool conflict = !timing;
      const auto found = anchors.find(token.key);
      if (found != anchors.end()) {
        const auto& anchor = *found->second;
        const auto noteFrame = pianoRoll.project().tempoMap().sampleFrameAt(note.absoluteStart, static_cast<double>(displayRate));
        const auto pixel = [&](time::SampleFrame frame) {
          const auto offset = static_cast<time::Microseconds>(std::llround(
              (static_cast<double>(frame) - static_cast<double>(noteFrame)) * 1000000.0 / static_cast<double>(displayRate)));
          return pianoRoll.pixelAtMicrosecondOffset(note.absoluteStart, offset);
        };
        inferred = anchor.inferredStartFrame.has_value() || inferredNuclei.contains(token.key);
        x = pixel(anchor.explicitStartFrame.value_or(anchor.inferredStartFrame.value_or(anchor.nucleusFrame)));
        end = pixel(anchor.endFrame);
        const bool syllabicN = token.symbol=="N" && token.role==domain::PhonemeRole::Coda &&
            !anchor.nucleusKey && tokens.size()==1U;
        if (procedural && (token.role==domain::PhonemeRole::Onset || token.role==domain::PhonemeRole::Coda) &&
            (anchor.explicitStartFrame || anchor.inferredStartFrame || syllabicN)) {
          if (token.role==domain::PhonemeRole::Onset && !anchor.endExplicit) end = pixel(anchor.nucleusFrame);
        } else if (secondary) {
          const auto boundary = token.role == domain::PhonemeRole::Coda ? end : pixel(anchor.nucleusFrame);
          std::size_t remaining = 1U;
          for (auto next = index + 1U; next < tokens.size() && tokens[next].role == token.role; ++next) ++remaining;
          if (!token.timing.startOffset) x = boundary - 28.0 * static_cast<double>(remaining);
          if (!token.timing.endOffset) end = boundary - 28.0 * static_cast<double>(remaining - 1U);
          estimated = !token.timing.startOffset || !token.timing.endOffset;
        }
      }

      if (token.timing.startOffset.has_value()) {
        x = pianoRoll.pixelAtMicrosecondOffset(
            note.absoluteStart, *token.timing.startOffset);
      }
      if (token.timing.endOffset.has_value()) {
        end = pianoRoll.pixelAtMicrosecondOffset(
            note.absoluteStart, *token.timing.endOffset);
      }
      double width = end - x;
      if (!std::isfinite(x) || !std::isfinite(width)) {
        continue;
      }
      conflict = conflict || width <= 0.0;
      if (!procedural || conflict) width = std::max(2.0, width);
      visuals_.push_back(PhonemeVisual{
          .key = token.key,
          .symbol = token.symbol,
          .role = token.role,
          .bounds = Rect{x, laneTop + (secondary ? 0.0 : laneHeight * 0.5), width, laneHeight * 0.5},
          .locked = token.locked,
          .timingOverridden = token.timing.startOffset.has_value() ||
                              token.timing.endOffset.has_value(),
          .timingEstimated = estimated,
          .timingConflict = conflict,
          .timingInferred = inferred,
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
