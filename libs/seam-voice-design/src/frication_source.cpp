#include "seam/voice_design/frication_source.hpp"
#include "excitation_noise.hpp"
#include <cmath>
#include <numbers>

namespace seam::voice_design {
core::Result<FricationSource> FricationSource::create(
    FricationConfig config, std::uint32_t sampleRate, time::SampleFrame origin) {
  if (sampleRate < 8000U || sampleRate > 384000U || origin < 0 || origin > (time::SampleFrame{1} << 52) ||
      !std::isfinite(config.centerHz) || !std::isfinite(config.bandwidthHz) || !std::isfinite(config.gain) ||
      config.centerHz < 80.0 || config.centerHz >= 0.45 * sampleRate || config.bandwidthHz < 20.0 ||
      config.bandwidthHz > 0.5 * sampleRate || config.gain < 0.0 || config.gain > 0.25)
    return core::failure<FricationSource>(core::ErrorCode::InvalidArgument, "Frication source configuration is outside bounds");
  const auto q = config.centerHz / config.bandwidthHz;
  if (q < 0.25 || q > 20.0) return core::failure<FricationSource>(core::ErrorCode::InvalidArgument, "Frication resonance Q is outside bounds");
  const auto omega = 2.0 * std::numbers::pi * config.centerHz / sampleRate;
  const auto alpha = std::sin(omega) / (2.0 * q);
  FricationSource result;
  result.config_ = config;
  result.origin_ = origin; result.position_ = origin;
  result.b0_ = alpha / (1.0 + alpha); result.b2_ = -result.b0_;
  result.a1_ = -2.0 * std::cos(omega) / (1.0 + alpha);
  result.a2_ = (1.0 - alpha) / (1.0 + alpha);
  return result;
}
core::Result<synthesis::PhraseAudio> FricationSource::render(std::size_t frames, std::stop_token stopToken) {
  using Output = synthesis::PhraseAudio;
  if (frames == 0U || frames > 32U * 1024U * 1024U || position_ > (time::SampleFrame{1} << 52) - static_cast<time::SampleFrame>(frames))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Frication block exceeds bounds");
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Frication rendering cancelled");
  Output output{position_, std::vector<float>(frames)};
  auto z1 = z1_, z2 = z2_;
  for (std::size_t index = 0U; index < frames; ++index) {
    if (index % 256U == 0U && stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Frication rendering cancelled");
    const auto input = internal::excitationNoiseAt(config_.seed ^ 0x465249434154494fULL, position_ + static_cast<time::SampleFrame>(index));
    const auto value = b0_ * input + z1;
    z1 = -a1_ * value + z2;
    z2 = b2_ * input - a2_ * value;
    const auto sample = value * config_.gain;
    if (!std::isfinite(sample) || !std::isfinite(z1) || !std::isfinite(z2) || std::abs(sample) > 1.0)
      return core::failure<Output>(core::ErrorCode::InvariantViolation, "Frication output exceeds normalized excitation bounds");
    output.samples[index] = static_cast<float>(sample);
  }
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Frication rendering cancelled");
  z1_ = z1; z2_ = z2; position_ += static_cast<time::SampleFrame>(frames);
  return output;
}
}
