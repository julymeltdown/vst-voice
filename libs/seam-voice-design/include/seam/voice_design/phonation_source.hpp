#pragma once
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/synthesis/phrase_backend.hpp"
#include <memory>

namespace seam::voice_design {
// Sequential excitation from a fixed context origin. Arbitrary seeks require
// replay from that origin; phase is never restarted at a processing-block edge.
class PhonationSource final {
public:
  [[nodiscard]] static core::Result<PhonationSource> create(const VoiceRecipe& recipe,
      const synthesis::CompiledScorePerformance& performance, time::SampleFrame origin);
  [[nodiscard]] core::Result<synthesis::PhraseAudio> render(std::size_t frames, std::stop_token stopToken = {});
  void reset() noexcept;
  [[nodiscard]] time::SampleFrame position() const noexcept { return position_; }
private:
  PhonationSource() = default;
  std::shared_ptr<const synthesis::CompiledScorePerformance> performance_;
  Phonation phonation_;
  Modulation modulation_;
  std::uint64_t seed_{0U};
  std::array<double, 256U> harmonics_{};
  // The harmonic table actually used, which is the recipe's own table tilted by the tension in force.
  // Retained until the absolute-frame compiled tilt changes, independent of
  // caller block size. Frames with the same tilt reuse the table.
  std::array<double, 256U> appliedHarmonics_{};
  // The key is the combined tilt in force, not one channel's value: tension and gender both change the
  // source's spectrum, and the table depends only on their sum. Zero means the recipe's own table.
  double appliedTiltDbPerOctave_{0.0};
  time::SampleFrame origin_{0}, position_{0};
  double phase_{0.0}, noise_{0.0};
  // The growl modulation runs at half the note's rate. It is a second accumulator rather than a function
  // of the note's phase, because a half-rate function of a phase that wraps once per fundamental is not
  // continuous: it jumps back once per period and modulates at the fundamental's rate instead. Both
  // accumulators advance from the same per-sample frequency by an exact ratio of two, so the modulation
  // cannot drift away from the note, and it starts over with the note on a reattack.
  double subPhase_{0.0};
  std::optional<domain::NoteId> lastNote_;
};
}
