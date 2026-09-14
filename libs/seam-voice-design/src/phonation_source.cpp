#include "seam/voice_design/phonation_source.hpp"
#include "excitation_noise.hpp"
#include "seam/domain/breathiness_automation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace seam::voice_design {
namespace {
constexpr auto noiseAt = internal::excitationNoiseAt;
}
core::Result<PhonationSource> PhonationSource::create(const VoiceRecipe& recipe,
    const synthesis::CompiledScorePerformance& performance, time::SampleFrame origin) {
  const auto valid = recipe.validate();
  if (!valid) return core::Result<PhonationSource>{valid.error()};
  if (origin < 0 || origin > (time::SampleFrame{1} << 52) || performance.notes().empty() ||
      performance.sampleRate() < 8000U || performance.sampleRate() > 384000U) return core::failure<PhonationSource>(
          core::ErrorCode::InvalidArgument, "Phonation source requires bounded compiled musical context");
  PhonationSource result;
  result.performance_ = std::make_shared<const synthesis::CompiledScorePerformance>(performance);
  result.phonation_ = recipe.phonation; result.modulation_ = recipe.modulation; result.seed_ = recipe.seed;
  result.origin_ = origin; result.position_ = origin;
  for (std::size_t i = 0; i < result.harmonics_.size(); ++i) {
    const auto harmonic = static_cast<double>(i + 1U);
    result.harmonics_[i] = std::sin(std::numbers::pi * harmonic * recipe.phonation.openQuotient) *
        std::pow(harmonic, recipe.phonation.spectralTiltDbPerOctave / 6.020599913279624);
  }
  // A source with no tension curve uses the recipe's table itself, not a copy that has been through a
  // neutral tilt: tension zero has to render exactly what the source rendered before the channel existed.
  result.appliedHarmonics_ = result.harmonics_;
  result.appliedTension_ = 0.0;
  return result;
}

core::Result<synthesis::PhraseAudio> PhonationSource::render(std::size_t frames, std::stop_token stopToken) {
  using Output = synthesis::PhraseAudio;
  if (!performance_ || frames == 0U || frames > 32U * 1024U * 1024U ||
      position_ > (time::SampleFrame{1} << 52) - static_cast<time::SampleFrame>(frames)) return core::failure<Output>(
          core::ErrorCode::InvalidArgument, "Phonation block exceeds bounds");
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Phonation rendering cancelled");
  Output output{position_, std::vector<float>(frames)};
  auto phase = phase_; auto noise = noise_; auto lastNote = lastNote_;
  const auto rate = static_cast<double>(performance_->sampleRate());
  const auto noisePole = std::exp(-2.0 * std::numbers::pi * std::min(6000.0, 0.3 * rate) / rate);
  const auto modulationPhase = 0.5 * (noiseAt(seed_, 0) + 1.0);
  // Tension is the source's own spectrum, so it is applied to the harmonic table the source generates
  // for itself rather than to the phrase's level. One tilt per processing block is enough, exactly as the
  // tract's formant shift is one shift per block, and the tilt for a tension of exactly zero is a factor
  // of exactly one, which leaves the table the recipe declared untouched.
  const auto tension = static_cast<double>(
      std::clamp(performance_->at(position_).tension, 0.0F, 1.0F));
  if (tension != appliedTension_) {
    const auto tilt = tension * static_cast<double>(domain::kTensionTiltDbPerOctave) / 6.020599913279624;
    for (std::size_t index = 0U; index < appliedHarmonics_.size(); ++index) {
      appliedHarmonics_[index] =
          harmonics_[index] * std::pow(static_cast<double>(index + 1U), tilt);
    }
    appliedTension_ = tension;
  }
  for (std::size_t i = 0; i < frames; ++i) {
    if (i % 256U == 0U && stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Phonation rendering cancelled");
    const auto frame = position_ + static_cast<time::SampleFrame>(i);
    const auto musical = performance_->at(frame);
    noise = noisePole * noise + (1.0 - noisePole) * noiseAt(seed_, frame);
    if (!musical.noteId) { lastNote.reset(); continue; }
    if (lastNote != musical.noteId && musical.reattack) phase = 0.0;
    lastNote = musical.noteId;
    const auto modulation = modulation_.rateHz == 0.0 ? 0.0 : std::sin(2.0 * std::numbers::pi *
        std::fmod(static_cast<double>(frame) / rate * modulation_.rateHz + modulationPhase, 1.0));
    double voiced = 0.0, weight = 0.0;
    if (musical.scoreFrequencyHz) {
      const auto frequency = *musical.scoreFrequencyHz * std::exp2(modulation_.jitterCents * modulation / 1200.0);
      if (!std::isfinite(frequency) || frequency < 20.0 || frequency >= 0.4 * rate) return core::failure<Output>(
          core::ErrorCode::Unsupported, "Compiled phonation frequency exceeds the source range");
      const auto count = static_cast<std::size_t>(std::min(256.0, std::floor(0.45 * rate / frequency)));
      for (std::size_t h = 0; h < count; ++h) {
        const auto harmonic = static_cast<double>(h + 1U);
        const auto taperPosition = std::clamp((harmonic * frequency - 0.35 * rate) / (0.1 * rate), 0.0, 1.0);
        const auto amplitude =
            appliedHarmonics_[h] * 0.5 * (1.0 + std::cos(std::numbers::pi * taperPosition));
        voiced += amplitude * std::sin(2.0 * std::numbers::pi * harmonic * phase);
        weight += std::abs(amplitude);
      }
      if (weight > 0.0) voiced /= weight;
      phase += frequency / rate; phase -= std::floor(phase);
    }
    // Breathiness is a balance, not an addition: it moves share from the periodic part of the
    // excitation to its aperiodic part, so a breathy phrase is not a louder phrase, and the aperiodic
    // share stops at the channel's own bound so that the source stays voiced rather than becoming a
    // whisper. A frame whose breathiness is exactly zero leaves the arithmetic below identical to the
    // source that had no breathiness channel at all.
    const auto breathiness = static_cast<double>(std::clamp(musical.breathiness, 0.0F, 1.0F));
    const auto periodicShare =
        (1.0 - phonation_.aspiration) *
        (1.0 - static_cast<double>(domain::kBreathinessAperiodicShare) * breathiness);
    const auto aperiodicShare =
        phonation_.aspiration + (1.0 - phonation_.aspiration) *
                                    static_cast<double>(domain::kBreathinessAperiodicShare) * breathiness;
    const auto sample = 0.25 * (periodicShare * voiced + aperiodicShare * noise) *
        (1.0 + modulation_.shimmerAmount * modulation);
    if (!std::isfinite(sample) || std::abs(sample) > 0.500001) return core::failure<Output>(
        core::ErrorCode::InvariantViolation, "Phonation output exceeded its safety bound");
    output.samples[i] = static_cast<float>(sample);
  }
  phase_ = phase; noise_ = noise; lastNote_ = lastNote; position_ += static_cast<time::SampleFrame>(frames);
  return output;
}
void PhonationSource::reset() noexcept { position_ = origin_; phase_ = 0.0; noise_ = 0.0; lastNote_.reset(); }
}
