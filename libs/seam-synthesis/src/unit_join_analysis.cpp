#include "seam/synthesis/unit_selection.hpp"

#include <algorithm>
#include <complex>
#include <cmath>
#include <iomanip>
#include <locale>
#include <numbers>
#include <sstream>
#include <vector>

namespace seam::synthesis {
namespace {

core::Result<void> forwardFft(std::vector<std::complex<double>>& values,
                              std::stop_token stop) {
  const auto size = values.size();
  for (std::size_t i = 1U, reversed = 0U; i < size; ++i) {
    auto bit = size >> 1U;
    while ((reversed & bit) != 0U) {
      reversed ^= bit;
      bit >>= 1U;
    }
    reversed ^= bit;
    if (i < reversed) std::swap(values[i], values[reversed]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict,
        "Source spectral-envelope analysis cancelled");
    const auto angle = -2.0 * std::numbers::pi / static_cast<double>(length);
    const std::complex<double> root{std::cos(angle), std::sin(angle)};
    for (std::size_t base = 0U; base < size; base += length) {
      std::complex<double> phase{1.0, 0.0};
      for (std::size_t offset = 0U; offset < length / 2U; ++offset) {
        const auto even = values[base + offset];
        const auto odd = values[base + offset + length / 2U] * phase;
        values[base + offset] = even + odd;
        values[base + offset + length / 2U] = even - odd;
        phase *= root;
      }
    }
  }
  return {};
}

core::Result<std::array<double, 8>> spectralEnvelope(
    std::span<const double> mono, std::uint32_t sampleRate,
    UnitSelectionBudget& budget, std::stop_token stop) {
  std::array<double, 8> result{};
  result.fill(-120.0);
  if (mono.empty()) return core::failure<std::array<double, 8>>(
      core::ErrorCode::InvalidArgument, "Source spectral-envelope window is empty");
  std::size_t fftSize = 1U;
  while (fftSize < mono.size()) fftSize <<= 1U;
  std::size_t stages = 0U;
  for (auto width = fftSize; width > 1U; width >>= 1U) ++stages;
  // Two operations per butterfly plus one bounded positive-frequency power scan.
  const auto work = fftSize * (2U * stages + 1U);
  const auto spent = budget.spend(SelectionWork::SpectralOps, work, stop);
  if (!spent) return core::Result<std::array<double, 8>>{spent.error()};

  std::vector<std::complex<double>> spectrum(fftSize);
  for (std::size_t i = 0U; i < mono.size(); ++i) {
    if ((i & 255U) == 0U && stop.stop_requested()) return core::failure<std::array<double, 8>>(
        core::ErrorCode::Conflict, "Source spectral-envelope analysis cancelled");
    const auto window = mono.size() == 1U ? 1.0 : 0.5 - 0.5 * std::cos(
        2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(mono.size() - 1U));
    spectrum[i] = {mono[i] * window, 0.0};
  }
  const auto transformed = forwardFft(spectrum, stop);
  if (!transformed) return core::Result<std::array<double, 8>>{transformed.error()};

  // The last band continues to Nyquist. This keeps the partition exhaustive
  // while retaining fixed absolute-frequency bands through the voice range.
  constexpr std::array<double, 8> lowerEdgesHz{
      0.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
  std::array<double, 8> bandEnergy{};
  double totalEnergy = 0.0;
  for (std::size_t bin = 0U; bin <= fftSize / 2U; ++bin) {
    if ((bin & 255U) == 0U && stop.stop_requested()) return core::failure<std::array<double, 8>>(
        core::ErrorCode::Conflict, "Source spectral-envelope analysis cancelled");
    const auto frequency = static_cast<double>(bin) * sampleRate / static_cast<double>(fftSize);
    const auto edge = std::upper_bound(lowerEdgesHz.begin(), lowerEdgesHz.end(), frequency);
    const auto band = std::min<std::size_t>(7U,
        static_cast<std::size_t>(std::distance(lowerEdgesHz.begin(), edge) - 1));
    const auto oneSidedWeight = (bin == 0U || bin == fftSize / 2U) ? 1.0 : 2.0;
    const auto energy = std::norm(spectrum[bin]) * oneSidedWeight;
    bandEnergy[band] += energy;
    totalEnergy += energy;
  }
  if (!std::isfinite(totalEnergy)) return core::failure<std::array<double, 8>>(
      core::ErrorCode::InvalidArgument, "Source spectral-envelope energy is nonfinite");
  if (totalEnergy > 1e-24) {
    for (std::size_t band = 0U; band < result.size(); ++band) {
      if (bandEnergy[band] > 0.0) result[band] = std::max(-120.0,
          std::min(0.0, 10.0 * std::log10(bandEnergy[band] / totalEnergy)));
    }
  }
  return result;
}

}  // namespace

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
    features.spectralEnvelopeDb.fill(-120.0);
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
    const auto envelope = spectralEnvelope(mono, audio.sampleRate, budget, stop);
    if (!envelope) return core::Result<SourceBoundaryFeatures>{envelope.error()};
    features.spectralEnvelopeDb = envelope.value();
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
      << "; incoming " << entry.rationale.incomingCost
      << " (spectral envelope " << entry.rationale.spectralEnvelopeCost << ")"
      << "; cumulative " << entry.rationale.cumulativeCost;
  if (!entry.rationale.predecessor.empty()) out << "; after " << entry.rationale.predecessor;
  if (!entry.rationale.joined) out << "; no acoustic edge";
  if (!entry.rationale.evidenceHash.empty()) out << "; evidence " << entry.rationale.evidenceHash;
  return out.str();
}
}  // namespace seam::synthesis
