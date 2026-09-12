#include "seam/native_ui/voice_designer_model.hpp"
#include <limits>

namespace seam::native_ui {

core::Result<VoiceDesignerModel> VoiceDesignerModel::create(voice_design::VoiceRecipe recipe) {
  auto resource = voice_design::freezeVoiceRecipeResource(recipe);
  if (!resource) return core::Result<VoiceDesignerModel>{resource.error()};
  return VoiceDesignerModel{Snapshot{std::move(recipe), std::move(resource.value())}};
}

core::Result<void> VoiceDesignerModel::checkRevision(std::uint64_t expected) const {
  if (revision_ != expected || revision_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Voice Designer edit is stale or revision is exhausted");
  return core::success();
}

core::Result<void> VoiceDesignerModel::edit(std::uint64_t expectedRevision, voice_design::VoiceRecipe desired) {
  return apply(expectedRevision, std::move(desired), false);
}

core::Result<void> VoiceDesignerModel::updateGesture(std::uint64_t expectedRevision, voice_design::VoiceRecipe desired) {
  return apply(expectedRevision, std::move(desired), true);
}

core::Result<void> VoiceDesignerModel::apply(std::uint64_t expectedRevision, voice_design::VoiceRecipe desired, bool preview) {
  const auto current = checkRevision(expectedRevision); if (!current) return current;
  if (preview != gesture_.has_value())
    return core::failure(core::ErrorCode::Conflict, "Finish the active Designer gesture or begin one before previewing");
  // Reserve a revision for closing the gesture even at exhaustion.
  if (preview && revision_ == std::numeric_limits<std::uint64_t>::max() - 1U)
    return core::failure(core::ErrorCode::Conflict, "Designer preview revision is exhausted; finish the gesture");
  if (desired.id != current_.recipe.id || desired.engineId != current_.recipe.engineId)
    return core::failure(core::ErrorCode::InvalidArgument, "Create a separate voice to change recipe or engine identity");
  auto resource = voice_design::freezeVoiceRecipeResource(desired);
  if (!resource) return core::Result<void>{resource.error()};
  if (resource.value().identity == current_.resource.identity) return core::success();
  Snapshot next{std::move(desired), std::move(resource.value())};
  // Copy before mutation so allocation failure cannot leave a partial edit.
  if (!preview) {
    undo_.push_back(current_);
    if (undo_.size() > kHistoryLimit) undo_.erase(undo_.begin());
    redo_.clear();
  }
  current_ = std::move(next);
  ++revision_;
  return core::success();
}

core::Result<void> VoiceDesignerModel::restore(std::uint64_t expected, std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
  const auto current = checkRevision(expected); if (!current) return current;
  if (gesture_) return core::failure(core::ErrorCode::Conflict, "Finish the Designer gesture before undo or redo");
  if (from.empty()) return core::failure(core::ErrorCode::InvalidState, "No Voice Designer history available");
  to.push_back(current_);
  if (to.size() > kHistoryLimit) to.erase(to.begin());
  current_ = std::move(from.back());
  from.pop_back();
  ++revision_;
  return core::success();
}

core::Result<void> VoiceDesignerModel::undo(std::uint64_t expectedRevision) { return restore(expectedRevision, undo_, redo_); }
core::Result<void> VoiceDesignerModel::redo(std::uint64_t expectedRevision) { return restore(expectedRevision, redo_, undo_); }

core::Result<void> VoiceDesignerModel::beginGesture(std::uint64_t expectedRevision) {
  const auto current = checkRevision(expectedRevision); if (!current) return current;
  if (gesture_ || revision_ == std::numeric_limits<std::uint64_t>::max() - 1U)
    return core::failure(core::ErrorCode::Conflict, "Designer gesture is active or revision is exhausted");
  gesture_ = current_;
  ++revision_;
  return core::success();
}

core::Result<void> VoiceDesignerModel::commitGesture(std::uint64_t expectedRevision) {
  const auto current = checkRevision(expectedRevision); if (!current) return current;
  if (!gesture_) return core::failure(core::ErrorCode::InvalidState, "No Designer gesture to commit");
  if (gesture_->resource.identity != current_.resource.identity) {
    undo_.push_back(*gesture_);
    if (undo_.size() > kHistoryLimit) undo_.erase(undo_.begin());
    redo_.clear();
  }
  gesture_.reset();
  ++revision_;
  return core::success();
}

core::Result<void> VoiceDesignerModel::cancelGesture(std::uint64_t expectedRevision) {
  const auto current = checkRevision(expectedRevision); if (!current) return current;
  if (!gesture_) return core::failure(core::ErrorCode::InvalidState, "No Designer gesture to cancel");
  current_ = std::move(*gesture_);
  gesture_.reset();
  ++revision_;
  return core::success();
}

core::Result<void> VoiceDesignerModel::acknowledgeSave(std::uint64_t expectedRevision, std::string_view persistedHash) {
  if (gesture_ || expectedRevision != revision_ || persistedHash != current_.resource.identity.contentHash)
    return core::failure(core::ErrorCode::Conflict, "Saved recipe differs from the current Designer snapshot");
  savedHash_ = persistedHash;
  return core::success();
}

} // namespace seam::native_ui
