#include "seam/synthesis/unit_selection.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace seam::synthesis {
core::Result<void> UnitSelectionBudget::spend(SelectionWork work, std::size_t amount,
                                            std::stop_token stop) {
  if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Unit selection cancelled");
  const auto index = static_cast<std::size_t>(work);
  const auto limit = std::min(limits[index], ceilings[index]);
  if (used[index] > limit || amount > limit - used[index]) {
    return core::failure(core::ErrorCode::Unsupported, "Unit selection work budget exceeded",
                         std::to_string(index));
  }
  used[index] += amount;
  return {};
}

core::Result<UnitJoinAnalysis> analyzeUnitJoin(const voicebank::Unit& unit,
    const voicebank::AudioBuffer& audio, std::string_view verifiedAudioSha256,
    UnitSelectionBudget& budget, std::stop_token stop) {
  if (audio.sampleRate < 8000U || audio.sampleRate > 384000U || audio.channels == 0U ||
      audio.channels > 8U || audio.interleaved.size() % audio.channels != 0U ||
      !std::isfinite(unit.gainDb) || std::abs(unit.gainDb) > 96.0F ||
      verifiedAudioSha256.size() != 64U ||
      !std::all_of(verifiedAudioSha256.begin(), verifiedAudioSha256.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      })) return core::failure<UnitJoinAnalysis>(core::ErrorCode::InvalidArgument,
          "Source join analysis requires finite bounded audio and verified byte identity", unit.id);
  const auto markers = unit.markers.validate(static_cast<time::SampleFrame>(audio.frameCount()));
  if (!markers) return core::Result<UnitJoinAnalysis>{markers.error()};
  const auto count = std::min<std::size_t>(audio.sampleRate / 50U,
      static_cast<std::size_t>(unit.markers.audioEnd - unit.markers.audioOffset));
  if (count == 0U) return core::failure<UnitJoinAnalysis>(core::ErrorCode::InvalidArgument,
      "Source join analysis requires a nonempty playable crop", unit.id);
  auto spent = budget.spend(SelectionWork::Samples, count * audio.channels * 2U, stop);
  if (!spent) return core::Result<UnitJoinAnalysis>{spent.error()};
  const auto measure = [&](std::size_t start) -> core::Result<SourceBoundaryFeatures> {
    // At most 7680 mono frames, regardless of the full source duration.
    std::vector<double> mono(count);
    double square = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
      if ((i % 64U) == 0U && stop.stop_requested()) return core::failure<SourceBoundaryFeatures>(
          core::ErrorCode::Conflict, "Source join analysis cancelled");
      for (std::size_t channel = 0; channel < audio.channels; ++channel) {
        const auto value = audio.interleaved[(start + i) * audio.channels + channel];
        if (!std::isfinite(value)) return core::failure<SourceBoundaryFeatures>(
            core::ErrorCode::InvalidArgument, "Nonfinite source join sample", unit.id);
        mono[i] += static_cast<double>(value) / audio.channels;
      }
      square += mono[i] * mono[i];
    }
    SourceBoundaryFeatures features;
    if (square > 0.0) features.levelDb = std::max(-180.0,
        10.0 * std::log10(square / static_cast<double>(count)) + static_cast<double>(unit.gainDb));
    for (std::size_t band = 0; band < features.correlation.size(); ++band) {
      const auto delay = std::max<std::size_t>(1U, static_cast<std::size_t>(std::llround(
          static_cast<double>(1U << band) * audio.sampleRate / 48000.0)));
      double dot = 0.0, first = 0.0, second = 0.0;
      for (std::size_t i = delay; i < count; ++i) {
        dot += mono[i] * mono[i - delay];
        first += mono[i] * mono[i]; second += mono[i - delay] * mono[i - delay];
      }
      if (first > 1e-18 && second > 1e-18) {
        features.correlation[band] = std::clamp(dot / std::sqrt(first * second), -1.0, 1.0);
      }
    }
    return features;
  };
  auto head = measure(static_cast<std::size_t>(unit.markers.audioOffset));
  if (!head) return core::Result<UnitJoinAnalysis>{head.error()};
  auto tail = measure(static_cast<std::size_t>(unit.markers.audioEnd) - count);
  if (!tail) return core::Result<UnitJoinAnalysis>{tail.error()};
  return UnitJoinAnalysis{unit.id, std::string{verifiedAudioSha256}, head.value(), tail.value()};
}

std::string describeUnitSelection(const UnitPlanEntry& entry) {
  std::ostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(3);
  out << (entry.rationale.acoustic ? "Source-boundary proxy" : "Metadata-only")
      << " v" << entry.rationale.revision << "; tokens " << entry.tokenStart << "+" << entry.tokenCount
      << (entry.forced ? "; forced" : "; automatic") << "; local " << entry.score
      << "; incoming " << entry.rationale.incomingCost << "; cumulative " << entry.rationale.cumulativeCost;
  if (!entry.rationale.predecessor.empty()) out << "; after " << entry.rationale.predecessor;
  if (!entry.rationale.joined) out << "; no acoustic edge";
  if (!entry.rationale.evidenceHash.empty()) out << "; evidence " << entry.rationale.evidenceHash;
  return out.str();
}
}  // namespace seam::synthesis
