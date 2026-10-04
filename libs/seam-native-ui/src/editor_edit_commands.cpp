// Selection and edit commands: choosing what is selected, and the edits a creator applies to it.
//
// SEAM-BETA-P2-01 names selection/edit commands as one of the boundaries worth taking out of
// editor_controller.cpp. This is that boundary. It holds the fifty-two methods that move the
// selection (track, adjacent track, region), the structural edits (add / remove / rename / reorder a
// track or region, split, duplicate, copy to track, delete, move, resize), the per-track and
// per-note parameter commands (mix, route, voicebank, seam and unit overrides, quantize, slur,
// melisma, lyric distribution), and the selection-to-host reconciliation that follows an edit.
//
// The methods moved verbatim: the controller diff is deletions only and the block was diffed
// byte-for-byte, so nothing about their behaviour changed with their location.
//
// reconcileWithProject and leavePlace stayed here rather than moving into the overlay file,
// because both are called from here and from painting, and a helper with two owners belongs with
// neither. They are the reason this boundary stops where it does.

#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_text_target.hpp"
#include "seam/native_ui/editor_notices.hpp"

#include "seam/application/arrangement_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/view_commands.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::native_ui {

namespace {

// Tries the host gets for one move of the editor selection: the move itself and two more,
// each before a key press or a pointer press.
constexpr unsigned kHostSelectionTries = 3U;

}  // namespace

core::Result<void> NativeEditorController::selectTrack(domain::TrackId trackId) {
  if (callbacks_.selectTrack) {
    const auto hostSelection = callbacks_.selectTrack(trackId);
    if (!hostSelection) return hostSelection;
    hostSelectionFollowed();
  }
  auto selected = arrangementPanel_.selectTrack(session_.project(), trackId);
  if (!selected) return selected;
  selectedTrackId_ = arrangementPanel_.selectedTrack();
  regionId_ = arrangementPanel_.selectedRegion();
  leavePlace();
  pianoRoll_.setRegionId(regionId_);
  pianoRoll_.rebuildIndex();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::selectAdjacentVocalTrack(
    int direction) {
  const auto& tracks = arrangementPanel_.tracks();
  if (tracks.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "No vocal tracks are available");
  }
  if (direction == 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Track navigation direction must be nonzero");
  }
  const auto current = std::find_if(
      tracks.begin(), tracks.end(), [this](const auto& track) {
        return track.id == selectedTrackId_;
      });
  auto index = current == tracks.end()
                   ? std::size_t{0U}
                   : static_cast<std::size_t>(current - tracks.begin());
  if (direction > 0) {
    index = (index + 1U) % tracks.size();
  } else {
    index = index == 0U ? tracks.size() - 1U : index - 1U;
  }
  return selectTrack(tracks[index].id);
}

core::Result<void> NativeEditorController::selectRegion(domain::RegionId regionId) {
  if (callbacks_.selectRegion) {
    const auto hostSelection = callbacks_.selectRegion(regionId);
    if (!hostSelection) return hostSelection;
    hostSelectionFollowed();
  }
  auto selected = arrangementPanel_.selectRegion(session_.project(), regionId);
  if (!selected) return selected;
  selectedTrackId_ = arrangementPanel_.selectedTrack();
  regionId_ = arrangementPanel_.selectedRegion();
  leavePlace();
  pianoRoll_.setRegionId(regionId_);
  pianoRoll_.rebuildIndex();
  repaint();
  return core::success();
}

void NativeEditorController::reconcileWithProject() {
  const auto& project = session_.project();
  const auto* vocal = project.findVocalTrack(selectedTrackId_);
  // An audio track is only somewhere to rest while the score has no vocal track. With a host connected
  // nothing lets the editor choose one, so an audio selection beside a vocal track is what a removal
  // left behind, and it gives way to the vocal track that has come back.
  const auto audioSelected = vocal == nullptr && project.vocalTracks().empty() &&
      std::any_of(project.audioTracks().begin(), project.audioTracks().end(),
                  [this](const auto& track) { return track.id == selectedTrackId_; });
  auto targetTrack = selectedTrackId_;
  auto targetRegion = regionId_;
  if (vocal == nullptr && !audioSelected) {
    // The track the editor was on is gone (a harmony track that was undone, a track whose removal was
    // redone), or none was selected because the last one had been removed and has since come back.
    targetTrack = {};
    if (!project.vocalTracks().empty()) {
      targetTrack = project.vocalTracks().front().id;
    } else if (!project.audioTracks().empty()) {
      targetTrack = project.audioTracks().front().id;
    }
    vocal = project.findVocalTrack(targetTrack);
  }
  if (vocal == nullptr) {
    targetRegion = {};  // An audio track holds no vocal region.
  } else if (vocal->findRegion(targetRegion) == nullptr) {
    // The region is gone, or none was selected because the track's only region had been removed and
    // has since come back.
    targetRegion = vocal->regions.empty() ? domain::RegionId{} : vocal->regions.front().id;
  }

  const bool moved = targetTrack != selectedTrackId_ || targetRegion != regionId_;
  selectedTrackId_ = targetTrack;
  regionId_ = targetRegion;
  if (moved) leavePlace();
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(project, selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  // The host follows, so that its render, preview and technical edits name the place the editor is
  // now working in rather than one that no longer exists.
  if (moved) followSelectionOnHost();
  repaint();
}

void NativeEditorController::leavePlace() {
  seamTarget_.reset();
  unitTarget_.reset();
  seamPreviewAlternate_ = false;
  session_.selection().clear();
}

void NativeEditorController::followSelectionOnHost() {
  // A new move starts a new count of tries.
  hostSelectionTries_ = 1U;
  tellHostSelection();
}

void NativeEditorController::tellHostSelection() {
  core::Result<void> told = core::success();
  if (regionId_.valid()) {
    if (callbacks_.selectRegion) told = callbacks_.selectRegion(regionId_);
  } else if (session_.project().findVocalTrack(selectedTrackId_) != nullptr) {
    if (callbacks_.selectTrack) told = callbacks_.selectTrack(selectedTrackId_);
  } else if (callbacks_.clearVocalTarget) {
    // Nothing is left to sing: the editor rests on an audio track or on none, and the host lets go of
    // the vocal track and region it was following.
    told = callbacks_.clearVocalTarget();
  }
  if (told) {
    hostSelectionFollowed();
    return;
  }
  // The score is the authority: the edit that moved the editor stays, and the host is asked again
  // later. Until it has followed, the creator is told that what the host renders and previews may
  // not be where the editor is.
  hostSelectionRefused_ = true;
  auto notice = makeEditorNotice(kSelectionSyncFailedCode, "editor.selection-sync-failed",
                           told.error().message);
  // One notice for the one failure: a refusal in other words replaces it, the same refusal again
  // raises its count.
  const auto held = std::find_if(notices_.begin(), notices_.end(), [](const auto& value) {
    return value.code == kSelectionSyncFailedCode;
  });
  if (held != notices_.end() && !held->sameIssueAs(notice)) notices_.erase(held);
  raiseNotice(std::move(notice));
}

void NativeEditorController::hostSelectionFollowed() {
  hostSelectionRefused_ = false;
  hostSelectionTries_ = 0U;
  removeNoticesWithCode(kSelectionSyncFailedCode);
}

void NativeEditorController::retryHostSelectionBeforeInput() {
  if (!hostSelectionRefused_ || hostSelectionTries_ >= kHostSelectionTries) return;
  ++hostSelectionTries_;
  tellHostSelection();
}

core::Result<domain::TrackId> NativeEditorController::addVocalTrack(
    std::string name) {
  if (name.empty()) {
    return core::failure<domain::TrackId>(
        core::ErrorCode::InvalidArgument,
        "A vocal track name must not be empty");
  }
  const auto id = factory_.nextTrackId();
  auto result = session_.execute(
      std::make_unique<application::AddVocalTrackCommand>(
          domain::VocalTrack{.id = id, .name = std::move(name)}));
  if (!result) return core::Result<domain::TrackId>{result.error()};
  selectedTrackId_ = id;
  regionId_ = {};
  leavePlace();
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  followSelectionOnHost();
  markDocumentChanged();
  repaint();
  return core::success(id);
}

core::Result<domain::RegionId> NativeEditorController::addVocalRegion(
    std::string name, time::Tick start, time::Tick duration) {
  if (!selectedTrackId_.valid() ||
      session_.project().findVocalTrack(selectedTrackId_) == nullptr) {
    return core::failure<domain::RegionId>(
        core::ErrorCode::Conflict,
        "A vocal track must be selected before adding a region");
  }
  if (name.empty() || start < time::Tick{0} || duration <= time::Tick{0}) {
    return core::failure<domain::RegionId>(
        core::ErrorCode::InvalidArgument,
        "A vocal region requires a name and positive time range");
  }
  const auto id = factory_.nextRegionId();
  auto result = session_.execute(
      std::make_unique<application::AddVocalRegionCommand>(
          selectedTrackId_, domain::VocalRegion{
              .id = id,
              .name = std::move(name),
              .startTick = start,
              .durationTick = duration,
          }));
  if (!result) return core::Result<domain::RegionId>{result.error()};
  regionId_ = id;
  leavePlace();
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  followSelectionOnHost();
  markDocumentChanged();
  repaint();
  return core::success(id);
}

core::Result<domain::RegionId> NativeEditorController::addRegionToSelectedTrack() {
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (track == nullptr) {
    return core::failure<domain::RegionId>(core::ErrorCode::Conflict,
                                           "A vocal track must be selected first");
  }
  // After the track's last region, so a new region never lands on existing notes.
  time::Tick start{0};
  for (const auto& region : track->regions)
    start = std::max(start, region.startTick + region.durationTick);
  return addVocalRegion("Region " + std::to_string(track->regions.size() + 1U), start,
                        time::Tick{4 * 4 * time::kDefaultPpq});
}

core::Result<void> NativeEditorController::removeSelectedTrack() {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected");
  }
  std::unique_ptr<application::ICommand> command;
  if (session_.project().findVocalTrack(selectedTrackId_) != nullptr) {
    command = std::make_unique<application::RemoveVocalTrackCommand>(
        selectedTrackId_);
  } else {
    const auto audio = std::find_if(
        session_.project().audioTracks().begin(),
        session_.project().audioTracks().end(),
        [this](const auto& track) { return track.id == selectedTrackId_; });
    if (audio == session_.project().audioTracks().end()) {
      return core::failure(core::ErrorCode::NotFound,
                           "Selected track is missing");
    }
    command = std::make_unique<application::RemoveAudioTrackCommand>(
        selectedTrackId_);
  }
  auto result = session_.execute(std::move(command));
  if (!result) return result;
  // The track the editor was on is gone: it moves to one the score still has.
  reconcileWithProject();
  markDocumentChanged();
  return core::success();
}

core::Result<void> NativeEditorController::renameSelectedTrack(std::string name) {
  if (!selectedTrackId_.valid() || name.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Selected track and name are required");
  }
  std::unique_ptr<application::ICommand> command;
  if (session_.project().findVocalTrack(selectedTrackId_) != nullptr) {
    command = std::make_unique<application::RenameVocalTrackCommand>(
        selectedTrackId_, std::move(name));
  } else {
    command = std::make_unique<application::RenameAudioTrackCommand>(
        selectedTrackId_, std::move(name));
  }
  auto result = session_.execute(std::move(command));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::beginSelectedTrackRename() {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected for rename");
  }
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  std::string name;
  if (track != nullptr) {
    name = track->name;
  } else {
    const auto iterator = std::find_if(
        session_.project().audioTracks().begin(),
        session_.project().audioTracks().end(),
        [this](const auto& value) { return value.id == selectedTrackId_; });
    if (iterator == session_.project().audioTracks().end()) {
      return core::failure(core::ErrorCode::NotFound,
                           "Selected track is missing");
    }
    name = iterator->name;
  }
  const auto text = domain::fromUtf8(name);
  if (!text) return core::Result<void>{text.error()};
  if (composition_.active()) composition_.cancel();
  tempoEdit_.reset(); hintEdit_.reset(); replacementInput_.reset();
  auto begun = composition_.begin(externalTextTarget(), text.value());
  if (!begun) return begun;
  renameTrackTarget_ = selectedTrackId_;
  renameRegionTarget_.reset();
  batchLyricTarget_.reset();
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = externalTextTarget(),
        .logicalBounds = ui::Rect{logicalWidth_ - 300.0,
                                  layout_.toolbarHeight + layout_.trackListTop,
                                  260.0, layout_.trackRowHeight},
        .currentText = text.value(),
        .anchor = TextInputAnchor::ArrangementField,
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::reorderSelectedTrack(
    std::size_t destinationIndex) {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected");
  }
  std::unique_ptr<application::ICommand> command;
  if (session_.project().findVocalTrack(selectedTrackId_) != nullptr) {
    command = std::make_unique<application::MoveVocalTrackCommand>(
        selectedTrackId_, destinationIndex);
  } else {
    command = std::make_unique<application::MoveAudioTrackCommand>(
        selectedTrackId_, destinationIndex);
  }
  auto result = session_.execute(std::move(command));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::reorderSelectedTrackBy(
    int direction) {
  if (direction == 0) return core::success();
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected");
  }
  std::size_t index = 0U;
  std::size_t count = 0U;
  bool found = false;
  if (session_.project().findVocalTrack(selectedTrackId_) != nullptr) {
    const auto& tracks = session_.project().vocalTracks();
    count = tracks.size();
    const auto iterator = std::find_if(
        tracks.begin(), tracks.end(), [this](const auto& track) {
          return track.id == selectedTrackId_;
        });
    if (iterator != tracks.end()) {
      index = static_cast<std::size_t>(std::distance(tracks.begin(), iterator));
      found = true;
    }
  } else {
    const auto& tracks = session_.project().audioTracks();
    count = tracks.size();
    const auto iterator = std::find_if(
        tracks.begin(), tracks.end(), [this](const auto& track) {
          return track.id == selectedTrackId_;
        });
    if (iterator != tracks.end()) {
      index = static_cast<std::size_t>(std::distance(tracks.begin(), iterator));
      found = true;
    }
  }
  if (!found) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected track is missing");
  }
  if (direction < 0) {
    if (index == 0U) return core::success();
    return reorderSelectedTrack(index - 1U);
  }
  if (index + 1U >= count) return core::success();
  return reorderSelectedTrack(index + 1U);
}

core::Result<void> NativeEditorController::renameSelectedRegion(
    std::string name) {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  auto result = session_.execute(
      std::make_unique<application::RenameVocalRegionCommand>(
          selectedTrackId_, regionId_, std::move(name)));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::beginSelectedRegionRename() {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected for rename");
  }
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected region is missing");
  }
  const auto text = domain::fromUtf8(region->name);
  if (!text) return core::Result<void>{text.error()};
  if (composition_.active()) composition_.cancel();
  tempoEdit_.reset(); hintEdit_.reset(); replacementInput_.reset();
  auto begun = composition_.begin(externalTextTarget(), text.value());
  if (!begun) return begun;
  renameTrackTarget_.reset();
  renameRegionTarget_ = regionId_;
  batchLyricTarget_.reset();
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = externalTextTarget(),
        .logicalBounds = ui::Rect{logicalWidth_ - 300.0,
                                  layout_.toolbarHeight + layout_.trackListTop +
                                      layout_.trackRowAdvance,
                                  260.0, layout_.regionAdvance},
        .currentText = text.value(),
        .anchor = TextInputAnchor::ArrangementField,
    });
  }
  repaint();
  return core::success();
}

core::Result<domain::SeamOverride> NativeEditorController::selectedSeamValue()
    const {
  if (!seamTarget_.has_value()) {
    return core::failure<domain::SeamOverride>(
        core::ErrorCode::Conflict,
        "No seam boundary is selected");
  }
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) {
    return core::failure<domain::SeamOverride>(
        core::ErrorCode::NotFound,
        "Selected seam region is missing");
  }
  if (const auto* existing = region->findSeamOverride(*seamTarget_);
      existing != nullptr) {
    return core::success(*existing);
  }
  return core::success(domain::SeamOverride{
      .incomingStartKey = *seamTarget_,
      .seamAmount = 0.5F,
      .overlap = time::Microseconds{0},
      .phaseReset = 0.0F,
      .envelopeBlend = 0.0F,
      .curve = domain::SeamCurve::Smooth,
      .locked = true,
  });
}

core::Result<void> NativeEditorController::commitSeam(
    domain::SeamOverride value) {
  const auto result = session_.execute(
      std::make_unique<application::UpsertSeamOverrideCommand>(
          regionId_, std::move(value)));
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedSeamAmount(float value) {
  auto current = selectedSeamValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  updated.seamAmount = std::clamp(value, 0.0F, 1.0F);
  return commitSeam(std::move(updated));
}

core::Result<void> NativeEditorController::setSelectedSeamOverlap(
    time::Microseconds value) {
  auto current = selectedSeamValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  updated.overlap = std::clamp(value, time::Microseconds{0},
                               time::Microseconds{1'000'000});
  return commitSeam(std::move(updated));
}

core::Result<void> NativeEditorController::setSelectedSeamPhaseReset(
    float value) {
  auto current = selectedSeamValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  updated.phaseReset = std::clamp(value, 0.0F, 1.0F);
  return commitSeam(std::move(updated));
}

core::Result<void> NativeEditorController::setSelectedSeamEnvelopeBlend(
    float value) {
  auto current = selectedSeamValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  updated.envelopeBlend = std::clamp(value, 0.0F, 1.0F);
  return commitSeam(std::move(updated));
}

core::Result<void> NativeEditorController::cycleSelectedSeamCurve() {
  auto current = selectedSeamValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  switch (updated.curve) {
    case domain::SeamCurve::Smooth:
      updated.curve = domain::SeamCurve::Linear;
      break;
    case domain::SeamCurve::Linear:
      updated.curve = domain::SeamCurve::EqualPower;
      break;
    case domain::SeamCurve::EqualPower:
      updated.curve = domain::SeamCurve::HardCharacter;
      break;
    case domain::SeamCurve::HardCharacter:
      updated.curve = domain::SeamCurve::Smooth;
      break;
  }
  return commitSeam(std::move(updated));
}

core::Result<void> NativeEditorController::applySelectedSeamPreset(
    SeamPreset preset) {
  auto current = selectedSeamValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  switch (preset) {
    case SeamPreset::Clean:
      updated.seamAmount = 0.2F;
      updated.overlap = time::Microseconds{2'000};
      updated.phaseReset = 0.0F;
      updated.envelopeBlend = 0.75F;
      updated.curve = domain::SeamCurve::EqualPower;
      break;
    case SeamPreset::Character:
      updated.seamAmount = 0.85F;
      updated.overlap = time::Microseconds{0};
      updated.phaseReset = 1.0F;
      updated.envelopeBlend = 0.1F;
      updated.curve = domain::SeamCurve::HardCharacter;
      break;
    case SeamPreset::PhaseAligned:
      updated.seamAmount = 0.5F;
      updated.overlap = time::Microseconds{10'000};
      updated.phaseReset = 0.0F;
      updated.envelopeBlend = 0.5F;
      updated.curve = domain::SeamCurve::Smooth;
      break;
  }
  return commitSeam(std::move(updated));
}

core::Result<void> NativeEditorController::resetSelectedSeam() {
  if (!seamTarget_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No seam boundary is selected");
  }
  const auto result = session_.execute(
      std::make_unique<application::RemoveSeamOverrideCommand>(
          regionId_, *seamTarget_));
  if (result) {
    seamPreviewAlternate_ = false;
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::toggleSelectedSeamPreview() {
  if (!seamTarget_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No seam boundary is selected");
  }
  if (!callbacks_.previewSeam) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Seam B preview is not available in the plug-in: "
                         "the DAW plays the song's own render");
  }
  const auto next = !seamPreviewAlternate_;
  const auto result = callbacks_.previewSeam(*seamTarget_, next);
  if (result) {
    seamPreviewAlternate_ = next;
    repaint();
  }
  return result;
}

core::Result<domain::UnitSelectionOverride>
NativeEditorController::selectedUnitValue() const {
  if (!unitTarget_.has_value()) {
    return core::failure<domain::UnitSelectionOverride>(
        core::ErrorCode::Conflict,
        "No Unit is selected");
  }
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) {
    return core::failure<domain::UnitSelectionOverride>(
        core::ErrorCode::NotFound,
        "Selected Unit region is missing");
  }
  const auto* existing = region->findUnitSelectionOverride(*unitTarget_);
  if (existing == nullptr) {
    return core::failure<domain::UnitSelectionOverride>(
        core::ErrorCode::Conflict,
        "Cycle Unit variant before editing renderer controls");
  }
  return core::success(*existing);
}

core::Result<void> NativeEditorController::commitUnitSelection(
    domain::UnitSelectionOverride value) {
  const auto result = session_.execute(
      std::make_unique<application::UpsertUnitSelectionOverrideCommand>(
          regionId_, std::move(value)));
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedUnitLoopPrint(
    float value) {
  auto current = selectedUnitValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  updated.loopPrint = std::clamp(value, 0.0F, 1.0F);
  return commitUnitSelection(std::move(updated));
}

core::Result<void> NativeEditorController::setSelectedUnitSourcePitchResidual(
    float value) {
  auto current = selectedUnitValue();
  if (!current) return core::Result<void>{current.error()};
  auto updated = std::move(current).value();
  updated.sourcePitchResidual = std::clamp(value, 0.0F, 1.0F);
  return commitUnitSelection(std::move(updated));
}

core::Result<void> NativeEditorController::splitSelectedRegion(
    time::Tick splitTick) {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  auto command = std::make_unique<application::SplitVocalRegionCommand>(
      selectedTrackId_, regionId_, splitTick);
  auto* commandPtr = command.get();
  auto result = session_.execute(std::move(command));
  if (!result) return result;
  const auto right = commandPtr->splitRegionId();
  if (right.valid()) regionId_ = right;
  // Notes at or after the split moved to the new region, so a selection made before the split may name
  // notes in either half. It is dropped rather than guessed at.
  leavePlace();
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  followSelectionOnHost();
  markDocumentChanged();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::duplicateSelectedTrack() {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal track is selected");
  }
  auto command = std::make_unique<application::DuplicateVocalTrackCommand>(
      selectedTrackId_);
  auto* commandPtr = command.get();
  auto result = session_.execute(std::move(command));
  if (!result) return result;
  const auto duplicated = commandPtr->duplicatedTrackId();
  if (duplicated.valid()) {
    selectedTrackId_ = duplicated;
    const auto* track = session_.project().findVocalTrack(duplicated);
    regionId_ = track == nullptr || track->regions.empty()
                    ? domain::RegionId{}
                    : track->regions.front().id;
    pianoRoll_.setRegionId(regionId_);
  }
  leavePlace();
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  followSelectionOnHost();
  markDocumentChanged();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::duplicateSelectedRegion() {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  auto command = std::make_unique<application::DuplicateVocalRegionCommand>(
      selectedTrackId_, regionId_);
  auto* commandPtr = command.get();
  auto result = session_.execute(std::move(command));
  if (!result) return result;
  const auto duplicated = commandPtr->duplicatedRegionId();
  if (duplicated.valid()) regionId_ = duplicated;
  leavePlace();
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.setRegionId(regionId_);
  pianoRoll_.rebuildIndex();
  followSelectionOnHost();
  markDocumentChanged();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::copySelectedRegionToTrack(
    domain::TrackId targetTrackId) {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  if (session_.project().findVocalTrack(targetTrackId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Copy destination vocal track was not found");
  }
  auto command = std::make_unique<application::DuplicateVocalRegionCommand>(
      selectedTrackId_, targetTrackId, regionId_);
  auto* commandPtr = command.get();
  auto result = session_.execute(std::move(command));
  if (!result) return result;
  selectedTrackId_ = targetTrackId;
  regionId_ = commandPtr->duplicatedRegionId();
  leavePlace();
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  followSelectionOnHost();
  markDocumentChanged();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::deleteSelectedRegion() {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  auto result = session_.execute(
      std::make_unique<application::RemoveVocalRegionCommand>(
          selectedTrackId_, regionId_));
  if (!result) return result;
  // The region the editor was on is gone: it moves to the track's first region, or to none, and the
  // notes the region held are no longer selected.
  reconcileWithProject();
  markDocumentChanged();
  return core::success();
}

core::Result<void> NativeEditorController::moveSelectedRegion(
    time::Tick newStart) {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  auto result = session_.execute(
      std::make_unique<application::MoveVocalRegionCommand>(
          selectedTrackId_, regionId_, newStart));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::resizeSelectedRegion(
    time::Tick newDuration) {
  if (!selectedTrackId_.valid() || !regionId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No vocal region is selected");
  }
  auto result = session_.execute(
      std::make_unique<application::ResizeVocalRegionCommand>(
          selectedTrackId_, regionId_, newDuration));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedTrackMix(
    float gainDb, float pan, bool muted, bool solo) {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected for mix editing");
  }
  return setTrackMix(selectedTrackId_, gainDb, pan, muted, solo);
}

// Edits one track's mix without moving the editor's track, region or note selection, so a mixer
// strip can be changed while the editor stays on what the creator is working on.
core::Result<void> NativeEditorController::setTrackMix(
    domain::TrackId trackId, float gainDb, float pan, bool muted, bool solo) {
  if (!trackId.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Mix editing needs a track");
  }
  std::unique_ptr<application::ICommand> command;
  if (session_.project().findVocalTrack(trackId) != nullptr) {
    command = std::make_unique<application::SetVocalTrackMixCommand>(
        trackId, gainDb, pan, muted, solo);
  } else {
    const auto audio = std::find_if(
        session_.project().audioTracks().begin(),
        session_.project().audioTracks().end(),
        [trackId](const auto& track) { return track.id == trackId; });
    if (audio == session_.project().audioTracks().end()) {
      return core::failure(core::ErrorCode::NotFound,
                           "Mix track is missing");
    }
    command = std::make_unique<application::SetAudioTrackMixCommand>(
        trackId, gainDb, pan, muted, solo);
  }
  auto result = session_.execute(std::move(command));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedTrackVoicebank(
    domain::VoicebankReference voicebank) {
  if (!selectedTrackId_.valid() ||
      session_.project().findVocalTrack(selectedTrackId_) == nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "A vocal track must be selected for voicebank editing");
  }
  auto result = session_.execute(
      std::make_unique<application::SetTrackVoicebankCommand>(
          selectedTrackId_, std::move(voicebank)));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedTrackRoute(
    domain::TrackOutputRoute route) {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected for routing");
  }
  return setTrackRoute(selectedTrackId_, std::move(route));
}

// Routes one track without moving the editor's track, region or note selection.
core::Result<void> NativeEditorController::setTrackRoute(
    domain::TrackId trackId, domain::TrackOutputRoute route) {
  if (!trackId.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routing needs a track");
  }
  auto result = session_.execute(
      std::make_unique<application::SetTrackOutputRouteCommand>(
          trackId, std::move(route)));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
  repaint();
  return result;
}

// The host applies the channel count (a plug-in reconfigures its output ports from the project), so
// the edit is its command, not one executed here; a host without that choice refuses it.
core::Result<void> NativeEditorController::configureOutputChannels(std::uint8_t channels) {
  if (!callbacks_.configureOutputChannels) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Output channels follow the audio device settings");
  }
  auto result = callbacks_.configureOutputChannels(channels);
  if (result) arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  repaint();
  return result;
}

core::Result<void> NativeEditorController::cycleSelectedTrackRoute() {
  if (!selectedTrackId_.valid()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No track is selected for routing");
  }
  const auto& buses = session_.project().routing().buses;
  if (buses.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Project has no output buses");
  }
  const auto current = trackInspector().outputRoute.bus;
  const auto iterator = std::find_if(
      buses.begin(), buses.end(), [current](const auto& bus) {
        return bus.id == current;
      });
  const auto index = iterator == buses.end()
                         ? 0U
                         : (static_cast<std::size_t>(std::distance(buses.begin(), iterator)) +
                            1U) % buses.size();
  auto route = trackInspector().outputRoute;
  route.bus = buses[index].id;
  return setSelectedTrackRoute(std::move(route));
}

core::Result<domain::NoteId> NativeEditorController::duplicateSelectedNotes() {
  const auto result = pianoRoll_.duplicateSelection();
  if (result) {
    markDocumentChanged();
    repaint();
  }
  return result;
}

core::Result<void> NativeEditorController::quantizeSelectedNotes(
    time::Tick grid) {
  const auto result = pianoRoll_.quantizeSelection(grid);
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedNotesSlur(bool enabled) {
  const auto result = pianoRoll_.setSelectionSlur(enabled);
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setSelectedNotesMelisma() {
  const auto result = pianoRoll_.setSelectionMelisma();
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<ui::LyricDistributionReport>
NativeEditorController::distributeSelectedLyrics(std::u32string text,
                                                 std::optional<domain::Language> language) {
  const auto result = pianoRoll_.distributeSelectedLyrics(std::move(text), language);
  if (result && result.value().committed && result.value().changedLyrics > 0U) markDocumentChanged();
  repaint();
  return result;
}


}  // namespace seam::native_ui
