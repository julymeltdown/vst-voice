#include "seam/voice_design/vocal_tract.hpp"
#include "seam/domain/formant_automation.hpp"
#include "seam/domain/gender_automation.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace seam::voice_design {

FormantControlSpan nextFormantControlSpan(const synthesis::CompiledScorePerformance& performance,
    time::SampleFrame origin, std::size_t maximumFrames) noexcept {
  if (maximumFrames == 0U) return {};
  const auto shiftAt = [&](time::SampleFrame frame) {
    const auto value = performance.at(frame);
    return static_cast<double>(value.formantSemitones) +
        static_cast<double>(std::clamp(value.gender, -1.0F, 1.0F)) *
            static_cast<double>(domain::kGenderFormantSemitones);
  };
  FormantControlSpan result{1U, shiftAt(origin)};
  while (result.frames < maximumFrames &&
      shiftAt(origin + static_cast<time::SampleFrame>(result.frames)) == result.semitones) ++result.frames;
  return result;
}

core::Result<void> VocalTract::scaleBanks(std::vector<Band>& bands,
    std::optional<NasalState>& nasal, double ratio, std::uint32_t sampleRate) {
  const auto nyquist = static_cast<double>(sampleRate) * 0.5;
  for (auto& band : bands) {
    const auto frequency = band.frequencyHz * ratio;
    const auto bandwidth = band.bandwidthHz * ratio;
    if (!(frequency > 0.0) || frequency >= nyquist || !(bandwidth > 0.0))
      return core::failure(core::ErrorCode::Unsupported,
                           "A formant shift would put a vocal tract resonance at or past Nyquist");
    const auto designed = designBandPass(frequency, bandwidth, static_cast<double>(sampleRate));
    if (!(1.0 + designed.a1 + designed.a2 > 0.0 && 1.0 - designed.a1 + designed.a2 > 0.0 &&
          1.0 - designed.a2 > 0.0))
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Shifted vocal tract coefficients are not strictly stable");
    band.frequencyHz = frequency;
    band.bandwidthHz = bandwidth;
    band.b0 = designed.b0;
    band.b2 = designed.b2;
    band.a1 = designed.a1;
    band.a2 = designed.a2;
  }
  if (nasal.has_value()) {
    const auto resonance = nasal->resonanceHz * ratio;
    const auto resonanceBandwidth = nasal->resonanceBandwidthHz * ratio;
    const auto antiresonance = nasal->antiresonanceHz * ratio;
    const auto antiresonanceBandwidth = nasal->antiresonanceBandwidthHz * ratio;
    if (!(resonance > 0.0) || resonance >= nyquist || !(antiresonance > 0.0) ||
        antiresonance >= nyquist || !(resonanceBandwidth > 0.0) || !(antiresonanceBandwidth > 0.0))
      return core::failure(core::ErrorCode::Unsupported,
                           "A formant shift would put the nasal stage at or past Nyquist");
    nasal->resonance = designBandPass(resonance, resonanceBandwidth, static_cast<double>(sampleRate));
    nasal->antiresonance = designNotch(antiresonance, antiresonanceBandwidth,
                                      static_cast<double>(sampleRate));
    nasal->resonanceHz = resonance;
    nasal->resonanceBandwidthHz = resonanceBandwidth;
    nasal->antiresonanceHz = antiresonance;
    nasal->antiresonanceBandwidthHz = antiresonanceBandwidth;
  }
  return core::success();
}

core::Result<void> VocalTract::setFormantShift(double semitones) {
  if (!std::isfinite(semitones) ||
      std::abs(semitones) > static_cast<double>(domain::kMaximumFormantShiftSemitones))
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Formant shift is outside the supported range");
  if (semitones == shiftSemitones_) return core::success();
  // The delta between the shift this tract already holds and the one requested. Scaling by the delta
  // keeps repeated calls idempotent instead of compounding the pose away from the one that was
  // authored.
  const auto ratio = std::pow(2.0, (semitones - shiftSemitones_) / 12.0);
  auto bands = bands_;
  auto targetBands = targetBands_;
  auto sourceBands = sourceBands_;
  auto nasal = nasal_;
  auto targetNasal = targetNasal_;
  auto sourceNasal = sourceNasal_;
  // Every bank is a copy, so a refusal anywhere leaves the tract exactly as it was.
  const std::array<core::Result<void>, 3> applied{
      scaleBanks(bands, nasal, ratio, sampleRate_),
      scaleBanks(targetBands, targetNasal, ratio, sampleRate_),
      scaleBanks(sourceBands, sourceNasal, ratio, sampleRate_)};
  for (const auto& result : applied)
    if (!result) return core::Result<void>{result.error()};
  bands_ = std::move(bands);
  targetBands_ = std::move(targetBands);
  sourceBands_ = std::move(sourceBands);
  nasal_ = std::move(nasal);
  targetNasal_ = std::move(targetNasal);
  sourceNasal_ = std::move(sourceNasal);
  shiftSemitones_ = semitones;
  return core::success();
}

VocalTract::Biquad VocalTract::designBandPass(double frequencyHz, double bandwidthHz, double sampleRate) noexcept {
  // W3C Audio EQ Cookbook, RBJ constant-0-dB-peak BPF. bandwidthHz parameterizes nominal Q=f/B;
  // the measured digital bandwidth is a separate question.
  const auto omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
  const auto alpha = std::sin(omega) / (2.0 * (frequencyHz / bandwidthHz));
  const auto a0 = 1.0 + alpha;
  return Biquad{alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(omega) / a0, (1.0 - alpha) / a0};
}

VocalTract::Biquad VocalTract::designNotch(double frequencyHz, double bandwidthHz, double sampleRate) noexcept {
  const auto omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
  const auto alpha = std::sin(omega) / (2.0 * (frequencyHz / bandwidthHz));
  const auto a0 = 1.0 + alpha;
  const auto a1 = -2.0 * std::cos(omega) / a0;
  return Biquad{1.0 / a0, a1, 1.0 / a0, a1, (1.0 - alpha) / a0};
}

core::Result<VocalTract> VocalTract::create(const VoiceRecipe& recipe,
    std::string_view phone, std::string_view style, std::uint32_t sampleRate) {
  const auto valid = recipe.validate();
  if (!valid) return core::Result<VocalTract>{valid.error()};
  if (sampleRate < 8000U || sampleRate > 384000U) return core::failure<VocalTract>(
      core::ErrorCode::InvalidArgument, "Vocal tract sample rate is unsupported");
  const auto pose = std::find_if(recipe.poses.begin(), recipe.poses.end(),
      [&](const auto& value) { return value.phone == phone && value.style == style; });
  if (pose == recipe.poses.end()) return core::failure<VocalTract>(core::ErrorCode::NotFound,
      "Vocal tract pose is missing for phone '" + std::string(phone) + "', style '" + std::string(style) + "' in recipe '" + recipe.id + "'");
  if (pose->nasalCoupling != 0.0 && !pose->nasal) return core::failure<VocalTract>(core::ErrorCode::Unsupported,
      "Legacy nasal coupling requires an explicit resonance and antiresonance model");
  VocalTract result;
  result.sampleRate_ = sampleRate;
  result.coupling_ = pose->nasalCoupling;
  result.nasalOnly_=phonemizer::isNasalSymbol(phone);
  if (result.nasalOnly_ && (!pose->nasal || pose->nasalCoupling<=0.0))
    return core::failure<VocalTract>(core::ErrorCode::Unsupported,"Nasal consonant pose needs an explicit active nasal tract");
  if (pose->nasal) {
    const auto& model = *pose->nasal;
    if (model.resonanceHz >= static_cast<double>(sampleRate)*0.5 || model.antiresonanceHz >= static_cast<double>(sampleRate)*0.5)
      return core::failure<VocalTract>(core::ErrorCode::InvalidArgument,"Nasal resonance or antiresonance reaches Nyquist");
    // RBJ constant-peak BPF plus notch (W3C Audio EQ Cookbook). This is a bounded pole/zero
    // coloration model, not a physical airway simulation. The design parameters are retained so a
    // transition can move the nasal stage instead of swapping it.
    result.nasal_ = NasalState{designBandPass(model.resonanceHz, model.resonanceBandwidthHz, static_cast<double>(sampleRate)),
        designNotch(model.antiresonanceHz, model.antiresonanceBandwidthHz, static_cast<double>(sampleRate)),
        model.resonanceHz, model.resonanceBandwidthHz, model.antiresonanceHz, model.antiresonanceBandwidthHz};
    for (const auto* filter : {&result.nasal_->resonance,&result.nasal_->antiresonance})
      if (!(1.0+filter->a1+filter->a2>0.0 && 1.0-filter->a1+filter->a2>0.0 && 1.0-filter->a2>0.0))
        return core::failure<VocalTract>(core::ErrorCode::InvalidArgument,"Nasal coefficients are not strictly stable");
  }
  double totalWeight = 0.0;
  for (const auto& formant : pose->formants) {
    if (formant.frequencyHz >= static_cast<double>(sampleRate) * 0.5) return core::failure<VocalTract>(
        core::ErrorCode::InvalidArgument, "Vocal tract resonance reaches or exceeds Nyquist");
    const auto designed = designBandPass(formant.frequencyHz, formant.bandwidthHz, static_cast<double>(sampleRate));
    if (!(1.0 + designed.a1 + designed.a2 > 0.0 && 1.0 - designed.a1 + designed.a2 > 0.0 && 1.0 - designed.a2 > 0.0)) {
      return core::failure<VocalTract>(core::ErrorCode::InvalidArgument, "Vocal tract coefficients are not strictly stable");
    }
    const auto weight = std::pow(10.0, formant.gainDb / 20.0);
    totalWeight += weight;
    result.bands_.push_back({designed.b0, designed.b2, designed.a1, designed.a2, weight, 0.0, 0.0,
        formant.frequencyHz, formant.bandwidthHz, weight});
  }
  for (auto& band : result.bands_) { band.gain /= totalWeight; band.weight = band.gain; }
  return result;
}

core::Result<void> VocalTract::transitionTo(const VoiceRecipe& recipe,
    std::string_view phone, std::string_view style, std::size_t frames) {
  if (remaining_ != 0U) return core::failure(core::ErrorCode::Conflict, "A vocal tract transition is already active");
  if (frames == 0U || frames > static_cast<std::size_t>(sampleRate_) * 2U) return core::failure(
      core::ErrorCode::InvalidArgument, "Vocal tract transition duration exceeds bounds");
  auto target = create(recipe, phone, style, sampleRate_);
  if (!target) return core::Result<void>{target.error()};
  targetBands_ = std::move(target.value().bands_);
  targetNasal_ = std::move(target.value().nasal_); targetCoupling_ = target.value().coupling_;
  targetNasalOnly_=target.value().nasalOnly_;
  // A target pose is designed from the recipe, which is unshifted, so it has to take the shift this
  // tract is holding or the crossfade would slide back to the unshifted tract.
  if (shiftSemitones_ != 0.0) {
    const auto applied = scaleBanks(targetBands_, targetNasal_,
        std::pow(2.0, shiftSemitones_ / 12.0), sampleRate_);
    if (!applied) return core::Result<void>{applied.error()};
  }
  sourceBands_.clear(); sourceNasal_.reset(); sourceCoupling_ = 0.0;
  mode_ = Mode::BankCrossfade;
  transitionFrames_ = frames; remaining_ = frames;
  return core::success();
}

core::Result<void> VocalTract::interpolateTo(const VoiceRecipe& recipe, std::string_view phone,
    std::string_view style, std::size_t frames) {
  if (remaining_ != 0U) return core::failure(core::ErrorCode::Conflict, "A vocal tract transition is already active");
  if (frames == 0U || frames > static_cast<std::size_t>(sampleRate_) * 2U) return core::failure(
      core::ErrorCode::InvalidArgument, "Vocal tract transition duration exceeds bounds");
  auto target = create(recipe, phone, style, sampleRate_);
  if (!target) return core::Result<void>{target.error()};
  if (target.value().bands_.size() != bands_.size())
    return core::failure(core::ErrorCode::Unsupported, "A formant transition needs the same resonance count on both sides");
  if (target.value().nasalOnly_ || nasalOnly_) return core::failure(core::ErrorCode::Unsupported,
      "A nasal-only tract changes topology; it is crossfaded rather than moved");
  // The window ends at the target pose and starts wherever this tract already is; only the filter
  // state and the pose it holds survive from before the window, which keeps the movement
  // continuous rather than a splice. Nothing has to be invented for the starting side.
  sourceBands_ = bands_;
  sourceNasal_ = nasal_;
  sourceCoupling_ = coupling_;
  targetBands_ = std::move(target.value().bands_);
  targetNasal_ = std::move(target.value().nasal_);
  targetCoupling_ = target.value().coupling_;
  targetNasalOnly_ = false;
  if (shiftSemitones_ != 0.0) {
    const auto applied = scaleBanks(targetBands_, targetNasal_,
        std::pow(2.0, shiftSemitones_ / 12.0), sampleRate_);
    if (!applied) return core::Result<void>{applied.error()};
  }
  // A nasal model that only the target declares is ramped in by its coupling, which starts at the
  // source value; its own parameters are the ones it will hold once the window is over.
  if (!nasal_ && targetNasal_) nasal_ = targetNasal_;
  if (nasal_ && !targetNasal_) targetCoupling_ = 0.0;
  mode_ = Mode::FormantInterpolation;
  transitionFrames_ = frames; remaining_ = frames;
  return core::success();
}

core::Result<std::vector<float>> VocalTract::process(std::span<const float> input, std::stop_token stopToken) {
  using Output = std::vector<float>;
  if (bands_.empty() || input.empty() || input.size() > 32U * 1024U * 1024U) return core::failure<Output>(
      core::ErrorCode::InvalidArgument, "Vocal tract input exceeds bounds");
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Vocal tract processing cancelled");
    if (!std::isfinite(input[i]) || std::abs(input[i]) > 1.0F) return core::failure<Output>(
        core::ErrorCode::InvalidArgument, "Vocal tract requires finite normalized excitation");
  }
  auto next = bands_;
  auto target = targetBands_;
  auto nasal = nasal_, targetNasal = targetNasal_;
  auto coupling = coupling_;
  auto nasalOnly=nasalOnly_;
  auto remaining = remaining_;
  auto mode = mode_;
  Output output(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Vocal tract processing cancelled");
    bool validState = true;
    const auto evaluate = [&](auto& bank,auto& nasalBank,double nasalCoupling,bool closedOralPath) {
      double valueSum = 0.0;
      for (auto& band : bank) {
        const auto value = band.b0 * input[i] + band.z1;
        band.z1 = -band.a1 * value + band.z2;
        band.z2 = band.b2 * input[i] - band.a2 * value;
        validState = validState && std::isfinite(band.z1) && std::isfinite(band.z2);
        valueSum += band.weight * value;
      }
      if (nasalBank && nasalCoupling != 0.0) {
        const auto filter = [&](auto& stage,double sample) {
          const auto value=stage.b0*sample+stage.z1;
          stage.z1=stage.b1*sample-stage.a1*value+stage.z2;
          stage.z2=stage.b2*sample-stage.a2*value;
          validState=validState && std::isfinite(value) && std::isfinite(stage.z1) && std::isfinite(stage.z2);
          return value;
        };
        const auto resonance=filter(nasalBank->resonance,input[i]);
        const auto nasalValue=filter(nasalBank->antiresonance,closedOralPath?resonance:0.5*valueSum+0.5*resonance);
        valueSum=closedOralPath?nasalCoupling*nasalValue:(1.0-nasalCoupling)*valueSum+nasalCoupling*nasalValue;
      }
      validState = validState && std::isfinite(valueSum) && std::abs(valueSum) <= 8.0;
      return valueSum;
    };
    double sum = evaluate(next,nasal,coupling,nasalOnly);
    if (remaining != 0U && mode == Mode::FormantInterpolation) {
      // Move this filter's own poles. The filter state carries across the window, so the sound is
      // continuous and the movement is a formant trajectory rather than two sounds spliced together.
      const auto progress = static_cast<double>(transitionFrames_ - remaining + 1U) / static_cast<double>(transitionFrames_);
      const auto shaped = progress * progress * (3.0 - 2.0 * progress);
      double totalGain = 0.0;
      for (std::size_t band = 0U; band < next.size(); ++band) {
        const auto& from = sourceBands_[band];
        const auto& to = target[band];
        const auto frequency = from.frequencyHz + (to.frequencyHz - from.frequencyHz) * shaped;
        const auto bandwidth = from.bandwidthHz + (to.bandwidthHz - from.bandwidthHz) * shaped;
        const auto gain = from.gain + (to.gain - from.gain) * shaped;
        const auto designed = designBandPass(frequency, bandwidth, static_cast<double>(sampleRate_));
        next[band].b0 = designed.b0; next[band].b2 = designed.b2;
        next[band].a1 = designed.a1; next[band].a2 = designed.a2;
        next[band].frequencyHz = frequency; next[band].bandwidthHz = bandwidth; next[band].gain = gain;
        totalGain += gain;
      }
      if (totalGain > 0.0) for (auto& band : next) band.weight = band.gain / totalGain;
      if (nasal && targetNasal && sourceNasal_) {
        const auto& from = *sourceNasal_;
        auto& running = *nasal;
        running.resonanceHz = from.resonanceHz + (targetNasal->resonanceHz - from.resonanceHz) * shaped;
        running.resonanceBandwidthHz = from.resonanceBandwidthHz +
            (targetNasal->resonanceBandwidthHz - from.resonanceBandwidthHz) * shaped;
        running.antiresonanceHz = from.antiresonanceHz + (targetNasal->antiresonanceHz - from.antiresonanceHz) * shaped;
        running.antiresonanceBandwidthHz = from.antiresonanceBandwidthHz +
            (targetNasal->antiresonanceBandwidthHz - from.antiresonanceBandwidthHz) * shaped;
        const auto resonance = designBandPass(running.resonanceHz, running.resonanceBandwidthHz, static_cast<double>(sampleRate_));
        const auto antiresonance = designNotch(running.antiresonanceHz, running.antiresonanceBandwidthHz, static_cast<double>(sampleRate_));
        running.resonance.b0 = resonance.b0; running.resonance.b1 = resonance.b1; running.resonance.b2 = resonance.b2;
        running.resonance.a1 = resonance.a1; running.resonance.a2 = resonance.a2;
        running.antiresonance.b0 = antiresonance.b0; running.antiresonance.b1 = antiresonance.b1;
        running.antiresonance.b2 = antiresonance.b2; running.antiresonance.a1 = antiresonance.a1;
        running.antiresonance.a2 = antiresonance.a2;
      }
      coupling = sourceCoupling_ + (targetCoupling_ - sourceCoupling_) * shaped;
      sum = evaluate(next,nasal,coupling,nasalOnly);
      --remaining;
      if (remaining == 0U) {
        // Land exactly on the pose the window declared rather than on its last interpolated frame.
        for (std::size_t band = 0U; band < next.size(); ++band) {
          next[band].b0 = target[band].b0; next[band].b2 = target[band].b2;
          next[band].a1 = target[band].a1; next[band].a2 = target[band].a2;
          next[band].weight = target[band].weight; next[band].gain = target[band].gain;
          next[band].frequencyHz = target[band].frequencyHz; next[band].bandwidthHz = target[band].bandwidthHz;
        }
        if (nasal && targetNasal) {
          nasal->resonanceHz = targetNasal->resonanceHz; nasal->resonanceBandwidthHz = targetNasal->resonanceBandwidthHz;
          nasal->antiresonanceHz = targetNasal->antiresonanceHz;
          nasal->antiresonanceBandwidthHz = targetNasal->antiresonanceBandwidthHz;
          nasal->resonance.b0 = targetNasal->resonance.b0; nasal->resonance.b1 = targetNasal->resonance.b1;
          nasal->resonance.b2 = targetNasal->resonance.b2; nasal->resonance.a1 = targetNasal->resonance.a1;
          nasal->resonance.a2 = targetNasal->resonance.a2;
          nasal->antiresonance.b0 = targetNasal->antiresonance.b0; nasal->antiresonance.b1 = targetNasal->antiresonance.b1;
          nasal->antiresonance.b2 = targetNasal->antiresonance.b2; nasal->antiresonance.a1 = targetNasal->antiresonance.a1;
          nasal->antiresonance.a2 = targetNasal->antiresonance.a2;
        }
        coupling = targetCoupling_;
        mode = Mode::Idle;
      }
    } else if (remaining != 0U) {
      const auto targetSum = evaluate(target,targetNasal,targetCoupling_,targetNasalOnly_);
      auto weight = static_cast<double>(transitionFrames_ - remaining + 1U) / static_cast<double>(transitionFrames_);
      weight = weight * weight * (3.0 - 2.0 * weight);
      sum = sum * (1.0 - weight) + targetSum * weight;
      --remaining;
      if (remaining == 0U) {
        next = std::move(target); target.clear(); nasal=std::move(targetNasal); targetNasal.reset(); coupling=targetCoupling_;
        nasalOnly=targetNasalOnly_;
        mode = Mode::Idle;
      }
    }
    if (!validState) return core::failure<Output>(core::ErrorCode::InvariantViolation, "Vocal tract bank exceeded its safety bound");
    if (!std::isfinite(sum) || std::abs(sum) > 8.0) return core::failure<Output>(
        core::ErrorCode::InvariantViolation, "Vocal tract output exceeded its safety bound");
    output[i] = static_cast<float>(sum);
  }
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Vocal tract processing cancelled");
  bands_ = std::move(next); // Failed/cancelled blocks never advance persistent state.
  targetBands_ = std::move(target); remaining_ = remaining;
  nasal_=std::move(nasal); targetNasal_=std::move(targetNasal); coupling_=coupling;
  nasalOnly_=nasalOnly;
  mode_ = mode;
  return output;
}
void VocalTract::reset() noexcept {
  for (auto& band : bands_) { band.z1 = 0.0; band.z2 = 0.0; }
  if (nasal_) {
    nasal_->resonance.z1=nasal_->resonance.z2=0.0;
    nasal_->antiresonance.z1=nasal_->antiresonance.z2=0.0;
  }
  targetNasal_.reset(); targetCoupling_=0.0;
  sourceNasal_.reset(); sourceCoupling_=0.0; sourceBands_.clear();
  targetBands_.clear(); transitionFrames_ = 0U; remaining_ = 0U;
  mode_ = Mode::Idle;
}
}
