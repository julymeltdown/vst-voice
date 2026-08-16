#pragma once

#include "seam/core/result.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"

#include <stop_token>

namespace seam::rendering {

struct PhrasePipelineResult final {
  phonemizer::Result phonemes;
  synthesis::UnitPlan unitPlan;
  synthesis::TimingPlan timing;
  synthesis::PhraseRenderResult rendered;
};

class PhraseRenderPipeline final {
public:
  [[nodiscard]] core::Result<PhrasePipelineResult> render(
      const RenderSnapshot& snapshot,
      const synthesis::PhraseRenderOptions& options = {},
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::rendering
