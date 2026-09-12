#include "seam/voice_design/vocal_tract.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace seam::voice_design {
core::Result<VocalTract> VocalTract::create(const VoiceRecipe& recipe,
    std::string_view phone, std::string_view style, std::uint32_t sampleRate) {
  const auto valid = recipe.validate();
  if (!valid) return core::Result<VocalTract>{valid.error()};
  if (sampleRate < 8000U || sampleRate > 384000U) return core::failure<VocalTract>(
      core::ErrorCode::InvalidArgument, "Vocal tract sample rate is unsupported");
  const auto pose = std::find_if(recipe.poses.begin(), recipe.poses.end(),
      [&](const auto& value) { return value.phone == phone && value.style == style; });
  if (pose == recipe.poses.end()) return core::failure<VocalTract>(core::ErrorCode::NotFound, "Vocal tract pose is missing");
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
    // RBJ constant-peak BPF plus notch (W3C Audio EQ Cookbook). This is
    // a bounded pole/zero coloration model, not a physical airway simulation.
    const auto make = [&](double frequency,double bandwidth,bool notch) {
      const auto omega = 2.0*std::numbers::pi*frequency/sampleRate;
      const auto alpha = std::sin(omega)/(2.0*(frequency/bandwidth));
      const auto a0 = 1.0+alpha, a1 = -2.0*std::cos(omega)/a0, a2 = (1.0-alpha)/a0;
      return Biquad{notch?1.0/a0:alpha/a0,notch?a1:0.0,notch?1.0/a0:-alpha/a0,a1,a2};
    };
    result.nasal_ = NasalState{make(model.resonanceHz,model.resonanceBandwidthHz,false),
        make(model.antiresonanceHz,model.antiresonanceBandwidthHz,true)};
    for (const auto* filter : {&result.nasal_->resonance,&result.nasal_->antiresonance})
      if (!(1.0+filter->a1+filter->a2>0.0 && 1.0-filter->a1+filter->a2>0.0 && 1.0-filter->a2>0.0))
        return core::failure<VocalTract>(core::ErrorCode::InvalidArgument,"Nasal coefficients are not strictly stable");
  }
  double totalWeight = 0.0;
  for (const auto& formant : pose->formants) {
    if (formant.frequencyHz >= static_cast<double>(sampleRate) * 0.5) return core::failure<VocalTract>(
        core::ErrorCode::InvalidArgument, "Vocal tract resonance reaches or exceeds Nyquist");
    // W3C Audio EQ Cookbook, RBJ constant-0-dB-peak BPF. bandwidthHz
    // parameterizes nominal Q=f/B; measured digital bandwidth is separate.
    const auto omega = 2.0 * std::numbers::pi * formant.frequencyHz / sampleRate;
    const auto q = formant.frequencyHz / formant.bandwidthHz;
    const auto alpha = std::sin(omega) / (2.0 * q);
    const auto a0 = 1.0 + alpha;
    const auto a1 = -2.0 * std::cos(omega) / a0;
    const auto a2 = (1.0 - alpha) / a0;
    if (!(1.0 + a1 + a2 > 0.0 && 1.0 - a1 + a2 > 0.0 && 1.0 - a2 > 0.0)) {
      return core::failure<VocalTract>(core::ErrorCode::InvalidArgument, "Vocal tract coefficients are not strictly stable");
    }
    const auto weight = std::pow(10.0, formant.gainDb / 20.0);
    totalWeight += weight;
    result.bands_.push_back({alpha / a0, -alpha / a0, a1, a2, weight});
  }
  for (auto& band : result.bands_) band.weight /= totalWeight;
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
    if (remaining != 0U) {
      const auto targetSum = evaluate(target,targetNasal,targetCoupling_,targetNasalOnly_);
      auto weight = static_cast<double>(transitionFrames_ - remaining + 1U) / static_cast<double>(transitionFrames_);
      weight = weight * weight * (3.0 - 2.0 * weight);
      sum = sum * (1.0 - weight) + targetSum * weight;
      --remaining;
      if (remaining == 0U) {
        next = std::move(target); target.clear(); nasal=std::move(targetNasal); targetNasal.reset(); coupling=targetCoupling_;
        nasalOnly=targetNasalOnly_;
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
  return output;
}
void VocalTract::reset() noexcept {
  for (auto& band : bands_) { band.z1 = 0.0; band.z2 = 0.0; }
  if (nasal_) {
    nasal_->resonance.z1=nasal_->resonance.z2=0.0;
    nasal_->antiresonance.z1=nasal_->antiresonance.z2=0.0;
  }
  targetNasal_.reset(); targetCoupling_=0.0;
  targetBands_.clear(); transitionFrames_ = 0U; remaining_ = 0U;
}
}
