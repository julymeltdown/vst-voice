#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include <mutex>

// Which vocal track and region the plug-in editor is working on, and the host state that follows it.
// These live in their own translation unit so no single adapter file has to exceed the 600-line limit
// the CLAP authoring-adapter gate enforces.
namespace seam::clap_editor {
using namespace detail;

core::Result<void> EditorRuntime::selectTrack(domain::TrackId trackId) {
  std::lock_guard lock(mutex_);
  const auto* track = session_.project().findVocalTrack(trackId);
  if (track == nullptr || track->regions.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected vocal track has no editable region");
  }
  const auto selected = authoring_->selectTrack(trackId);
  if (!selected) return selected;
  trackId_ = authoring_->selectedTrack();
  regionId_ = authoring_->selectedRegion();
  refreshAllVoicebankResolutionsLocked();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::selectRegion(domain::RegionId regionId) {
  std::lock_guard lock(mutex_);
  const auto* track = session_.project().findVocalTrack(trackId_);
  if (track == nullptr || track->findRegion(regionId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected region does not belong to the active track");
  }
  const auto selected = authoring_->selectRegion(regionId);
  if (!selected) return selected;
  trackId_ = authoring_->selectedTrack();
  regionId_ = authoring_->selectedRegion();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

// The controller already moved its own selection; only the host side follows here. A region names
// its track, so a region selection moves both.
core::Result<void> EditorRuntime::followEditorSelection(domain::TrackId trackId,
                                                        domain::RegionId regionId) {
  std::lock_guard lock(mutex_);
  const auto selected = regionId.valid() ? authoring_->selectRegion(regionId)
                                         : authoring_->selectTrack(trackId);
  if (!selected) return selected;
  const auto trackChanged = authoring_->selectedTrack() != trackId_;
  const auto regionChanged = authoring_->selectedRegion() != regionId_;
  trackId_ = authoring_->selectedTrack();
  regionId_ = authoring_->selectedRegion();
  if (trackChanged) {
    refreshAllVoicebankResolutionsLocked();
    if (controller_)
      controller_->setAudioState(voicebankResolution_.resolved(),
                                 voicebankStatusLabel(voicebankResolution_));
  }
  if (trackChanged || regionChanged) requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

// The controller found no vocal track left to work on and has already cleared its own selection. The
// host lets go too, so the render, the live voicebank resource and the audio state do not go on naming
// the track that was removed.
core::Result<void> EditorRuntime::clearEditorSelection() {
  std::lock_guard lock(mutex_);
  authoring_->clearSelection();
  const auto changed = trackId_.valid() || regionId_.valid();
  trackId_ = {};
  regionId_ = {};
  if (changed) {
    refreshAllVoicebankResolutionsLocked();
    if (controller_)
      controller_->setAudioState(voicebankResolution_.resolved(),
                                 voicebankStatusLabel(voicebankResolution_));
    requestRender(renderSampleRate_);
  }
  requestRepaint();
  return core::success();
}

// What the editor selects, the host follows: MIX region and track choices move the host's render, and
// an editor with nothing to select makes the host let go.
void EditorRuntime::attachSelectionCallbacks(native_ui::EditorHostCallbacks& callbacks) {
  callbacks.selectTrack = [this](domain::TrackId id) { return followEditorSelection(id, {}); };
  callbacks.selectRegion = [this](domain::RegionId id) { return followEditorSelection({}, id); };
  callbacks.clearVocalTarget = [this] { return clearEditorSelection(); };
}

}  // namespace seam::clap_editor
