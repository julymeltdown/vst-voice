#pragma once
#include "seam/voice_design/voice_recipe.hpp"
#include <span>
#include <stop_token>

namespace seam::voice_design {
// Stateful linear oral/nasal-resonance stage, not a complete voice renderer.
// Input is finite normalized excitation; gainDb is relative band weighting.
class VocalTract final {
public:
  [[nodiscard]] static core::Result<VocalTract> create(const VoiceRecipe& recipe,
      std::string_view phone, std::string_view style, std::uint32_t sampleRate);
  [[nodiscard]] core::Result<std::vector<float>> process(std::span<const float> input,
      std::stop_token stopToken = {});
  void reset() noexcept;
  // Crossfades two independently stable banks; does not interpolate poles.
  // A pending transition must finish or be reset before another is scheduled.
  [[nodiscard]] core::Result<void> transitionTo(const VoiceRecipe& recipe,
      std::string_view phone, std::string_view style, std::size_t frames);
  [[nodiscard]] std::size_t transitionFramesRemaining() const noexcept { return remaining_; }
private:
  VocalTract() = default;
  struct Band final { double b0, b2, a1, a2, weight, z1{0.0}, z2{0.0}; };
  struct Biquad final { double b0,b1,b2,a1,a2,z1{0.0},z2{0.0}; };
  struct NasalState final { Biquad resonance,antiresonance; };
  std::vector<Band> bands_;
  std::vector<Band> targetBands_;
  std::optional<NasalState> nasal_, targetNasal_;
  double coupling_{0.0}, targetCoupling_{0.0};
  bool nasalOnly_{false}, targetNasalOnly_{false};
  std::uint32_t sampleRate_{0U};
  std::size_t transitionFrames_{0U}, remaining_{0U};
};
}
