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
  // One table per processing block is a control-rate change like the vocal tract's formant shift, and a
  // block whose tension equals the applied one costs nothing.
  std::array<double, 256U> appliedHarmonics_{};
  double appliedTension_{0.0};
  time::SampleFrame origin_{0}, position_{0};
  double phase_{0.0}, noise_{0.0};
  std::optional<domain::NoteId> lastNote_;
};
}
