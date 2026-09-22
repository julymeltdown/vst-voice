#include "seam/voice_design/phonation_source.hpp"
#include "excitation_noise.hpp"
#include "seam/domain/breathiness_automation.hpp"
#include "seam/domain/airiness_automation.hpp"
#include "seam/domain/growl_automation.hpp"
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
  result.appliedTiltDbPerOctave_ = 0.0;
  return result;
}

core::Result<synthesis::PhraseAudio> PhonationSource::render(std::size_t frames, std::stop_token stopToken,
    std::optional<domain::NoteId> phoneticOwner) {
  using Output = synthesis::PhraseAudio;
  if (!performance_ || frames == 0U || frames > 32U * 1024U * 1024U ||
      position_ > (time::SampleFrame{1} << 52) - static_cast<time::SampleFrame>(frames)) return core::failure<Output>(
          core::ErrorCode::InvalidArgument, "Phonation block exceeds bounds");
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Phonation rendering cancelled");
  Output output{position_, std::vector<float>(frames)};
  auto phase = phase_; auto subPhase = subPhase_; auto noise = noise_; auto lastNote = lastNote_;
  const auto rate = static_cast<double>(performance_->sampleRate());
  const auto noisePole = std::exp(-2.0 * std::numbers::pi * std::min(6000.0, 0.3 * rate) / rate);
  const auto modulationPhase = 0.5 * (noiseAt(seed_, 0) + 1.0);
  for (std::size_t i = 0; i < frames; ++i) {
    if (i % 256U == 0U && stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Phonation rendering cancelled");
    const auto frame = position_ + static_cast<time::SampleFrame>(i);
    const auto musical = phoneticOwner ? performance_->atPhonetic(frame, *phoneticOwner) : performance_->at(frame);
    // The white sample and the filtered aspiration are kept apart on purpose: the difference between
    // them is the part of the stream the aspiration filter rejected, which is the high-frequency band
    // the airiness channel adds.
    const auto white = noiseAt(seed_, frame);
    noise = noisePole * noise + (1.0 - noisePole) * white;
    if (!musical.noteId) { lastNote.reset(); continue; }
    // Source tilt and tract shift observe the same absolute score frame. A
    // short accepted/manual span must not disappear between caller blocks.
    const auto tension = static_cast<double>(std::clamp(musical.tension, 0.0F, 1.0F));
    const auto gender = static_cast<double>(std::clamp(musical.gender, -1.0F, 1.0F));
    const auto tiltDbPerOctave = tension * static_cast<double>(domain::kTensionTiltDbPerOctave) +
                                 gender * static_cast<double>(domain::kGenderTiltDbPerOctave);
    if (tiltDbPerOctave != appliedTiltDbPerOctave_) {
      const auto tilt = tiltDbPerOctave / 6.020599913279624;
      for (std::size_t index = 0U; index < appliedHarmonics_.size(); ++index)
        appliedHarmonics_[index] = harmonics_[index] * std::pow(static_cast<double>(index + 1U), tilt);
      appliedTiltDbPerOctave_ = tiltDbPerOctave;
    }
    if (lastNote != musical.noteId && musical.reattack) { phase = 0.0; subPhase = 0.0; }
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
        // At and below its own corner the taper window is cos(0), so its value there is the constant
        // 1.0 + 1.0 rather than a cosine the loop has to evaluate. Substituting that constant keeps the
        // operands of the product below identical to the ones the cosine produced, so the sample is
        // bit-identical, and it removes one transcendental per partial per output sample for every
        // partial the taper does not touch, which is the common case for the recipe's own spectrum.
        const auto taperWindow = taperPosition == 0.0
            ? 2.0
            : 1.0 + std::cos(std::numbers::pi * taperPosition);
        const auto amplitude = appliedHarmonics_[h] * 0.5 * taperWindow;
        voiced += amplitude * std::sin(2.0 * std::numbers::pi * harmonic * phase);
        weight += std::abs(amplitude);
      }
      if (weight > 0.0) voiced /= weight;
      phase += frequency / rate; phase -= std::floor(phase);
      subPhase += 0.5 * frequency / rate; subPhase -= std::floor(subPhase);
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
    // Airiness is a band rather than a balance: it adds a small share of the high-frequency part of the
    // same stream, above the aspiration filter's corner, where the periodic source has almost no energy.
    // A frame whose airiness is exactly zero adds exactly nothing and renders by the arithmetic above.
    const auto airiness = static_cast<double>(std::clamp(musical.airiness, 0.0F, 1.0F));
    const auto air =
        airiness * static_cast<double>(domain::kAirinessNoiseShare) * (white - noise);
    // Growl modulates the periodic part using a half-rate phase accumulator driven by the note's
    // frequency. Its sidebands sit half a fundamental from each harmonic; in the measured vowel they
    // survive tract filtering much better than an added subharmonic tone. The depth bounds the gain. A
    // frame whose growl is exactly zero multiplies by exactly one.
    const auto growl = static_cast<double>(std::clamp(musical.growl, 0.0F, 1.0F));
    const auto halfRate = 0.5 * (1.0 + std::cos(2.0 * std::numbers::pi * subPhase));
    const auto roughGain =
        1.0 - growl * static_cast<double>(domain::kGrowlSubharmonicDepth) * (1.0 - halfRate);
    const auto sample = 0.25 * (periodicShare * voiced * roughGain + aperiodicShare * noise + air) *
        (1.0 + modulation_.shimmerAmount * modulation);
    if (!std::isfinite(sample) || std::abs(sample) > 0.500001) return core::failure<Output>(
        core::ErrorCode::InvariantViolation, "Phonation output exceeded its safety bound");
    output.samples[i] = static_cast<float>(sample);
  }
  phase_ = phase; subPhase_ = subPhase; noise_ = noise; lastNote_ = lastNote;
  position_ += static_cast<time::SampleFrame>(frames);
  return output;
}
void PhonationSource::reset() noexcept {
  position_ = origin_; phase_ = 0.0; subPhase_ = 0.0; noise_ = 0.0; lastNote_.reset();
}
}
