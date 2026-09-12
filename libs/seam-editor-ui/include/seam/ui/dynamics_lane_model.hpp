#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include <stop_token>

namespace seam::ui {

// A region-scoped draft, not a selected-note envelope. Interpolation and endpoint
// extension follow DynamicsAutomation; generated takes and ownership stay intact.
class DynamicsLaneModel final {
public:
  struct Influence final {
    std::size_t generatedSelections{0U};
    std::size_t manualReplacementScopes{0U};
  };
  struct TargetSample final {
    time::Tick tick;
    domain::NoteId note;
    std::size_t voice;
    float linearGain;
    std::optional<float> selectedGeneratedGain{};
  };
  struct TargetWindow final { time::Tick first, last; };
  enum class State { Ready, Applied, Cancelled };
  [[nodiscard]] static core::Result<DynamicsLaneModel> prepare(
      const application::EditorSession& session, domain::RegionId region, std::stop_token stop = {});
  [[nodiscard]] const domain::DynamicsAutomation& curve() const noexcept { return draft_; }
  [[nodiscard]] const domain::DynamicsAutomation& sourceCurve() const noexcept {
    return context_.sourceProject().findRegion(region_)->dynamicsAutomation;
  }
  [[nodiscard]] bool hasChanges() const noexcept;
  // Captured scope counts, not an estimate of rendered coverage or audibility.
  [[nodiscard]] Influence influence() const noexcept { return influence_; }
  // Explicit bounded score-only compilation, never called from paint or drag.
  void refreshTargetPreview(std::optional<TargetWindow> window = {});
  [[nodiscard]] const std::vector<TargetSample>& targetSamples() const noexcept { return targetSamples_; }
  [[nodiscard]] const std::string& targetError() const noexcept { return targetError_; }
  [[nodiscard]] bool targetReady() const noexcept { return targetReady_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId activeRegion) const;
  [[nodiscard]] core::Result<void> upsert(domain::DynamicsAutomationPoint point);
  [[nodiscard]] core::Result<void> validatePoint(domain::DynamicsAutomationPoint point) const;
  [[nodiscard]] core::Result<void> erase(time::Tick tick);
  // Moving onto a different existing point rejects rather than silently merging.
  [[nodiscard]] core::Result<void> move(time::Tick source, domain::DynamicsAutomationPoint destination);
  [[nodiscard]] core::Result<void> reset();
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session,
      domain::RegionId activeRegion, std::stop_token stop = {});
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }
private:
  DynamicsLaneModel(application::PerformanceJobContext context, domain::RegionId region, std::uint64_t revision);
  [[nodiscard]] core::Result<void> editable() const;
  void invalidateTarget() noexcept { targetSamples_.clear(); targetError_.clear(); targetReady_ = false; targetCompiled_.reset(); }
  application::PerformanceJobContext context_;
  domain::RegionId region_;
  std::uint64_t revision_;
  domain::DynamicsAutomation draft_;
  Influence influence_;
  std::vector<TargetSample> targetSamples_;
  std::optional<std::vector<synthesis::CompiledVoicePerformance>> targetCompiled_;
  std::string targetError_;
  bool targetReady_{false};
  State state_{State::Ready};
};
}
