#pragma once
#include "seam/synthesis/phrase_backend.hpp"
#include "seam/voice_design/frication_config.hpp"

namespace seam::voice_design {
// Aperiodic band-shaped excitation only. No consonant label, timing plan,
// recipe persistence or intelligibility claim follows from this primitive.
class FricationSource final {
public:
  [[nodiscard]] static core::Result<FricationSource> create(
      FricationConfig config, std::uint32_t sampleRate, time::SampleFrame origin);
  [[nodiscard]] core::Result<synthesis::PhraseAudio> render(std::size_t frames, std::stop_token stopToken = {});
  void reset() noexcept { position_ = origin_; z1_ = 0.0; z2_ = 0.0; }
  [[nodiscard]] time::SampleFrame position() const noexcept { return position_; }
  static constexpr std::uint32_t algorithmRevision = 1U;
private:
  FricationSource() = default;
  FricationConfig config_;
  time::SampleFrame origin_{0}, position_{0};
  double b0_{0.0}, b2_{0.0}, a1_{0.0}, a2_{0.0}, z1_{0.0}, z2_{0.0};
};
}
