#include "seam/synthesis/spectral_classic.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace seam::synthesis {
namespace {

bool powerOfTwo(std::size_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

void fft(std::vector<std::complex<double>>& values, bool inverse) {
  const auto size = values.size();
  for (std::size_t index = 1U, reversed = 0U; index < size; ++index) {
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0U; bit >>= 1U) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) std::swap(values[index], values[reversed]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    const auto direction = inverse ? 1.0 : -1.0;
    const auto angle = direction * 2.0 * std::numbers::pi /
                       static_cast<double>(length);
    const std::complex<double> step{std::cos(angle), std::sin(angle)};
    for (std::size_t start = 0U; start < size; start += length) {
      std::complex<double> phase{1.0, 0.0};
      const auto half = length / 2U;
      for (std::size_t offset = 0U; offset < half; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + half] * phase;
        values[start + offset] = even + odd;
        values[start + offset + half] = even - odd;
        phase *= step;
      }
    }
  }
  if (inverse) {
    const auto scale = 1.0 / static_cast<double>(size);
    for (auto& value : values) value *= scale;
  }
}

float interpolate(std::span<const float> samples, double position) noexcept {
  if (samples.empty()) return 0.0F;
  const auto clamped = std::clamp(position, 0.0,
                                  static_cast<double>(samples.size() - 1U));
  const auto left = static_cast<std::size_t>(std::floor(clamped));
  const auto right = std::min(left + 1U, samples.size() - 1U);
  const auto fraction = static_cast<float>(clamped - static_cast<double>(left));
  return samples[left] * (1.0F - fraction) + samples[right] * fraction;
}

float loopInterpolate(std::span<const float> samples,
                      double position,
                      double loopStart,
                      double loopEnd) noexcept {
  const auto length = loopEnd - loopStart;
  if (length <= 1.0) return interpolate(samples, position);
  auto relative = std::fmod(position - loopStart, length);
  if (relative < 0.0) relative += length;
  return interpolate(samples, loopStart + relative);
}

double midiRatio(std::int32_t targetMidi,
                 std::int32_t rootMidi,
                 float cents) noexcept {
  return std::pow(2.0,
      (static_cast<double>(targetMidi - rootMidi) +
       static_cast<double>(cents) / 100.0) / 12.0);
}

double wrappedPhaseDifference(double target, double source) noexcept {
  if (!std::isfinite(target) || !std::isfinite(source)) return 0.0;
  auto difference = target - source;
  // Use std::remainder for a bounded phase wrap instead of unbounded while
  // loops that spin forever on non-finite differences under GCC -O3.
  difference = std::remainder(difference, 2.0 * std::numbers::pi);
  return difference;
}

double blendPhase(double continuous, double source, double reset) noexcept {
  return continuous + wrappedPhaseDifference(source, continuous) * reset;
}

void finishUnit(RenderedUnit& result,
                float gainDb,
                std::uint32_t sampleRate) {
  const auto gain = static_cast<float>(std::pow(10.0,
      static_cast<double>(gainDb) / 20.0));
  long double mean = 0.0L;
  for (auto& sample : result.samples) {
    sample = std::isfinite(sample) ? sample * gain : 0.0F;
    mean += sample;
  }
  if (!result.samples.empty()) {
    mean /= static_cast<long double>(result.samples.size());
    for (auto& sample : result.samples) {
      sample -= static_cast<float>(mean);
    }
  }
  const auto fadeFrames = std::min<std::size_t>(
      result.samples.size() / 2U,
      std::max<std::size_t>(1U, static_cast<std::size_t>(sampleRate / 1000U)));
  for (std::size_t index = 0U; index < fadeFrames; ++index) {
    const auto factor = static_cast<float>(index + 1U) /
                        static_cast<float>(fadeFrames + 1U);
    result.samples[index] *= factor;
    result.samples[result.samples.size() - 1U - index] *= factor;
  }
}

// Warp the envelope of the already pitch/time-rendered signal, not its harmonic
// positions or phases. This covers recorded attacks, releases and unvoiced spans
// which intentionally bypass the legacy sustain resynthesizer above.
// Integer-semitone anchor deltas are interpolated at EACH destination sample's
// compiled value. Only anchors needed in this FFT window are computed; a short
// accepted/manual island between hop centers cannot disappear. Zero contributes
// exactly zero delta, including inside a nonneutral phrase.
struct FormantPlan {
  std::vector<std::uint64_t> anchors;
  std::size_t fftCells{0U};
  time::SampleFrame firstCenter{0};
};

// No carrier DSP or destination PCM allocation precedes this exact work plan.
// A separate scan bound also caps neutral/rapidly alternating automation; a
// rejected curve is never decimated to fit. Scratch stays O(window count + FFT).
core::Result<FormantPlan> prepareFormants(const SpectralRenderParameters& parameters,
    time::SampleFrame length, std::stop_token stop) {
  FormantPlan plan;
  if (!parameters.performance || !parameters.performance->requiresFormantControl()) return plan;
  constexpr int maximumSemitones = static_cast<int>(domain::kMaximumFormantShiftSemitones);
  constexpr std::size_t maximumCells = 64U * 1024U * 1024U;
  const auto hop = static_cast<time::SampleFrame>(parameters.hopSize);
  const auto half = static_cast<time::SampleFrame>(parameters.fftSize / 2U);
  const auto remainder = (parameters.performanceStartFrame % hop + hop) % hop;
  plan.firstCenter = -remainder - ((half - 1 - remainder) / hop) * hop;
  const auto count = static_cast<std::size_t>((length + half - plan.firstCenter + hop - 1) / hop);
  if (count > maximumCells / parameters.fftSize)
    return core::failure<FormantPlan>(core::ErrorCode::Unsupported, "Formant control scan exceeds the per-unit budget");
  plan.anchors.reserve(count);
  for (auto center = plan.firstCenter; center < length + half; center += hop) {
    std::uint64_t mask = 0U;
    const auto begin = std::max<time::SampleFrame>(0, center - half);
    const auto end = std::min(length, center + half);
    for (auto frame = begin; frame < end; ++frame) {
      if ((frame & 1023) == 0 && stop.stop_requested())
        return core::failure<FormantPlan>(core::ErrorCode::Conflict, "Formant planning was cancelled");
      const auto value = parameters.performance->at(parameters.performanceStartFrame + frame).formantSemitones;
      if (!std::isfinite(value) || std::abs(value) > domain::kMaximumFormantShiftSemitones)
        return core::failure<FormantPlan>(core::ErrorCode::InvalidArgument, "Invalid compiled formant shift");
      for (const auto anchor : {static_cast<int>(std::floor(value)), static_cast<int>(std::ceil(value))})
        if (anchor != 0) mask |= std::uint64_t{1} << static_cast<unsigned>(anchor + maximumSemitones);
    }
    if (stop.stop_requested())
      return core::failure<FormantPlan>(core::ErrorCode::Conflict, "Formant planning was cancelled");
    const auto cells = mask == 0U ? 0U : parameters.fftSize * (static_cast<std::size_t>(std::popcount(mask)) + 1U);
    if (cells > maximumCells - plan.fftCells)
      return core::failure<FormantPlan>(core::ErrorCode::Unsupported, "Formant FFT work exceeds the per-unit budget");
    plan.fftCells += cells;
    plan.anchors.push_back(mask);
  }
  return plan;
}

core::Result<bool> shiftFormants(RenderedUnit& result, const SpectralRenderParameters& parameters,
    const FormantPlan& plan,
    std::uint32_t rate, std::vector<float>& overlap, std::vector<float>& weights,
    std::stop_token stop) {
  if (plan.fftCells == 0U) return false;
  constexpr int maximumSemitones = static_cast<int>(domain::kMaximumFormantShiftSemitones);
  const auto size = parameters.fftSize, half = size / 2U;
  const auto length = static_cast<time::SampleFrame>(result.samples.size());
  std::fill(overlap.begin(), overlap.end(), 0.0F);
  std::fill(weights.begin(), weights.end(), 0.0F);
  std::vector<std::complex<double>> spectrum(size), delta(size);
  std::vector<double> envelope(half + 1U), prefix(half + 2U), values(size), window(size);
  for (std::size_t i = 0; i < size; ++i)
    window[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                    static_cast<double>(size - 1U));
  const auto radius = std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(
      300.0 * static_cast<double>(size) / static_cast<double>(rate))));
  std::size_t windowIndex = 0U;
  const auto hop = static_cast<time::SampleFrame>(parameters.hopSize);
  for (auto center = plan.firstCenter; center < length + static_cast<time::SampleFrame>(half); center += hop) {
    if (stop.stop_requested()) return core::failure<bool>(core::ErrorCode::Conflict, "Formant rendering was cancelled");
    const auto needed = plan.anchors[windowIndex++];
    if (needed == 0U) continue;
    const auto begin = center - static_cast<time::SampleFrame>(half);
    for (std::size_t i = 0; i < size; ++i) {
      if ((i & 1023U) == 0U && stop.stop_requested())
        return core::failure<bool>(core::ErrorCode::Conflict, "Formant rendering was cancelled");
      const auto frame = begin + static_cast<time::SampleFrame>(i);
      values[i] = 0.0;
      spectrum[i] = {};
      if (frame < 0 || frame >= length) continue;
      const auto sample = result.samples[static_cast<std::size_t>(frame)];
      if (!std::isfinite(sample)) return core::failure<bool>(core::ErrorCode::InvalidArgument, "Nonfinite formant source");
      const auto semitones = parameters.performance->at(parameters.performanceStartFrame + frame).formantSemitones;
      if (!std::isfinite(semitones) || std::abs(semitones) > domain::kMaximumFormantShiftSemitones)
        return core::failure<bool>(core::ErrorCode::InvalidArgument, "Invalid compiled formant shift");
      values[i] = semitones;
      spectrum[i] = {static_cast<double>(sample) * window[i], 0.0};
    }
    fft(spectrum, false);
    prefix[0] = 0.0;
    for (std::size_t bin = 0; bin <= half; ++bin) prefix[bin + 1U] = prefix[bin] + std::abs(spectrum[bin]);
    for (std::size_t bin = 0; bin <= half; ++bin) {
      const auto first = bin > radius ? bin - radius : 0U, last = std::min(half, bin + radius);
      envelope[bin] = (prefix[last + 1U] - prefix[first]) / static_cast<double>(last - first + 1U);
    }
    const auto floor = std::max(1.0e-12, *std::max_element(envelope.begin(), envelope.end()) * 1.0e-4);
    for (int anchor = -maximumSemitones; anchor <= maximumSemitones; ++anchor) {
      if ((needed & (std::uint64_t{1} << static_cast<unsigned>(anchor + maximumSemitones))) == 0U) continue;
      if (stop.stop_requested()) return core::failure<bool>(core::ErrorCode::Conflict, "Formant rendering was cancelled");
      const auto ratio = std::exp2(static_cast<double>(anchor) / 12.0);
      for (std::size_t bin = 0; bin <= half; ++bin) {
        const auto position = static_cast<double>(bin) / ratio;
        double target = 0.0;
        if (position <= static_cast<double>(half)) {
          const auto left = static_cast<std::size_t>(position), right = std::min(left + 1U, half);
          const auto fraction = position - static_cast<double>(left);
          target = envelope[left] * (1.0 - fraction) + envelope[right] * fraction;
        }
        // Explicit regularization bounds the filter, not an output-driven gain
        // normalization. Above-Nyquist source envelope is zero, never clamped to
        // the last bin and invented as a high-frequency shelf.
        const auto correction = std::clamp(target / std::max(envelope[bin], floor), 0.0, 16.0);
        delta[bin] = spectrum[bin] * (correction - 1.0);
        if (bin != 0U && bin != half) delta[size - bin] = std::conj(delta[bin]);
      }
      fft(delta, true);
      for (std::size_t i = 0; i < size; ++i) {
        const auto frame = begin + static_cast<time::SampleFrame>(i);
        if (frame < 0 || frame >= length || values[i] == 0.0) continue;
        const auto mix = std::max(0.0, 1.0 - std::abs(values[i] - static_cast<double>(anchor)));
        overlap[static_cast<std::size_t>(frame)] += static_cast<float>(delta[i].real() * window[i] * mix);
      }
    }
    for (std::size_t i = 0; i < size; ++i) {
      const auto frame = begin + static_cast<time::SampleFrame>(i);
      if (frame >= 0 && frame < length && values[i] != 0.0)
        weights[static_cast<std::size_t>(frame)] += static_cast<float>(window[i] * window[i]);
    }
  }
  const auto fadeFrames = std::min<std::size_t>(result.samples.size() / 2U, std::max<std::size_t>(1U, rate / 1000U));
  for (std::size_t i = 0; i < result.samples.size(); ++i) {
    if ((i & 1023U) == 0U && stop.stop_requested())
      return core::failure<bool>(core::ErrorCode::Conflict, "Formant rendering was cancelled");
    if (weights[i] > 1.0e-7F) {
      const auto edge = std::min(i, result.samples.size() - 1U - i);
      // The carrier already has its legacy fade. Fade only the added delta so
      // filtering cannot reopen that edge, without double-fading the baseline.
      const auto fade = edge < fadeFrames ? static_cast<float>(edge + 1U) / static_cast<float>(fadeFrames + 1U) : 1.0F;
      result.samples[i] += overlap[i] / weights[i] * fade;
    }
    if (!std::isfinite(result.samples[i])) return core::failure<bool>(core::ErrorCode::Unsupported, "Formant output is nonfinite");
  }
  return true;
}

}  // namespace

core::Result<RenderedUnit> SpectralClassicRenderer::render(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const SpectralRenderParameters& parameters,
    std::stop_token stopToken) const {
  if (source.sampleRate == 0U || source.channels == 0U ||
      source.interleaved.empty() || outputSampleRate < 8000U ||
      outputSampleRate > 384000U || outputFrames <= 0 || outputFrames > 32 * 1024 * 1024 || targetMidi < 0 ||
      targetMidi > 127 || !powerOfTwo(parameters.fftSize) ||
      parameters.fftSize < 128U || parameters.fftSize > 8192U ||
      parameters.hopSize == 0U || parameters.hopSize > parameters.fftSize / 2U ||
      !std::isfinite(parameters.formantFollow) ||
      parameters.formantFollow < 0.0F || parameters.formantFollow > 1.0F ||
      !std::isfinite(parameters.phaseReset) || parameters.phaseReset < 0.0F ||
      parameters.phaseReset > 1.0F ||
      !std::isfinite(parameters.additionalGainDb)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
                                       "SpectralClassic input is invalid",
                                       unit.id);
  }
  const auto curveValidation = parameters.pitchCurve.validate();
  if (!curveValidation) return core::Result<RenderedUnit>{curveValidation.error()};
  if (parameters.performance && (parameters.performance->sampleRate() != outputSampleRate ||
      !parameters.pitchCurve.points().empty() ||
      parameters.performanceStartFrame > std::numeric_limits<time::SampleFrame>::max() - outputFrames)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
        "Compiled spectral performance requires matching rate, bounded origin and no duplicate pitch curve", unit.id);
  }
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return core::Result<RenderedUnit>{markerValidation.error()};
  if (stopToken.stop_requested()) {
    return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                       "SpectralClassic render was cancelled",
                                       unit.id);
  }

  if (parameters.sourceMap) {
    const auto validation = parameters.sourceMap->validate(static_cast<time::SampleFrame>(source.frameCount()));
    if (!validation) return core::Result<RenderedUnit>{validation.error()};
    const auto& knots = parameters.sourceMap->knots;
    if (!parameters.performance || knots.front().sourceFrame != unit.markers.audioOffset ||
        knots.back().sourceFrame != unit.markers.audioEnd || knots.front().targetFrame != parameters.performanceStartFrame ||
        knots.back().targetFrame - knots.front().targetFrame != outputFrames) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Spectral source map does not match output extent", unit.id);
    }
  }
  const auto formantPlan = prepareFormants(parameters, outputFrames, stopToken);
  if (!formantPlan) return core::Result<RenderedUnit>{formantPlan.error()};
  const auto mono = source.monoMix();
  if (parameters.performance && parameters.performance->requiresFormantControl() &&
      !std::all_of(mono.begin(), mono.end(), [](float value) { return std::isfinite(value); }))
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument, "Nonfinite formant source", unit.id);
  const auto sourcePerOutput = static_cast<double>(source.sampleRate) /
                               static_cast<double>(outputSampleRate);
  const auto& markers = unit.markers;
  const auto offset = markers.audioOffset;
  const auto audioEnd = markers.audioEnd;
  const auto stableStart = markers.stableStart;
  const auto loopStart = markers.loopStart.value_or(stableStart);
  const auto loopEnd = markers.loopEnd.value_or(
      markers.releaseStart.value_or(audioEnd));
  const auto releaseStart = markers.releaseStart.value_or(audioEnd);
  if (loopEnd - loopStart < static_cast<time::SampleFrame>(parameters.fftSize / 4U)) {
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                       "SpectralClassic sustain loop is too short",
                                       unit.id);
  }

  auto vowelOnsetFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.vowelOnset - offset) / sourcePerOutput)),
      0, outputFrames - 1);
  // Preserve the complete recorded vowel transition. Spectral processing starts
  // at stableStart, not vowelOnset, so the singer's physical onset remains audible.
  auto preFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.stableStart - offset) / sourcePerOutput)),
      vowelOnsetFrames, outputFrames - 1);
  const auto sourceReleaseFrames = std::max<time::SampleFrame>(0, audioEnd - releaseStart);
  const auto releaseFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(sourceReleaseFrames) / sourcePerOutput)),
      0, std::max<time::SampleFrame>(0, outputFrames - preFrames));
  auto releaseOutputStart = std::max(preFrames, outputFrames - releaseFrames);
  if (parameters.sourceMap) {
    const auto mapped = [&](time::SampleFrame frame) {
      return static_cast<time::SampleFrame>(std::llround(parameters.sourceMap->targetAt(static_cast<double>(frame)))) - parameters.performanceStartFrame;
    };
    vowelOnsetFrames = std::clamp<time::SampleFrame>(mapped(markers.vowelOnset), 0, outputFrames - 1);
    preFrames = std::clamp<time::SampleFrame>(mapped(stableStart), vowelOnsetFrames, outputFrames);
    releaseOutputStart = std::clamp<time::SampleFrame>(mapped(releaseStart), preFrames, outputFrames);
  }

  const bool continuation = parameters.performance && unit.phones.size() == 1U &&
      !parameters.performance->at(parameters.performanceStartFrame + vowelOnsetFrames).reattack;
  if (continuation) preFrames = 0;
  RenderedUnit result{
      .unitId = unit.id,
      .samples = std::vector<float>(static_cast<std::size_t>(outputFrames), 0.0F),
      .vowelOnsetOffset = vowelOnsetFrames,
  };

  // Preserve consonant/transition and release directly from the source. Only the
  // stable vowel is spectrally transformed, retaining the unit's physical edges.
  for (time::SampleFrame frame = 0; frame < preFrames; ++frame) {
    auto sourcePosition = static_cast<double>(offset) +
                                static_cast<double>(frame) * sourcePerOutput;
    if (parameters.sourceMap) sourcePosition = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + frame));
    result.samples[static_cast<std::size_t>(frame)] = interpolate(mono, sourcePosition);
  }
  for (time::SampleFrame frame = releaseOutputStart; frame < outputFrames; ++frame) {
    auto sourcePosition = static_cast<double>(releaseStart) +
        static_cast<double>(frame - releaseOutputStart) * sourcePerOutput;
    if (parameters.sourceMap) sourcePosition = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + frame));
    result.samples[static_cast<std::size_t>(frame)] =
        interpolate(mono, std::min(sourcePosition, static_cast<double>(audioEnd - 1)));
  }

  const auto fftSize = parameters.fftSize;
  const auto half = fftSize / 2U;
  const auto hop = static_cast<time::SampleFrame>(parameters.hopSize);
  const auto stableFrames = std::max<time::SampleFrame>(1, releaseOutputStart - preFrames);
  const auto loopLength = static_cast<double>(loopEnd - loopStart);
  std::vector<std::complex<double>> spectrum(fftSize);
  std::vector<std::complex<double>> shifted(fftSize);
  std::vector<double> outputPhase(half + 1U, 0.0);
  std::vector<double> previousSourcePhase(half + 1U, 0.0);
  std::vector<double> sourceFrequency(half + 1U, 0.0);
  std::vector<float> overlap(result.samples.size(), 0.0F);
  std::vector<float> weights(result.samples.size(), 0.0F);
  bool firstFrame = true;
  std::size_t frameIndex = 0U;
  double previousSourceCenter = 0.0;

  const auto firstCenter = preFrames;
  for (auto center = firstCenter; center < releaseOutputStart; center += hop, ++frameIndex) {
    if ((frameIndex & 0x0fU) == 0U && stopToken.stop_requested()) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                         "SpectralClassic render was cancelled",
                                         unit.id);
    }
    const auto progress = static_cast<double>(center - preFrames) /
                          static_cast<double>(stableFrames);
    auto sourceCenter = static_cast<double>(loopStart) +
        std::fmod(progress * loopLength, loopLength);
    if (parameters.sourceMap) sourceCenter = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + center));
    if (continuation && parameters.sourceMap) sourceCenter = std::max(sourceCenter, static_cast<double>(loopStart));
    for (std::size_t index = 0U; index < fftSize; ++index) {
      const auto relative = static_cast<double>(index) - static_cast<double>(half);
      const auto sourcePosition = sourceCenter + relative * sourcePerOutput;
      const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                         static_cast<double>(fftSize - 1U);
      const auto window = 0.5 - 0.5 * std::cos(phase);
      const auto sample = parameters.sourceMap ? interpolate(mono, std::clamp(sourcePosition,
          static_cast<double>(offset), static_cast<double>(audioEnd - 1))) :
          loopInterpolate(mono, sourcePosition, static_cast<double>(loopStart), static_cast<double>(loopEnd));
      spectrum[index] = std::complex<double>{static_cast<double>(sample) * window, 0.0};
      shifted[index] = {0.0, 0.0};
    }
    fft(spectrum, false);
    if (parameters.performance) {
      std::vector<std::complex<double>> probe;
      if (firstFrame && continuation && parameters.sourceMap) {
        // A held source center has zero inter-frame motion. Measure its local
        // off-bin frequency with one adjacent analysis window, then retain that
        // estimate until the authored map advances into the sustain.
        probe.resize(fftSize);
        for (std::size_t index = 0; index < fftSize; ++index) {
          const auto position = sourceCenter + (static_cast<double>(index) - static_cast<double>(half) + static_cast<double>(hop)) * sourcePerOutput;
          const auto window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(fftSize - 1U));
          probe[index] = {static_cast<double>(interpolate(mono, std::clamp(position,
              static_cast<double>(offset), static_cast<double>(audioEnd - 1)))) * window, 0.0};
        }
        fft(probe, false);
      }
      const auto analysisAdvance = parameters.sourceMap ? (sourceCenter - previousSourceCenter) / sourcePerOutput :
          loopLength / static_cast<double>(stableFrames) * static_cast<double>(hop) / sourcePerOutput;
      for (std::size_t bin = 0U; bin <= half; ++bin) {
        const auto phase = std::arg(spectrum[bin]);
        const auto nominal = 2.0 * std::numbers::pi * static_cast<double>(bin) / static_cast<double>(fftSize);
        if (firstFrame) {
          sourceFrequency[bin] = nominal;
          if (!probe.empty()) sourceFrequency[bin] += wrappedPhaseDifference(std::arg(probe[bin]),
              phase + nominal * static_cast<double>(hop)) / static_cast<double>(hop);
        } else if (analysisAdvance > 1.0e-9) {
          sourceFrequency[bin] = nominal + wrappedPhaseDifference(phase,
              previousSourcePhase[bin] + nominal * analysisAdvance) / analysisAdvance;
        }
        previousSourcePhase[bin] = phase;
      }
    }
    previousSourceCenter = sourceCenter;

    // Estimate a deliberately coarse spectral envelope.  Pitch shifting must
    // move harmonic energy first; formant preservation is applied as a
    // multiplicative envelope correction rather than mixing the unshifted
    // spectrum back in.  Mixing unshifted magnitudes would leave a second
    // fundamental at the source pitch, which is especially obvious on the
    // single-harmonic fixtures used by the regression suite.
    std::vector<double> magnitudes(half + 1U, 0.0);
    std::vector<double> envelope(half + 1U, 0.0);
    for (std::size_t bin = 0U; bin <= half; ++bin) {
      magnitudes[bin] = std::abs(spectrum[bin]);
    }
    constexpr std::size_t kEnvelopeRadius = 12U;
    for (std::size_t bin = 0U; bin <= half; ++bin) {
      const auto first = bin > kEnvelopeRadius ? bin - kEnvelopeRadius : 0U;
      const auto last = std::min(half, bin + kEnvelopeRadius);
      double sum = 0.0;
      for (auto neighbor = first; neighbor <= last; ++neighbor) {
        sum += magnitudes[neighbor];
      }
      envelope[bin] = sum / static_cast<double>(last - first + 1U);
    }

    const auto cents = parameters.pitchCurve.centsAt(center);
    auto ratio = midiRatio(targetMidi, unit.rootMidi, cents);
    if (parameters.performance) {
      const auto value = parameters.performance->at(parameters.performanceStartFrame + center);
      const bool unvoicedSource = parameters.sourceMap && parameters.sourceMap->voicedAtSource(sourceCenter) == false;
      if (value.noteId && !value.scoreFrequencyHz && !unvoicedSource) return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
          "Spectral rendering needs a voicing-aware path for accepted unvoiced pitch", unit.id);
      if (value.scoreFrequencyHz) ratio = *value.scoreFrequencyHz /
          (440.0 * std::exp2((static_cast<double>(unit.rootMidi) - 69.0) / 12.0));
    }
    if (!std::isfinite(ratio) || ratio < 0.125 || ratio > 8.0) {
      return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                         "SpectralClassic pitch ratio is unsupported",
                                         unit.id);
    }
    for (std::size_t bin = 0U; bin <= half; ++bin) {
      const auto sourceBin = static_cast<double>(bin) / ratio;
      if (sourceBin > static_cast<double>(half)) continue;
      const auto left = static_cast<std::size_t>(std::floor(sourceBin));
      const auto right = std::min(left + 1U, half);
      const auto fraction = sourceBin - static_cast<double>(left);
      const auto shiftedValue = spectrum[left] * (1.0 - fraction) +
                                spectrum[right] * fraction;
      const auto shiftedMagnitude = std::abs(shiftedValue);
      const auto interpolateEnvelope = [&](double position) {
        const auto bounded = std::clamp(position, 0.0, static_cast<double>(half));
        const auto envelopeLeft = static_cast<std::size_t>(std::floor(bounded));
        const auto envelopeRight = std::min(envelopeLeft + 1U, half);
        const auto envelopeFraction = bounded - static_cast<double>(envelopeLeft);
        return envelope[envelopeLeft] * (1.0 - envelopeFraction) +
               envelope[envelopeRight] * envelopeFraction;
      };
      const auto sourceEnvelope = interpolateEnvelope(sourceBin);
      const auto targetEnvelope = envelope[bin];
      const auto formantPreserved = shiftedMagnitude *
          std::clamp(targetEnvelope / std::max(sourceEnvelope, 1.0e-9),
                     0.5, 2.0);
      const auto magnitude = shiftedMagnitude *
          static_cast<double>(1.0F - parameters.formantFollow) +
          formantPreserved * static_cast<double>(parameters.formantFollow);
      const auto sourcePhase = std::arg(shiftedValue);
      if (firstFrame) {
        outputPhase[bin] = parameters.performance ? sourcePhase * parameters.phaseReset : sourcePhase;
      } else if (parameters.performance) {
        // Track measured off-bin frequency. Repeatedly resetting to a source
        // phase would detune the authoritative score during time stretching.
        const auto frequency = sourceFrequency[left] * (1.0 - fraction) + sourceFrequency[right] * fraction;
        outputPhase[bin] += frequency * ratio * static_cast<double>(parameters.hopSize);
      } else {
        const auto expectedAdvance = 2.0 * std::numbers::pi *
            static_cast<double>(bin) * static_cast<double>(parameters.hopSize) /
            static_cast<double>(fftSize);
        const auto continuous = outputPhase[bin] + expectedAdvance;
        outputPhase[bin] = blendPhase(
            continuous, sourcePhase, static_cast<double>(parameters.phaseReset));
      }
      shifted[bin] = std::polar(magnitude, outputPhase[bin]);
      if (bin != 0U && bin != half) shifted[fftSize - bin] = std::conj(shifted[bin]);
    }
    firstFrame = false;
    fft(shifted, true);

    for (std::size_t index = 0U; index < fftSize; ++index) {
      const auto destination = center + static_cast<time::SampleFrame>(index) -
                               static_cast<time::SampleFrame>(half);
      if (destination < preFrames || destination >= releaseOutputStart ||
          destination < 0 || destination >= outputFrames) {
        continue;
      }
      const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                         static_cast<double>(fftSize - 1U);
      const auto window = static_cast<float>(0.5 - 0.5 * std::cos(phase));
      const auto destinationIndex = static_cast<std::size_t>(destination);
      if (parameters.sourceMap && (parameters.sourceMap->voicedAtSource(sourceCenter) == false ||
          parameters.sourceMap->voicedAtSource(parameters.sourceMap->sourceAt(
              static_cast<double>(parameters.performanceStartFrame + destination))) == false)) continue;
      overlap[destinationIndex] += static_cast<float>(shifted[index].real()) * window;
      weights[destinationIndex] += window * window;
    }
  }

  for (time::SampleFrame frame = preFrames; frame < releaseOutputStart; ++frame) {
    const auto index = static_cast<std::size_t>(frame);
    if (weights[index] > 1.0e-7F) {
      result.samples[index] = overlap[index] / weights[index];
    } else {
      if (parameters.sourceMap) {
        result.samples[index] = interpolate(mono, std::min(static_cast<double>(audioEnd - 1),
            parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + frame))));
        continue;
      }
      const auto sourcePosition = static_cast<double>(loopStart) +
          static_cast<double>(frame - preFrames) * sourcePerOutput;
      result.samples[index] = loopInterpolate(
          mono, sourcePosition, static_cast<double>(loopStart),
          static_cast<double>(loopEnd));
    }
  }

  finishUnit(result, unit.gainDb + parameters.additionalGainDb, outputSampleRate);
  const auto formants = shiftFormants(result, parameters, formantPlan.value(), outputSampleRate, overlap, weights, stopToken);
  if (!formants) return core::Result<RenderedUnit>{formants.error()};
  if (parameters.performance) {
    const auto gain = applyCompiledPerformanceGain(result.samples, *parameters.performance, parameters.performanceStartFrame, stopToken);
    if (!gain) return core::Result<RenderedUnit>{gain.error()};
  }
  if (formants.value() && std::any_of(result.samples.begin(), result.samples.end(),
      [](float value) { return !std::isfinite(value) || std::abs(value) > 1.0F; }))
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
        "Formant output exceeds unit headroom; reduce source or unit gain", unit.id);
  return result;
}

}  // namespace seam::synthesis
