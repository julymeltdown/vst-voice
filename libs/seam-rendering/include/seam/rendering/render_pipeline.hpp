#pragma once

#include "seam/core/result.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voice_design/procedural_renderer.hpp"
#include "seam/voice_design/articulated_stream.hpp"

#include <stop_token>

namespace seam::rendering {

// Validates the currently supported procedural capability without rendering.
[[nodiscard]] core::Result<std::string> validateProceduralSnapshot(const RenderSnapshot& snapshot);
[[nodiscard]] core::Result<std::vector<voice_design::ProceduralPhoneMarker>> projectProceduralMarkers(
    const RenderSnapshot& snapshot);

// Single-owner checkpoint bound to one immutable snapshot context. Only
// factory-derived siblings sharing that context may reuse its DSP state.
class ProceduralSnapshotStream final {
public:
  [[nodiscard]] static core::Result<ProceduralSnapshotStream> create(
      RenderSnapshot snapshot, std::stop_token stopToken = {});
  [[nodiscard]] core::Result<voice_design::SustainedPoseResult> render(
      const RenderSnapshot& snapshot, std::stop_token stopToken = {});
  [[nodiscard]] time::SampleFrame position() const noexcept {
    return std::visit([](const auto& stream) { return stream.position(); }, stream_);
  }
  [[nodiscard]] bool matchesContext(const RenderSnapshot& snapshot) const noexcept;
private:
  using Stream = std::variant<voice_design::SustainedPoseStream, voice_design::ArticulatedStream>;
  ProceduralSnapshotStream(RenderSnapshot snapshot, Stream stream)
      : snapshot_(std::move(snapshot)), stream_(std::move(stream)) {}
  RenderSnapshot snapshot_;
  Stream stream_;
};

struct PhrasePipelineResult final {
  phonemizer::Result phonemes;
  synthesis::UnitPlan unitPlan;
  synthesis::TimingPlan timing;
  synthesis::PhraseRenderResult rendered;
  domain::SingerResourceKind resourceKind{domain::SingerResourceKind::Sample};
  std::vector<voice_design::ProceduralPhoneMarker> proceduralMarkers{};
};

class PhraseRenderPipeline final {
public:
  [[nodiscard]] core::Result<PhrasePipelineResult> render(
      const RenderSnapshot& snapshot,
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::rendering
