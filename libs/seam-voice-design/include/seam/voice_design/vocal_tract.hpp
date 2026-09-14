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
  // Moves this tract's own poles from the pose it currently holds to another over the window: one
  // filter runs the whole time and its resonance frequencies, bandwidths and weights are
  // re-designed each frame, so the transition is a formant movement rather than a change of filter.
  // The filter state is carried across the movement, which is what makes the result a continuous
  // sound instead of two sounds spliced together. A pose with no nasal model ramps its coupling in
  // or out instead. Nothing is invented for the starting side: it is whatever this tract holds.
  // A pending transition must finish or be reset before another is scheduled.
  [[nodiscard]] core::Result<void> interpolateTo(const VoiceRecipe& recipe, std::string_view phone,
      std::string_view style, std::size_t frames);
  // Moves every resonance this tract holds -- the pose in force, any pose it is moving to, and either
  // side of a movement already under way -- by the given semitone shift. The filters are re-designed
  // from their own retained parameters and the filter state is carried across, so a shift is a
  // trajectory rather than a splice, and zero is exactly the pose as authored. The nasal stage moves
  // with the oral one. A shift that would put a resonance at or past Nyquist is refused rather than
  // clamped, because a clamp would silently sing a different vowel than the one that was asked for.
  [[nodiscard]] core::Result<void> setFormantShift(double semitones);
  [[nodiscard]] double formantShiftSemitones() const noexcept { return shiftSemitones_; }
  [[nodiscard]] std::size_t transitionFramesRemaining() const noexcept { return remaining_; }
  [[nodiscard]] bool interpolating() const noexcept { return mode_ == Mode::FormantInterpolation; }
private:
  VocalTract() = default;
  enum class Mode { Idle, BankCrossfade, FormantInterpolation };
  struct Band final {
    double b0{0.0}, b2{0.0}, a1{0.0}, a2{0.0}, weight{0.0}, z1{0.0}, z2{0.0};
    // The pose this band was designed from, kept so a transition can move the filter itself.
    double frequencyHz{0.0}, bandwidthHz{0.0}, gain{1.0};
  };
  struct Biquad final { double b0,b1,b2,a1,a2,z1{0.0},z2{0.0}; };
  // The RBJ designs both the pose banks and a moving transition use, so the two cannot drift apart.
  [[nodiscard]] static Biquad designBandPass(double frequencyHz, double bandwidthHz, double sampleRate) noexcept;
  [[nodiscard]] static Biquad designNotch(double frequencyHz, double bandwidthHz, double sampleRate) noexcept;
  struct NasalState final {
    Biquad resonance, antiresonance;
    double resonanceHz{0.0}, resonanceBandwidthHz{0.0}, antiresonanceHz{0.0}, antiresonanceBandwidthHz{0.0};
  };
  // Scales one bank (and its nasal stage, when it has one) in place. Callers scale copies first, so a
  // refusal leaves the tract exactly as it was.
  [[nodiscard]] static core::Result<void> scaleBanks(std::vector<Band>& bands,
      std::optional<NasalState>& nasal, double ratio, std::uint32_t sampleRate);
  std::vector<Band> bands_;
  std::vector<Band> targetBands_;
  // The pose this tract held when the window opened, kept so the movement goes from there to the
  // target once rather than compounding frame by frame.
  std::vector<Band> sourceBands_;
  std::optional<NasalState> nasal_, targetNasal_;
  std::optional<NasalState> sourceNasal_;
  double coupling_{0.0}, targetCoupling_{0.0};
  double sourceCoupling_{0.0};
  bool nasalOnly_{false}, targetNasalOnly_{false};
  std::uint32_t sampleRate_{0U};
  std::size_t transitionFrames_{0U}, remaining_{0U};
  Mode mode_{Mode::Idle};
  double shiftSemitones_{0.0};
};
}
