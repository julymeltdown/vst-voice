#include "seam/ui/vibrato_model.hpp"
#include <algorithm>
#include <unordered_set>

namespace seam::ui {
VibratoModel::VibratoModel(application::PerformanceJobContext context, domain::RegionId region,
    std::uint64_t revision, std::vector<domain::NoteId> selected)
    : context_(std::move(context)), region_(region), revision_(revision), selected_(std::move(selected)) {}

core::Result<VibratoModel> VibratoModel::prepare(const application::EditorSession& session,
    domain::RegionId regionId, const VibratoFields& patch, std::stop_token stop) {
  const auto cancelled = [] { return core::failure<VibratoModel>(core::ErrorCode::Conflict, "Vibrato preparation cancelled"); };
  if (stop.stop_requested()) return cancelled();
  const auto* region = session.project().findRegion(regionId); auto selected = session.selection().noteIds();
  if (!region || selected.empty() || selected.size() > 10000U || region->notes.size() > 10000U)
    return core::failure<VibratoModel>(core::ErrorCode::InvalidArgument, "Select 1 to 10000 notes in the active region");
  std::sort(selected.begin(), selected.end());
  auto context = session.capturePerformanceJob(); if (!context) return core::Result<VibratoModel>{context.error()};
  VibratoModel model{std::move(context.value()), regionId, session.revision(), std::move(selected)};
  const std::unordered_set<domain::NoteId> ids(model.selected_.begin(), model.selected_.end());
  std::vector<const domain::Note*> notes;
  for (const auto& note : model.context_.sourceProject().findRegion(regionId)->notes) {
    if (stop.stop_requested()) return cancelled();
    if (ids.contains(note.id)) notes.push_back(&note);
  }
  if (notes.size() != model.selected_.size())
    return core::failure<VibratoModel>(core::ErrorCode::Conflict, "Every vibrato target must belong to the active region");
  std::sort(notes.begin(), notes.end(), [](const auto* a, const auto* b) {
    return a->startTick == b->startTick ? a->id < b->id : a->startTick < b->startTick;
  });
  const auto common = [&]<typename T>(T domain::NoteVibrato::*field) -> std::optional<T> {
    const auto value = notes.front()->vibrato.*field;
    for (const auto* note : notes) if (note->vibrato.*field != value) return std::nullopt;
    return value;
  };
  model.values_ = {common(&domain::NoteVibrato::enabled), common(&domain::NoteVibrato::startFraction),
      common(&domain::NoteVibrato::fadeInFraction), common(&domain::NoteVibrato::fadeOutFraction),
      common(&domain::NoteVibrato::depthCents), common(&domain::NoteVibrato::periodMilliseconds), common(&domain::NoteVibrato::phaseTurns)};
  for (const auto* note : notes) {
    if (stop.stop_requested()) return cancelled();
    auto after = note->vibrato;
    if (patch.enabled) after.enabled = *patch.enabled;
    if (patch.startFraction) after.startFraction = *patch.startFraction;
    if (patch.fadeInFraction) after.fadeInFraction = *patch.fadeInFraction;
    if (patch.fadeOutFraction) after.fadeOutFraction = *patch.fadeOutFraction;
    if (patch.depthCents) after.depthCents = *patch.depthCents;
    if (patch.periodMilliseconds) after.periodMilliseconds = *patch.periodMilliseconds;
    if (patch.phaseTurns) after.phaseTurns = *patch.phaseTurns;
    const auto valid = after.validate(); if (!valid) return core::Result<VibratoModel>{valid.error()};
    if (after != note->vibrato) {
      model.edits_.push_back({note->id, note->vibrato, after});
      model.expressions_.push_back({note->id, after, note->phoneticHint});
    }
  }
  if (stop.stop_requested()) return cancelled();
  return model;
}

bool VibratoModel::matches(const application::EditorSession& session, domain::RegionId activeRegion) const {
  if (state_ != State::Ready || activeRegion != region_ || session.revision() != revision_) return false;
  auto selected = session.selection().noteIds(); std::sort(selected.begin(), selected.end());
  return selected == selected_ && static_cast<bool>(session.validatePerformanceJob(context_));
}

core::Result<void> VibratoModel::apply(application::EditorSession& session, domain::RegionId activeRegion, std::stop_token stop) {
  if (stop.stop_requested() || !matches(session, activeRegion))
    return core::failure(core::ErrorCode::Conflict, "Vibrato preview is cancelled, stale or already applied");
  if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Vibrato edit cancelled before commit");
  if (!expressions_.empty()) {
    const auto result = session.executePerformanceResult(context_, std::make_unique<application::EditPerformanceCommand>(expressions_));
    if (!result) return result;
  }
  state_ = State::Applied; return core::success();
}
}
