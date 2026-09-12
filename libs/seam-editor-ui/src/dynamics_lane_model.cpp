#include "seam/ui/dynamics_lane_model.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include <algorithm>
#include <unordered_map>

namespace seam::ui {
DynamicsLaneModel::DynamicsLaneModel(application::PerformanceJobContext context,
    domain::RegionId region, std::uint64_t revision)
    : context_(std::move(context)), region_(region), revision_(revision),
      draft_(context_.sourceProject().findRegion(region)->dynamicsAutomation) {
  const auto& performance = context_.sourceProject().findRegion(region)->performance;
  for (const auto& selection : performance.accepted)
    if (selection.channel == domain::PerformanceChannel::Dynamics) ++influence_.generatedSelections;
  for (const auto& ownership : performance.ownership)
    if (ownership.channel == domain::PerformanceChannel::Dynamics && ownership.mode == domain::ManualPerformanceMode::Replace)
      ++influence_.manualReplacementScopes;
}

core::Result<DynamicsLaneModel> DynamicsLaneModel::prepare(const application::EditorSession& session,
    domain::RegionId regionId, std::stop_token stop) {
  if (stop.stop_requested())
    return core::failure<DynamicsLaneModel>(core::ErrorCode::Conflict, "Dynamics lane capture cancelled");
  const auto* region = session.project().findRegion(regionId);
  if (!region || region->notes.size() > 10000U)
    return core::failure<DynamicsLaneModel>(core::ErrorCode::InvalidArgument, "Choose a region with at most 10000 notes");
  const auto valid = region->dynamicsAutomation.validate();
  if (!valid) return core::Result<DynamicsLaneModel>{valid.error()};
  auto context = session.capturePerformanceJob();
  if (!context) return core::Result<DynamicsLaneModel>{context.error()};
  if (stop.stop_requested())
    return core::failure<DynamicsLaneModel>(core::ErrorCode::Conflict, "Dynamics lane capture cancelled");
  return DynamicsLaneModel{std::move(context.value()), regionId, session.revision()};
}
bool DynamicsLaneModel::hasChanges() const noexcept {
  return draft_ != context_.sourceProject().findRegion(region_)->dynamicsAutomation;
}
core::Result<void> DynamicsLaneModel::editable() const {
  if (state_ != State::Ready)
    return core::failure(core::ErrorCode::Conflict, "Dynamics lane draft is closed");
  return core::success();
}
core::Result<void> DynamicsLaneModel::validatePoint(domain::DynamicsAutomationPoint point) const {
  const auto valid = point.validate(); if (!valid) return valid;
  if (point.tick > context_.sourceProject().findRegion(region_)->durationTick)
    return core::failure(core::ErrorCode::InvariantViolation, "Dynamics automation extends beyond the region");
  return core::success();
}
core::Result<void> DynamicsLaneModel::upsert(domain::DynamicsAutomationPoint point) {
  const auto ready = editable(); if (!ready) return ready;
  const auto valid = validatePoint(point); if (!valid) return valid;
  const auto result = draft_.upsert(point); if (result) invalidateTarget(); return result;
}
core::Result<void> DynamicsLaneModel::erase(time::Tick tick) {
  const auto ready = editable(); if (!ready) return ready;
  if (!draft_.erase(tick)) return core::failure(core::ErrorCode::NotFound, "Dynamics point no longer exists");
  invalidateTarget();
  return core::success();
}
core::Result<void> DynamicsLaneModel::move(time::Tick source, domain::DynamicsAutomationPoint destination) {
  const auto ready = editable(); if (!ready) return ready;
  const auto valid = validatePoint(destination); if (!valid) return valid;
  const auto contains = [&](time::Tick tick) {
    const auto& points = draft_.points();
    const auto it = std::lower_bound(points.begin(), points.end(), tick,
        [](const auto& point, time::Tick value) { return point.tick < value; });
    return it != points.end() && it->tick == tick;
  };
  if (!contains(source)) return core::failure(core::ErrorCode::NotFound, "Dynamics point no longer exists");
  if (source != destination.tick && contains(destination.tick))
    return core::failure(core::ErrorCode::Conflict, "A dynamics point already occupies the destination");
  auto next = draft_;
  static_cast<void>(next.erase(source));
  const auto changed = next.upsert(destination); if (!changed) return changed;
  draft_ = std::move(next); invalidateTarget(); return core::success();
}
core::Result<void> DynamicsLaneModel::reset() {
  const auto ready = editable(); if (!ready) return ready;
  draft_ = context_.sourceProject().findRegion(region_)->dynamicsAutomation;
  invalidateTarget();
  return core::success();
}
bool DynamicsLaneModel::matches(const application::EditorSession& session, domain::RegionId activeRegion) const {
  return state_ == State::Ready && activeRegion == region_ && session.revision() == revision_ &&
      static_cast<bool>(session.validatePerformanceJob(context_));
}
void DynamicsLaneModel::refreshTargetPreview(std::optional<TargetWindow> window) {
  targetSamples_.clear(); targetError_.clear(); targetReady_ = false;
  if (state_ != State::Ready) { targetError_ = "Dynamics draft is closed"; return; }
  if (window && (window->first < time::Tick{0} || window->last <= window->first)) {
    targetError_ = "Dynamics preview window requires ordered nonnegative ticks"; return;
  }
  if (context_.sourceProject().findRegion(region_)->notes.size() > synthesis::kMaximumScoreVoiceAllocationNotes) {
    targetError_ = "Target preview supports at most " + std::to_string(synthesis::kMaximumScoreVoiceAllocationNotes) +
        " region notes; native editing remains available"; return;
  }
  const auto& sourceProject = context_.sourceProject();
  const auto* region = sourceProject.findRegion(region_);
  // The production voice allocator/compiler is authoritative for overlap and
  // accepted/manual precedence. Failure does not prohibit editing native data.
  if (!targetCompiled_) {
    auto project = sourceProject; project.findRegion(region_)->dynamicsAutomation = draft_;
    auto compiled = synthesis::compileScoreVoices(project, *project.findRegion(region_), 48000U);
    if (!compiled) { targetError_ = compiled.error().message; return; }
    targetCompiled_.emplace(std::move(compiled.value()));
  }
  const auto end = window ? std::min(window->last.value(), region->durationTick.value()) : region->durationTick.value();
  const auto start = window ? std::min(window->first.value(), region->durationTick.value()) : std::int64_t{0};
  if (start == end) { targetReady_ = true; return; }
  std::unordered_map<domain::NoteId, const domain::Note*> notes;
  for (const auto& note : region->notes) notes.emplace(note.id, &note);
  const auto span = end - start;
  const auto intervals = std::min(std::int64_t{256}, span);
  for (std::size_t voice = 0U; voice < targetCompiled_->size(); ++voice) {
    const auto& performance = (*targetCompiled_)[voice].performance;
    std::vector<time::Tick> ticks;
    // Quotient/remainder arithmetic avoids overflowing long score extents.
    for (std::int64_t i = 0; i <= intervals; ++i)
      ticks.push_back(time::Tick{start + (span / intervals) * i + ((span % intervals) * i) / intervals});
    // Uniform overview samples alone can miss short notes in a long region.
    for (const auto& note : performance.notes()) {
      const auto found = notes.find(note.id);
      if (found == notes.end()) { targetError_ = "Compiled dynamics note is missing"; targetSamples_.clear(); return; }
      const auto* source = found->second;
      ticks.push_back(source->startTick);
      ticks.push_back(source->startTick + time::Tick{source->durationTick.value() / 2});
      ticks.push_back(source->startTick + source->durationTick - time::Tick{1});
    }
    std::sort(ticks.begin(), ticks.end()); ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    for (const auto tick : ticks) {
      if (tick.value() < start || tick.value() > end) continue;
      const auto sample = performance.inspectAt(sourceProject.tempoMap().sampleFrameAt(region->startTick + tick, 48000U));
      if (sample.noteId) targetSamples_.push_back({tick, *sample.noteId, voice, sample.dynamicsGain, sample.selectedGeneratedDynamicsGain});
    }
  }
  targetReady_ = true;
}
core::Result<void> DynamicsLaneModel::apply(application::EditorSession& session,
    domain::RegionId activeRegion, std::stop_token stop) {
  if (stop.stop_requested() || !matches(session, activeRegion))
    return core::failure(core::ErrorCode::Conflict, "Dynamics lane draft is stale, cancelled or closed");
  if (hasChanges()) {
    const auto result = session.executePerformanceResult(context_, std::make_unique<application::EditPerformanceCommand>(
        std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{{region_, draft_}}));
    if (!result) return result;
  }
  state_ = State::Applied; return core::success();
}
}
