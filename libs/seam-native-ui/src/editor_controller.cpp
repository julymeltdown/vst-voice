#include "seam/native_ui/editor_controller.hpp"

#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"

#include "seam/application/lyric_commands.hpp"
#include "seam/application/arrangement_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/ui/phoneme_lane_model.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>

namespace seam::native_ui {

namespace {

domain::LyricTokenId externalTextTarget() noexcept {
  return domain::LyricTokenId{std::numeric_limits<std::uint64_t>::max()};
}

bool exportCancellable(authoring::ExportState state) noexcept {
  return state == authoring::ExportState::Preflight ||
         state == authoring::ExportState::Staging ||
         state == authoring::ExportState::Prepared;
}

}

NativeEditorController::NativeEditorController(
    application::EditorSession& session,
    application::ProjectFactory& factory,
    domain::RegionId regionId,
    EditorHostCallbacks callbacks)
    : session_(session),
      factory_(factory),
      regionId_(regionId),
      pianoRoll_(session, factory, regionId),
      callbacks_(std::move(callbacks)) {
  diagnosticPanel_.setActionHandler(
      [this](const authoring::Diagnostic& diagnostic,
             authoring::DiagnosticAction action) {
        if (!callbacks_.diagnosticAction) {
          return core::failure(core::ErrorCode::Unsupported,
                               "Diagnostic action is not connected");
        }
        return callbacks_.diagnosticAction(diagnostic, action);
      });
  const auto owner = std::find_if(
      session_.project().vocalTracks().begin(),
      session_.project().vocalTracks().end(),
      [regionId](const auto& track) {
        return track.findRegion(regionId) != nullptr;
      });
  selectedTrackId_ = owner == session_.project().vocalTracks().end()
                         ? domain::TrackId{}
                         : owner->id;
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  resize(logicalWidth_, logicalHeight_);
}

EditorSceneState NativeEditorController::sceneState() const {
  EditorSceneState state{
      .projectName = session_.project().name(),
      .revision = session_.revision(),
      .playing = playing_,
      .loopEnabled = loopEnabled_,
      .loopAvailable = callbacks_.toggleLoop != nullptr,
      .dirty = dirty_,
      .audioDeviceOnline = audioOnline_,
      .audioBackend = audioBackend_,
      .tempoBpm = session_.project().tempoMap().bpmAt(time::Tick{0}),
      .renderStatus = renderStatus_.view(),
      .logicalWidth = logicalWidth_,
      .logicalHeight = logicalHeight_,
      .boxSelection = std::nullopt,
      .lyricEditor = std::nullopt,
      .compositionPreview = {},
      .playheadPixel = playheadPixel_,
      .phonemes = {},
      .unitOverrides = {},
      .seamOverrides = {},
      .selectedSeam = seamTarget_,
      .seamPreviewAlternate = seamPreviewAlternate_,
      .pitchAutomation = {},
      .technicalLanes = session_.project().settings().technicalLanes,
      .characterMode = session_.project().settings().characterDisplay,
      .characterState = playing_ ? character::State::Focused
                                 : (dirty_ ? character::State::Warning
                                           : character::State::Neutral),
      .characterName = characterName_,
      .characterStyle = characterStyle_,
      .characterPortrait = characterPortrait_,
  };
  state.selectedNoteCount = session_.selection().noteIds().size();
  state.hoveredNote = hoveredNote_;
  state.arrangementTracks = arrangementPanel_.tracks();
  state.inspector = TrackInspectorModel::snapshot(session_.project(),
                                                   selectedTrackId_);
  state.voicebankBrowserVisible = voicebankBrowserVisible_;
  state.voicebankCards = voicebankCards_;
  state.audioSettings = audioSettings_;
  state.exportProgress = exportProgress_;
  state.lastExport = lastExport_;
  state.diagnostics.reserve(diagnosticPanel_.entries().size());
  for (const auto& entry : diagnosticPanel_.entries()) {
    state.diagnostics.push_back(entry.diagnostic);
  }
  if (const auto* track = session_.project().findVocalTrack(selectedTrackId_);
      track != nullptr) {
    const auto card = std::find_if(voicebankCards_.begin(), voicebankCards_.end(),
                                   [&track](const auto& candidate) {
      return candidate.id == track->voicebank.id &&
             candidate.version == track->voicebank.version &&
             candidate.contentHash == track->voicebank.contentHash;
    });
    state.voiceIdentity = resolveVoiceIdentity(VoiceIdentityInput{
        .reference = track->voicebank,
        .card = card == voicebankCards_.end() ? nullptr : &*card,
        .character = characterBinding_.has_value() ? &*characterBinding_ : nullptr,
        .renderStatus = state.renderStatus,
        .diagnostics = state.diagnostics,
        .focused = playing_,
        .completeDwell = std::chrono::steady_clock::now() < voiceCompleteUntil_,
    });
  }
  if (microscopeUnit_.has_value()) {
    state.sampleMicroscope = EditorSceneState::SampleMicroscopeView{
        .model = &microscope_,
        .unitId = microscopeUnitId_,
        .destinationContext = microscopeDestinationContext_,
        .canPlay = callbacks_.playMicroscopeSample != nullptr,
    };
  }
  if (dragMode_ == DragMode::BoxSelect) {
    const auto left = std::min(dragStart_.x, dragCurrent_.x);
    const auto top = std::min(dragStart_.y, dragCurrent_.y);
    state.boxSelection = ui::Rect{left, top,
                                  std::abs(dragCurrent_.x - dragStart_.x),
                                  std::abs(dragCurrent_.y - dragStart_.y)};
  }
  if (composition_.active()) {
    if (const auto* region = session_.project().findRegion(regionId_); region != nullptr) {
      const auto note = std::find_if(region->notes.begin(), region->notes.end(),
                                     [this](const domain::Note& candidate) {
                                       return candidate.lyricTokenId == composition_.lyricId();
                                     });
      if (note != region->notes.end()) {
        if (const auto bounds = noteWindowBounds(note->id); bounds.has_value()) {
          state.lyricEditor = *bounds;
        }
      }
      if (batchLyricTarget_ && !state.lyricEditor.has_value()) {
        const auto selected = session_.selection().noteIds();
        const auto first = std::find_if(
            selected.begin(), selected.end(), [region](domain::NoteId noteId) {
              return region->findNote(noteId) != nullptr;
            });
        if (first != selected.end()) state.lyricEditor = noteWindowBounds(*first);
      }
    }
    state.compositionPreview = domain::toUtf8(composition_.compositionText());
  }
  if (const auto* region = session_.project().findRegion(regionId_); region != nullptr) {
    phonemizer::JapaneseKanaPhonemizer phonemizer;
    state.phonemes = phonemizer.phonemize(*region);
    state.unitOverrides = region->unitSelectionOverrides;
    state.seamOverrides = region->seamOverrides;
    state.pitchAutomation = region->pitchAutomation.points();
  }
  if (const auto* focused = accessibilityTree_.focusedNode(); focused != nullptr) {
    state.focusedElementBounds = focused->bounds;
  }
  return state;
}

void NativeEditorController::setRenderStatus(RenderStatusView status) noexcept {
  const auto previous = renderStatus_.view().state;
  const auto completes = status.state == RenderStatusState::Ready &&
                         (previous == RenderStatusState::Queued ||
                          previous == RenderStatusState::Rendering);
  renderStatus_.update(std::move(status));
  voiceCompleteUntil_ = completes
                            ? std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds{1200}
                            : std::chrono::steady_clock::time_point{};
}

void NativeEditorController::rebuildAccessibilityTree() {
  accessibilityTree_.rebuild(sceneState(), pianoRoll_);
}

core::Result<void> NativeEditorController::dispatchAccessibility(
    std::string_view id, SemanticAction action) {
  return accessibilityTree_.dispatch(
      id, action,
      [this](std::string_view element, SemanticAction requested) {
        if (requested == SemanticAction::SetFocus) {
          const auto focused = accessibilityTree_.setFocus(element);
          if (!focused) return focused;
          repaint();
        }
        if (element == "microscope.close" &&
            requested == SemanticAction::Activate) {
          closeSampleMicroscope();
          return core::success();
        }
        if (element == "microscope.waveform" &&
            requested == SemanticAction::Activate) {
          if (!callbacks_.playMicroscopeSample || !microscopeUnit_.has_value()) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Microscope playback is not connected");
          }
          const auto played = callbacks_.playMicroscopeSample(
              *microscopeUnit_, microscopeAudio_);
          repaint();
          return played;
        }
        if (element == "toolbar.transport" &&
            (requested == SemanticAction::Activate ||
             requested == SemanticAction::Toggle)) {
          if (!renderStatus_.view().hasAudibleAudio) {
            return core::failure(core::ErrorCode::Conflict,
                                 "Transport has no audible render");
          }
          const auto requestedPlaying = !playing_;
          if (callbacks_.setPlaying) {
            const auto result = callbacks_.setPlaying(requestedPlaying);
            if (!result) {
              repaint();
              return result;
            }
          }
          playing_ = requestedPlaying;
          repaint();
          return core::success();
        }
        if (element == "toolbar.stop" &&
            requested == SemanticAction::Activate) {
          if (callbacks_.stopPlaying) {
            const auto result = callbacks_.stopPlaying();
            if (!result) {
              repaint();
              return result;
            }
          }
          playing_ = false;
          repaint();
          return core::success();
        }
        if (element == "toolbar.loop" &&
            (requested == SemanticAction::Activate ||
             requested == SemanticAction::Toggle)) {
          if (!callbacks_.toggleLoop) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Loop transport is not connected");
          }
          const auto result = callbacks_.toggleLoop();
          if (result) loopEnabled_ = !loopEnabled_;
          repaint();
          return result;
        }
        if (element == "toolbar.batch-lyrics" &&
            requested == SemanticAction::Activate) {
          return beginBatchLyricEdit();
        }
        if (element == "toolbar.tempo" &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if ((element == "audio.settings" || element == "audio.diagnostics" ||
             element == "audio.sample-rate" ||
             element == "audio.block-frames" ||
             element == "audio.channels" ||
             element.rfind("audio.device.", 0U) == 0U) &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element.rfind("audio.device.", 0U) == 0U &&
            requested == SemanticAction::Activate) {
          const auto indexStart = std::string_view{"audio.device."}.size();
          std::size_t index = 0U;
          const auto parsed = std::from_chars(
              element.data() + indexStart, element.data() + element.size(), index);
          if (parsed.ec != std::errc{} ||
              parsed.ptr != element.data() + element.size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Audio device accessibility index is invalid");
          }
          return selectAudioDevice(index);
        }
        if (element == "audio.sample-rate" &&
            requested == SemanticAction::Activate) {
          return cycleAudioSettings(AudioSettingsField::SampleRate, 1);
        }
        if (element == "audio.block-frames" &&
            requested == SemanticAction::Activate) {
          return cycleAudioSettings(AudioSettingsField::BlockFrames, 1);
        }
        if (element == "audio.channels" &&
            requested == SemanticAction::Activate) {
          return cycleAudioSettings(AudioSettingsField::Channels, 1);
        }
        if (element == "inspector.mute" || element == "inspector.solo") {
          const auto snapshot = trackInspector();
          if (!snapshot.valid || requested == SemanticAction::SetFocus) {
            return core::success();
          }
          return setSelectedTrackMix(
              snapshot.gainDb, snapshot.pan,
              element == "inspector.mute" ? !snapshot.muted : snapshot.muted,
              element == "inspector.solo" ? !snapshot.solo : snapshot.solo);
        }
        if (element == "inspector.route" &&
            requested == SemanticAction::Activate) {
          return cycleSelectedTrackRoute();
        }
        if (requested == SemanticAction::SetFocus &&
            (element == "arrangement.add-track" ||
             element == "arrangement.add-region" ||
             element == "arrangement.rename" ||
             element == "arrangement.move-up" ||
             element == "arrangement.move-down")) {
          return core::success();
        }
        if (element == "arrangement.add-track") {
          auto added = addVocalTrack(
              "Voice " + std::to_string(
                  session_.project().vocalTracks().size() + 1U));
          return added ? core::success() : core::Result<void>{added.error()};
        }
        if (element == "arrangement.add-region") {
          const auto* track = session_.project().findVocalTrack(selectedTrackId_);
          if (track == nullptr) {
            return core::failure(core::ErrorCode::Conflict,
                                 "A vocal track must be selected first");
          }
          auto added = addVocalRegion(
              "Region " + std::to_string(track->regions.size() + 1U),
              time::Tick{0}, time::Tick{15360});
          return added ? core::success() : core::Result<void>{added.error()};
        }
        if (element == "arrangement.rename") {
          return regionId_.valid() ? beginSelectedRegionRename()
                                   : beginSelectedTrackRename();
        }
        if (element == "arrangement.move-up") {
          return reorderSelectedTrackBy(-1);
        }
        if (element == "arrangement.move-down") {
          return reorderSelectedTrackBy(1);
        }
        if (element.rfind("voicebank.card.", 0U) == 0U &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element.rfind("voicebank.card.", 0U) == 0U &&
            requested == SemanticAction::Activate) {
          const auto indexStart = std::string_view{"voicebank.card."}.size();
          std::size_t index = 0U;
          const auto parsed = std::from_chars(
              element.data() + indexStart, element.data() + element.size(), index);
          if (parsed.ec != std::errc{} ||
              parsed.ptr != element.data() + element.size() ||
              index >= voicebankCards_.size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Voicebank accessibility index is invalid");
          }
          const auto& card = voicebankCards_[index];
          if (!card.selectable) {
            return core::failure(core::ErrorCode::Conflict,
                                 "Selected voicebank is not trusted");
          }
          if (!callbacks_.selectVoicebank) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Voicebank selection is not connected");
          }
          const auto selected = callbacks_.selectVoicebank(
              card.id, card.version, card.contentHash);
          if (selected) voicebankBrowserVisible_ = false;
          repaint();
          return selected;
        }
        if (element.rfind("arrangement.track.", 0U) == 0U) {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Arrangement track only supports activation");
          }
          const auto suffix = element.substr(std::string_view{"arrangement.track."}.size());
          for (const auto& track : arrangementPanel_.tracks()) {
            if (track.id.toString() == suffix) return selectTrack(track.id);
          }
        }
        if (element.rfind("arrangement.region.", 0U) == 0U) {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Arrangement region only supports activation");
          }
          const auto suffix = element.substr(std::string_view{"arrangement.region."}.size());
          for (const auto& track : arrangementPanel_.tracks()) {
            for (const auto& region : track.regions) {
              if (region.id.toString() == suffix) return selectRegion(region.id);
            }
          }
        }
        if (element.rfind("note.", 0U) == 0U) {
          const auto suffix = element.substr(std::string_view{"note."}.size());
          std::uint64_t rawId = 0U;
          const auto parsed = std::from_chars(
              suffix.data(), suffix.data() + suffix.size(), rawId, 16);
          const auto noteId = domain::NoteId{rawId};
          const auto* selectedRegion = session_.project().findRegion(regionId_);
          if (parsed.ec == std::errc{} &&
              parsed.ptr == suffix.data() + suffix.size() &&
              selectedRegion != nullptr && selectedRegion->findNote(noteId) != nullptr) {
            if (requested == SemanticAction::SetFocus) return core::success();
            if (requested != SemanticAction::Activate &&
                requested != SemanticAction::EditText) {
              return core::failure(core::ErrorCode::Unsupported,
                                   "Note only supports activation or lyric editing");
            }
            session_.selection().selectOnly(noteId);
            if (requested == SemanticAction::EditText) {
              return beginLyricEdit(noteId);
            }
            repaint();
            return core::success();
          }
        }
        if ((element == "diagnostics.panel" || element == "export.progress") &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element == "export.cancel") {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Export cancellation only supports activation");
          }
          if (!exportCancellable(exportProgress_.state)) {
            return core::failure(core::ErrorCode::Conflict,
                                 "Export is no longer cancellable");
          }
          if (!callbacks_.cancelExport) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Export cancellation is not connected");
          }
          callbacks_.cancelExport();
          repaint();
          return core::success();
        }
        constexpr auto diagnosticActionPrefix =
            std::string_view{"diagnostic-action."};
        if (element.starts_with(diagnosticActionPrefix)) {
          const auto indexStart = diagnosticActionPrefix.size();
          const auto indexEnd = element.find('.', indexStart);
          if (indexEnd == std::string_view::npos) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Diagnostic action accessibility id is malformed");
          }
          std::size_t index = 0U;
          const auto parsed = std::from_chars(
              element.data() + indexStart, element.data() + indexEnd, index);
          if (parsed.ec != std::errc{} ||
              parsed.ptr != element.data() + indexEnd ||
              index >= diagnosticPanel_.entries().size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Diagnostic action accessibility index is invalid");
          }
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Diagnostic action only supports activation");
          }
          const auto actionName = element.substr(indexEnd + 1U);
          const auto& actions = diagnosticPanel_.entries()[index].diagnostic.actions;
          const auto action = std::find_if(
              actions.begin(), actions.end(), [actionName](const auto candidate) {
                return authoring::toString(candidate) == actionName;
              });
          if (action == actions.end()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Diagnostic action is not available");
          }
          return activateDiagnostic(index, *action);
        }
        if (element.rfind("diagnostic.", 0U) == 0U &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element.rfind("diagnostic.", 0U) == 0U &&
            requested == SemanticAction::Activate) {
          const auto indexStart = std::string_view{"diagnostic."}.size();
          const auto indexEnd = element.find('.', indexStart);
          if (indexEnd == std::string_view::npos) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Diagnostic accessibility id is malformed");
          }
          std::size_t index = 0U;
          const auto parsed = std::from_chars(
              element.data() + indexStart, element.data() + indexEnd, index);
          if (parsed.ec != std::errc{} || parsed.ptr != element.data() + indexEnd ||
              index >= diagnosticPanel_.entries().size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Diagnostic accessibility index is invalid");
          }
          const auto& actions =
              diagnosticPanel_.entries()[index].diagnostic.actions;
          if (actions.empty()) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Diagnostic has no recovery action");
          }
          return activateDiagnostic(index, actions.front());
        }
        if (requested == SemanticAction::SetFocus) {
          return core::success();
        }
        return core::failure(core::ErrorCode::Unsupported,
                             "Accessibility action is not implemented");
      });
}

core::Result<void> NativeEditorController::setAccessibilityValue(
    std::string_view id, std::string_view value) {
  constexpr auto prefix = std::string_view{"note."};
  if (!id.starts_with(prefix)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Accessibility value target is not a note");
  }
  if (value.size() > 4096U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Accessibility lyric value is too large");
  }
  const auto suffix = id.substr(prefix.size());
  std::uint64_t rawId = 0U;
  const auto parsed = std::from_chars(
      suffix.data(), suffix.data() + suffix.size(), rawId, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Accessibility note value target is malformed");
  }
  const auto noteId = domain::NoteId{rawId};
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || region->findNote(noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Accessibility note value target is unavailable");
  }
  const auto decoded = domain::fromUtf8(std::string{value});
  if (!decoded) return core::Result<void>{decoded.error()};
  session_.selection().selectOnly(noteId);
  const auto begun = beginLyricEdit(noteId);
  if (!begun) return begun;
  return commitTextComposition(decoded.value());
}

core::Result<void> NativeEditorController::rebuildSampleMicroscope() {
  if (!microscopeUnit_.has_value() || microscopeAudio_.frameCount() == 0U) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Sample microscope source is unavailable");
  }
  return microscope_.rebuild(
      *microscopeUnit_, microscopeAudio_,
      layout_.microscopeWaveformBounds(logicalWidth_, logicalHeight_),
      layout_.microscopeSpectrogramBounds(logicalWidth_, logicalHeight_),
      1200U);
}

core::Result<void> NativeEditorController::openSampleMicroscope(
    domain::PhonemeKey key) {
  if (!callbacks_.loadSampleMicroscope) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Sample microscope source is not connected");
  }
  auto loaded = callbacks_.loadSampleMicroscope(key);
  if (!loaded) return core::Result<void>{loaded.error()};
  auto data = std::move(loaded).value();
  if (data.audio.frameCount() == 0U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Sample microscope source contains no audio");
  }
  microscopeUnit_ = std::move(data.unit);
  microscopeAudio_ = std::move(data.audio);
  microscopeUnitId_ = microscopeUnit_->id;
  microscopeDestinationContext_ = std::move(data.destinationContext);
  microscopeKey_ = key;
  const auto rebuilt = rebuildSampleMicroscope();
  if (!rebuilt) {
    closeSampleMicroscope();
    return rebuilt;
  }
  repaint();
  return core::success();
}

void NativeEditorController::closeSampleMicroscope() noexcept {
  dragMode_ = DragMode::None;
  dragMicroscopeMarker_.reset();
  dragMicroscopePitchMark_.reset();
  microscopeKey_.reset();
  microscopeUnit_.reset();
  microscopeAudio_ = {};
  microscopeUnitId_.clear();
  microscopeDestinationContext_.clear();
  repaint();
}

void NativeEditorController::setDiagnostics(
    std::vector<authoring::Diagnostic> diagnostics) {
  diagnosticPanel_.clear();
  for (auto& diagnostic : diagnostics) diagnosticPanel_.add(std::move(diagnostic));
  repaint();
}

core::Result<void> NativeEditorController::activateDiagnostic(
    std::size_t index, authoring::DiagnosticAction action) const {
  return diagnosticPanel_.activate(index, action);
}

void NativeEditorController::dismissDiagnostic(std::size_t index) {
  diagnosticPanel_.dismiss(index);
  repaint();
}

void NativeEditorController::resize(double logicalWidth,
                                    double logicalHeight) noexcept {
  logicalWidth_ = std::max(480.0, logicalWidth);
  logicalHeight_ = std::max(320.0, logicalHeight);
  const auto contentHeight = layout_.pianoContentHeight(logicalHeight_);
  pianoRoll_.setViewport(ui::PianoRollViewport{
      .bounds = ui::Rect{0.0, 0.0, logicalWidth_, contentHeight},
      .keyboardWidth = layout_.keyboardWidth,
  });
  pianoRoll_.rebuildIndex();
  if (microscopeUnit_.has_value()) {
    static_cast<void>(rebuildSampleMicroscope());
  }
  repaint();
}

core::Result<void> NativeEditorController::selectTrack(domain::TrackId trackId) {
  auto selected = arrangementPanel_.selectTrack(session_.project(), trackId);
  if (!selected) return selected;
  selectedTrackId_ = arrangementPanel_.selectedTrack();
  regionId_ = arrangementPanel_.selectedRegion();
  seamTarget_.reset();
  unitTarget_.reset();
  seamPreviewAlternate_ = false;
  pianoRoll_.setRegionId(regionId_);
  session_.selection().clear();
  pianoRoll_.rebuildIndex();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::selectRegion(domain::RegionId regionId) {
  auto selected = arrangementPanel_.selectRegion(session_.project(), regionId);
  if (!selected) return selected;
  selectedTrackId_ = arrangementPanel_.selectedTrack();
  regionId_ = arrangementPanel_.selectedRegion();
  seamTarget_.reset();
  unitTarget_.reset();
  seamPreviewAlternate_ = false;
  pianoRoll_.setRegionId(regionId_);
  session_.selection().clear();
  pianoRoll_.rebuildIndex();
  repaint();
  return core::success();
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
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
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
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  markDocumentChanged();
  repaint();
  return core::success(id);
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
  selectedTrackId_ = {};
  regionId_ = {};
  if (!session_.project().vocalTracks().empty()) {
    selectedTrackId_ = session_.project().vocalTracks().front().id;
    if (!session_.project().vocalTracks().front().regions.empty()) {
      regionId_ = session_.project().vocalTracks().front().regions.front().id;
    }
  } else if (!session_.project().audioTracks().empty()) {
    selectedTrackId_ = session_.project().audioTracks().front().id;
  }
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  session_.selection().clear();
  markDocumentChanged();
  repaint();
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
  auto begun = composition_.begin(externalTextTarget(), text.value());
  if (!begun) return begun;
  renameTrackTarget_ = selectedTrackId_;
  renameRegionTarget_.reset();
  batchLyricTarget_ = false;
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = externalTextTarget(),
        .logicalBounds = ui::Rect{logicalWidth_ - 300.0,
                                  layout_.toolbarHeight + layout_.trackListTop,
                                  260.0, layout_.trackRowHeight},
        .currentText = text.value(),
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
  auto begun = composition_.begin(externalTextTarget(), text.value());
  if (!begun) return begun;
  renameTrackTarget_.reset();
  renameRegionTarget_ = regionId_;
  batchLyricTarget_ = false;
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = externalTextTarget(),
        .logicalBounds = ui::Rect{logicalWidth_ - 300.0,
                                  layout_.toolbarHeight + layout_.trackListTop +
                                      layout_.trackRowAdvance,
                                  260.0, layout_.regionAdvance},
        .currentText = text.value(),
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
                         "Seam A/B preview is not connected");
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
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
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
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
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
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.setRegionId(regionId_);
  pianoRoll_.rebuildIndex();
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
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
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
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  regionId_ = track == nullptr || track->regions.empty()
                  ? domain::RegionId{}
                  : track->regions.front().id;
  pianoRoll_.setRegionId(regionId_);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  pianoRoll_.rebuildIndex();
  markDocumentChanged();
  repaint();
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
  std::unique_ptr<application::ICommand> command;
  if (session_.project().findVocalTrack(selectedTrackId_) != nullptr) {
    command = std::make_unique<application::SetVocalTrackMixCommand>(
        selectedTrackId_, gainDb, pan, muted, solo);
  } else {
    const auto audio = std::find_if(
        session_.project().audioTracks().begin(),
        session_.project().audioTracks().end(),
        [this](const auto& track) { return track.id == selectedTrackId_; });
    if (audio == session_.project().audioTracks().end()) {
      return core::failure(core::ErrorCode::NotFound,
                           "Selected track is missing");
    }
    command = std::make_unique<application::SetAudioTrackMixCommand>(
        selectedTrackId_, gainDb, pan, muted, solo);
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
  auto result = session_.execute(
      std::make_unique<application::SetTrackOutputRouteCommand>(
          selectedTrackId_, std::move(route)));
  if (result) {
    arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
    markDocumentChanged();
  }
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
                                                 domain::Language language) {
  const auto result = pianoRoll_.distributeSelectedLyrics(std::move(text), language);
  if (result && result.value().committed) markDocumentChanged();
  repaint();
  return result;
}

ui::Point NativeEditorController::modelPoint(ui::Point windowPoint) const noexcept {
  return ui::Point{windowPoint.x, windowPoint.y - layout_.contentTop()};
}

void NativeEditorController::repaint() const {
  if (callbacks_.requestRepaint) callbacks_.requestRepaint();
}

void NativeEditorController::finishTextInput() const {
  if (callbacks_.endTextInput) callbacks_.endTextInput();
}

void NativeEditorController::markDocumentChanged() {
  dirty_ = true;
  if (callbacks_.documentChanged) callbacks_.documentChanged();
}

core::Result<void> NativeEditorController::beginBatchLyricEdit() {
  if (session_.selection().empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Select notes before distributing lyrics");
  }
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected lyric region is missing");
  }
  const auto selected = session_.selection().noteIds();
  const auto first = std::find_if(
      selected.begin(), selected.end(),
      [region](domain::NoteId noteId) { return region->findNote(noteId) != nullptr; });
  if (first == selected.end()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Select notes in the active region before distributing lyrics");
  }
  if (composition_.active()) composition_.cancel();
  auto begun = composition_.begin(externalTextTarget(), {});
  if (!begun) return begun;
  batchLyricTarget_ = true;
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = externalTextTarget(),
        .logicalBounds = noteWindowBounds(*first).value_or(
            ui::Rect{layout_.keyboardWidth, layout_.contentTop(), 240.0, 30.0}),
        .currentText = {},
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerDown(
    const PointerEvent& event) {
  if (sampleMicroscopeOpen()) {
    if (event.button == PointerButton::Right) {
      closeSampleMicroscope();
      return core::success();
    }
    if (event.button != PointerButton::Left) return core::success();
    if (event.clickCount >= 2) {
      if (callbacks_.playMicroscopeSample && microscopeUnit_.has_value()) {
        const auto played = callbacks_.playMicroscopeSample(
            *microscopeUnit_, microscopeAudio_);
        repaint();
        return played;
      }
      closeSampleMicroscope();
      return core::success();
    }
    if (const auto pitchMark = microscope_.hitTestPitchMark(event.position);
        pitchMark.has_value()) {
      if (!callbacks_.microscopeUnitChanged) {
        return core::failure(core::ErrorCode::Unsupported,
                             "Microscope pitch-mark editing is not connected");
      }
      dragMode_ = DragMode::MicroscopePitchMark;
      dragMicroscopePitchMark_ = pitchMark;
      dragStart_ = event.position;
      dragCurrent_ = event.position;
      repaint();
      return core::success();
    }
    if (const auto marker = microscope_.hitTestMarker(event.position);
        marker.has_value()) {
      if (!callbacks_.microscopeUnitChanged) {
        return core::failure(core::ErrorCode::Unsupported,
                             "Microscope marker editing is not connected");
      }
      dragMode_ = DragMode::MicroscopeMarker;
      dragMicroscopeMarker_ = marker;
      dragStart_ = event.position;
      dragCurrent_ = event.position;
      repaint();
      return core::success();
    }
    return core::success();
  }
  if (event.button != PointerButton::Left) return core::success();
  const auto diagnosticsBounds = layout_.diagnosticBounds(
      logicalWidth_, logicalHeight_, exportProgress_.totalFiles != 0U);
  if (diagnosticsBounds.contains(event.position) &&
      !diagnosticPanel_.entries().empty()) {
    const auto& diagnostic = diagnosticPanel_.entries().front().diagnostic;
    const auto presentation = presentDiagnostic(diagnostic);
    const auto actionCount = std::min<std::size_t>(
        2U, presentation.primaryActionKinds.size());
    for (std::size_t index = 0U; index < actionCount; ++index) {
      const auto bounds = layout_.diagnosticActionBounds(
          logicalWidth_, logicalHeight_, exportProgress_.totalFiles != 0U,
          actionCount, index);
      if (bounds.contains(event.position)) {
        return activateDiagnostic(0U, presentation.primaryActionKinds[index]);
      }
    }
    return core::success();
  }
  if (exportCancellable(exportProgress_.state) &&
      exportProgress_.totalFiles != 0U &&
      layout_.exportCancelBounds(logicalWidth_, logicalHeight_)
          .contains(event.position)) {
    if (!callbacks_.cancelExport) {
      return core::failure(core::ErrorCode::Unsupported,
                           "Export cancellation is not connected");
    }
    callbacks_.cancelExport();
    repaint();
    return core::success();
  }
  if (!voicebankBrowserVisible_ && !audioSettings_.visible &&
      session_.project().settings().characterDisplay ==
          domain::CharacterDisplayMode::Off &&
      !arrangementPanel_.tracks().empty()) {
    const auto panelX = std::max(
        layout_.keyboardWidth + layout_.minimumTimelineWidth,
        logicalWidth_ - layout_.characterDockWidth);
    if (event.position.x >= panelX &&
        event.position.y >= layout_.toolbarHeight &&
        event.position.y < layout_.pianoBottom(logicalHeight_)) {
      for (std::size_t index = 0U; index < 5U; ++index) {
        const auto actionBounds =
            layout_.arrangementActionBoundsForWidth(logicalWidth_, index);
        if (!actionBounds.contains(event.position)) continue;
        if (index == 0U) {
          auto added = addVocalTrack(
              "Voice " + std::to_string(
                  session_.project().vocalTracks().size() + 1U));
          if (!added) return core::Result<void>{added.error()};
          return core::success();
        }
        if (index == 1U) {
          const auto* track =
              session_.project().findVocalTrack(selectedTrackId_);
          if (track == nullptr) {
            return core::failure(core::ErrorCode::Conflict,
                                 "A vocal track must be selected first");
          }
          auto added = addVocalRegion(
              "Region " + std::to_string(track->regions.size() + 1U),
              time::Tick{0}, time::Tick{15360});
          if (!added) return core::Result<void>{added.error()};
          return core::success();
        }
        if (index == 2U) {
          return regionId_.valid() ? beginSelectedRegionRename()
                                   : beginSelectedTrackRename();
        }
        return reorderSelectedTrackBy(index == 3U ? -1 : 1);
      }
      double y = layout_.toolbarHeight + layout_.trackListTop;
      for (const auto& track : arrangementPanel_.tracks()) {
        if (event.position.y >= y + layout_.trackRowTopOffset &&
            event.position.y < y + layout_.trackRowHeight -
                                    layout_.regionBottomPadding) {
          if (event.clickCount >= 2) return beginSelectedTrackRename();
          return selectTrack(track.id);
        }
        y += layout_.trackRowAdvance;
        for (const auto& region : track.regions) {
          if (event.position.y >= y &&
              event.position.y < y + layout_.regionAdvance -
                                      layout_.regionBottomPadding) {
            if (event.clickCount >= 2) return beginSelectedRegionRename();
            return selectRegion(region.id);
          }
          y += layout_.regionAdvance;
        }
      }
      const auto inspector = trackInspector();
      if (inspector.valid) {
        const auto inspectorTop = std::max(
            layout_.toolbarHeight,
            layout_.pianoBottom(logicalHeight_) - layout_.inspectorHeight);
        const auto firstFieldBaseline =
            inspectorTop + layout_.inspectorNameBaseline +
            layout_.inspectorNameToFirstFieldAdvance;
        const auto toggleTop = firstFieldBaseline +
                               layout_.inspectorFieldAdvance * 3.0 -
                               layout_.inspectorFontSize;
        const auto toggleWidth = std::max(
            1.0, (logicalWidth_ - panelX - layout_.inspectorTextInsetX * 2.0) *
                     0.5);
        const ui::Rect muteBounds{panelX + layout_.inspectorTextInsetX,
                                  toggleTop, toggleWidth,
                                  layout_.inspectorFieldAdvance};
        const ui::Rect soloBounds{
            muteBounds.right(), toggleTop, toggleWidth,
            layout_.inspectorFieldAdvance};
        const ui::Rect routeBounds{
            panelX + layout_.inspectorTextInsetX,
            firstFieldBaseline + layout_.inspectorFieldAdvance * 4.0 -
                layout_.inspectorFontSize,
            std::max(1.0, logicalWidth_ - panelX -
                              layout_.inspectorTextInsetX * 2.0),
            layout_.inspectorFieldAdvance};
        if (muteBounds.contains(event.position)) {
          return setSelectedTrackMix(inspector.gainDb, inspector.pan,
                                     !inspector.muted, inspector.solo);
        }
        if (soloBounds.contains(event.position)) {
          return setSelectedTrackMix(inspector.gainDb, inspector.pan,
                                     inspector.muted, !inspector.solo);
        }
        if (routeBounds.contains(event.position)) {
          return cycleSelectedTrackRoute();
        }
      }
    }
  }
  if (audioSettings_.visible) {
    const auto panelX = std::max(
        layout_.keyboardWidth + layout_.minimumTimelineWidth,
        logicalWidth_ - layout_.characterDockWidth);
    if (event.position.x >= panelX &&
        event.position.y >= layout_.toolbarHeight &&
        event.position.y < layout_.pianoBottom(logicalHeight_)) {
      const auto rowX = panelX + layout_.audioSettingsInsetX;
      const auto rowWidth = std::max(
          1.0, logicalWidth_ - panelX - layout_.audioSettingsInsetX * 2.0);
      const auto rowY = layout_.toolbarHeight + layout_.audioSettingsRowTop;
      for (std::size_t index = 0U; index < audioSettings_.devices.size(); ++index) {
        const ui::Rect row{
            rowX,
            rowY + static_cast<double>(index) *
                       (layout_.audioSettingsRowHeight +
                        layout_.audioSettingsRowGap),
            rowWidth,
            layout_.audioSettingsRowHeight,
        };
        if (!row.contains(event.position)) continue;
        return selectAudioDevice(index);
      }
      const auto fieldsTop = rowY +
                             static_cast<double>(audioSettings_.devices.size()) *
                                 (layout_.audioSettingsRowHeight +
                                  layout_.audioSettingsRowGap);
      const auto fieldIndex = static_cast<int>(
          (event.position.y - fieldsTop) /
          (layout_.audioSettingsRowHeight + layout_.audioSettingsRowGap));
      if (fieldIndex >= 0 && fieldIndex < 3) {
        return cycleAudioSettings(
            static_cast<AudioSettingsField>(fieldIndex), 1);
      }
    }
  }
  if (voicebankBrowserVisible_) {
    const auto panelX = std::max(
        layout_.keyboardWidth + layout_.minimumTimelineWidth,
        logicalWidth_ - layout_.characterDockWidth);
    if (event.position.x >= panelX &&
        event.position.y >= layout_.toolbarHeight &&
        event.position.y < layout_.pianoBottom(logicalHeight_)) {
      const auto cardX = panelX + layout_.voicebankCardInsetX;
      const auto cardWidth = std::max(
          1.0, logicalWidth_ - panelX - layout_.voicebankCardInsetX * 2.0);
      for (std::size_t index = 0U; index < voicebankCards_.size(); ++index) {
        const ui::Rect cardBounds{
            cardX,
            layout_.toolbarHeight + layout_.voicebankCardTop +
                static_cast<double>(index) *
                    (layout_.voicebankCardHeight + layout_.voicebankCardGap),
            cardWidth,
            layout_.voicebankCardHeight,
        };
        if (!cardBounds.contains(event.position)) continue;
        const auto& card = voicebankCards_[index];
        if (!card.selectable) {
          return core::failure(core::ErrorCode::Conflict,
                               "Selected voicebank is not trusted");
        }
        if (!callbacks_.selectVoicebank) {
          return core::failure(core::ErrorCode::Unsupported,
                               "Voicebank selection is not connected");
        }
        const auto selected = callbacks_.selectVoicebank(
            card.id, card.version, card.contentHash);
        if (selected) voicebankBrowserVisible_ = false;
        repaint();
        return selected;
      }
    }
  }
  if (event.position.y >= layout_.toolbarControlTop &&
      event.position.y < layout_.toolbarControlTop +
                              layout_.toolbarControlHeight) {
    const auto transportBounds =
        layout_.transportBoundsForWidth(logicalWidth_);
    if (transportBounds.contains(event.position)) {
      if (!renderStatus_.view().hasAudibleAudio) {
        repaint();
        return core::success();
      }
      const auto requestedPlaying = !playing_;
      if (callbacks_.setPlaying) {
        const auto result = callbacks_.setPlaying(requestedPlaying);
        if (!result) {
          repaint();
          return result;
        }
      }
      playing_ = requestedPlaying;
      repaint();
      return core::success();
    }
    const auto stopBounds = layout_.stopBoundsForWidth(logicalWidth_);
    if (stopBounds.contains(event.position)) {
      if (callbacks_.stopPlaying) {
        const auto result = callbacks_.stopPlaying();
        if (!result) {
          repaint();
          return result;
        }
      }
      playing_ = false;
      repaint();
      return core::success();
    }
    const auto portraitVisible =
        !layout_.compactToolbar(logicalWidth_) &&
        session_.project().settings().characterDisplay ==
            domain::CharacterDisplayMode::Minimal &&
        sceneState().voiceIdentity.characterActive &&
        characterPortrait_ != nullptr;
    const auto batchLyricsBounds =
        layout_.batchLyricsBoundsForWidth(logicalWidth_, portraitVisible);
    if (batchLyricsBounds.width > 0.0 &&
        batchLyricsBounds.contains(event.position)) {
      return beginBatchLyricEdit();
    }
    const auto loopBounds =
        layout_.loopBoundsForWidth(logicalWidth_, portraitVisible);
    if (callbacks_.toggleLoop && loopBounds.width > 0.0 &&
        loopBounds.contains(event.position)) {
      if (!renderStatus_.view().hasAudibleAudio) {
        repaint();
        return core::success();
      }
      const auto result = callbacks_.toggleLoop();
      if (result) loopEnabled_ = !loopEnabled_;
      repaint();
      return result;
    }
  }
  if (event.position.y >= layout_.toolbarHeight &&
      event.position.y < layout_.contentTop()) {
    const auto tick = pianoRoll_.timeline().pixelToTick(
        std::max(0.0, event.position.x - layout_.keyboardWidth));
    if (event.modifiers.shift) {
      if (!loopAnchorTick_.has_value()) {
        loopAnchorTick_ = tick;
      } else {
        const auto start = std::min(*loopAnchorTick_, tick);
        const auto end = std::max(*loopAnchorTick_, tick);
        if (end > start && callbacks_.setLoopTicks) {
          const auto result = callbacks_.setLoopTicks(start, end);
          if (!result) {
            loopAnchorTick_.reset();
            repaint();
            return result;
          }
          loopEnabled_ = true;
        }
        loopAnchorTick_.reset();
      }
      repaint();
      return core::success();
    }
    if (callbacks_.seekTick) {
      const auto result = callbacks_.seekTick(tick);
      if (!result) {
        repaint();
        return result;
      }
    }
    dragMode_ = DragMode::RulerSeek;
    repaint();
    return core::success();
  }
  const auto state = sceneState();
  const auto overlayInset = layout_.diagnosticHeight(!state.diagnostics.empty()) +
                           layout_.exportHeight(state.exportProgress.totalFiles != 0U);
  const auto technical = resolveTechnicalLaneHeights(TechnicalLaneLayoutInput{
      .presentation = state.technicalLanes,
      .populated = { !state.phonemes.tokens.empty(), !state.unitOverrides.empty() ||
                         callbacks_.cycleUnitVariant || callbacks_.loadSampleMicroscope,
                     !state.seamOverrides.empty(), !state.pitchAutomation.empty() },
      .previewHeights = { layout_.phonemeLaneHeight, layout_.unitLaneHeight,
                          layout_.seamLaneHeight, layout_.automationLaneHeight },
      .contentTop = layout_.contentTop(),
      .contentBottom = logicalHeight_ - layout_.statusHeight - overlayInset,
  });
  const auto pianoBottom = technical.pianoBottom;
  const auto phonemeHeight = technical.values[0U];
  const auto unitHeight = technical.values[1U];
  const auto seamHeight = technical.values[2U];
  const auto automationHeight = technical.values[3U];
  const auto phonemeTop = pianoBottom;
  const auto unitTop = phonemeTop + phonemeHeight;
  const auto seamTop = unitTop + unitHeight;
  const auto automationTop = seamTop + seamHeight;
  if (event.position.y >= phonemeTop && event.position.y < unitTop) {
    if (callbacks_.movePhonemeBoundary) {
      phonemizer::JapaneseKanaPhonemizer phonemizer;
      ui::PhonemeLaneModel lane;
      const auto current = session_.project().findRegion(regionId_);
      if (current != nullptr) {
        lane.rebuild(pianoRoll_, phonemizer.phonemize(*current),
                     layout_.phonemeContentTop(phonemeTop),
                     layout_.phonemeContentHeight(phonemeHeight));
        auto boundary = lane.hitTestBoundary(event.position);
        if (!boundary.has_value()) {
          if (const auto key = lane.hitTest(event.position); key.has_value()) {
            boundary = std::pair{*key, true};
          }
        }
        if (boundary.has_value()) {
          dragMode_ = DragMode::MovePhonemeBoundary;
          dragStart_ = event.position;
          dragCurrent_ = event.position;
          dragPhoneme_ = boundary->first;
          dragPhonemeStart_ = boundary->second;
          repaint();
          return core::success();
        }
      }
    }
    return core::success();
  }
  if (event.position.y >= unitTop && event.position.y < seamTop) {
    if (callbacks_.cycleUnitVariant || callbacks_.loadSampleMicroscope) {
      phonemizer::JapaneseKanaPhonemizer phonemizer;
      ui::PhonemeLaneModel lane;
      const auto current = session_.project().findRegion(regionId_);
      if (current != nullptr) {
        lane.rebuild(pianoRoll_, phonemizer.phonemize(*current),
                     layout_.phonemeContentTop(unitTop),
                     layout_.phonemeContentHeight(unitHeight));
        if (const auto key = lane.hitTest(event.position); key.has_value()) {
          unitTarget_ = *key;
          seamTarget_.reset();
          seamPreviewAlternate_ = false;
          if (event.clickCount >= 2 && callbacks_.loadSampleMicroscope) {
            return openSampleMicroscope(*key);
          }
          if (!callbacks_.cycleUnitVariant) return core::success();
          const auto result = callbacks_.cycleUnitVariant(*key);
          if (result) markDocumentChanged();
          repaint();
          return result;
        }
      }
    }
    return core::success();
  }
  if (event.position.y >= automationTop &&
      event.position.y < automationTop + automationHeight) {
    const auto existing = pitchPointAt(event.position, automationTop,
                                       automationHeight);
    if (existing.has_value()) {
      if (event.modifiers.shift) {
        if (!callbacks_.removePitchPoint) {
          return core::failure(core::ErrorCode::Unsupported,
                               "Pitch point removal is not connected");
        }
        const auto result = callbacks_.removePitchPoint(existing->tick);
        if (result) markDocumentChanged();
        repaint();
        return result;
      }
      if (event.clickCount >= 2) {
        if (!callbacks_.cyclePitchInterpolation) {
          return core::failure(core::ErrorCode::Unsupported,
                               "Pitch interpolation is not connected");
        }
        const auto result = callbacks_.cyclePitchInterpolation(existing->tick);
        if (result) markDocumentChanged();
        repaint();
        return result;
      }
      if (!callbacks_.movePitchPoint) {
        return core::failure(core::ErrorCode::Unsupported,
                             "Pitch point movement is not connected");
      }
      dragMode_ = DragMode::MovePitchPoint;
      dragPitchTick_ = existing->tick;
      dragStart_ = event.position;
      dragCurrent_ = event.position;
      repaint();
      return core::success();
    }
    if (!callbacks_.upsertPitchPoint) return core::success();
    auto tick = pianoRoll_.timeline().pixelToTick(
        std::max(0.0, event.position.x - layout_.keyboardWidth));
    if (session_.project().settings().snapEnabled) {
      tick = time::Quantizer(session_.project().settings().snapGrid).snap(tick);
    }
    const auto normalized = std::clamp(
        (automationTop + automationHeight * layout_.automationCenterFraction -
         event.position.y) /
            (automationHeight * layout_.pitchAutomationVerticalScale),
        -1.0, 1.0);
    const auto result = callbacks_.upsertPitchPoint(
        domain::PitchAutomationPoint{
            .tick = tick,
            .cents = static_cast<float>(normalized *
                                        layout_.pitchAutomationCentsRange),
            .interpolation = domain::CurveInterpolation::Linear,
        });
    if (result) markDocumentChanged();
    repaint();
    return result;
  }
  if (event.position.y >= seamTop &&
      event.position.y < seamTop + seamHeight) {
    auto* region = session_.project().findRegion(regionId_);
    if (region == nullptr || region->notes.empty()) return core::success();
    const auto localX = event.position.x - layout_.keyboardWidth;
    const auto tick = pianoRoll_.timeline().pixelToTick(localX);
    const auto nearest = std::min_element(
        region->notes.begin(), region->notes.end(),
        [tick](const domain::Note& lhs, const domain::Note& rhs) {
          return std::abs(lhs.startTick.value() - tick.value()) <
                 std::abs(rhs.startTick.value() - tick.value());
        });
    if (nearest == region->notes.end()) return core::success();
    seamTarget_ = domain::PhonemeKey{.noteId = nearest->id, .ordinal = 0U};
    unitTarget_.reset();
    seamPreviewAlternate_ = false;
    const auto normalized = std::clamp(
        1.0 - (event.position.y - seamTop) / seamHeight, 0.0, 1.0);
    return setSelectedSeamAmount(static_cast<float>(normalized));
  }
  if (event.position.y < layout_.contentTop() ||
      event.position.y >= pianoBottom) {
    return core::success();
  }
  const auto point = modelPoint(event.position);
  if (const auto hit = pianoRoll_.hitTest(point); hit.has_value()) {
    const auto overlapCandidates = pianoRoll_.overlapCandidatesAt(point);
    auto selectedHit = *hit;
    if (!event.modifiers.shift && overlapCandidates.size() > 1U) {
      const auto selected = session_.selection().noteIds();
      const auto current = std::find_first_of(
          overlapCandidates.begin(), overlapCandidates.end(), selected.begin(),
          selected.end());
      if (current != overlapCandidates.end()) {
        selectedHit = overlapCandidates[(static_cast<std::size_t>(
            std::distance(overlapCandidates.begin(), current)) + 1U) %
                                        overlapCandidates.size()];
      }
    }
    if (event.clickCount >= 2) {
      return beginLyricEdit(selectedHit);
    }
    if (event.modifiers.shift) {
      session_.selection().toggle(selectedHit);
    } else if (!session_.selection().contains(selectedHit)) {
      session_.selection().selectOnly(selectedHit);
    }
    const auto visuals = pianoRoll_.visibleNotes();
    const auto resizeHandle = std::any_of(
        visuals.begin(), visuals.end(),
        [selectedHit, event](const auto& visual) {
          return visual.noteId == selectedHit &&
                 event.position.x >= visual.bounds.right() - 8.0;
        });
    dragMode_ = resizeHandle ? DragMode::ResizeNotes : DragMode::MoveNotes;
    dragStart_ = event.position;
    dragCurrent_ = event.position;
    repaint();
    return core::success();
  }
  if (event.clickCount >= 2) {
    const auto drawn = pianoRoll_.drawNote(point, session_.project().settings().snapGrid,
                                           U"あ");
    if (!drawn) return core::Result<void>{drawn.error()};
    markDocumentChanged();
    repaint();
    return core::success();
  }
  dragMode_ = DragMode::BoxSelect;
  dragStart_ = event.position;
  dragCurrent_ = event.position;
  dragAdditive_ = event.modifiers.shift;
  if (!dragAdditive_) session_.selection().clear();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerMove(
    const PointerEvent& event) {
  if (dragMode_ == DragMode::None) {
    const auto point = modelPoint(event.position);
    const auto hovered = pianoRoll_.hitTest(point);
    if (hovered != hoveredNote_) {
      hoveredNote_ = hovered;
      repaint();
    }
    return core::success();
  }
  if (dragMode_ == DragMode::RulerSeek) {
    const auto tick = pianoRoll_.timeline().pixelToTick(
        std::max(0.0, event.position.x - layout_.keyboardWidth));
    if (callbacks_.seekTick) {
      const auto result = callbacks_.seekTick(tick);
      if (!result) {
        repaint();
        return result;
      }
    }
    repaint();
    return core::success();
  }
  if (dragMode_ == DragMode::MovePhonemeBoundary) {
    dragCurrent_ = event.position;
    repaint();
    return core::success();
  }
  if (dragMode_ == DragMode::MovePitchPoint) {
    dragCurrent_ = event.position;
    repaint();
    return core::success();
  }
  if (dragMode_ == DragMode::MicroscopeMarker ||
      dragMode_ == DragMode::MicroscopePitchMark) {
    dragCurrent_ = event.position;
    repaint();
    return core::success();
  }
  if (dragMode_ == DragMode::None) return core::success();
  dragCurrent_ = event.position;
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerUp(
    const PointerEvent& event) {
  if (event.button != PointerButton::Left || dragMode_ == DragMode::None) {
    return core::success();
  }
  if (dragMode_ == DragMode::RulerSeek) {
    dragMode_ = DragMode::None;
    repaint();
    return core::success();
  }
  if (dragMode_ == DragMode::MicroscopeMarker ||
      dragMode_ == DragMode::MicroscopePitchMark) {
    core::Result<void> result = core::success();
    if (!microscopeUnit_.has_value() || !microscopeKey_.has_value() ||
        !callbacks_.microscopeUnitChanged) {
      result = core::failure(core::ErrorCode::InvalidState,
                             "Microscope edit target is unavailable");
    } else {
      const auto before = *microscopeUnit_;
      if (dragMode_ == DragMode::MicroscopeMarker &&
          dragMicroscopeMarker_.has_value()) {
        result = microscope_.moveMarker(
            *microscopeUnit_, *dragMicroscopeMarker_, dragCurrent_.x,
            static_cast<time::SampleFrame>(microscopeAudio_.frameCount()));
      } else if (dragMode_ == DragMode::MicroscopePitchMark &&
                 dragMicroscopePitchMark_.has_value()) {
        result = microscope_.movePitchMark(
            *microscopeUnit_, *dragMicroscopePitchMark_, dragCurrent_.x);
      } else {
        result = core::failure(core::ErrorCode::InvalidState,
                               "Microscope edit has no selected target");
      }
      if (result) {
        result = callbacks_.microscopeUnitChanged(
            *microscopeKey_, *microscopeUnit_);
      }
      if (!result) {
        *microscopeUnit_ = before;
        static_cast<void>(rebuildSampleMicroscope());
      }
    }
    dragMode_ = DragMode::None;
    dragMicroscopeMarker_.reset();
    dragMicroscopePitchMark_.reset();
    repaint();
    return result;
  }
  if (dragMode_ == DragMode::MovePitchPoint) {
    core::Result<void> result = core::success();
    if (!dragPitchTick_.has_value() || !callbacks_.movePitchPoint) {
      result = core::failure(core::ErrorCode::InvalidState,
                             "Pitch point drag has no target");
    } else {
      const auto overlayInset =
          layout_.diagnosticHeight(!diagnosticPanel_.entries().empty()) +
          layout_.exportHeight(exportProgress_.totalFiles != 0U);
      const auto pianoBottom =
          layout_.pianoBottom(logicalHeight_, overlayInset);
      const auto automationTop =
          pianoBottom + layout_.phonemeLaneHeightForHeight(logicalHeight_, overlayInset) +
          layout_.unitLaneHeightForHeight(logicalHeight_, overlayInset) +
          layout_.seamLaneHeightForHeight(logicalHeight_, overlayInset);
      const auto automationHeight =
          layout_.automationLaneHeightForHeight(logicalHeight_, overlayInset);
      const auto* region = session_.project().findRegion(regionId_);
      if (region == nullptr) {
        result = core::failure(core::ErrorCode::NotFound,
                               "Pitch automation region is missing");
      } else {
        auto tick = pianoRoll_.timeline().pixelToTick(
            std::max(0.0, dragCurrent_.x - layout_.keyboardWidth));
        tick = std::clamp(tick, time::Tick{0}, region->durationTick);
        if (session_.project().settings().snapEnabled) {
          tick = time::Quantizer(session_.project().settings().snapGrid).snap(tick);
          tick = std::clamp(tick, time::Tick{0}, region->durationTick);
        }
        const auto normalized = std::clamp(
            (automationTop +
             automationHeight * layout_.automationCenterFraction -
             dragCurrent_.y) /
                (automationHeight * layout_.pitchAutomationVerticalScale),
            -2.0, 2.0);
        const auto existing = std::find_if(
            region->pitchAutomation.points().begin(),
            region->pitchAutomation.points().end(),
            [this](const auto& point) { return point.tick == *dragPitchTick_; });
        const auto interpolation =
            existing == region->pitchAutomation.points().end()
                ? domain::CurveInterpolation::Linear
                : existing->interpolation;
        result = callbacks_.movePitchPoint(
            *dragPitchTick_,
            domain::PitchAutomationPoint{
                .tick = tick,
                .cents = static_cast<float>(normalized *
                                            layout_.pitchAutomationCentsRange),
                .interpolation = interpolation,
            });
      }
    }
    if (result) markDocumentChanged();
    dragMode_ = DragMode::None;
    dragPitchTick_.reset();
    repaint();
    return result;
  }
  dragCurrent_ = event.position;
  core::Result<void> result = core::success();
  if (dragMode_ == DragMode::MovePhonemeBoundary) {
    if (!dragPhoneme_.has_value() || !callbacks_.movePhonemeBoundary) {
      result = core::failure(core::ErrorCode::InvalidState,
                             "Phoneme boundary drag has no target");
    } else {
      const auto* region = session_.project().findRegion(regionId_);
      const auto* note = region == nullptr
                             ? nullptr
                             : region->findNote(dragPhoneme_->noteId);
      if (region == nullptr || note == nullptr) {
        result = core::failure(core::ErrorCode::NotFound,
                               "Phoneme boundary note is missing");
      } else {
        const auto absoluteStart = region->startTick + note->startTick;
        const auto tick = pianoRoll_.timeline().pixelToTick(
            std::max(0.0, dragCurrent_.x - layout_.keyboardWidth));
        const auto seconds = session_.project().tempoMap().secondsAt(tick) -
                             session_.project().tempoMap().secondsAt(absoluteStart);
        const auto micros = std::clamp(
            static_cast<time::Microseconds>(std::llround(seconds * 1'000'000.0)),
            static_cast<time::Microseconds>(-10'000'000),
            static_cast<time::Microseconds>(10'000'000));
        result = callbacks_.movePhonemeBoundary(
            *dragPhoneme_, dragPhonemeStart_, micros);
      }
    }
    if (result) markDocumentChanged();
  } else if (dragMode_ == DragMode::MoveNotes) {
    const auto deltaX = dragCurrent_.x - dragStart_.x;
    const auto deltaY = dragCurrent_.y - dragStart_.y;
    const auto originTick = pianoRoll_.timeline().pixelToTick(0.0);
    const auto movedTick = pianoRoll_.timeline().pixelToTick(deltaX);
    const auto deltaTick = movedTick - originTick;
    const auto semitone = -static_cast<std::int32_t>(
        std::lround(deltaY / pianoRoll_.pitch().rowHeight()));
    if (deltaTick != time::Tick{0} || semitone != 0) {
      result = pianoRoll_.moveSelection(deltaTick, semitone);
      if (result) markDocumentChanged();
    }
  } else if (dragMode_ == DragMode::ResizeNotes) {
    const auto deltaX = dragCurrent_.x - dragStart_.x;
    const auto originTick = pianoRoll_.timeline().pixelToTick(0.0);
    const auto movedTick = pianoRoll_.timeline().pixelToTick(deltaX);
    const auto deltaTick = movedTick - originTick;
    if (deltaTick != time::Tick{0}) {
      result = pianoRoll_.resizeSelection(time::Tick{0}, deltaTick);
      if (result) markDocumentChanged();
    }
  } else {
    const auto left = std::min(dragStart_.x, dragCurrent_.x);
    const auto top = std::min(dragStart_.y, dragCurrent_.y) - layout_.contentTop();
    const auto box = ui::Rect{left, top,
                              std::abs(dragCurrent_.x - dragStart_.x),
                              std::abs(dragCurrent_.y - dragStart_.y)};
    pianoRoll_.selectInBox(box, dragAdditive_);
  }
  dragMode_ = DragMode::None;
  dragPhoneme_.reset();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::keyDown(const KeyEvent& event) {
  if (sampleMicroscopeOpen() && event.key == NativeKey::Escape) {
    closeSampleMicroscope();
    return core::success();
  }
  if (composition_.active()) {
    if (event.key == NativeKey::Escape) {
      cancelTextComposition();
      return core::success();
    }
    if (event.key == NativeKey::Enter) {
      return commitTextComposition(composition_.compositionText());
    }
    if (event.key == NativeKey::Tab) {
      const auto committed = commitTextComposition(composition_.compositionText());
      if (!committed) return committed;
      return navigateLyricEdit(event.modifiers.shift ? -1 : 1);
    }
  }

  if (event.key == NativeKey::Tab) {
    rebuildAccessibilityTree();
    const auto focused = accessibilityTree_.focusNext(event.modifiers.shift);
    if (focused) repaint();
    return focused;
  }

  if (unitTarget_.has_value() && !seamTarget_.has_value() &&
      !event.modifiers.primaryShortcut() &&
      (event.key == NativeKey::S || event.key == NativeKey::R)) {
    if (event.key == NativeKey::S && callbacks_.cycleUnitVariant) {
      const auto result = callbacks_.cycleUnitVariant(*unitTarget_);
      if (result) markDocumentChanged();
      repaint();
      return result;
    }
    if (event.key == NativeKey::R && callbacks_.cycleUnitRenderer) {
      const auto result = callbacks_.cycleUnitRenderer(*unitTarget_);
      if (result) markDocumentChanged();
      repaint();
      return result;
    }
  }

  if (audioSettings_.visible) {
    if (event.key == NativeKey::Escape || event.key == NativeKey::I) {
      audioSettings_.visible = false;
      repaint();
      return core::success();
    }
    if (event.key == NativeKey::Left || event.key == NativeKey::Right) {
      return cycleAudioSettings(
          AudioSettingsField::SampleRate,
          event.key == NativeKey::Right ? 1 : -1);
    }
    if (event.key == NativeKey::Up || event.key == NativeKey::Down) {
      const auto field = event.modifiers.shift
                             ? AudioSettingsField::Channels
                             : AudioSettingsField::BlockFrames;
      return cycleAudioSettings(field, event.key == NativeKey::Up ? 1 : -1);
    }
  }

  if (event.modifiers.alt && !event.modifiers.primaryShortcut() &&
      unitTarget_.has_value() && !seamTarget_.has_value()) {
    auto current = selectedUnitValue();
    if (!current) {
      repaint();
      return core::Result<void>{current.error()};
    }
    const auto direction = event.key == NativeKey::Right ||
                                   event.key == NativeKey::Up
                               ? 1.0F
                               : -1.0F;
    core::Result<void> unitResult = core::failure(
        core::ErrorCode::Unsupported, "Unknown Unit renderer shortcut");
    if (event.key == NativeKey::Left || event.key == NativeKey::Right) {
      unitResult = setSelectedUnitLoopPrint(
          current.value().loopPrint.value_or(1.0F) + direction * 0.05F);
    } else if (event.key == NativeKey::Up || event.key == NativeKey::Down) {
      unitResult = setSelectedUnitSourcePitchResidual(
          current.value().sourcePitchResidual.value_or(0.35F) +
          direction * 0.05F);
    }
    repaint();
    return unitResult;
  }

  if (event.modifiers.alt && !event.modifiers.primaryShortcut() &&
      seamTarget_.has_value()) {
    core::Result<void> seamResult = core::success();
    if (event.key == NativeKey::Up || event.key == NativeKey::Down) {
      if (event.modifiers.shift) {
        auto current = selectedSeamValue();
        if (current) {
          seamResult = setSelectedSeamPhaseReset(
              current.value().phaseReset.value_or(0.0F) +
              (event.key == NativeKey::Up ? 0.05F : -0.05F));
        } else {
          seamResult = core::Result<void>{current.error()};
        }
      } else {
        auto current = selectedSeamValue();
        if (current) {
          seamResult = setSelectedSeamAmount(
              current.value().seamAmount.value_or(0.5F) +
              (event.key == NativeKey::Up ? 0.05F : -0.05F));
        } else {
          seamResult = core::Result<void>{current.error()};
        }
      }
    } else if (event.key == NativeKey::Left || event.key == NativeKey::Right) {
      auto current = selectedSeamValue();
      if (current) {
        const auto direction = event.key == NativeKey::Right ? 1 : -1;
        if (event.modifiers.shift) {
          seamResult = setSelectedSeamEnvelopeBlend(
              current.value().envelopeBlend.value_or(0.0F) +
              static_cast<float>(direction) * 0.05F);
        } else {
          seamResult = setSelectedSeamOverlap(
              current.value().overlap.value_or(time::Microseconds{0}) +
              time::Microseconds{direction * 1'000});
        }
      } else {
        seamResult = core::Result<void>{current.error()};
      }
    } else if (event.key == NativeKey::C) {
      seamResult = cycleSelectedSeamCurve();
    } else if (event.key == NativeKey::R) {
      seamResult = resetSelectedSeam();
    } else if (event.key == NativeKey::N) {
      seamResult = applySelectedSeamPreset(SeamPreset::Clean);
    } else if (event.key == NativeKey::A) {
      seamResult = applySelectedSeamPreset(SeamPreset::Character);
    } else if (event.key == NativeKey::P) {
      seamResult = applySelectedSeamPreset(SeamPreset::PhaseAligned);
    } else if (event.key == NativeKey::B) {
      seamResult = toggleSelectedSeamPreview();
    } else {
      seamResult = core::failure(core::ErrorCode::Unsupported,
                                 "Unknown seam shortcut");
    }
    repaint();
    return seamResult;
  }

  if (event.key == NativeKey::Escape && renderStatus_.canCancel()) {
    if (callbacks_.cancelRender) callbacks_.cancelRender();
    repaint();
    return core::success();
  }
  if (event.key == NativeKey::R && renderStatus_.canRetry()) {
    if (callbacks_.retryRender) callbacks_.retryRender();
    repaint();
    return core::success();
  }

  core::Result<void> result = core::success();
  if (event.modifiers.primaryShortcut() && event.key == NativeKey::Z) {
    result = event.modifiers.shift ? session_.redo() : session_.undo();
    if (result) {
      pianoRoll_.rebuildIndex();
      markDocumentChanged();
    }
  } else if (event.modifiers.primaryShortcut() && event.key == NativeKey::Y) {
    result = session_.redo();
    if (result) {
      pianoRoll_.rebuildIndex();
      markDocumentChanged();
    }
  } else if ((event.key == NativeKey::Delete ||
              event.key == NativeKey::Backspace || event.key == NativeKey::X) &&
             event.modifiers.shift && session_.selection().empty()) {
    if (regionId_.valid()) {
      result = deleteSelectedRegion();
    } else {
      result = removeSelectedTrack();
    }
  } else if (event.key == NativeKey::Delete || event.key == NativeKey::Backspace ||
             event.key == NativeKey::X) {
    if (!session_.selection().empty()) {
      result = pianoRoll_.deleteSelection();
      if (result) markDocumentChanged();
    } else if (regionId_.valid()) {
      result = deleteSelectedRegion();
    }
  } else if (event.key == NativeKey::D) {
    if (!session_.selection().empty()) {
      const auto duplicated = duplicateSelectedNotes();
      if (!duplicated) result = core::Result<void>{duplicated.error()};
    } else if (regionId_.valid()) {
      result = duplicateSelectedRegion();
    } else if (selectedTrackId_.valid()) {
      result = duplicateSelectedTrack();
    }
  } else if (event.key == NativeKey::A) {
    if (!session_.selection().empty()) {
      result = event.modifiers.shift
                   ? setSelectedNotesSlur(false)
                   : (event.modifiers.alt ? setSelectedNotesMelisma()
                                          : setSelectedNotesSlur(true));
    } else {
      const auto added = addVocalTrack("Voice " +
                                      std::to_string(session_.project().vocalTracks().size() + 1U));
      if (!added) result = core::Result<void>{added.error()};
    }
  } else if (event.key == NativeKey::Q) {
    if (!session_.selection().empty()) {
      result = quantizeSelectedNotes(session_.project().settings().snapGrid);
    }
  } else if (event.key == NativeKey::S && !event.modifiers.primaryShortcut()) {
    if (regionId_.valid()) {
      const auto* region = session_.project().findRegion(regionId_);
      if (region != nullptr) {
        result = splitSelectedRegion(
            time::Tick{region->durationTick.value() / 2});
      }
    }
  } else if (event.key == NativeKey::E && !event.modifiers.primaryShortcut()) {
    result = regionId_.valid() ? beginSelectedRegionRename()
                               : beginSelectedTrackRename();
  } else if (event.key == NativeKey::Space) {
    if (!renderStatus_.view().hasAudibleAudio) {
      repaint();
      return core::success();
    }
    const auto requestedPlaying = !playing_;
    if (callbacks_.setPlaying) {
      result = callbacks_.setPlaying(requestedPlaying);
      if (result) playing_ = requestedPlaying;
    } else {
      playing_ = requestedPlaying;
    }
  } else if (event.key == NativeKey::L && event.modifiers.shift &&
             !event.modifiers.primaryShortcut()) {
    result = beginBatchLyricEdit();
  } else if (event.key == NativeKey::L) {
    if (callbacks_.toggleLoop) {
      result = callbacks_.toggleLoop();
      if (result) loopEnabled_ = !loopEnabled_;
    }
  } else if (event.key == NativeKey::Enter) {
    const auto selected = session_.selection().noteIds();
    if (!selected.empty()) result = beginLyricEdit(selected.front());
  } else if (event.key == NativeKey::C) {
    auto& mode = session_.project().settings().characterDisplay;
    if (mode == domain::CharacterDisplayMode::Full) {
      mode = domain::CharacterDisplayMode::Minimal;
    } else if (mode == domain::CharacterDisplayMode::Minimal) {
      mode = domain::CharacterDisplayMode::Off;
    } else {
      mode = domain::CharacterDisplayMode::Full;
    }
    if (callbacks_.viewChanged) {
      callbacks_.viewChanged();
    } else {
      repaint();
    }
  } else if (event.key == NativeKey::V) {
    voicebankBrowserVisible_ = !voicebankBrowserVisible_;
    if (voicebankBrowserVisible_) audioSettings_.visible = false;
    if (callbacks_.viewChanged) {
      callbacks_.viewChanged();
    } else {
      repaint();
    }
  } else if (event.key == NativeKey::I) {
    audioSettings_.visible = !audioSettings_.visible;
    if (audioSettings_.visible) voicebankBrowserVisible_ = false;
    if (callbacks_.viewChanged) {
      callbacks_.viewChanged();
    } else {
      repaint();
    }
  } else if (event.key == NativeKey::Plus || event.key == NativeKey::Minus) {
    pianoRoll_.timeline().zoomAround(
        (logicalWidth_ - layout_.keyboardWidth) * 0.5,
        event.key == NativeKey::Plus ? 1.25 : 0.8);
  } else if (event.modifiers.shift && session_.selection().empty() &&
             (event.key == NativeKey::Up || event.key == NativeKey::Down)) {
    result = reorderSelectedTrackBy(event.key == NativeKey::Up ? -1 : 1);
  } else if (event.key == NativeKey::Left || event.key == NativeKey::Right ||
             event.key == NativeKey::Up || event.key == NativeKey::Down) {
    const auto tick = event.key == NativeKey::Left
                          ? time::Tick{-session_.project().settings().snapGrid.value()}
                          : event.key == NativeKey::Right
                                ? session_.project().settings().snapGrid
                                : time::Tick{0};
    const auto semitone = event.key == NativeKey::Up
                              ? 1
                              : event.key == NativeKey::Down ? -1 : 0;
    if (!session_.selection().empty()) {
      result = pianoRoll_.moveSelection(tick, semitone);
      if (result) markDocumentChanged();
    }
  }
  repaint();
  return result;
}

void NativeEditorController::scroll(double deltaX, double deltaY,
                                    ui::Point anchor,
                                    InputModifiers modifiers) noexcept {
  if (modifiers.control || modifiers.command) {
    pianoRoll_.timeline().zoomAround(
        std::max(0.0, anchor.x - layout_.keyboardWidth),
        deltaY < 0.0 ? 1.12 : 0.89);
  } else {
    pianoRoll_.timeline().panPixels(deltaX + deltaY);
  }
  repaint();
}

std::optional<ui::Rect> NativeEditorController::noteWindowBounds(
    domain::NoteId noteId) const {
  for (const auto& visual : pianoRoll_.visibleNotes()) {
    if (visual.noteId != noteId) continue;
    auto bounds = visual.bounds;
    bounds.y += layout_.contentTop();
    return bounds;
  }
  return std::nullopt;
}

std::optional<domain::PitchAutomationPoint> NativeEditorController::pitchPointAt(
    ui::Point point, double automationTop, double automationHeight) const {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return std::nullopt;
  const auto centerY = automationTop +
                       automationHeight * layout_.automationCenterFraction;
  std::optional<domain::PitchAutomationPoint> result;
  auto bestDistance = std::numeric_limits<double>::max();
  for (const auto& candidate : region->pitchAutomation.points()) {
    const auto x = layout_.keyboardWidth +
                   pianoRoll_.timeline().tickToPixel(candidate.tick);
    const auto y = centerY -
                   static_cast<double>(candidate.cents) /
                       layout_.pitchAutomationCentsRange *
                       (automationHeight * layout_.pitchAutomationVerticalScale);
    const auto dx = point.x - x;
    const auto dy = point.y - y;
    const auto distance = dx * dx + dy * dy;
    if (distance > 64.0 || distance >= bestDistance) continue;
    bestDistance = distance;
    result = candidate;
  }
  return result;
}

core::Result<void> NativeEditorController::beginLyricEdit(domain::NoteId noteId) {
  const auto* note = session_.project().findNote(noteId);
  auto* region = session_.project().findRegion(regionId_);
  if (note == nullptr || region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Cannot edit lyric for a missing note");
  }
  const auto* lyric = region->findLyric(note->lyricTokenId);
  if (lyric == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Cannot edit a missing lyric token");
  }
  if (composition_.active()) composition_.cancel();
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  batchLyricTarget_ = false;
  auto begun = composition_.begin(lyric->id, lyric->surface);
  if (!begun) return begun;
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = lyric->id,
        .logicalBounds = noteWindowBounds(noteId).value_or(
            ui::Rect{layout_.keyboardWidth, layout_.contentTop(), 160.0, 30.0}),
        .currentText = lyric->surface,
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::updateTextComposition(
    std::u32string text, ui::CompositionSelection selection) {
  const auto result = composition_.update(std::move(text), selection);
  repaint();
  return result;
}

core::Result<void> NativeEditorController::commitTextComposition(
    std::u32string text) {
  if (!composition_.active()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No native text composition is active");
  }
  auto updated = composition_.update(std::move(text),
                                      ui::CompositionSelection{});
  if (!updated) return updated;
  auto commit = composition_.commit();
  if (!commit) return core::Result<void>{commit.error()};

  core::Result<void> result = core::success();
  if (renameTrackTarget_.has_value()) {
    result = renameSelectedTrack(domain::toUtf8(commit.value().text));
    renameTrackTarget_.reset();
    renameRegionTarget_.reset();
    batchLyricTarget_ = false;
    finishTextInput();
    repaint();
    return result;
  }
  if (renameRegionTarget_.has_value()) {
    result = renameSelectedRegion(domain::toUtf8(commit.value().text));
    renameTrackTarget_.reset();
    renameRegionTarget_.reset();
    batchLyricTarget_ = false;
    finishTextInput();
    repaint();
    return result;
  }

  if (batchLyricTarget_) {
    const auto distributed = distributeSelectedLyrics(commit.value().text);
    batchLyricTarget_ = false;
    finishTextInput();
    repaint();
    if (!distributed) return core::Result<void>{distributed.error()};
    if (!distributed.value().committed) {
      return core::failure(
          core::ErrorCode::Conflict,
          "Lyric count must match the selected note count",
          "requested=" + std::to_string(distributed.value().requestedSyllables) +
              ", target=" + std::to_string(distributed.value().targetNotes));
    }
    return core::success();
  }

  domain::Language language = domain::Language::Unspecified;
  for (const auto& track : session_.project().vocalTracks()) {
    for (const auto& region : track.regions) {
      if (const auto* lyric = region.findLyric(commit.value().lyricId)) {
        language = lyric->language;
      }
    }
  }
  result = session_.execute(
      std::make_unique<application::SetLyricCommand>(
          commit.value().lyricId, std::move(commit.value().text), language));
  if (result) {
    markDocumentChanged();
    pianoRoll_.rebuildIndex();
  }
  finishTextInput();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::navigateLyricEdit(int direction) {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || region->notes.empty() || direction == 0) {
    return core::failure(core::ErrorCode::NotFound,
                         "No adjacent lyric note is available");
  }
  std::vector<const domain::Note*> notes;
  notes.reserve(region->notes.size());
  for (const auto& note : region->notes) notes.push_back(&note);
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
    return lhs->startTick < rhs->startTick;
  });
  const auto selected = session_.selection().noteIds();
  if (selected.empty()) return core::failure(core::ErrorCode::NotFound,
                                             "No current lyric note is selected");
  const auto current = std::find_if(
      notes.begin(), notes.end(), [selected](const auto* note) {
        return std::find(selected.begin(), selected.end(), note->id) !=
               selected.end();
      });
  if (current == notes.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Current lyric note is not in the selected region");
  }
  const auto nextIndex = static_cast<std::ptrdiff_t>(
      std::distance(notes.begin(), current)) + direction;
  if (nextIndex < 0 || nextIndex >= static_cast<std::ptrdiff_t>(notes.size())) {
    return core::failure(core::ErrorCode::NotFound,
                         "No adjacent lyric note is available");
  }
  session_.selection().selectOnly(notes[static_cast<std::size_t>(nextIndex)]->id);
  return beginLyricEdit(notes[static_cast<std::size_t>(nextIndex)]->id);
}

core::Result<void> NativeEditorController::selectAudioDevice(
    std::size_t index) {
  if (index >= audioSettings_.devices.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Audio device option does not exist");
  }
  auto requested = audioSettings_.current;
  requested.deviceId = audioSettings_.devices[index].id;
  if (!callbacks_.applyAudioSettings) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Audio settings apply callback is not connected");
  }
  const auto applied = callbacks_.applyAudioSettings(requested);
  if (applied) audioSettings_.diagnostic.clear();
  else audioSettings_.diagnostic = applied.error().message;
  repaint();
  return applied;
}

core::Result<void> NativeEditorController::cycleAudioSettings(
    AudioSettingsField field, int direction) {
  if (direction == 0) return core::success();
  auto requested = audioSettings_.current;
  const auto cycle = [direction](auto& value, const auto& values) {
    const auto current = std::find(values.begin(), values.end(), value);
    auto index = current == values.end()
                     ? 0U
                     : static_cast<std::size_t>(
                           std::distance(values.begin(), current));
    const auto count = values.size();
    if (direction > 0) {
      index = (index + 1U) % count;
    } else {
      index = index == 0U ? count - 1U : index - 1U;
    }
    value = values[index];
  };
  constexpr std::array<std::uint32_t, 3> sampleRates{44100U, 48000U, 96000U};
  constexpr std::array<std::size_t, 4> blockFrames{64U, 128U, 256U, 512U};
  constexpr std::array<std::uint8_t, 4> channels{1U, 2U, 4U, 8U};
  switch (field) {
    case AudioSettingsField::SampleRate:
      cycle(requested.sampleRate, sampleRates);
      break;
    case AudioSettingsField::BlockFrames:
      cycle(requested.blockFrames, blockFrames);
      break;
    case AudioSettingsField::Channels:
      cycle(requested.outputChannels, channels);
      break;
  }
  if (!callbacks_.applyAudioSettings) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Audio settings apply callback is not connected");
  }
  const auto applied = callbacks_.applyAudioSettings(requested);
  if (applied) audioSettings_.diagnostic.clear();
  else audioSettings_.diagnostic = applied.error().message;
  repaint();
  return applied;
}

void NativeEditorController::cancelTextComposition() noexcept {
  composition_.cancel();
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  batchLyricTarget_ = false;
  finishTextInput();
  repaint();
}

void NativeEditorController::setAudioState(bool online, std::string backend) {
  audioOnline_ = online;
  audioBackend_ = std::move(backend);
  repaint();
}

void NativeEditorController::setDirty(bool dirty) noexcept {
  dirty_ = dirty;
  repaint();
}

void NativeEditorController::setPlayheadPixel(double value) noexcept {
  playheadPixel_ = std::max(0.0, value);
  repaint();
}

}  // namespace seam::native_ui
