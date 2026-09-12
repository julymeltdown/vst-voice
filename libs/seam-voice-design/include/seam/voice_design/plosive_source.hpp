#pragma once
#include "seam/voice_design/frication_source.hpp"
#include <span>

namespace seam::voice_design {
struct PlosiveConfig final {
  FricationConfig burst;
  std::uint32_t closureFrames{0U}, burstFrames{0U};
  friend bool operator==(const PlosiveConfig&,const PlosiveConfig&)=default;
};

// Explicit closure followed by a finite noise burst. No phoneme label, recipe
// binding or pronunciation approval is inferred from this source primitive.
class PlosiveSource final {
public:
  [[nodiscard]] static core::Result<PlosiveSource> create(PlosiveConfig config,
      std::uint32_t sampleRate,time::SampleFrame origin);
  [[nodiscard]] core::Result<synthesis::PhraseAudio> render(std::size_t frames,std::stop_token stop={});
  void reset() noexcept { position_=origin_; burst_.reset(); }
  [[nodiscard]] time::SampleFrame position() const noexcept { return position_; }
  [[nodiscard]] time::SampleFrame burstStart() const noexcept { return origin_+config_.closureFrames; }
  [[nodiscard]] time::SampleFrame end() const noexcept { return burstStart()+config_.burstFrames; }
  static constexpr std::uint32_t algorithmRevision=1U;
private:
  PlosiveSource(PlosiveConfig config,std::uint32_t sampleRate,time::SampleFrame origin,FricationSource source)
      :config_(config),sampleRate_(sampleRate),origin_(origin),position_(origin),burst_(std::move(source)) {}
  PlosiveConfig config_;
  std::uint32_t sampleRate_;
  time::SampleFrame origin_,position_;
  FricationSource burst_;
};

// Experimental source primitive, not an admitted phoneme/recipe model. The
// caller supplies score-derived excitation; this source never invents pitch.
struct VoicedPlosiveConfig final {
  PlosiveConfig release;
  double closureVoicingGain{0.15};
  double closureLowpassHz{500.0};
};
class VoicedPlosiveSource final {
public:
  [[nodiscard]] static core::Result<VoicedPlosiveSource> create(
      VoicedPlosiveConfig config, std::uint32_t sampleRate, time::SampleFrame origin);
  [[nodiscard]] core::Result<synthesis::PhraseAudio> render(
      std::span<const float> excitation, std::stop_token stop = {});
  void reset() noexcept { release_.reset(); lowpassState_ = 0.0; }
  [[nodiscard]] time::SampleFrame position() const noexcept { return release_.position(); }
  static constexpr std::uint32_t algorithmRevision = 1U;
private:
  VoicedPlosiveSource(VoicedPlosiveConfig config, PlosiveSource release,
      time::SampleFrame origin, double coefficient, std::uint32_t ramp)
      : config_(config), release_(std::move(release)), origin_(origin),
        coefficient_(coefficient), ramp_(ramp) {}
  VoicedPlosiveConfig config_;
  PlosiveSource release_;
  time::SampleFrame origin_;
  double coefficient_, lowpassState_{0.0};
  std::uint32_t ramp_;
};
}
