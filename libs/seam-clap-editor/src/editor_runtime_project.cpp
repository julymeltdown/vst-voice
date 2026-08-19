#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>

namespace seam::clap_editor {
using namespace detail;

domain::Project EditorRuntime::projectCopy() const {
  std::lock_guard lock(mutex_);
  return session_.project();
}

core::Result<void> EditorRuntime::replaceProject(domain::Project project) {
  std::lock_guard lock(mutex_);
  const auto replacementTrack = firstTrackId(project);
  const auto replacementRegion = firstRegionId(project);
  if (!replacementTrack.valid() || !replacementRegion.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP editor state contains no vocal track and region");
  }
  const auto replaced = authoring_->document().replaceProject(std::move(project));
  if (!replaced) return replaced;
  trackId_ = replacementTrack;
  regionId_ = replacementRegion;
  static_cast<void>(voicebankSession_.refresh());
  static_cast<void>(authoring_->selectTrack(trackId_));
  static_cast<void>(authoring_->selectRegion(regionId_));
  refreshAllVoicebankResolutionsLocked();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  dirty_ = authoring_->document().dirty();
  controller_->setDirty(dirty_);
  authoring_->handleDocumentChanged();
  requestRepaint();
  return core::success();
}

std::uint64_t EditorRuntime::revision() const noexcept {
  std::lock_guard lock(mutex_);
  return session_.revision();
}

void EditorRuntime::requestRender(std::uint32_t sampleRate) {
  std::lock_guard lock(mutex_);
  renderSampleRate_ = std::clamp(sampleRate, 8000U, 192000U);
  static_cast<void>(authoring_->setPreviewSampleRate(renderSampleRate_));
  authoring_->setRenderQuality(renderQuality_);
  authoring_->requestPreview();
}

void EditorRuntime::setRenderQuality(rendering::RenderQuality quality) {
  std::lock_guard lock(mutex_);
  if (renderQuality_ == quality) return;
  renderQuality_ = quality;
  authoring_->setRenderQuality(quality);
  authoring_->requestPreview();
}

rendering::RenderQuality EditorRuntime::renderQuality() const noexcept {
  std::lock_guard lock(mutex_);
  return renderQuality_;
}

std::shared_ptr<const RenderedPreview> EditorRuntime::renderedPreview() const {
  auto handle = previewPublication_.acquire();
  if (!handle) return std::make_shared<RenderedPreview>();
  return std::make_shared<RenderedPreview>(*handle);
}

RenderServiceStats EditorRuntime::renderStats() const noexcept {
  const auto stats = authoring_->renderer().stats();
  return RenderServiceStats{
      .submitted = stats.submitted,
      .completed = stats.completed,
      .cancelled = stats.cancelled,
      .stale = stats.stale,
  };
}

std::vector<domain::TrackId> EditorRuntime::vocalTrackIds() const {
  std::lock_guard lock(mutex_);
  std::vector<domain::TrackId> ids;
  ids.reserve(session_.project().vocalTracks().size());
  for (const auto& track : session_.project().vocalTracks()) ids.push_back(track.id);
  return ids;
}

std::vector<domain::RegionId> EditorRuntime::regionIds(
    domain::TrackId trackId) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::RegionId> ids;
  const auto* track = session_.project().findVocalTrack(trackId);
  if (track == nullptr) return ids;
  ids.reserve(track->regions.size());
  for (const auto& region : track->regions) ids.push_back(region.id);
  return ids;
}

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
  rebuildTechnicalModelsLocked();
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
  rebuildTechnicalModelsLocked();
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::setTrackMix(
    domain::TrackId trackId, float gainDb, float pan, bool muted, bool solo) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->execute(
      std::make_unique<application::SetVocalTrackMixCommand>(
          trackId, gainDb, pan, muted, solo));
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::configureOutputChannels(
    std::uint8_t channels) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->execute(
      std::make_unique<application::ConfigureProjectOutputCommand>(channels));
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::setHostStartOffset(time::Tick tick) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->execute(
      std::make_unique<application::SetHostStartOffsetCommand>(tick));
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

}  // namespace seam::clap_editor
