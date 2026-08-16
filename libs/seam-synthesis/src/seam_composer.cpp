#include "seam/synthesis/seam_composer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::synthesis {
namespace {

float characterCurve(float t, domain::SeamCurve curve) noexcept {
  const auto clamped = std::clamp(t, 0.0F, 1.0F);
  switch (curve) {
    case domain::SeamCurve::Smooth:
      return clamped * clamped * (3.0F - 2.0F * clamped);
    case domain::SeamCurve::Linear:
      return clamped;
    case domain::SeamCurve::EqualPower: {
      const auto angle = clamped * 1.57079632679489661923F;
      const auto incoming = std::sin(angle);
      const auto outgoing = std::cos(angle);
      const auto denominator = incoming + outgoing;
      return denominator > 1.0e-6F ? incoming / denominator : clamped;
    }
    case domain::SeamCurve::HardCharacter:
      return clamped < 0.5F ? 0.0F : 1.0F;
  }
  return clamped;
}

std::ptrdiff_t phaseAlignmentOffset(std::span<const float> outgoing,
                                    std::span<const float> incoming,
                                    std::size_t overlapLength) noexcept {
  if (overlapLength < 8U || outgoing.size() < overlapLength || incoming.empty()) {
    return 0;
  }
  const auto compared = std::min<std::size_t>(overlapLength, 512U);
  const auto radius = static_cast<std::ptrdiff_t>(
      std::min<std::size_t>({64U, overlapLength / 4U, incoming.size() / 4U}));
  double bestScore = -2.0;
  std::ptrdiff_t bestOffset = 0;
  for (auto offset = -radius; offset <= radius; ++offset) {
    double cross = 0.0;
    double outgoingEnergy = 0.0;
    double incomingEnergy = 0.0;
    std::size_t count = 0;
    for (std::size_t index = 0; index < compared; ++index) {
      const auto incomingIndex = static_cast<std::ptrdiff_t>(index) + offset;
      if (incomingIndex < 0 ||
          incomingIndex >= static_cast<std::ptrdiff_t>(incoming.size())) {
        continue;
      }
      const auto left = static_cast<double>(outgoing[index]);
      const auto right = static_cast<double>(
          incoming[static_cast<std::size_t>(incomingIndex)]);
      cross += left * right;
      outgoingEnergy += left * left;
      incomingEnergy += right * right;
      ++count;
    }
    if (count < compared / 2U || outgoingEnergy <= 1.0e-12 ||
        incomingEnergy <= 1.0e-12) {
      continue;
    }
    const auto score = cross / std::sqrt(outgoingEnergy * incomingEnergy);
    if (score > bestScore) {
      bestScore = score;
      bestOffset = offset;
    }
  }
  return bestOffset;
}

float overlapGain(std::span<const float> outgoing,
                  std::span<const float> incoming,
                  std::size_t overlapLength,
                  std::ptrdiff_t phaseOffset) noexcept {
  if (overlapLength == 0U || outgoing.size() < overlapLength || incoming.empty()) {
    return 1.0F;
  }
  double outgoingEnergy = 0.0;
  double incomingEnergy = 0.0;
  std::size_t count = 0;
  const auto compared = std::min<std::size_t>(overlapLength, 1024U);
  for (std::size_t index = 0; index < compared; ++index) {
    const auto incomingIndex = static_cast<std::ptrdiff_t>(index) + phaseOffset;
    if (incomingIndex < 0 ||
        incomingIndex >= static_cast<std::ptrdiff_t>(incoming.size())) {
      continue;
    }
    const auto left = static_cast<double>(outgoing[index]);
    const auto right = static_cast<double>(
        incoming[static_cast<std::size_t>(incomingIndex)]);
    outgoingEnergy += left * left;
    incomingEnergy += right * right;
    ++count;
  }
  if (count == 0U || incomingEnergy <= 1.0e-12) return 1.0F;
  const auto ratio = std::sqrt(outgoingEnergy / incomingEnergy);
  return static_cast<float>(std::clamp(ratio, 0.25, 4.0));
}

float shiftedSample(std::span<const float> samples,
                    std::size_t index,
                    std::ptrdiff_t offset) noexcept {
  if (samples.empty()) return 0.0F;
  const auto shifted = std::clamp<std::ptrdiff_t>(
      static_cast<std::ptrdiff_t>(index) + offset, 0,
      static_cast<std::ptrdiff_t>(samples.size() - 1U));
  return samples[static_cast<std::size_t>(shifted)];
}

}  // namespace

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
    if (unit.incomingBoundary.has_value() &&
        (!std::isfinite(unit.incomingBoundary->seamAmount) ||
         unit.incomingBoundary->seamAmount < 0.0F ||
         unit.incomingBoundary->seamAmount > 1.0F ||
         !std::isfinite(unit.incomingBoundary->phaseReset) ||
         unit.incomingBoundary->phaseReset < 0.0F ||
         unit.incomingBoundary->phaseReset > 1.0F ||
         !std::isfinite(unit.incomingBoundary->envelopeBlend) ||
         unit.incomingBoundary->envelopeBlend < 0.0F ||
         unit.incomingBoundary->envelopeBlend > 1.0F ||
         (unit.incomingBoundary->maxOverlapFrames.has_value() &&
          *unit.incomingBoundary->maxOverlapFrames < 0))) {
      return core::failure<PhraseAudio>(core::ErrorCode::InvalidArgument,
                                        "Boundary seam override is invalid",
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

  for (const auto* placed : ordered) {
    const auto localStart = static_cast<std::size_t>(
        placed->destinationStart - phraseStart);
    std::size_t overlapLength = 0;
    while (overlapLength < placed->unit.samples.size() &&
           localStart + overlapLength < occupied.size() &&
           occupied[localStart + overlapLength]) {
      ++overlapLength;
    }
    if (placed->incomingBoundary.has_value() &&
        placed->incomingBoundary->maxOverlapFrames.has_value()) {
      overlapLength = std::min(
          overlapLength,
          static_cast<std::size_t>(*placed->incomingBoundary->maxOverlapFrames));
    }
    const auto baseAmount = placed->incomingBoundary.has_value()
        ? placed->incomingBoundary->seamAmount
        : settings.seamAmount;
    // phaseReset=0 attempts short-range phase continuity; phaseReset=1 keeps
    // the incoming unit's original phase and therefore exposes the reset.
    const auto phaseReset = placed->incomingBoundary.has_value()
        ? placed->incomingBoundary->phaseReset
        : 1.0F;
    const auto envelopeBlend = placed->incomingBoundary.has_value()
        ? placed->incomingBoundary->envelopeBlend
        : 0.0F;
    const auto effectiveCurve = placed->incomingBoundary.has_value()
        ? placed->incomingBoundary->curve
        : settings.curve;
    const auto seam = std::clamp(baseAmount, 0.0F, 1.0F);

    const auto outgoingOverlap = std::span<const float>{result.samples}.subspan(
        localStart, std::min(overlapLength, result.samples.size() - localStart));
    const auto incomingSamples = std::span<const float>{placed->unit.samples};
    const auto alignedOffset = phaseAlignmentOffset(
        outgoingOverlap, incomingSamples, overlapLength);
    const auto matchedGain = overlapGain(
        outgoingOverlap, incomingSamples, overlapLength, alignedOffset);

    for (std::size_t index = 0; index < placed->unit.samples.size(); ++index) {
      const auto destination = localStart + index;
      if (destination >= result.samples.size()) break;
      const auto originalIncoming = placed->unit.samples[index];
      if (!occupied[destination]) {
        result.samples[destination] = originalIncoming;
        occupied[destination] = true;
        continue;
      }
      const auto t = overlapLength <= 1U
                         ? 1.0F
                         : static_cast<float>(std::min(index, overlapLength - 1U)) /
                               static_cast<float>(overlapLength - 1U);
      const auto alignedIncoming = shiftedSample(
          incomingSamples, index, alignedOffset);
      auto incoming = alignedIncoming * (1.0F - phaseReset) +
                      originalIncoming * phaseReset;
      const auto gain = 1.0F + (matchedGain - 1.0F) * envelopeBlend * (1.0F - t);
      incoming *= gain;
      const auto smooth = t * t * (3.0F - 2.0F * t);
      const auto character = characterCurve(t, effectiveCurve);
      const auto mix = smooth * (1.0F - seam) + character * seam;
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
