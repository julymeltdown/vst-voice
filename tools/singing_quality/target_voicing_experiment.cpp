#include "target_voicing_experiment.hpp"
#include "target_voicing_fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

namespace seam::synthesis::experimental {
namespace {
std::uint64_t mix(std::uint64_t value) noexcept {
  value ^= value >> 30U; value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U; value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}
double noise(std::uint64_t coordinate, std::uint64_t seed) noexcept {
  return static_cast<double>(mix(coordinate + seed) >> 11U) * 0x1.0p-52 - 1.0;
}
void smoothPower(const std::vector<double>& input, std::vector<double>& output, std::size_t radius) {
  // A sliding physical-frequency box, not an O(bins * radius) inner loop.
  double sum = 0.0;
  std::size_t left = 0U, right = 0U;
  for (std::size_t bin = 0U; bin < input.size(); ++bin) {
    const auto first = bin > radius ? bin - radius : 0U;
    const auto end = std::min(input.size(), bin + radius + 1U);
    while (right < end) sum += input[right++];
    while (left < first) sum -= input[left++];
    output[bin] = std::max(0.0, sum / static_cast<double>(end - first));
  }
}
}

core::Result<void> applySampleTargetVoicing(std::span<float> carrier,
    const CompiledScorePerformance& performance, time::SampleFrame origin,
    double carrierFrequencyHz, std::string_view streamId,
    const SourceTargetMap* sourceMap, std::stop_token stopToken) {
  const auto rate = performance.sampleRate();
  if (stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Sample target-voicing was cancelled");
  if (carrier.empty() || carrier.size() > 32U * 1024U * 1024U || rate < 8000U || rate > 384000U ||
      origin > std::numeric_limits<time::SampleFrame>::max() - static_cast<time::SampleFrame>(carrier.size()) ||
      !std::isfinite(carrierFrequencyHz) || carrierFrequencyHz <= 1.0 || carrierFrequencyHz >= rate * 0.5 ||
      streamId.empty() || streamId.size() > 4096U)
    return core::failure(core::ErrorCode::InvalidArgument, "Sample target-voicing exceeds supported bounds");
  const auto frames = static_cast<time::SampleFrame>(carrier.size());
  if (sourceMap) {
    const auto valid = sourceMap->validate(sourceMap->knots.empty() ? 0 : sourceMap->knots.back().sourceFrame);
    if (!valid) return valid;
    if (sourceMap->knots.front().targetFrame != origin || sourceMap->knots.back().targetFrame != origin + frames)
      return core::failure(core::ErrorCode::Conflict, "Sample target-voicing map does not match carrier extent");
  }
  for (std::size_t i = 0U; i < carrier.size(); ++i) {
    if ((i & 4095U) == 0U && stopToken.stop_requested())
      return core::failure(core::ErrorCode::Conflict, "Sample target-voicing was cancelled");
    if (!std::isfinite(carrier[i])) return core::failure(core::ErrorCode::InvalidArgument, "Sample target-voicing needs finite carrier PCM");
  }

  // 32 ms rounded upward to radix-2, 75% overlap, anchored at absolute frame 0.
  std::size_t size = 256U;
  while (size < static_cast<std::size_t>(rate) * 32U / 1000U) size *= 2U;
  const auto hop = size / 4U;
  const auto half = size / 2U;
  const auto fade = std::max<std::size_t>(1U, static_cast<std::size_t>(rate) * 3U / 1000U);
  std::vector<double> original(size), window(size), overlap(size), weights(size);
  std::vector<std::uint8_t> eligible(size);
  std::vector<std::size_t> forward(size);
  std::vector<std::complex<double>> spectrum(size), excitation(size);
  std::vector<double> power(half + 1U), envelope(half + 1U), smoothed(half + 1U);
  double windowPower = 0.0;
  for (std::size_t i = 0U; i < size; ++i) {
    window[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(size));
    windowPower += window[i] * window[i];
  }
  // Cover one carrier-harmonic interval, then soften its box edges by half that
  // width. The 100 Hz floor prevents narrow-band phase randomization masquerading
  // as unvoicing. This is broad-envelope resynthesis, not WORLD/voice qualification.
  const auto radius = std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(
      std::max(100.0, carrierFrequencyHz) * static_cast<double>(size) / (2.0 * rate))), 1U, half);
  std::uint64_t seed = 0xcbf29ce484222325ULL;
  for (const char byte : streamId) { seed ^= static_cast<unsigned char>(byte); seed *= 0x100000001b3ULL; }
  seed = mix(seed);
  const auto hopFrames = static_cast<time::SampleFrame>(hop);
  const auto remainder = (origin % hopFrames + hopFrames) % hopFrames;
  auto start = -static_cast<time::SampleFrame>(size) + hopFrames - remainder;
  const auto fill = [&](std::size_t first) {
    for (std::size_t i = first; i < size; ++i) {
      const auto frame = start + static_cast<time::SampleFrame>(i);
      original[i] = frame >= 0 && frame < frames ? carrier[static_cast<std::size_t>(frame)] : 0.0;
      eligible[i] = 0U;
      if (frame < 0 || frame >= frames) continue;
      const auto absolute = origin + frame;
      const auto value = performance.at(absolute);
      if (value.noteId && !value.scoreFrequencyHz && (!sourceMap ||
          sourceMap->voicedAtSource(sourceMap->sourceAt(static_cast<double>(absolute))) != false)) eligible[i] = 1U;
    }
  };
  fill(0U);
  std::size_t preceding = 0U;
  for (; start < frames; start += hopFrames) {
    if (stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Sample target-voicing was cancelled");
    if (std::any_of(eligible.begin(), eligible.end(), [](auto value) { return value != 0U; })) {
      double sourceEnergy = 0.0, sourcePeak = 0.0;
      for (std::size_t i = 0U; i < size; ++i) {
        const auto sample = original[i] * window[i];
        spectrum[i] = {sample, 0.0};
        sourceEnergy += sample * sample;
        sourcePeak = std::max(sourcePeak, std::abs(original[i]));
        // Unsigned arithmetic permits negative padded coordinates without signed overflow.
        const auto absolute = static_cast<std::uint64_t>(origin) + static_cast<std::uint64_t>(start) + i;
        excitation[i] = {noise(absolute, seed) * window[i], 0.0};
      }
      if (sourceEnergy > 1.0e-24) {
        detail::fft(spectrum, false); detail::fft(excitation, false);
        for (std::size_t bin = 0U; bin <= half; ++bin) power[bin] = std::norm(spectrum[bin]);
        smoothPower(power, envelope, radius);
        smoothPower(envelope, smoothed, std::max<std::size_t>(1U, radius / 2U));
        for (std::size_t bin = 0U; bin <= half; ++bin) {
          excitation[bin] *= std::sqrt(smoothed[bin] / (windowPower / 3.0));
          if (bin == 0U || bin == half) excitation[bin] = {0.0, 0.0};
          else excitation[size - bin] = std::conj(excitation[bin]);
        }
        detail::fft(excitation, true);
        double noiseEnergy = 0.0;
        for (const auto sample : excitation) noiseEnergy += sample.real() * sample.real();
        // Local correction is bounded; no whole-unit limiter or post-stage DC
        // removal may alter protected frames. Near-silence cannot generate hiss.
        const auto gain = noiseEnergy > sourceEnergy * 1.0e-12
            ? std::clamp(std::sqrt(sourceEnergy / noiseEnergy), 0.25, 4.0) : 0.0;
        for (std::size_t i = 0U; i < size; ++i)
          overlap[i] += std::clamp(excitation[i].real() * gain, -4.0 * sourcePeak, 4.0 * sourcePeak) * window[i];
      }
      for (std::size_t i = 0U; i < size; ++i) weights[i] += window[i] * window[i];
    }
    std::size_t following = 0U;
    for (std::size_t i = size; i-- > 0U;) {
      following = eligible[i] ? std::min(2U * fade, following + 1U) : 0U;
      forward[i] = following;
    }
    // Only the first hop is final: all later hops still need future windows.
    for (std::size_t i = 0U; i < hop; ++i) {
      preceding = eligible[i] ? std::min(2U * fade, preceding + 1U) : 0U;
      if (!eligible[i]) continue;
      const auto frame = start + static_cast<time::SampleFrame>(i);
      // Compress BOTH ramps on short runs: even a one-frame run is replaced,
      // never silently ignored. Such spans cannot promise perceptual aperiodicity.
      const auto ramp = std::max<std::size_t>(1U, std::min(fade, (preceding + forward[i]) / 2U));
      auto blend = std::min(1.0, static_cast<double>(std::min(preceding, forward[i])) / static_cast<double>(ramp));
      blend = blend * blend * (3.0 - 2.0 * blend);
      const auto converted = weights[i] > 1.0e-12 ? overlap[i] / weights[i] : 0.0;
      const auto value = original[i] * (1.0 - blend) + converted * blend;
      if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        return core::failure(core::ErrorCode::Unsupported, "Sample target-voicing exceeds finite PCM headroom");
      carrier[static_cast<std::size_t>(frame)] = static_cast<float>(value);
    }
    const auto advance = [hop](auto& values) {
      std::rotate(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(hop), values.end());
    };
    advance(original); advance(eligible); advance(overlap); advance(weights);
    std::fill(overlap.end() - static_cast<std::ptrdiff_t>(hop), overlap.end(), 0.0);
    std::fill(weights.end() - static_cast<std::ptrdiff_t>(hop), weights.end(), 0.0);
    // Original overlapping input lives in the ring. Read only untouched future
    // carrier samples, never PCM already converted in an earlier hop.
    start += hopFrames; fill(size - hop); start -= hopFrames;
  }
  return {};
}
}
