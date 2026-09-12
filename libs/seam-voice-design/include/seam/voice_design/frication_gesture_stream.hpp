#pragma once
#include "seam/voice_design/articulation_plan.hpp"
#include <memory>

namespace seam::voice_design {
// Aperiodic lane: sustained frication and finite plosive closure/burst sources.
// Voiced spans and unassigned gaps are exactly silent.
// Copies are independent checkpoints; owned-window seeks replay the prefix.
class FricationGestureStream final {
public:
  [[nodiscard]] static core::Result<FricationGestureStream> create(ArticulationPlan plan, std::size_t blockFrames = 512U,
      bool voicedStopsRenderedSeparately = false);
  [[nodiscard]] core::Result<synthesis::PhraseAudio> renderOwned(synthesis::PhraseFrameRange owned, std::stop_token stop = {});
  void reset() noexcept { position_ = plan_->context().start; next_ = 0U; source_.reset(); plosive_.reset(); }
  [[nodiscard]] time::SampleFrame position() const noexcept { return position_; }
  static constexpr std::uint32_t algorithmRevision = 3U;
private:
  FricationGestureStream() = default;
  std::shared_ptr<const ArticulationPlan> plan_;
  std::optional<FricationSource> source_;
  std::optional<PlosiveSource> plosive_;
  time::SampleFrame position_{0};
  std::size_t next_{0U}, blockFrames_{512U};
};
}
