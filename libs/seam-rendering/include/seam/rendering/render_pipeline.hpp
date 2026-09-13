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
#include <memory>

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

// Executes one prepared neural snapshot. The application selects the concrete
// runner, so helper selection, expected digest and process budgets stay out of
// the bank and out of the audio callback. A runner must return mono audio for
// the snapshot's declared owned window at the snapshot's sample rate; the
// pipeline verifies the window and frame count, but `PhraseAudio` carries no
// rate, so rate agreement remains the runner's contract.
class NeuralPhraseRunner {
public:
  virtual ~NeuralPhraseRunner() = default;
  [[nodiscard]] virtual core::Result<synthesis::PhraseRenderResult> render(
      const RenderSnapshot& snapshot, std::stop_token stopToken) const = 0;
};

class PhraseRenderPipeline final {
public:
  PhraseRenderPipeline() = default;
  explicit PhraseRenderPipeline(std::shared_ptr<const NeuralPhraseRunner> neuralRunner)
      : neuralRunner_(std::move(neuralRunner)) {}
  [[nodiscard]] core::Result<PhrasePipelineResult> render(
      const RenderSnapshot& snapshot,
      std::stop_token stopToken = {}) const;
private:
  // No default runner: neural execution is refused until the application
  // selects a first-party helper through its deployment identity.
  std::shared_ptr<const NeuralPhraseRunner> neuralRunner_{};
};

}  // namespace seam::rendering
