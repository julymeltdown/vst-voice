#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include <algorithm>
#include <span>
#include <stop_token>
#include <unordered_set>

namespace seam::ui {

struct VibratoClearEdit final {
  domain::NoteId noteId;
  domain::NoteVibrato before;
  domain::NoteVibrato after;
};

// Owner-thread capture/apply. Disabling modulation does not erase settings or
// relinquish independently authored pitch ownership. No view/backend callback.
class VibratoClearPreview final {
public:
  enum class State { Ready, Applied, Cancelled };
  static constexpr std::size_t maximumNotes = 10000U;
  [[nodiscard]] static core::Result<VibratoClearPreview> prepare(
      const application::EditorSession& session, domain::RegionId regionId,
      std::stop_token stop = {}) {
    if (stop.stop_requested()) return core::failure<VibratoClearPreview>(core::ErrorCode::Conflict, "Vibrato clear cancelled");
    auto selected = session.selection().noteIds();
    const auto* region = session.project().findRegion(regionId);
    if (!region || selected.empty() || selected.size() > maximumNotes || region->notes.size() > maximumNotes)
      return core::failure<VibratoClearPreview>(core::ErrorCode::InvalidArgument, "Select 1 to 10000 notes in the active region");
    std::sort(selected.begin(), selected.end());
    auto context = session.capturePerformanceJob(); if (!context) return core::Result<VibratoClearPreview>{context.error()};
    VibratoClearPreview preview{std::move(context.value()), regionId, session.revision(), std::move(selected)};
    const std::unordered_set<domain::NoteId> ids(preview.selected_.begin(), preview.selected_.end());
    const auto& source = *preview.context_.sourceProject().findRegion(regionId);
    std::vector<const domain::Note*> notes;
    for (const auto& note : source.notes) {
      if (stop.stop_requested()) return core::failure<VibratoClearPreview>(core::ErrorCode::Conflict, "Vibrato clear cancelled");
      if (ids.contains(note.id)) notes.push_back(&note);
    }
    if (notes.size() != preview.selected_.size())
      return core::failure<VibratoClearPreview>(core::ErrorCode::Conflict, "Every vibrato-clear target must belong to the active region");
    std::sort(notes.begin(), notes.end(), [](const auto* left, const auto* right) {
      return left->startTick != right->startTick ? left->startTick < right->startTick : left->id < right->id;
    });
    for (const auto* note : notes) {
      if (!note->vibrato.enabled) continue;
      auto after = note->vibrato; after.enabled = false;
      preview.edits_.push_back({note->id, note->vibrato, after});
      preview.expressions_.push_back({note->id, after, note->phoneticHint});
    }
    if (stop.stop_requested()) return core::failure<VibratoClearPreview>(core::ErrorCode::Conflict, "Vibrato clear cancelled");
    return core::success(std::move(preview));
  }

  [[nodiscard]] std::size_t selectedCount() const noexcept { return selected_.size(); }
  [[nodiscard]] std::span<const VibratoClearEdit> edits() const noexcept { return edits_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId selectedRegion) const {
    auto selection = session.selection().noteIds(); std::sort(selection.begin(), selection.end());
    const auto* current = session.project().findRegion(regionId_);
    return state_ == State::Ready && selectedRegion == regionId_ && session.revision() == revision_ &&
        session.project().id() == context_.sourceProject().id() && selection == selected_ && current &&
        *current == *context_.sourceProject().findRegion(regionId_);
  }
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session,
      domain::RegionId selectedRegion, std::stop_token stop = {}) {
    if (state_ != State::Ready || stop.stop_requested())
      return core::failure(core::ErrorCode::Conflict, "Vibrato clear is cancelled or already applied");
    if (!matches(session, selectedRegion))
      return core::failure(core::ErrorCode::Conflict, "Vibrato-clear targets changed after preview");
    const auto valid = session.validatePerformanceJob(context_); if (!valid) return valid;
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Vibrato clear cancelled before commit");
    if (!expressions_.empty()) {
      const auto result = session.executePerformanceResult(context_,
          std::make_unique<application::EditPerformanceCommand>(expressions_));
      if (!result) return result;
    }
    state_ = State::Applied; return core::success();
  }
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }

private:
  VibratoClearPreview(application::PerformanceJobContext context, domain::RegionId regionId,
      std::uint64_t revision, std::vector<domain::NoteId> selected)
      : context_(std::move(context)), regionId_(regionId), revision_(revision), selected_(std::move(selected)) {}
  application::PerformanceJobContext context_;
  domain::RegionId regionId_;
  std::uint64_t revision_;
  std::vector<domain::NoteId> selected_;
  std::vector<VibratoClearEdit> edits_;
  std::vector<application::NoteExpressionEdit> expressions_;
  State state_{State::Ready};
};
}
