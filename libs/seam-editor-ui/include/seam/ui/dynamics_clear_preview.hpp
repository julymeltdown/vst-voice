#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include <algorithm>
#include <span>
#include <stop_token>

namespace seam::ui {

// Explicit region scope: the continuous native curve is not note-owned.
// Generated take selections and manual ownership are deliberately preserved.
class DynamicsClearPreview final {
public:
  enum class State { Ready, Applied, Cancelled };
  [[nodiscard]] static core::Result<DynamicsClearPreview> prepare(
      const application::EditorSession& session, domain::RegionId regionId, std::stop_token stop = {}) {
    if (stop.stop_requested()) return core::failure<DynamicsClearPreview>(core::ErrorCode::Conflict, "Dynamics clear cancelled");
    const auto* region = session.project().findRegion(regionId);
    if (!region || region->notes.size() > 10000U)
      return core::failure<DynamicsClearPreview>(core::ErrorCode::InvalidArgument, "Choose a region with at most 10000 notes");
    auto context = session.capturePerformanceJob(); if (!context) return core::Result<DynamicsClearPreview>{context.error()};
    if (stop.stop_requested()) return core::failure<DynamicsClearPreview>(core::ErrorCode::Conflict, "Dynamics clear cancelled");
    return DynamicsClearPreview{std::move(context.value()), regionId, session.revision()};
  }
  [[nodiscard]] std::span<const domain::DynamicsAutomationPoint> points() const noexcept { return source().dynamicsAutomation.points(); }
  [[nodiscard]] std::size_t regionNoteCount() const noexcept { return source().notes.size(); }
  [[nodiscard]] std::size_t retainedGeneratedSelections() const noexcept {
    return static_cast<std::size_t>(std::count_if(source().performance.accepted.begin(), source().performance.accepted.end(),
        [](const auto& selection) { return selection.channel == domain::PerformanceChannel::Dynamics; }));
  }
  [[nodiscard]] bool hasChanges() const noexcept { return !points().empty(); }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId selectedRegion) const {
    const auto* region = session.project().findRegion(regionId_);
    return state_ == State::Ready && selectedRegion == regionId_ && session.revision() == revision_ &&
        session.project().id() == context_.sourceProject().id() && region && *region == source();
  }
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session,
      domain::RegionId selectedRegion, std::stop_token stop = {}) {
    if (stop.stop_requested() || !matches(session, selectedRegion))
      return core::failure(core::ErrorCode::Conflict, "Dynamics clear is cancelled, stale or already applied");
    const auto valid = session.validatePerformanceJob(context_); if (!valid) return valid;
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Dynamics clear cancelled before commit");
    if (hasChanges()) {
      const auto result = session.executePerformanceResult(context_, std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{{regionId_, {}}}));
      if (!result) return result;
    }
    state_ = State::Applied; return core::success();
  }
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }
private:
  DynamicsClearPreview(application::PerformanceJobContext context, domain::RegionId regionId, std::uint64_t revision)
      : context_(std::move(context)), regionId_(regionId), revision_(revision) {}
  [[nodiscard]] const domain::VocalRegion& source() const noexcept { return *context_.sourceProject().findRegion(regionId_); }
  application::PerformanceJobContext context_;
  domain::RegionId regionId_;
  std::uint64_t revision_;
  State state_{State::Ready};
};
}
