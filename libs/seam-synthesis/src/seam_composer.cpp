#include "seam/synthesis/seam_composer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::synthesis {

core::Result<PhraseAudio> SeamComposer::compose(
    std::span<const PlacedRenderedUnit> units,
    SeamSettings settings) const {
  if (units.empty() || settings.sampleRate < 8000 || settings.sampleRate > 384000 ||
      !std::isfinite(settings.seamAmount)) {
    return core::failure<PhraseAudio>(core::ErrorCode::InvalidArgument,
                                      "Seam composer input is invalid");
  }
  std::vector<const PlacedRenderedUnit*> ordered;
  ordered.reserve(units.size());
  for (const auto& unit : units) {
    if (unit.unit.samples.empty()) {
      return core::failure<PhraseAudio>(core::ErrorCode::InvalidArgument,
                                        "Seam composer received an empty unit",
                                        unit.unit.unitId);
    }
    ordered.push_back(&unit);
  }
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
    if (lhs->destinationStart == rhs->destinationStart) {
      return lhs->unit.unitId < rhs->unit.unitId;
    }
    return lhs->destinationStart < rhs->destinationStart;
  });

  auto phraseStart = ordered.front()->destinationStart;
  auto phraseEnd = phraseStart;
  for (const auto* unit : ordered) {
    const auto size = static_cast<time::SampleFrame>(unit->unit.samples.size());
    if (unit->destinationStart > std::numeric_limits<time::SampleFrame>::max() - size) {
      return core::failure<PhraseAudio>(core::ErrorCode::Unsupported,
                                        "Phrase position exceeds supported range");
    }
    phraseStart = std::min(phraseStart, unit->destinationStart);
    phraseEnd = std::max(phraseEnd, unit->destinationStart + size);
  }
  if (phraseEnd <= phraseStart ||
      static_cast<std::uint64_t>(phraseEnd - phraseStart) > 100'000'000ULL) {
    return core::failure<PhraseAudio>(core::ErrorCode::Unsupported,
                                      "Composed phrase is outside supported length");
  }

  PhraseAudio result;
  result.startFrame = phraseStart;
  result.samples.resize(static_cast<std::size_t>(phraseEnd - phraseStart), 0.0F);
  std::vector<bool> occupied(result.samples.size(), false);
  const auto seam = std::clamp(settings.seamAmount, 0.0F, 1.0F);

  for (const auto* placed : ordered) {
    const auto localStart = static_cast<std::size_t>(
        placed->destinationStart - phraseStart);
    std::size_t overlapLength = 0;
    while (overlapLength < placed->unit.samples.size() &&
           localStart + overlapLength < occupied.size() &&
           occupied[localStart + overlapLength]) {
      ++overlapLength;
    }
    for (std::size_t index = 0; index < placed->unit.samples.size(); ++index) {
      const auto destination = localStart + index;
      if (destination >= result.samples.size()) break;
      const auto incoming = placed->unit.samples[index];
      if (!occupied[destination]) {
        result.samples[destination] = incoming;
        occupied[destination] = true;
        continue;
      }
      const auto t = overlapLength <= 1U
                         ? 1.0F
                         : static_cast<float>(std::min(index, overlapLength - 1U)) /
                               static_cast<float>(overlapLength - 1U);
      const auto smooth = t * t * (3.0F - 2.0F * t);
      const auto hard = t < 0.5F ? 0.0F : 1.0F;
      const auto mix = smooth * (1.0F - seam) + hard * seam;
      result.samples[destination] = result.samples[destination] * (1.0F - mix) +
                                    incoming * mix;
    }
  }

  const auto fadeFrames = std::min<std::size_t>(
      result.samples.size() / 2U,
      std::max<std::size_t>(1, static_cast<std::size_t>(settings.sampleRate / 1000U)));
  for (std::size_t index = 0; index < fadeFrames; ++index) {
    const auto factor = static_cast<float>(index + 1U) /
                        static_cast<float>(fadeFrames + 1U);
    result.samples[index] *= factor;
    result.samples[result.samples.size() - 1U - index] *= factor;
  }
  return result;
}

}  // namespace seam::synthesis
