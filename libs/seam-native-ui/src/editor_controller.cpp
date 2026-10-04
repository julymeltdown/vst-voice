#include "seam/native_ui/editor_controller.hpp"
#include "seam/core/finite_decimal.hpp"

#include "seam/native_ui/editor_notices.hpp"
#include "seam/native_ui/editor_text_target.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/diagnostic_ids.hpp"
#include "seam/native_ui/list_entry_ids.hpp"

#include "seam/application/lyric_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/arrangement_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/view_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/ui/phoneme_lane_model.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

namespace seam::native_ui {

namespace {

std::atomic<std::uint64_t> nextControllerSerial{1U};

// A nudge moves a channel by a fraction of its range, so ten of them have to land exactly on the
// channel's neutral value rather than a hundred-millionth away from it: the sum of ten tenths is not
// exactly one in binary floating point, and a stored point that is neutral to seven decimal places is
// still a stored curve that reports a non-neutral value, refuses to be cleared by the nudge that created
// it, and makes an edit out of an edit that changed nothing. The tolerance is seven orders of magnitude
// below one step, so it can only catch arithmetic residue and never a value someone chose.
float snappedToNeutral(float value) noexcept {
  constexpr float kNeutralTolerance = 1.0e-6F;
  return std::abs(value) < kNeutralTolerance ? 0.0F : value;
}

std::optional<domain::NoteId> noteIdForSemanticId(std::string_view id) noexcept {
  constexpr auto prefix = std::string_view{"note."};
  if (!id.starts_with(prefix)) return std::nullopt;
  const auto suffix = id.substr(prefix.size());
  std::uint64_t rawId = 0U;
  const auto parsed = std::from_chars(
      suffix.data(), suffix.data() + suffix.size(), rawId, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) {
    return std::nullopt;
  }
  return domain::NoteId{rawId};
}

// The diagnostics the editor raises itself. The owner's own are never these: the editor answers
// their actions, not the owner.
bool isEditorNotice(const authoring::Diagnostic& diagnostic) noexcept {
  return diagnostic.code == kEditRefusedCode || diagnostic.code == kSelectionSyncFailedCode;
}

}

NativeEditorController::NativeEditorController(
    application::EditorSession& session,
    application::ProjectFactory& factory,
    domain::RegionId regionId,
    EditorHostCallbacks callbacks)
    : instanceSerial_(nextControllerSerial.fetch_add(1U, std::memory_order_relaxed)),
      session_(session),
      factory_(factory),
      regionId_(regionId),
      pianoRoll_(session, factory, regionId),
      callbacks_(std::move(callbacks)) {
  diagnosticPanel_.setActionHandler(
      [this](const authoring::Diagnostic& diagnostic,
             authoring::DiagnosticAction action) -> core::Result<void> {
        if (isEditorNotice(diagnostic)) {
          // Copied first: removing the notice rebuilds the panel that this reference points into.
          const auto notice = diagnostic;
          if (action == authoring::DiagnosticAction::Dismiss) {
            removeNotice(notice);
            return core::success();
          }
          if (action == authoring::DiagnosticAction::Retry) {
            hostSelectionTries_ = 1U;  // A new count: this is the first try of it.
            tellHostSelection();
            return core::success();
          }
          return core::failure(core::ErrorCode::InvalidArgument,
                               "The editor does not offer that action on its own notices");
        }
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
  // The selection belongs to the session, which outlives a controller that is rebuilt around another
  // region (the plug-in editor does that whenever the host points it somewhere else). A controller shows
  // one region, so it keeps only the part of the selection that names notes of that region.
  const auto owned = pianoRoll_.ownedSelection();
  if (owned.elsewhere != 0U) session_.selection().replace(owned.notes);
  arrangementPanel_.rebuild(session_.project(), selectedTrackId_, regionId_);
  resize(logicalWidth_, logicalHeight_);
}

std::chrono::steady_clock::time_point NativeEditorController::uiNow() const noexcept {
  if (callbacks_.uiClock) {
    try {
      return callbacks_.uiClock();
    } catch (...) {
    }
  }
  return std::chrono::steady_clock::now();
}

bool NativeEditorController::reduceMotionEnabled() const noexcept {
  if (callbacks_.reduceMotionEnabled) {
    try {
      return callbacks_.reduceMotionEnabled();
    } catch (...) {
      return true;
    }
  }
  return true;
}

void NativeEditorController::beginLayoutTransition(
    const EditorSceneState& fromState) {
  if (reduceMotionEnabled()) {
    layoutTransition_.reset();
    return;
  }
  const auto overlayInset =
      layout_.diagnosticHeight(!fromState.diagnostics.empty()) +
      layout_.exportHeight(fromState.exportProgress.totalFiles != 0U);
  const auto technical = resolveEditorTechnicalLaneHeights(
      fromState, layout_,
      fromState.logicalHeight - layout_.statusHeight - overlayInset);
  layoutTransition_ = LayoutTransitionState{
      .fromLaneHeights = technical.values,
      .fromDockWidth = resolveEditorDockWidth(fromState, layout_),
      .startedAt = uiNow(),
  };
}

// The project's character display: view state kept with the project (C cycles it, the SINGER
// menu's switch sets it). It never schedules a render or dirties the document.
void NativeEditorController::setCharacterDisplay(domain::CharacterDisplayMode mode) {
  auto& current = session_.project().settings().characterDisplay;
  if (current == mode) return;
  const auto fromState = sceneState();
  current = mode;
  beginLayoutTransition(fromState);
  if (callbacks_.viewChanged) {
    callbacks_.viewChanged();
  } else {
    repaint();
  }
}

void NativeEditorController::applyLayoutTransition(
    EditorSceneState& state) const {
  if (!layoutTransition_.has_value() || reduceMotionEnabled()) return;
  constexpr auto duration = std::chrono::milliseconds{150};
  const auto elapsed = uiNow() - layoutTransition_->startedAt;
  if (elapsed >= duration) return;
  const auto linear = std::clamp(
      std::chrono::duration<double>(std::max(elapsed, decltype(elapsed)::zero())) /
          duration,
      0.0, 1.0);
  const auto progress = linear * linear * (3.0 - 2.0 * linear);
  const auto overlayInset = layout_.diagnosticHeight(!state.diagnostics.empty()) +
                           layout_.exportHeight(state.exportProgress.totalFiles != 0U);
  const auto target = resolveEditorTechnicalLaneHeights(
      state, layout_, state.logicalHeight - layout_.statusHeight - overlayInset);
  std::array<double, 4U> laneHeights{};
  for (std::size_t index = 0U; index < laneHeights.size(); ++index) {
    laneHeights[index] = layoutTransition_->fromLaneHeights[index] +
                         (target.values[index] -
                          layoutTransition_->fromLaneHeights[index]) *
                             progress;
  }
  state.technicalLaneHeightsOverride = laneHeights;
  const auto targetDockWidth = resolveEditorDockWidth(state, layout_);
  state.dockWidthOverride =
      layoutTransition_->fromDockWidth +
      (targetDockWidth - layoutTransition_->fromDockWidth) * progress;
  if (callbacks_.requestRepaint) callbacks_.requestRepaint();
}

void NativeEditorController::syncInteractionToAccessibilityFocus() {
  const auto* focused = accessibilityTree_.focusedNode();
  if (focused == nullptr) {
    static_cast<void>(interaction_.clearFocus());
    return;
  }
  if (const auto noteId = noteIdForSemanticId(focused->id);
      noteId.has_value()) {
    const auto* region = session_.project().findRegion(regionId_);
    const auto* note = region == nullptr ? nullptr : region->findNote(*noteId);
    const auto* lyric = note == nullptr || region == nullptr
                            ? nullptr
                            : region->findLyric(note->lyricTokenId);
    static_cast<void>(interaction_.updateFocusedNote(
        *noteId,
        lyric == nullptr ? std::string{} : domain::toUtf8(lyric->surface)));
    return;
  }
  if (!focused->id.starts_with("detail.note.")) {
    static_cast<void>(interaction_.clearFocus());
  }
}

const phonemizer::Result& NativeEditorController::regionPronunciation(
    const domain::VocalRegion& region) const {
  auto& cache = pronunciation_;
  const auto revision = session_.revision();
  if (!cache.valid || cache.revision != revision || cache.region != region.id ||
      cache.address != &region || cache.notes != region.notes.size() ||
      cache.lyrics != region.lyrics.size() || cache.overrides != region.phonemeOverrides.size()) {
    cache.result = phonemizer::inspectPronunciation(region);
    cache.revision = revision;
    cache.region = region.id;
    cache.address = &region;
    cache.notes = region.notes.size();
    cache.lyrics = region.lyrics.size();
    cache.overrides = region.phonemeOverrides.size();
    cache.valid = true;
  }
  return cache.result;
}

EditorSceneState NativeEditorController::sceneState() const {
  EditorSceneState state{
      .projectName = session_.project().name(),
      .revision = session_.revision(),
      .playing = playing_,
      .loopEnabled = loopEnabled_,
      .loopAvailable = callbacks_.toggleLoop != nullptr,
      .bounceFollowHost = bounceFollowHost_,
      .bounceTimingAvailable = callbacks_.setBounceTiming != nullptr,
      .dirty = dirty_,
      .audioDeviceOnline = audioOnline_,
      .audioBackend = audioBackend_,
      .tempoBpm = session_.project().tempoMap().bpmAt(time::Tick{0}),
      .meter = session_.project().meterMap().meterAt(time::Tick{0}),
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
      .seamPreviewConnected = static_cast<bool>(callbacks_.previewSeam),
      .pitchAutomation = {},
      .technicalLanes = session_.project().settings().technicalLanes,
      .technicalLaneAvailable = {
          false,
          static_cast<bool>(callbacks_.cycleUnitVariant) ||
              static_cast<bool>(callbacks_.loadSampleMicroscope),
          false,
          false,
      },
      .characterMode = session_.project().settings().characterDisplay,
      .characterState = playing_ ? character::State::Focused
                                 : (dirty_ ? character::State::Warning
                                           : character::State::Neutral),
      .characterPerformance = characterPerformance_,
      .characterName = characterName_,
      .characterStyle = characterStyle_,
      .characterPortrait = characterPortrait_,
      // Dock presence asks whether there is a package the dock can draw from, which the surface that
      // owns the package knows. A surface that publishes only a portrait is one that has already decided
      // the dock belongs, so a decoded frame is the answer available here; the surfaces that own a
      // package set this from CharacterPresentation::dockVisible and overwrite it.
      .characterDockReserved = characterPortrait_ != nullptr,
  };
  state.selectedNoteCount = session_.selection().noteIds().size();
  if (vibratoHandleDrag_) {
    state.vibratoGesturePreview = EditorSceneState::VibratoGesturePreview{
        .noteId = vibratoHandleDrag_->noteId,
        .value = vibratoHandleDrag_->preview,
    };
  }
  state.vibratoKeyboardFocus = vibratoKeyboardFocus_;
  state.hoveredNote = interaction_.hoveredNote();
  state.focusedNote = interaction_.focusedNote();
  state.detail = interaction_.detail();
  state.overlapDetail = overlapDetail_;
  state.arrangementTracks = arrangementPanel_.tracks();
  // The playhead is passed so a channel row can report its value where the creator is working, in the
  // same way the automation lane does, rather than only at the channel's neutral.
  state.inspector = TrackInspectorModel::snapshot(session_.project(), selectedTrackId_,
                                                  playheadTick_);
  if (state.inspector.vocal && callbacks_.validateSingerControl) {
    // Replace the conservative carrier-only default with the host's admitted-resource decision.
    // This matters for renderer-specific sample capabilities such as Spectral Classic formant.
    for (auto& capability : state.inspector.expressionCapabilities) {
      const auto control = ui::describeExpressionChannel(capability.channel).control;
      const auto resolved = callbacks_.validateSingerControl(selectedTrackId_, control);
      capability.refusal = resolved ? std::string{} : resolved.error().message;
    }
    for (auto& row : state.inspector.expressionRows) {
      const auto capability = std::find_if(
          state.inspector.expressionCapabilities.begin(),
          state.inspector.expressionCapabilities.end(),
          [&row](const auto& candidate) { return candidate.channel == row.channel; });
      if (capability != state.inspector.expressionCapabilities.end())
        row.refusal = capability->refusal;
    }
  }
  state.vibratoEditable = state.inspector.vocal && session_.project().findRegion(regionId_) &&
      !session_.selection().empty() && session_.selection().noteIds().size() <= 10000U;
  const auto* dynamicsRegion = session_.project().findRegion(regionId_);
  state.dynamicsEditable = state.inspector.vocal && dynamicsRegion && dynamicsRegion->notes.size() <= 10000U;
  state.styleEditable = state.dynamicsEditable && (styleBankResolver_ || styleSnapshotResolver_);
  state.voicebankBrowserVisible = voicebankBrowserVisible_;
  state.voicebankCards = voicebankCards_;
  state.audioSettings = audioSettings_;
  state.outputLevel = outputLevel_;
  state.recoverySupport = recoverySupportPanel_.view();
  state.exportProgress = exportProgress_;
  state.lastExport = lastExport_;
  state.diagnostics.reserve(diagnosticPanel_.entries().size());
  for (const auto& entry : diagnosticPanel_.entries()) {
    state.diagnostics.push_back(entry.diagnostic);
  }
  if (const auto* track = session_.project().findVocalTrack(selectedTrackId_);
      track != nullptr) {
    domain::SingerResourceIdentity activeResource{
        .kind = domain::SingerResourceKind::Sample,
        .id = track->voicebank.id,
        .version = track->voicebank.version,
        .contentHash = track->voicebank.contentHash};
    if (track->proceduralRecipe) activeResource = track->proceduralRecipe->resource;
    else if (track->neuralResource) activeResource = track->neuralResource->resource;
    const auto card = std::find_if(voicebankCards_.begin(), voicebankCards_.end(),
                                   [&track](const auto& candidate) {
      return candidate.id == track->voicebank.id &&
             candidate.version == track->voicebank.version &&
             candidate.contentHash == track->voicebank.contentHash;
    });
    state.voiceIdentity = resolveVoiceIdentity(VoiceIdentityInput{
        .reference = track->voicebank,
        .activeResource = activeResource,
        .card = card == voicebankCards_.end() ? nullptr : &*card,
        .character = characterBinding_.has_value() ? &*characterBinding_ : nullptr,
        .renderStatus = state.renderStatus,
        .diagnostics = state.diagnostics,
        .focused = playing_,
        .completeDwell = std::chrono::steady_clock::now() < voiceCompleteUntil_,
    });
    // Character artwork follows the verified voice/render state instead of
    // remaining permanently neutral/focused.  Native hosts select the actual
    // state asset from this semantic value after the scene is built.
    switch (state.voiceIdentity.state) {
      case VoiceIdentityState::Missing:
        state.characterState = character::State::Warning;
        break;
      case VoiceIdentityState::Selected:
        state.characterState = character::State::Neutral;
        break;
      case VoiceIdentityState::Ready:
        state.characterState = playing_ ? character::State::Focused
                                        : character::State::Neutral;
        break;
      case VoiceIdentityState::Rendering:
        state.characterState = character::State::Rendering;
        break;
      case VoiceIdentityState::Complete:
        state.characterState = character::State::Complete;
        break;
      case VoiceIdentityState::Warning:
        state.characterState = character::State::Warning;
        break;
      case VoiceIdentityState::Error:
        state.characterState = character::State::Error;
        break;
    }
    state.characterPortrait = characterPortrait_;
  }
  if (state.characterPerformance.has_value())
    state.characterPerformance->reducedMotion =
        callbacks_.reduceMotionEnabled && callbacks_.reduceMotionEnabled();
  if (microscopeUnit_.has_value()) {
    state.sampleMicroscope = EditorSceneState::SampleMicroscopeView{
        .model = &microscope_,
        .unitId = microscopeUnitId_,
        .destinationContext = microscopeDestinationContext_,
        .canPlay = callbacks_.playMicroscopeSample != nullptr,
        .detailsVisible = microscopeDetailsVisible_,
        .detailsText = microscopeDetailsText_,
        .detailsLines = {},
        .detailsPage = microscopeDetailsPage_,
        .detailsPageCount = std::max<std::size_t>(1U,
            (microscopeDetailsLines_.size() + microscopeDetailsRows_ - 1U) / microscopeDetailsRows_),
    };
    if (microscopeDetailsVisible_) {
      const auto start = microscopeDetailsPage_ * microscopeDetailsRows_;
      for (auto i = start; i < std::min(start + microscopeDetailsRows_, microscopeDetailsLines_.size()); ++i) {
        const auto line = microscopeDetailsLines_[i];
        state.sampleMicroscope->detailsLines.push_back(microscopeDetailsText_.substr(line.offset, line.length));
      }
    }
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
        const auto& selected = batchLyricTarget_->notes;
        const auto first = std::find_if(
            selected.begin(), selected.end(), [region](domain::NoteId noteId) {
              return region->findNote(noteId) != nullptr;
            });
        if (first != selected.end()) state.lyricEditor = noteWindowBounds(*first);
      }
    }
    state.compositionPreview = domain::toUtf8(composition_.compositionText());
    if (tempoEdit_) {
      state.timeMapInputActive = true;
      state.lyricEditor = layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, timeMapPanel_.has_value());
    }
    if (hintEdit_ || replacementInput_) {
      state.hintInputActive = true;
      state.timeMapInputActive = true; // Shared bounded non-lyric input painter.
      state.boundedInputLabel = boundedInputLabel();
      state.lyricEditor = layout_.hintTextBounds(logicalWidth_, logicalHeight_);
    }
  }
  if (const auto* region = session_.project().findRegion(regionId_); region != nullptr) {
    state.phonemes = regionPronunciation(*region);
    state.unitOverrides = region->unitSelectionOverrides;
    state.seamOverrides = region->seamOverrides;
    state.pitchAutomation = region->pitchAutomation.points();
    state.automationOriginTick = region->startTick;
    state.automationRegionDuration = region->durationTick;
    state.playheadInsideRegion = regionPlayheadForEdit().hasValue();
  }
  if (expressionLaneVisible_) {
    const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
    state.expression.channel = expressionChannel_;
    state.expression.label = std::string{descriptor.label};
    state.expression.unit = std::string{descriptor.unit};
    state.expression.minimum = descriptor.minimum;
    state.expression.maximum = descriptor.maximum;
    state.expression.neutral = descriptor.neutral;
    state.expression.valueAtPlayhead = expressionValueAtPlayhead();
    // Only a draft prepared at the current revision stands in for the stored curve; a stale one
    // would publish points the document no longer holds.
    const auto* draft = expressionDraft_ && expressionDraft_->current(session_, regionId_)
                            ? &expressionDraft_.value()
                            : nullptr;
    state.expression.draftOpen = draft != nullptr;
    state.expression.draftChanged = draft != nullptr && draft->hasChanges();
    if (draft != nullptr) state.expression.points = draft->points();
    else if (const auto* region = session_.project().findRegion(regionId_); region != nullptr)
      state.expression.points = ui::readExpressionPoints(*region, expressionChannel_);
    // The lane reports the same carrier decision the renderer makes, so a stored curve is never
    // silently dropped and a refusal is never invisible.
    if (!session_.project().findVocalTrack(selectedTrackId_)) {
      state.expression.refusal = "Select a vocal track to see channel applicability";
    } else {
      const auto allowed = callbacks_.validateSingerControl
          ? callbacks_.validateSingerControl(selectedTrackId_, descriptor.control)
          : ui::validateExpressionCarrier(session_.project(), selectedTrackId_, expressionChannel_);
      state.expression.refusal = allowed ? std::string{} : allowed.error().message;
    }
  }
  state.phonemeReview.available = callbacks_.reviewPhonemeBindings && callbacks_.rebindPhonemeOverride;
  state.phonemeReview.visible = phonemeReview_.has_value();
  if (phonemeReview_) {
    const auto& review = *phonemeReview_;
    const auto& targets = reviewTargets();
    const bool source = reviewedEdit_ < reviewEditCount();
    const bool target = reviewedTarget_ && *reviewedTarget_ < targets.size();
    state.phonemeReview.source = "No retained edits";
    if (source && reviewedEdit_ < review.retainedEdits.size()) {
      const auto& edit = review.retainedEdits[reviewedEdit_];
      state.phonemeReview.source = "Edit " + std::to_string(reviewedEdit_ + 1U) + "/" +
          std::to_string(reviewEditCount()) + "  " + edit.key.toString() + "  " +
          edit.symbol.value_or("timing") + (edit.locked ? " locked  " : " unlocked  ") + "start/end us: " +
          (edit.timing.startOffset ? std::to_string(*edit.timing.startOffset) : "auto") + "/" +
          (edit.timing.endOffset ? std::to_string(*edit.timing.endOffset) : "auto");
    }
    if (source && reviewedEdit_ >= review.retainedEdits.size() && renderEditReview_) {
      const auto index = reviewedEdit_ - review.retainedEdits.size();
      const auto& edits = *renderEditReview_;
      if (index < edits.units.size()) {
        const auto& edit = edits.units[index];
        state.phonemeReview.source = "Unit " + edit.unitId + "  " + edit.startKey.toString() +
            "  span " + std::to_string(edit.tokenCount) + "  " + std::string(domain::unitRendererKindName(edit.renderer)) +
            (edit.locked ? " locked" : " unlocked");
      } else {
        const auto& edit = edits.seams[index - edits.units.size()];
        state.phonemeReview.source = "Seam " + edit.incomingStartKey.toString() + "  amount " +
            (edit.seamAmount ? std::to_string(*edit.seamAmount) : "auto") + "  overlap us " +
            (edit.overlap ? std::to_string(*edit.overlap) : "auto");
      }
    }
    state.phonemeReview.target = target ? "Target " + targets[*reviewedTarget_].key.toString() +
        "  " + targets[*reviewedTarget_].symbol : "Choose a current sound with Next sound";
    if (target && source && reviewedEdit_ >= review.retainedEdits.size() && renderEditReview_) {
      const auto index = reviewedEdit_ - review.retainedEdits.size();
      if (index < renderEditReview_->units.size()) {
        const auto count = renderEditReview_->units[index].tokenCount;
        if (count > 0U && count <= targets.size() - *reviewedTarget_) {
          const auto& last = targets[*reviewedTarget_ + count - 1U];
          state.phonemeReview.target += " through " + last.key.toString() + " " + last.symbol;
        } else state.phonemeReview.target += " (incomplete span)";
      } else {
        state.phonemeReview.target += *reviewedTarget_ == 0U ? " (initial boundary)"
            : " after " + targets[*reviewedTarget_ - 1U].key.toString() + " " + targets[*reviewedTarget_ - 1U].symbol;
      }
    }
    state.phonemeReview.status = reviewStatus_;
    state.phonemeReview.enabled = {source && reviewedEdit_ > 0U,
        source && reviewedEdit_ + 1U < reviewEditCount(), true,
        target && *reviewedTarget_ > 0U,
        source && !targets.empty() && (!target || *reviewedTarget_ + 1U < targets.size()),
        source && target};
  }
  if (const auto* focused = accessibilityTree_.focusedNode(); focused != nullptr &&
      (sampleMicroscopeOpen() == focused->id.starts_with("microscope."))) {
    state.focusedElementBounds = focused->bounds;
  }
  if (timeMapPanel_) {
    state.timeMapVisible = true;
    state.timeMapStale = !timeMapPanel_->matches(session_.project(), session_.revision());
    if (tempoEdit_) state.timeMapPrompt = tempoEdit_->chooseTick ? "ENTER NEW EVENT TICK IN THE TEXT FIELD / ESC CANCEL" :
        (tempoEdit_->meter ? "ENTER METER N/D / ESC CANCEL" : "ENTER BPM / ESC CANCEL");
    const auto rows = timeMapPanel_->page(timeMapPage_);
    for (const auto& row : rows) state.timeMapRows.push_back(std::to_string(row.tick.value()) +
        (row.meter ? " ticks   METER   " : " ticks   TEMPO   ") + row.value + (row.removable() ? "" : "   INITIAL"));
    if (timeMapPanel_->selectedIndex() / TempoMeterModel::pageSize == timeMapPage_)
      state.timeMapSelectedRow = timeMapPanel_->selectedIndex() % TempoMeterModel::pageSize;
  }
  applyLayoutTransition(state);
  state.replacementReview = replacementReviewView();
  state.replacementReview.page = replacementDetail_ ? replacementDetail_->page : replacementPage_;
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

std::string NativeEditorController::timeMapSemanticPrefix() const {
  if (!timeMapPanel_) return {};
  return "time-map." + std::to_string(timeMapInteraction_) + "." + session_.project().id().toString() + "." + std::to_string(session_.revision()) + "." +
      std::to_string(timeMapPanel_->revision()) + "." + std::to_string(timeMapPage_) + "." +
      std::to_string(timeMapPanel_->selectedIndex()) + "." +
      (tempoEdit_ ? std::to_string(tempoEdit_->tick.value()) + (tempoEdit_->meter ? "m" : "t") +
          (tempoEdit_->chooseTick ? "tick" : "value") : "idle") + ".";
}

core::Result<void> NativeEditorController::beginReplacementInput() {
  return beginSearchInput(false, false);
}

std::string NativeEditorController::replacementSemanticPrefix() const {
  return "replacement." + std::to_string(replacementInteraction_) + "." +
      (japaneseReadingMode_ ? "japanese-reading." + std::string(japaneseReadingDetail_ ? "detail." : "list.") : "") +
      (styleDraft_ ? "style-coverage." + std::string(styleIssues_ ? "issues." : "styles.") +
          (styleIssue_ ? "detail." + std::to_string(*styleIssue_) + "." : "list.") : "") +
      (clearVibrato_ ? "vibrato." : "") +
      (vibratoDraft_ ? "vibrato-inspector." + std::to_string(static_cast<std::size_t>(selectedVibratoField_)) + "." : "") +
      (dynamicsDraft_ ? "dynamics-inspector." + std::string(dynamicsPointEdit_ ? "point." : "list.") : "") +
      (findMode_ ? "find." + std::to_string(static_cast<int>(findField_)) + "." +
          (diagnosticFindMode_ ? "active-diagnostics." + std::to_string(static_cast<int>(diagnosticFindJob_.state())) + "." : "") +
          std::to_string(static_cast<int>(findJob_.state())) + "." +
          (findDetailIndex_ ? std::to_string(*findDetailIndex_) : "list") + "." : "") +
      (clearDynamics_ ? "dynamics." : "") +
      (noteCleanup_ ? "cleanup." + std::to_string(static_cast<int>(noteCleanup_->kind())) + "." : "") +
      session_.project().id().toString() + "." + std::to_string(session_.revision()) + "." +
      regionId_.toString() + "." + std::to_string(static_cast<int>(replacementJob_.state())) + "." +
      std::to_string(replacementPage_) + (replacementDependencies_ ? ".dependencies." : ".lyrics.") +
      (replacementDetail_ ? "detail." + replacementDetail_->lyricId.toString() + "." +
          std::to_string(replacementDetail_->side) + "." + std::to_string(replacementDetail_->page) + "." : "");
}

ReplacementReviewView NativeEditorController::replacementReviewView() const {
  ReplacementReviewView view; view.visible = replacementOpen_;
  if (!replacementOpen_) return view;
  view.enabled[4] = true;
  if (japaneseReadingMode_) {
    view.dockedInspector = true;
    const auto* review = japaneseReadingIdentity_ ? japaneseReadingJob_.current(session_, regionId_, *japaneseReadingIdentity_) : nullptr;
    const bool current = review != nullptr;
    view.status = japaneseReadingJob_.state() == authoring::JapaneseReadingJob::State::Preparing ?
        "Preparing Japanese contextual reading…" : current ? "Japanese dictionary reading — review before Apply" :
        replacementError_.empty() ? "Japanese reading is unavailable" : replacementError_;
    view.labels[0] = "Previous"; view.labels[1] = "Next"; view.labels[2] = japaneseReadingDetail_ ? "Back to readings" : "Inspect first";
    view.labels[3] = "Apply reading"; view.labels[4] = "Cancel"; view.labels[5] = "Retry";
    if (japaneseReadingDetail_) {
      const auto count = japaneseReadingDetailLines_.size(); const auto offset = replacementPage_ * 6U;
      view.summary = "Reading detail " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U)) +
          " / read-only";
      for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) view.rows.push_back(japaneseReadingDetailLines_[i]);
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count; view.enabled[2] = true; view.enabled[3] = false; view.enabled[5] = false;
      return view;
    }
    if (current) {
      const auto& tokens = review->reading.tokens; const auto offset = replacementPage_ * 6U;
      view.summary = std::to_string(tokens.size()) + " dictionary tokens / " + std::to_string(review->reading.tokens.size()) + " source spans";
      for (std::size_t i = offset; i < std::min(offset + 6U, tokens.size()); ++i) {
        const auto& token = tokens[i]; const auto& binding = review->bindings[i];
        std::string row = token.surface + " → " + (token.pronunciation ? *token.pronunciation : "(unresolved)") +
            " / owners " + std::to_string(binding.notes.size());
        if (binding.crossesLyrics) row += " / cross-lyric";
        if (binding.touchesExplicitHint) row += " / explicit hint";
        view.rows.push_back(std::move(row));
      }
      view.rowsInspectable = true; view.enabled[2] = !tokens.empty();
      const bool canApply = japaneseReadingIdentity_ && japaneseReadingJob_.canApplyCurrent(
          session_, regionId_, *japaneseReadingIdentity_);
      view.enabled[3] = canApply;
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < tokens.size(); view.enabled[5] = false;
    } else {
      view.summary = replacementError_.empty() ? "Waiting for the reading worker" : replacementError_;
      view.rows.push_back(replacementError_.empty() ? "No reading result yet" : "Open Diagnostics for the complete error");
      view.enabled[2] = false; view.enabled[3] = false; view.enabled[5] = japaneseReadingJob_.state() != authoring::JapaneseReadingJob::State::Preparing;
    }
    return view;
  }
  if (styleDraft_) {
    view.dockedInspector = true;
    const bool current = styleSourceCurrent();
    view.status = current ? "Style / structural coverage — draft only" : "Source changed; Refresh or Cancel";
    view.summary = styleDraft_->diagnostic();
    if (const auto& report = styleDraft_->coverage()) view.summary = std::to_string(report->summary.coveredPhonemes) + "/" +
        std::to_string(report->summary.totalPhonemes) + " phones covered; not audio QA";
    const auto& styles = styleDraft_->styles(); const auto offset = replacementPage_ * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, styles.size()); ++i)
      view.rows.push_back(std::string(styles[i].id == styleDraft_->selection().styleId ? "Selected: " : "Choose: ") + styles[i].id +
          " / enabled " + std::to_string(styles[i].enabled) + " / disabled " + std::to_string(styles[i].disabled));
    if (offset <= styles.size() && styles.size() < offset + 6U) view.rows.push_back("Configure PCM style crossfade");
    view.rowsInspectable = current;
    view.labels[2] = "Draft only"; view.labels[3] = "Apply track style"; view.labels[4] = "Cancel draft"; view.labels[5] = "Refresh";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < styles.size() + 1U;
    view.enabled[3] = current && styleDraft_->hasChanges() && !styleDraft_->selection().styleId.empty(); view.enabled[5] = true;
    view.labels[2] = "Coverage details"; view.enabled[2] = true;
    if (styleBlendMode_) {
      view.status = current ? "PCM style crossfade — draft only" : "Source changed; Refresh or Cancel";
      view.labels[2] = styleBlendChoosingSecondary_ ? "Back to pair" : "Back to styles";
      view.summary = "Render checks both styles' timing and local cancellation; not voice-morph or listening approval";
      if (styleBlendChoosingSecondary_) {
        view.rows.clear();
        for (std::size_t i = offset; i < std::min(offset + 6U, styles.size()); ++i)
          view.rows.push_back("Secondary: " + styles[i].id);
        view.enabled[1] = offset + 6U < styles.size();
      } else {
        const auto& selection = styleDraft_->selection();
        const auto percent = selection.blend ? std::to_string(static_cast<int>(std::lround(selection.blend->amount * 100.0F))) : "0";
        view.rows = {"Primary: " + selection.styleId + " (back to styles to change)",
            "Choose secondary: " + (selection.blend ? selection.blend->targetStyleId : std::string{"none"}),
            "Decrease by 5% (current " + percent + "%)", "Increase by 5% (current " + percent + "%)", "Disable crossfade pair"};
        view.enabled[0] = false; view.enabled[1] = false;
        if (const auto& secondary = styleDraft_->secondaryCoverage()) view.summary =
            "Secondary coverage " + std::to_string(secondary->summary.coveredPhonemes) + "/" +
            std::to_string(secondary->summary.totalPhonemes) + "; timing and phase checked when rendered, not audio approval";
      }
      return view;
    }
    if (styleIssues_) {
      const auto& report = styleDraft_->coverage();
      view.rows.clear(); view.rowsInspectable = current && !styleIssue_;
      view.labels[2] = styleIssue_ ? "Back to issues" : "Back to styles";
      const auto count = styleIssue_ ? styleDetailLines_.size() : report ? report->issues.size() : 1U;
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count;
      view.status = current ? (styleIssue_ ? "Coverage issue details — read only" : "Coverage issues — read only") : "Source changed; Refresh or Cancel";
      view.summary = "Page " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1}, (count + 5U) / 6U)) +
          (report ? " / structural coverage, not audio QA" : " / coverage unavailable");
      for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
        if (styleIssue_) view.rows.push_back(styleDetailLines_[i]);
        else if (!report) view.rows.push_back("Inspect why coverage is unavailable");
        else view.rows.push_back(std::string(voicebank::coverageIssueKindName(report->issues[i].kind)) + ": " + report->issues[i].symbol);
      }
      if (!styleIssue_ && report && report->issues.empty()) view.summary = "No structural issues; audio QA still required";
    }
    return view;
  }
  if (dynamicsDraft_) {
    view.dockedInspector = true;
    const bool current = dynamicsDraft_->matches(session_, regionId_);
    const auto influence = dynamicsDraft_->influence();
    view.status = !current ? "Source changed; Refresh or Cancel" : influence.generatedSelections == 0U
        ? "Region dynamics — native curve draft"
        : "Region: " + std::to_string(influence.generatedSelections) + " generated / " +
          std::to_string(influence.manualReplacementScopes) + " native Replace scopes";
    view.summary = replacementError_.empty() ? (influence.generatedSelections > 0U
        ? "Generated dynamics may override this curve"
        : "ENTIRE region / no generated dynamics selected") : replacementError_;
    view.labels[4] = "Cancel draft";
    ReplacementReviewView::DynamicsPlot plot;
    plot.bounds = layout_.dynamicsPlotBounds(logicalWidth_, logicalHeight_); plot.editable = current;
    plot.influenceDescription = std::to_string(influence.generatedSelections) + " accepted dynamics selections; " +
        std::to_string(influence.manualReplacementScopes) + " manual dynamics replacement scopes. " +
        "Non-null generated dynamics values override the native curve inside accepted scopes unless a manual Dynamics Replace scope applies. " +
        "A native curve edit does not claim ownership or remove generated selections. Scope counts are not rendered coverage. " +
        "Articulation, track gain and renderer processing may further change the audible level.";
    const auto* region = session_.project().findRegion(regionId_);
    plot.endTick = region ? std::max(std::int64_t{1}, region->durationTick.value()) : 1;
    for (const auto* curve : {&dynamicsDraft_->sourceCurve(), &dynamicsDraft_->curve()})
      if (!curve->points().empty()) plot.endTick = std::max(plot.endTick, curve->points().back().tick.value());
    const auto candidate = dynamicsPointValue();
    if (candidate) plot.endTick = std::max(plot.endTick, candidate.value().tick.value());
    plot.fullEndTick = plot.endTick;
    const auto visible = dynamicsViewport_.resolve(plot.fullEndTick); plot.startTick = visible.start; plot.endTick = visible.end;
    const auto navigation = layout_.reviewRowBounds(logicalWidth_, logicalHeight_, 2U, true);
    const auto navWidth = (navigation.width - 12.0) / 4.0;
    for (std::size_t i = 0U; i < 4U; ++i) plot.navigation[i] = {navigation.x + static_cast<double>(i) * (navWidth + 4.0), navigation.y, navWidth, navigation.height};
    const auto inView = [&](time::Tick tick) { return tick.value() >= plot.startTick && tick.value() <= plot.endTick; };
    const auto position = [&](domain::DynamicsAutomationPoint point) {
      return ui::Point{plot.bounds.x + plot.bounds.width * (static_cast<double>(point.tick.value() - plot.startTick) / static_cast<double>(plot.endTick - plot.startTick)),
          plot.bounds.y + plot.bounds.height * (1.0 - static_cast<double>(point.linearGain) / domain::kMaximumDynamicsGain)};
    };
    const auto line = [&](const domain::DynamicsAutomation& curve) {
      std::vector<ui::Point> points; points.reserve(curve.points().size() + 2U);
      points.push_back(position({time::Tick{plot.startTick}, curve.valueAt(time::Tick{plot.startTick})}));
      for (const auto& point : curve.points()) if (inView(point.tick)) points.push_back(position(point));
      points.push_back(position({time::Tick{plot.endTick}, curve.valueAt(time::Tick{plot.endTick})})); return points;
    };
    plot.score = line(dynamicsDraft_->sourceCurve()); plot.draft = line(dynamicsDraft_->curve());
    plot.targetStatus = dynamicsDraft_->targetReady() ? "Cyan dots: sampled staged score dynamics. Orange marks: selected generated dynamics before manual replacement. Per voice; not measured audio or final amplitude. Missing generated marks mean no selected non-null generated value."
        : "Target unavailable: " + dynamicsDraft_->targetError();
    if (current && dynamicsDraft_->targetReady()) for (const auto& sample : dynamicsDraft_->targetSamples()) {
      if (!inView(sample.tick)) continue;
      plot.target.push_back(position({sample.tick, sample.linearGain}));
      if (sample.selectedGeneratedGain) plot.selectedGenerated.push_back(position({sample.tick, *sample.selectedGeneratedGain}));
    }
    if (!dynamicsDraft_->targetReady() && replacementError_.empty()) view.summary = dynamicsDraft_->targetError().empty()
        ? "Target unavailable; native editing remains available" : dynamicsDraft_->targetError();
    const auto& draftPoints = dynamicsDraft_->curve().points();
    if (!dynamicsPointEdit_) for (std::size_t i = replacementPage_ * 2U; i < std::min(replacementPage_ * 2U + 2U, draftPoints.size()); ++i)
      if (inView(draftPoints[i].tick)) plot.handles.push_back({position(draftPoints[i]), i % 2U});
    if (candidate && inView(candidate.value().tick)) plot.candidate = position(candidate.value());
    plot.measurementAvailable = measurementCoordinator_ != nullptr; plot.measuredMode = measuredChannel_ != 0U;
    plot.measuredChannel = std::max(std::size_t{1U}, measuredChannel_);
    if (plot.measuredMode) {
      plot.handles.clear(); plot.candidate.reset(); plot.measurementLabel = measurementStatus_;
      if (current) view.status = "Measured rendered output (read-only)";
      view.summary = measurementStatus_;
      const auto measurementAudio = measurementCoordinator_ ? measurementCoordinator_->acquireCurrent()
          : authoring::RealtimeProjectAudioPublication::ReadHandle{};
      const auto* measured = measurementCoordinator_ && current ? measurementJob_.current(session_, *measurementCoordinator_) : nullptr;
      if (measured && measurementWindow_ && measurementAudio && measurementAudio->sourceIdentity == measurementWindow_->identity &&
          measurementAudio->requestId == measurementWindow_->requestId && measured->firstFrame == measurementWindow_->first &&
          measured->frameCount == measurementWindow_->count && measured->channelCount > 0U) {
        plot.measuredChannel = std::min(measuredChannel_, static_cast<std::size_t>(measured->channelCount));
        const auto channel = plot.measuredChannel - 1U;
        double squares = 0.0, peak = 0.0; std::size_t fullScale = 0U;
        for (const auto& bin : measured->bins) {
          const auto& level = bin.channels[channel];
          if (const auto db = level.rmsDbfs()) plot.measuredCeilingDb = std::max(plot.measuredCeilingDb, std::ceil(*db / 6.0) * 6.0);
          squares += level.rms * level.rms * static_cast<double>(bin.frameCount); peak = std::max(peak, level.peak);
          fullScale += level.atOrAboveFullScale;
        }
        const auto dbText = [](double amplitude) {
          if (amplitude == 0.0) return std::string{"silence"};
          std::array<char, 32> buffer{};
          const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), 20.0 * std::log10(amplitude), std::chars_format::fixed, 2);
          return std::string(buffer.data(), result.ptr);
        };
        view.summary = "RMS " + dbText(std::sqrt(squares / static_cast<double>(measured->frameCount))) +
            " / peak " + dbText(peak) + " dBFS / FS " + std::to_string(fullScale);
        const auto* audio = measurementAudio.get();
        if (audio) {
          plot.measurementLabel = "Rendered output ch" + std::to_string(plot.measuredChannel) + " RMS dBFS / " +
              (audio->quality == rendering::RenderQuality::Final ? "Final" : "Preview") + " / rev " + std::to_string(audio->projectRevision);
          for (const auto& bin : measured->bins) {
            const auto frame = bin.firstFrame + (bin.frameCount - 1U) / 2U;
            const auto tick = session_.project().tempoMap().tickAtSampleFrame(static_cast<time::SampleFrame>(frame), audio->result.sampleRate) - region->startTick;
            if (!inView(tick)) continue;
            const auto db = bin.channels[channel].rmsDbfs().value_or(-96.0);
            plot.measured.push_back({plot.bounds.x + plot.bounds.width * static_cast<double>(tick.value() - plot.startTick) / static_cast<double>(plot.endTick - plot.startTick),
                plot.bounds.y + plot.bounds.height * (1.0 - (std::clamp(db, -96.0, plot.measuredCeilingDb) + 96.0) / (plot.measuredCeilingDb + 96.0))});
          }
        }
      }
    }
    if (plot.bounds.width >= 16.0 && plot.bounds.height >= 16.0) view.dynamicsPlot = std::move(plot);
    if (dynamicsPointEdit_) {
      if (current) view.status = measuredChannel_ != 0U ? "Point fields / read-only measured plot" : "Point draft: drag gain / Shift-drag time";
      view.rows = {"Region tick: " + dynamicsPointEdit_->tickText, "Linear gain: " + dynamicsPointEdit_->gainText};
      view.rowsInspectable = current;
      view.labels[2] = "Delete point"; view.enabled[2] = current && dynamicsPointEdit_->source.has_value();
      view.labels[3] = "Save to draft"; view.enabled[3] = current && static_cast<bool>(dynamicsPointValue());
      view.labels[5] = "Back to curve"; view.enabled[5] = true;
      return view;
    }
    const auto& points = dynamicsDraft_->curve().points(); const auto offset = replacementPage_ * 2U;
    for (std::size_t i = offset; i < std::min(offset + 2U, points.size()); ++i) {
      std::array<char, 32> buffer{}; const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), points[i].linearGain);
      view.rows.push_back("Tick " + std::to_string(points[i].tick.value()) + " / gain " + std::string(buffer.data(), formatted.ptr));
    }
    view.rowsInspectable = current && !points.empty();
    if (points.empty()) view.rows.push_back("No native points: unity gain (1)");
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 2U < points.size();
    view.labels[2] = "Add point"; view.enabled[2] = current && points.size() < domain::kMaximumDynamicsPoints;
    view.labels[3] = "Apply region curve"; view.enabled[3] = current && dynamicsDraft_->hasChanges();
    view.labels[5] = "Refresh / reset draft"; view.enabled[5] = true;
    return view;
  }
  if (vibratoDraft_) {
    view.dockedInspector = true;
    const bool current = vibratoDraft_->current(session_, regionId_);
    view.status = current ? "Vibrato inspector — draft only" : "Selection changed; Refresh or Cancel";
    const auto error = replacementError_.empty() ? vibratoDraft_->error() : replacementError_;
    view.summary = error.empty() ? std::to_string(vibratoDraft_->selectedCount()) + " selected / " + std::to_string(vibratoDraft_->changedCount()) + " changes / " +
        std::string{VibratoInspectorDraft::label(selectedVibratoField_)} + " / page " + std::to_string(replacementPage_ + 1U) + "/2" : error;
    const auto offset = replacementPage_ * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, static_cast<std::size_t>(VibratoField::Count)); ++i) {
      const auto field = static_cast<VibratoField>(i);
      view.rows.push_back(std::string{VibratoInspectorDraft::label(field)} + ": " + vibratoDraft_->text(field));
    }
    view.rowsInspectable = current; view.labels[2] = "Reset selected field"; view.labels[3] = "Apply to Selection"; view.labels[4] = "Cancel draft";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < static_cast<std::size_t>(VibratoField::Count);
    view.enabled[2] = current; view.enabled[3] = error.empty() && vibratoDraft_->canApply(session_, regionId_); view.enabled[5] = true;
    return view;
  }
  if (findMode_) {
    if (findPreparing()) {
      view.status = "Preparing Find results...";
      view.summary = "Search runs on a private snapshot; Close Find cancels";
      view.labels[2] = "Search field"; view.labels[3] = "Inspect result";
      view.labels[4] = "Close Find";
      return view;
    }
    if (diagnosticFindMode_) {
      const bool current = diagnosticFindReview_ && diagnosticFindReview_->matches(session_, diagnosticPanel_);
      const auto count = diagnosticFindReview_ ? diagnosticFindReview_->snapshot().hits().size() : 0U;
      view.status = replacementError_.empty() ? (current ? "Find: Active diagnostics" : "Diagnostic results changed. Refresh first.") : replacementError_;
      view.summary = std::to_string(count) + " matching diagnostics / no automatic recovery actions";
      view.labels[2] = "Search lyrics"; view.labels[3] = "Inspect first result"; view.labels[4] = "Close Find";
      view.enabled[2] = session_.project().findRegion(regionId_) != nullptr; view.enabled[5] = true;
      if (replacementDetail_ && findDetailIndex_) {
        const auto& detail = *replacementDetail_; const auto& lines = detail.lines[0]; const auto offset = detail.page * 6U;
        view.summary = "Diagnostic " + std::to_string(*findDetailIndex_ + 1U) + "/" + std::to_string(count) +
            " / page " + std::to_string(detail.page + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (lines.size() + 5U) / 6U));
        for (std::size_t i = offset; i < std::min(offset + 6U, lines.size()); ++i)
          view.rows.push_back(detail.text[0].substr(lines[i].offset, lines[i].length));
        view.enabled[0] = detail.page > 0U; view.enabled[1] = offset + 6U < lines.size();
        view.enabled[2] = false; view.labels[3] = "Read-only diagnostic"; view.labels[4] = "Back to results";
      } else {
        const auto offset = replacementPage_ * 6U;
        view.summary += " / " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U));
        for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
          const auto& snapshot = diagnosticFindReview_->snapshot(); const auto& hit = snapshot.hits()[i];
          view.rows.push_back(snapshot.source()[hit.sourceIndex].code + " / " + domain::toUtf8(hit.matchedText));
        }
        view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count;
        view.rowsInspectable = current && count > 0U; view.enabled[3] = current && offset < count;
        if (count == 0U && replacementError_.empty()) view.rows.push_back("No matching active diagnostics");
      }
      return view;
    }
    constexpr std::array<const char*, 5U> fields{"Lyrics", "Hints", "Note IDs", "Resolved phones (Japanese)", "Pronunciation warnings (Japanese)"};
    const auto fieldIndex = static_cast<std::size_t>(findField_);
    const std::string fieldName = fieldIndex < fields.size() ? fields[fieldIndex] : "Invalid field";
    const bool current = findNavigation_ && findNavigation_->matches(session_, regionId_);
    const auto count = findNavigation_ ? findNavigation_->result().hits.size() : 0U;
    view.status = replacementError_.empty() ? (current ? "Find: " + fieldName : "Find results changed. Refresh first.") : replacementError_;
    view.summary = std::to_string(count) + " matching notes / " + fieldName;
    view.labels[2] = "Next search field"; view.labels[3] = "Inspect first result";
    view.labels[4] = "Close Find";
    view.enabled[2] = regionId_ == replacementRegion_;
    view.enabled[5] = regionId_ == replacementRegion_;
    if (replacementDetail_ && findDetailIndex_) {
      const auto& detail = *replacementDetail_; const auto& lines = detail.lines[0];
      const auto offset = detail.page * 6U;
      view.summary = "Match " + std::to_string(*findDetailIndex_ + 1U) + "/" + std::to_string(count) +
          " / text page " + std::to_string(detail.page + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (lines.size() + 5U) / 6U));
      for (std::size_t i = offset; i < std::min(offset + 6U, lines.size()); ++i)
        view.rows.push_back(detail.text[0].substr(lines[i].offset, lines[i].length));
      view.labels[2] = "Read-only result"; view.labels[3] = "Select and reveal note"; view.labels[4] = "Back to results";
      view.enabled[0] = detail.page > 0U; view.enabled[1] = offset + 6U < lines.size();
      view.enabled[2] = false; view.enabled[3] = current; view.enabled[5] = false;
    } else {
      const auto offset = replacementPage_ * 6U;
      view.summary += " / page " + std::to_string(replacementPage_ + 1U) + "/" +
          std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U));
      for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
        const auto& hit = findNavigation_->result().hits[i];
        view.rows.push_back(hit.noteId.toString() + " / " + std::to_string(hit.start.value()) + " ticks / " + domain::toUtf8(hit.matchedText));
      }
      view.rowsInspectable = current && count > 0U;
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count;
      view.enabled[3] = current && offset < count;
      if (count == 0U && replacementError_.empty()) view.rows.push_back("No matching notes in this field");
    }
    return view;
  }
  if (clearDynamics_) {
    const auto points = clearDynamics_->points(); const auto offset = replacementPage_ * 6U;
    const bool current = clearDynamics_->matches(session_, regionId_);
    view.status = current ? (clearDynamics_->retainedGeneratedSelections() > 0U
        ? "Clear ENTIRE region native curve; generated dynamics may still apply"
        : "Clear native dynamics curve for the ENTIRE region") : "Region changed. Refresh before clearing dynamics.";
    if (!replacementError_.empty()) view.status = replacementError_;
    view.summary = std::to_string(clearDynamics_->regionNoteCount()) + " region notes / " + std::to_string(points.size()) +
        " points / " + std::to_string(clearDynamics_->retainedGeneratedSelections()) + " generated selections retained";
    view.summary += " / " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (points.size() + 5U) / 6U));
    for (std::size_t i = offset; i < std::min(offset + 6U, points.size()); ++i)
      view.rows.push_back(std::to_string(points[i].tick.value()) + " ticks: gain " + std::to_string(points[i].linearGain) + " -> native default 1.0");
    if (points.empty()) view.rows.push_back("Native region dynamics curve is already empty");
    view.labels[2] = "Generated takes kept"; view.labels[3] = "Clear region curve";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < points.size();
    view.enabled[3] = current && replacementError_.empty() && clearDynamics_->hasChanges();
    view.enabled[5] = regionId_ == replacementRegion_; return view;
  }
  if (noteCleanup_) {
    const auto rows = noteCleanup_->rows(); const auto dependencies = noteCleanup_->dependencies();
    const auto count = replacementDependencies_ ? dependencies.size() : rows.size(); const auto offset = replacementPage_ * 6U;
    const bool current = noteCleanup_->matches(session_, regionId_);
    const bool overlap = noteCleanup_->kind() == ui::NoteCleanupKind::RemoveOverlap;
    const bool legato = noteCleanup_->kind() == ui::NoteCleanupKind::AutoLegato;
    view.status = current ? (legato ? "Auto legato: adjacent selected notes; gap at most one grid" :
        (overlap ? "Review overlap removal; note starts stay fixed" : "Review gap closure; off-grid/staccato notes may be skipped")) : "Targets or grid changed. Refresh before applying.";
    if (!replacementError_.empty()) view.status = replacementError_;
    view.summary = std::to_string(rows.size()) + " selected / " + std::to_string(noteCleanup_->changedCount()) + " changes / grid " +
        std::to_string(noteCleanup_->grid().value()) + " / " + (replacementDependencies_ ? "dependencies " : "notes ") +
        std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U));
    for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
      if (!replacementDependencies_) {
        const auto& row = rows[i];
        view.rows.push_back(row.noteId.toString() + ": " + std::to_string(row.beforeDuration.value()) + " -> " +
            std::to_string(row.afterDuration.value()) + " ticks / " + ui::noteCleanupOutcomeName(row.outcome));
        if (row.beforeArticulation != row.afterArticulation) view.rows.back() += " / set legato";
      } else {
        const auto& row = dependencies[i];
        const auto status = [](std::optional<bool> state) { return !state ? "absent" : (*state ? "unresolved" : "resolved"); };
        const auto kind = row.kind == ui::DependentEditKind::Phoneme ? "Phone " : (row.kind == ui::DependentEditKind::Unit ? "Unit " : "Seam ");
        view.rows.push_back(std::string{kind} + row.key.toString() + " " + status(row.beforeUnresolved) + " -> " + status(row.afterUnresolved));
      }
    }
    if (count == 0U) view.rows.push_back("No retained dependency records");
    view.labels[2] = "Notes / dependencies"; view.labels[3] = legato ? "Auto legato" : (overlap ? "Remove overlaps" : "Close gaps");
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count; view.enabled[2] = true;
    view.enabled[3] = current && replacementError_.empty() && noteCleanup_->changedCount() > 0U;
    view.enabled[5] = regionId_ == replacementRegion_; return view;
  }
  if (clearVibrato_) {
    const auto edits = clearVibrato_->edits();
    const bool current = clearVibrato_->matches(session_, regionId_);
    view.status = current ? "Clear vibrato; saved settings and other expressions stay intact" : "Targets changed. Refresh before clearing vibrato.";
    if (!replacementError_.empty()) view.status = replacementError_;
    view.summary = std::to_string(clearVibrato_->selectedCount()) + " selected / " + std::to_string(edits.size()) +
        " enabled vibratos / page " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (edits.size() + 5U) / 6U));
    const auto offset = replacementPage_ * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, edits.size()); ++i)
      view.rows.push_back(edits[i].noteId.toString() + ": enabled -> disabled");
    if (edits.empty()) view.rows.push_back("Selected notes have no enabled vibrato");
    view.labels[2] = "Settings preserved"; view.labels[3] = "Clear vibrato";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < edits.size();
    view.enabled[3] = current && replacementError_.empty() && !edits.empty();
    view.enabled[5] = regionId_ == replacementRegion_;
    return view;
  }
  view.status = replacementDistribution_ ? "Preparing distribution review..." : "Preparing replacement review...";
  if (replacementDistribution_) view.labels[3] = "Apply distribution";
  const auto* review = replacementJob_.review();
  if (!review) {
    if (!replacementError_.empty()) { view.status = replacementError_; view.summary = replacementErrorContext_; }
    else if (replacementJob_.state() != ui::LyricReplacementJob::State::Preparing) view.status = "Review is no longer available";
    view.enabled[5] = replacementJob_.state() != ui::LyricReplacementJob::State::Preparing && regionId_ == replacementRegion_;
    return view;
  }
  const bool current = review->matches(session_, regionId_);
  view.status = current ? "Review before applying; one undo group" : "Source changed. Refresh before applying.";
  if (!replacementError_.empty()) view.status = replacementError_;
  if (replacementDetail_) {
    const auto& detail = *replacementDetail_;
    const auto& lines = detail.lines[detail.side];
    view.summary = detail.lyricId.toString() + (detail.side == 0U ? " BEFORE " : " AFTER ") +
        std::to_string(detail.page + 1U) + "/" + std::to_string((lines.size() + 5U) / 6U);
    const auto offset = detail.page * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, lines.size()); ++i) {
      const auto range = lines[i];
      view.rows.push_back(detail.text[detail.side].substr(range.offset, range.length));
    }
    view.labels[2] = detail.side == 0U ? "Show after" : "Show before";
    view.labels[3] = "Back to results";
    view.enabled[0] = detail.page > 0U; view.enabled[1] = offset + 6U < lines.size();
    view.enabled[2] = true; view.enabled[3] = true;
    view.enabled[5] = regionId_ == replacementRegion_;
    return view;
  }
  view.summary = std::to_string(review->preview().changedNotes()) + " notes / " +
      std::to_string(review->preview().edits().size()) + " lyrics / " +
      std::to_string(review->preview().replacements()) + (replacementDistribution_ ? " assignments" : " replacements");
  if (replacementDistribution_) view.summary += " / " + std::to_string(review->preview().matchedNotes()) + " selected notes";
  const auto count = replacementDependencies_ ? review->dependencyCount() : review->preview().edits().size();
  view.summary += " / " + std::string(replacementDependencies_ ? "Dependencies " : "Lyrics ") +
      std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1}, (count + 5U) / 6U));
  if (replacementDependencies_) {
    const auto status = [](std::optional<bool> value) { return !value ? "absent" : (*value ? "unresolved" : "resolved"); };
    for (const auto& row : review->dependenciesPage(replacementPage_)) {
      const auto kind = row.kind == ui::ReplacementDependencyKind::Phoneme ? "Phone " :
          (row.kind == ui::ReplacementDependencyKind::Unit ? "Unit " : "Seam ");
      view.rows.push_back(std::string{kind} + row.key.toString() + "  " + status(row.beforeUnresolved) + " -> " + status(row.afterUnresolved));
    }
  } else {
    for (const auto& row : review->editsPage(replacementPage_))
      view.rows.push_back(row.lyricId.toString() + ": " + domain::toUtf8(row.before) + " -> " + domain::toUtf8(row.after));
  }
  if (view.rows.empty()) view.rows.push_back(replacementDependencies_ ? "No retained dependency records" : "No lyrics would change");
  view.enabled[0] = replacementPage_ > 0U;
  view.enabled[1] = (replacementPage_ + 1U) * 6U < count;
  view.enabled[2] = true; view.enabled[3] = current && replacementError_.empty() && !review->preview().edits().empty();
  view.enabled[5] = regionId_ == replacementRegion_;
  view.rowsInspectable = !replacementDependencies_ && !review->preview().edits().empty();
  return view;
}

core::Result<void> NativeEditorController::openReplacementRow(std::size_t pageRow) {
  if (japaneseReadingMode_ && replacementOpen_) {
    if (japaneseReadingDetail_ || !japaneseReadingIdentity_ ||
        japaneseReadingJob_.state() != authoring::JapaneseReadingJob::State::Ready)
      return core::failure(core::ErrorCode::Conflict, "Japanese reading row is unavailable or already open");
    const auto* review = japaneseReadingJob_.current(session_, regionId_, *japaneseReadingIdentity_);
    if (!review) return core::failure(core::ErrorCode::Conflict, "Japanese reading result is stale");
    const auto index = replacementPage_ * 6U + pageRow;
    if (pageRow >= 6U || index >= review->reading.tokens.size()) return core::failure(core::ErrorCode::NotFound, "Japanese reading row is unavailable");
    const auto& token = review->reading.tokens[index]; const auto& binding = review->bindings[index];
    std::string detail = "Surface: " + token.surface + "\nStatus: " +
        (token.status == phonemizer::JapaneseReadingStatus::Known ? "known" : token.status == phonemizer::JapaneseReadingStatus::Unknown ? "unknown" : "missing-reading") +
        "\nLexical reading: " + (token.lexicalReading ? *token.lexicalReading : "(none)") +
        "\nSinging pronunciation: " + (token.pronunciation ? *token.pronunciation : "(none)") +
        "\nSource bytes: " + std::to_string(token.byteOffset) + ".." + std::to_string(token.byteOffset + token.byteLength) +
        "\nOwners: " + std::to_string(binding.notes.size()) + (binding.crossesLyrics ? " (cross-lyric)" : "") +
        (binding.touchesExplicitHint ? "\nTouches explicit phone hint; Apply is blocked." : "") +
        (binding.crossesUnownedText ? "\nCrosses unowned source text; Apply is blocked." : "") +
        "\nStructural dictionary evidence only; native-language and audio review remain required.";
    auto lines = text::wrapUtf8ToDisplayWidth(detail, 32U); if (!lines) return core::Result<void>{lines.error()};
    std::vector<std::string> rendered; rendered.reserve(lines.value().size());
    for (const auto& line : lines.value()) rendered.push_back(detail.substr(line.offset, line.length));
    japaneseReadingDetailLines_ = std::move(rendered); japaneseReadingDetail_ = index; replacementPage_ = 0U; ++replacementInteraction_; repaint(); return core::success();
  }
  if (styleDraft_ && replacementOpen_) {
    if (styleBlendMode_) {
      if (!styleSourceCurrent() || replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
        return core::failure(core::ErrorCode::Conflict, "Style pair source is stale or interaction identity is exhausted");
      core::Result<void> chosen = core::success();
      if (styleBlendChoosingSecondary_) {
        const auto index = replacementPage_ * 6U + pageRow;
        if (pageRow >= 6U || index >= styleDraft_->styles().size()) return core::failure(core::ErrorCode::NotFound, "Secondary style row is unavailable");
        const auto& blend = styleDraft_->selection().blend;
        chosen = styleDraft_->chooseBlend(styleDraft_->styles()[index].id, blend ? blend->amount : 0.5F);
        if (chosen) { styleBlendChoosingSecondary_ = false; replacementPage_ = 0U; }
      } else {
        const auto& blend = styleDraft_->selection().blend;
        if (pageRow == 0U) { styleBlendMode_ = false; replacementPage_ = 0U; }
        else if (pageRow == 1U) { styleBlendChoosingSecondary_ = true; replacementPage_ = 0U; }
        else if ((pageRow == 2U || pageRow == 3U) && blend) chosen = styleDraft_->chooseBlend(blend->targetStyleId,
            static_cast<float>(std::clamp(std::round(static_cast<double>(blend->amount) * 20.0) + (pageRow == 2U ? -1.0 : 1.0), 0.0, 20.0) / 20.0));
        else if (pageRow == 4U) chosen = styleDraft_->clearBlend();
        else return core::failure(core::ErrorCode::Conflict, "Choose a secondary style before adjusting its crossfade amount");
      }
      ++replacementInteraction_; repaint(); return chosen;
    }
    if (styleIssues_) {
      const auto& report = styleDraft_->coverage(); const auto index = replacementPage_ * 6U + pageRow;
      if (styleIssue_ || pageRow >= 6U || index >= (report ? report->issues.size() : 1U) || !styleSourceCurrent())
        return core::failure(core::ErrorCode::Conflict, "Coverage issue is stale or unavailable");
      if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
        return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
      std::string detail = styleDraft_->diagnostic();
      if (report) {
        const auto& issue = report->issues[index];
        detail = std::string(voicebank::coverageIssueKindName(issue.kind)) + "\nPhone: " + issue.symbol +
            "\nStyle: " + issue.requestedStyle + "\nMIDI: " + std::to_string(issue.targetMidi) +
            "\nNote: " + issue.phonemeKey.noteId.toString() + "\nPhone ordinal (zero-based): " + std::to_string(issue.phonemeKey.ordinal) + "\n" + issue.diagnostic;
        for (const auto& unit : issue.relatedUnitIds) detail += "\nRelated unit: " + unit;
        detail += "\nStructural evidence only; not audio QA.";
      }
      auto lines = text::wrapUtf8ToDisplayWidth(detail, 32U); if (!lines) return core::Result<void>{lines.error()};
      std::vector<std::string> renderedLines; renderedLines.reserve(lines.value().size());
      for (const auto& line : lines.value()) renderedLines.push_back(detail.substr(line.offset, line.length));
      styleDetailLines_ = std::move(renderedLines); styleIssue_ = index; replacementPage_ = 0U;
      ++replacementInteraction_; repaint(); return core::success();
    }
    if (!styleSourceCurrent() ||
        pageRow >= 6U || replacementPage_ * 6U + pageRow > styleDraft_->styles().size())
      return core::failure(core::ErrorCode::Conflict, "Style row is stale or unavailable");
    if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
    if (replacementPage_ * 6U + pageRow == styleDraft_->styles().size()) {
      styleBlendMode_ = true; styleBlendChoosingSecondary_ = false; replacementPage_ = 0U;
      ++replacementInteraction_; repaint(); return core::success();
    }
    const auto chosen = styleDraft_->choose(styleDraft_->styles()[replacementPage_ * 6U + pageRow].id);
    ++replacementInteraction_; repaint(); return chosen;
  }
  if (dynamicsDraft_ && replacementOpen_) {
    if (!dynamicsDraft_->matches(session_, regionId_) || pageRow >= 2U)
      return core::failure(core::ErrorCode::Conflict, "Dynamics row is stale or unavailable");
    if (dynamicsPointEdit_) {
      if (pageRow > 1U) return core::failure(core::ErrorCode::NotFound, "Dynamics field is unavailable");
      return beginDynamicsFieldInput(pageRow == 0U);
    }
    const auto& points = dynamicsDraft_->curve().points(); const auto index = replacementPage_ * 2U + pageRow;
    if (index >= points.size()) return core::failure(core::ErrorCode::NotFound, "Dynamics point is unavailable");
    if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
    std::array<char, 32> buffer{}; const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), points[index].linearGain);
    dynamicsPointEdit_ = DynamicsPointEdit{points[index].tick, std::to_string(points[index].tick.value()), std::string(buffer.data(), formatted.ptr)};
    ++replacementInteraction_; replacementError_.clear(); repaint(); return core::success();
  }
  if (vibratoDraft_ && replacementOpen_) {
    const auto field = replacementPage_ * 6U + pageRow;
    if (pageRow >= 6U || field >= static_cast<std::size_t>(VibratoField::Count)) return core::failure(core::ErrorCode::NotFound, "Vibrato field is unavailable");
    return beginVibratoFieldInput(static_cast<VibratoField>(field));
  }
  if (findMode_ && diagnosticFindMode_ && replacementOpen_ && !replacementDetail_) {
    if (!diagnosticFindReview_ || pageRow >= 6U)
      return core::failure(core::ErrorCode::Conflict, "Diagnostic result is unavailable");
    const auto index = replacementPage_ * 6U + pageRow;
    const auto source = diagnosticFindReview_->resolve(session_, diagnosticPanel_, index);
    if (!source) return core::Result<void>{source.error()};
    const auto& diagnostic = diagnosticFindReview_->snapshot().source()[source.value()];
    const auto presentation = presentDiagnostic(diagnostic);
    ReplacementDetail detail;
    detail.text[0] = presentation.title + "\n" + presentation.impact + "\nCode: " + diagnostic.code +
        "\nMessage: " + diagnostic.messageKey + "\nSeverity: " + std::string{authoring::toString(diagnostic.severity)} +
        "\nOccurrences: " + std::to_string(diagnostic.occurrenceCount);
    if (!diagnostic.detail.empty()) {
      detail.text[0] += "\nDetail";
      if (diagnostic.detailTruncated) detail.text[0] += " [truncated]";
      if (diagnostic.detailEscaped) detail.text[0] += " [escaped bytes]";
      detail.text[0] += ": " + diagnostic.detail;
    }
    if (diagnostic.affectedIds.empty()) detail.text[0] += "\nScope: global or unspecified; no note binding";
    else {
      detail.text[0] += "\nReferences (opaque; not note targets):";
      for (const auto& id : diagnostic.affectedIds) detail.text[0] += "\n" + id;
    }
    detail.text[0] += "\nAvailable recovery actions (not executed here):";
    for (const auto action : diagnostic.actions) detail.text[0] += "\n" + diagnosticActionLabel(action);
    auto lines = text::wrapUtf8ToDisplayWidth(detail.text[0], 32U);
    if (!lines) return core::Result<void>{lines.error()};
    detail.lines[0] = std::move(lines.value()); replacementDetail_.emplace(std::move(detail)); findDetailIndex_ = index;
    rebuildAccessibilityTree(); repaint(); return core::success();
  }
  if (findMode_ && replacementOpen_ && !replacementDetail_) {
    if (!findNavigation_ || !findNavigation_->matches(session_, regionId_) || pageRow >= 6U ||
        replacementPage_ * 6U + pageRow >= findNavigation_->result().hits.size())
      return core::failure(core::ErrorCode::Conflict, "Find row is stale or unavailable");
    const auto index = replacementPage_ * 6U + pageRow;
    const auto& hit = findNavigation_->result().hits[index];
    ReplacementDetail detail; detail.lyricId = hit.lyricId; detail.text[0] = domain::toUtf8(hit.matchedText);
    auto lines = text::wrapUtf8ToDisplayWidth(detail.text[0], 32U);
    if (!lines) return core::Result<void>{lines.error()};
    detail.lines[0] = std::move(lines.value()); replacementDetail_.emplace(std::move(detail)); findDetailIndex_ = index;
    rebuildAccessibilityTree();
    repaint(); return core::success();
  }
  const auto* review = replacementJob_.review();
  if (!replacementOpen_ || replacementDependencies_ || replacementDetail_ || !review)
    return core::failure(core::ErrorCode::Conflict, "No lyric result list is open");
  const auto page = review->editsPage(replacementPage_);
  if (pageRow >= page.size()) return core::failure(core::ErrorCode::NotFound, "Replacement row is missing");
  ReplacementDetail detail;
  detail.lyricId = page[pageRow].lyricId;
  detail.text = {domain::toUtf8(page[pageRow].before), domain::toUtf8(page[pageRow].after)};
  for (std::size_t side = 0U; side < 2U; ++side) {
    auto wrapped = text::wrapUtf8ToDisplayWidth(detail.text[side], 32U);
    if (!wrapped) { replacementError_ = wrapped.error().message; repaint(); return core::Result<void>{wrapped.error()}; }
    detail.lines[side] = std::move(wrapped.value());
  }
  replacementDetail_.emplace(std::move(detail)); repaint(); return core::success();
}

core::Result<void> NativeEditorController::replacementReviewAction(std::size_t action) {
  const auto view = replacementReviewView();
  if (!replacementOpen_ || action >= view.enabled.size() || !view.enabled[action])
    return core::failure(core::ErrorCode::Conflict, "Replacement review action is unavailable");
  if (japaneseReadingMode_) {
    if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Reading interaction identity exhausted");
    ++replacementInteraction_;
    if (japaneseReadingDetail_) {
      if (action == 0U && replacementPage_ > 0U) --replacementPage_;
      if (action == 1U && (replacementPage_ + 1U) * 6U < japaneseReadingDetailLines_.size()) ++replacementPage_;
      if (action == 2U) { japaneseReadingDetail_.reset(); japaneseReadingDetailLines_.clear(); replacementPage_ = 0U; }
      repaint(); return core::success();
    }
    if (action == 0U && replacementPage_ > 0U) --replacementPage_;
    if (action == 1U) ++replacementPage_;
    if (action == 2U) {
      const auto* review = japaneseReadingIdentity_ ? japaneseReadingJob_.current(session_, regionId_, *japaneseReadingIdentity_) : nullptr;
      const auto index = replacementPage_ * 6U;
      if (!review || index >= review->reading.tokens.size()) return core::failure(core::ErrorCode::NotFound, "Japanese reading result is unavailable");
      const auto& token = review->reading.tokens[index]; const auto& binding = review->bindings[index];
      std::string detail = "Surface: " + token.surface + "\nStatus: " +
          (token.status == phonemizer::JapaneseReadingStatus::Known ? "known" : token.status == phonemizer::JapaneseReadingStatus::Unknown ? "unknown" : "missing-reading") +
          "\nLexical reading: " + (token.lexicalReading ? *token.lexicalReading : "(none)") +
          "\nSinging pronunciation: " + (token.pronunciation ? *token.pronunciation : "(none)") +
          "\nOwners: " + std::to_string(binding.notes.size()) +
          "\nExplicit hint contact: " + std::string(binding.touchesExplicitHint ? "yes" : "no") +
          "\nCross-lyric: " + std::string(binding.crossesLyrics ? "yes" : "no") +
          "\nStructural evidence only; audio/native-language review remains required.";
      auto lines = text::wrapUtf8ToDisplayWidth(detail, 32U); if (!lines) return core::Result<void>{lines.error()};
      japaneseReadingDetailLines_.clear(); for (const auto& line : lines.value()) japaneseReadingDetailLines_.push_back(detail.substr(line.offset, line.length));
      japaneseReadingDetail_ = index; replacementPage_ = 0U; repaint(); return core::success();
    }
    if (action == 3U) {
      if (!japaneseReadingIdentity_) return core::failure(core::ErrorCode::Conflict, "Japanese reading identity is unavailable");
      const auto applied = japaneseReadingJob_.applyCurrent(session_, regionId_, *japaneseReadingIdentity_);
      if (!applied) { replacementError_ = applied.error().message; replacementErrorContext_ = applied.error().context; repaint(); return applied; }
      japaneseReadingMode_ = false; japaneseReadingIdentity_.reset(); japaneseReadingDetail_.reset(); japaneseReadingDetailLines_.clear(); replacementOpen_ = false; markDocumentChanged(); repaint(); return core::success();
    }
    if (action == 4U) { japaneseReadingJob_.cancel(); japaneseReadingMode_ = false; japaneseReadingIdentity_.reset(); japaneseReadingDetail_.reset(); japaneseReadingDetailLines_.clear(); replacementOpen_ = false; repaint(); return core::success(); }
    if (action == 5U) {
      if (japaneseReadingJob_.preparing()) return core::failure(core::ErrorCode::Conflict, "Wait for the previous reading worker to retire");
      japaneseReadingMode_ = false; japaneseReadingIdentity_.reset(); replacementOpen_ = false; japaneseReadingDetail_.reset(); japaneseReadingDetailLines_.clear();
      return openJapaneseReadingReview();
    }
    repaint(); return core::success();
  }
  if (styleDraft_) {
    if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
    ++replacementInteraction_;
    if (action == 0U) --replacementPage_;
    if (action == 1U) ++replacementPage_;
    if (action == 2U) {
      if (styleBlendMode_) {
        if (styleBlendChoosingSecondary_) styleBlendChoosingSecondary_ = false;
        else styleBlendMode_ = false;
        replacementPage_ = 0U;
      } else if (styleIssue_) { replacementPage_ = *styleIssue_ / 6U; styleIssue_.reset(); styleDetailLines_.clear(); }
      else { styleIssues_ = !styleIssues_; replacementPage_ = 0U; }
    }
    if (action == 3U) {
      const auto applied = styleSnapshotResolver_ ? styleDraft_->apply(session_, selectedTrackId_, regionId_, styleSnapshotResolver_(selectedTrackId_)) :
          styleDraft_->apply(session_, selectedTrackId_, regionId_, styleBankResolver_(selectedTrackId_));
      if (!applied) return applied;
      styleDraft_.reset(); replacementOpen_ = false; markDocumentChanged();
    }
    if (action == 4U) { styleDraft_->cancel(); styleDraft_.reset(); replacementOpen_ = false; }
    if (action == 5U) return openStyleCoverageSheet();
    repaint(); return core::success();
  }
  if (dynamicsDraft_) {
    if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
    ++replacementInteraction_;
    dynamicsGainDragging_ = false;
    if (action == 4U) { dynamicsDraft_->cancel(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset(); replacementOpen_ = false; repaint(); return core::success(); }
    if (dynamicsPointEdit_) {
      core::Result<void> edited = core::success();
      if (action == 2U) edited = dynamicsDraft_->erase(*dynamicsPointEdit_->source);
      if (action == 3U) {
        const auto point = dynamicsPointValue(); if (!point) return core::Result<void>{point.error()};
        if (dynamicsPointEdit_->source) edited = dynamicsDraft_->move(*dynamicsPointEdit_->source, point.value());
        else {
          const auto& points = dynamicsDraft_->curve().points();
          const bool occupied = std::any_of(points.begin(), points.end(), [&](const auto& existing) { return existing.tick == point.value().tick; });
          edited = occupied ? core::failure(core::ErrorCode::Conflict, "Tick is occupied; edit the existing point") : dynamicsDraft_->upsert(point.value());
        }
      }
      replacementError_ = edited ? std::string{} : edited.error().message;
      if (edited && (action == 2U || action == 3U || action == 5U)) {
        if (action != 5U) {
          const auto updated = replacementReviewView();
          if (updated.dynamicsPlot) dynamicsDraft_->refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{
              time::Tick{updated.dynamicsPlot->startTick}, time::Tick{updated.dynamicsPlot->endTick}});
          else dynamicsDraft_->refreshTargetPreview();
        }
        dynamicsPointEdit_.reset();
        const auto count = dynamicsDraft_->curve().points().size();
        replacementPage_ = std::min(replacementPage_, count == 0U ? 0U : (count - 1U) / 2U);
      }
      repaint(); return edited;
    }
    if (action == 0U) --replacementPage_;
    if (action == 1U) ++replacementPage_;
    if (action == 2U) dynamicsPointEdit_ = DynamicsPointEdit{};
    if (action == 3U) {
      const auto applied = dynamicsDraft_->apply(session_, regionId_); if (!applied) return applied;
      dynamicsDraft_.reset(); replacementOpen_ = false; markDocumentChanged();
    }
    if (action == 5U) return openDynamicsInspector();
    replacementError_.clear(); repaint(); return core::success();
  }
  if (vibratoDraft_) {
    if (action == 0U) { --replacementPage_; selectedVibratoField_ = static_cast<VibratoField>(replacementPage_ * 6U); }
    if (action == 1U) { ++replacementPage_; selectedVibratoField_ = static_cast<VibratoField>(replacementPage_ * 6U); }
    if (action == 2U) {
      if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(core::ErrorCode::Conflict, "Inspector identity exhausted");
      ++replacementInteraction_; const auto reset = vibratoDraft_->reset(session_, regionId_, selectedVibratoField_);
      replacementError_ = reset ? std::string{} : reset.error().message; repaint(); return reset;
    }
    if (action == 3U) {
      const auto applied = vibratoDraft_->apply(session_, regionId_); if (!applied) return applied;
      vibratoDraft_.reset(); replacementOpen_ = false; markDocumentChanged();
    }
    if (action == 4U) { vibratoDraft_->cancel(); vibratoDraft_.reset(); replacementOpen_ = false; }
    if (action == 5U) return openVibratoInspector();
    repaint(); return core::success();
  }
  if (findMode_) {
    if (diagnosticFindMode_) {
      if (replacementDetail_) {
        if (action == 0U) --replacementDetail_->page;
        if (action == 1U) ++replacementDetail_->page;
        if (action == 4U) { replacementDetail_.reset(); findDetailIndex_.reset(); }
      } else {
        if (action == 0U) --replacementPage_;
        if (action == 1U) ++replacementPage_;
        if (action == 2U) return openFindReview(replacementQuery_);
        if (action == 3U) return openReplacementRow(0U);
        if (action == 4U) {
          diagnosticFindJob_.cancel(); diagnosticFindReview_.reset(); diagnosticFindMode_ = false; findMode_ = false; replacementOpen_ = false;
        }
      }
      if (action == 5U) return openDiagnosticFindReview(replacementQuery_);
      repaint(); return core::success();
    }
    if (replacementDetail_ && findDetailIndex_) {
      if (action == 0U) --replacementDetail_->page;
      if (action == 1U) ++replacementDetail_->page;
      if (action == 3U) {
        const auto selected = findNavigation_->select(session_, regionId_, *findDetailIndex_);
        if (!selected) return core::Result<void>{selected.error()};
        replacementOpen_ = false; findMode_ = false; replacementDetail_.reset(); findDetailIndex_.reset();
        return revealFindNote(selected.value());
      }
      if (action == 4U) { replacementDetail_.reset(); findDetailIndex_.reset(); }
    } else {
      if (action == 0U) --replacementPage_;
      if (action == 1U) ++replacementPage_;
      if (action == 2U) {
        if (findField_ == ui::NoteSearchField::PronunciationDiagnostic) return openDiagnosticFindReview(replacementQuery_);
        return openFindReview(replacementQuery_, static_cast<ui::NoteSearchField>(static_cast<unsigned>(findField_) + 1U));
      }
      if (action == 3U) return openReplacementRow(0U);
      if (action == 4U) { findJob_.cancel(); if (findNavigation_) findNavigation_->close(); findNavigation_.reset(); findMode_ = false; replacementOpen_ = false; }
      if (action == 5U) return openFindReview(replacementQuery_, findField_);
    }
    repaint(); return core::success();
  }
  if (clearDynamics_) {
    if (action == 0U) --replacementPage_;
    if (action == 1U) ++replacementPage_;
    if (action == 3U) {
      const auto applied = clearDynamics_->apply(session_, regionId_);
      if (!applied) { replacementError_ = applied.error().message; repaint(); return applied; }
      clearDynamics_.reset(); replacementOpen_ = false; markDocumentChanged();
    }
    if (action == 4U) { clearDynamics_->cancel(); clearDynamics_.reset(); replacementOpen_ = false; }
    if (action == 5U) return refreshClearDynamicsReview();
    repaint(); return core::success();
  }
  if (noteCleanup_) {
    if (action == 0U) --replacementPage_;
    if (action == 1U) ++replacementPage_;
    if (action == 2U) { replacementDependencies_ = !replacementDependencies_; replacementPage_ = 0U; }
    if (action == 3U) {
      const auto applied = noteCleanup_->apply(session_, regionId_);
      if (!applied) { replacementError_ = applied.error().message; repaint(); return applied; }
      noteCleanup_.reset(); replacementOpen_ = false; markDocumentChanged(); pianoRoll_.rebuildIndex();
    }
    if (action == 4U) { noteCleanup_->cancel(); noteCleanup_.reset(); replacementOpen_ = false; }
    if (action == 5U) return refreshNoteCleanupReview(noteCleanup_->kind());
    repaint(); return core::success();
  }
  if (clearVibrato_) {
    if (action == 0U) --replacementPage_;
    if (action == 1U) ++replacementPage_;
    if (action == 3U) {
      const auto applied = clearVibrato_->apply(session_, regionId_);
      if (!applied) { replacementError_ = applied.error().message; repaint(); return applied; }
      clearVibrato_.reset(); replacementOpen_ = false; markDocumentChanged();
    }
    if (action == 4U) { clearVibrato_->cancel(); clearVibrato_.reset(); replacementOpen_ = false; }
    if (action == 5U) return refreshClearVibratoReview();
    repaint(); return core::success();
  }
  if (replacementDetail_ && action <= 3U) {
    if (action == 0U) --replacementDetail_->page;
    if (action == 1U) ++replacementDetail_->page;
    if (action == 2U) { replacementDetail_->side = 1U - replacementDetail_->side; replacementDetail_->page = 0U; }
    if (action == 3U) replacementDetail_.reset();
    repaint(); return core::success();
  }
  if (action == 0U) --replacementPage_;
  if (action == 1U) ++replacementPage_;
  if (action == 2U) { replacementDependencies_ = !replacementDependencies_; replacementPage_ = 0U; }
  if (action == 3U) {
    const auto applied = replacementJob_.apply(session_, regionId_);
    if (!applied) { replacementError_ = applied.error().message; repaint(); return applied; }
    replacementOpen_ = false; markDocumentChanged();
  }
  if (action == 4U) { replacementJob_.cancel(); replacementOpen_ = false; replacementDetail_.reset(); }
  if (action == 5U) return openLyricReview(replacementQuery_, replacementText_, replacementDistribution_);
  repaint(); return core::success();
}

std::string NativeEditorController::hintSemanticPrefix() const {
  if (replacementInput_) return "lyric-find." + std::to_string(replacementInputSerial_) + "." +
      std::to_string(replacementInput_->revision) + "." + std::to_string(session_.revision()) +
      (replacementInput_->query ? ".replacement." : ".query.");
  if (!hintEdit_) return {};
  return "phone-hint." + std::to_string(hintInteraction_) + "." +
      hintEdit_->projectId.toString() + "." + hintEdit_->noteId.toString() + "." +
      std::to_string(hintEdit_->revision) + "." + std::to_string(session_.revision()) + ".";
}

std::string NativeEditorController::boundedInputLabel() const {
  std::string label = hintEditError_.empty() ? "PHONE HINT (EMPTY = CLEAR)" : "INVALID PHONE HINT";
  if (replacementInput_) label = replacementInputError_.empty() ?
      (replacementInput_->diagnostics ? "FIND ACTIVE DIAGNOSTICS (LITERAL)" : replacementInput_->findOnly ? "FIND NOTES (LITERAL; CHOOSE FIELD IN RESULTS)" :
       replacementInput_->query ? "REPLACE WITH (EMPTY ALLOWED)" : "FIND LYRIC (LITERAL)") : replacementInputError_;
  if (replacementInput_ && replacementInput_->vibratoField) label = "VIBRATO: " + std::string{VibratoInspectorDraft::label(*replacementInput_->vibratoField)};
  if (replacementInput_ && replacementInput_->dynamicsTickField) label = *replacementInput_->dynamicsTickField ? "DYNAMICS: REGION TICK" : "DYNAMICS: LINEAR GAIN (UNITY = 1)";
  return label;
}

NativeEditorController::TextFieldView NativeEditorController::textFieldView() const {
  TextFieldView view;
  if (!composition_.active()) return view;
  view.text = domain::toUtf8(composition_.compositionText());
  if (hintEdit_ || replacementInput_) {
    // The names the classic bounded field published; its tree carries the same ids.
    const auto prefix = hintSemanticPrefix();
    view.kind = TextFieldView::Kind::Bounded;
    view.label = boundedInputLabel();
    view.inputId = prefix + "input";
    view.cancelId = prefix + "cancel";
    view.inputName = "Space-separated phone symbols; empty clears hint";
    view.cancelName = "Cancel pronunciation hint";
    if (replacementInput_) {
      view.inputName = replacementInput_->diagnostics ? "Nonempty literal diagnostic search query" : replacementInput_->findOnly ? "Nonempty literal note search query" :
          replacementInput_->query ? "Replacement text; empty is allowed" : "Nonempty literal lyric query";
      view.cancelName = replacementInput_->findOnly ? "Cancel Find" : "Cancel find and replace";
      if (replacementInput_->vibratoField) {
        view.inputName = std::string{VibratoInspectorDraft::label(*replacementInput_->vibratoField)};
        view.cancelName = "Cancel vibrato field edit";
      }
      if (replacementInput_->dynamicsTickField) {
        view.inputName = *replacementInput_->dynamicsTickField ? "Nonnegative region tick" : "Linear gain: zero to 3.9810717, unity is one";
        view.cancelName = "Cancel dynamics field edit";
      }
    }
    view.error = replacementInput_ ? replacementInputError_ : hintEditError_;
    return view;
  }
  if (tempoEdit_) {
    view.kind = timeMapPanel_ ? TextFieldView::Kind::TimeMap : TextFieldView::Kind::Transport;
    view.inputName = tempoEdit_->chooseTick ? "New event tick" : (tempoEdit_->meter ? "Time signature numerator slash denominator" : "Tempo in BPM");
    view.label = tempoEdit_->chooseTick ? "NEW EVENT TICK" : (tempoEdit_->meter ? "TIME SIGNATURE (N/D)" : "TEMPO (BPM)");
    view.cancelName = "Cancel event input";
    if (timeMapPanel_) {
      const auto prefix = timeMapSemanticPrefix();
      view.inputId = prefix + "input";
      view.cancelId = prefix + "cancel";
    } else {
      // The transport's field keeps the toolbar readout's value path (setAccessibilityValue commits
      // it); there is no classic cancel node for it.
      view.inputId = tempoEdit_->meter ? "toolbar.meter" : "toolbar.tempo";
    }
    return view;
  }
  if (renameTrackTarget_ || renameRegionTarget_) {
    view.kind = TextFieldView::Kind::Rename;
    view.label = renameTrackTarget_ ? "TRACK NAME" : "REGION NAME";
    view.inputName = renameTrackTarget_ ? "Track name" : "Region name";
    view.cancelName = "Cancel rename";
  }
  return view;
}

core::Result<void> NativeEditorController::refreshVoicebanks() {
  if (!callbacks_.refreshVoicebanks)
    return core::failure(core::ErrorCode::Unsupported, "Voicebank refresh is not connected");
  const auto refreshed = callbacks_.refreshVoicebanks();
  repaint();
  return refreshed;
}

core::Result<void> NativeEditorController::openVoicebankInstaller() {
  if (!callbacks_.openVoicebankInstaller)
    return core::failure(core::ErrorCode::Unsupported,
                         "Standalone voicebank installation is not connected");
  const auto opened = callbacks_.openVoicebankInstaller();
  repaint();
  return opened;
}

core::Result<void> NativeEditorController::selectVoicebankCard(std::size_t index) {
  if (index >= voicebankCards_.size())
    return core::failure(core::ErrorCode::InvalidArgument, "Voicebank accessibility index is invalid");
  const auto& card = voicebankCards_[index];
  if (!card.selectable)
    return core::failure(core::ErrorCode::Conflict, "Selected voicebank is not trusted");
  if (!callbacks_.selectVoicebank)
    return core::failure(core::ErrorCode::Unsupported, "Voicebank selection is not connected");
  const auto selected = callbacks_.selectVoicebank(card.id, card.version, card.contentHash);
  if (selected) voicebankBrowserVisible_ = false;
  repaint();
  return selected;
}

core::Result<void> NativeEditorController::rebuildMicroscopeDetails() {
  const auto oldLine = std::min(microscopeDetailsPage_ * microscopeDetailsRows_, microscopeDetailsLines_.size());
  const auto anchor = oldLine < microscopeDetailsLines_.size() ? microscopeDetailsLines_[oldLine].offset : 0U;
  const auto bounds = layout_.microscopeDetailsBounds(logicalWidth_, logicalHeight_);
  // A full font-size advance per display column is deliberately conservative
  // for proportional Latin/CJK text and unsplittable long identity strings.
  const auto columns = static_cast<std::size_t>(std::clamp(std::floor(bounds.width / layout_.microscopeDetailsFontSize), 2.0, 1024.0));
  auto lines = text::wrapUtf8ToDisplayWidth(microscopeDetailsText_, columns, 65536U);
  if (!lines) return core::Result<void>{lines.error()};
  microscopeDetailsLines_ = std::move(lines.value());
  microscopeDetailsRows_ = static_cast<std::size_t>(std::clamp(std::floor(bounds.height / layout_.microscopeDetailsLineHeight), 1.0, 256.0));
  std::size_t anchorLine = 0U;
  while (anchorLine + 1U < microscopeDetailsLines_.size() && microscopeDetailsLines_[anchorLine + 1U].offset <= anchor) ++anchorLine;
  microscopeDetailsPage_ = anchorLine / microscopeDetailsRows_;
  return core::success();
}

core::Result<void> NativeEditorController::microscopeDetailsAction(std::size_t action) {
  if (!sampleMicroscopeOpen() || action > 2U)
    return core::failure(core::ErrorCode::Conflict, "Sample details are unavailable");
  if (action == 0U) {
    microscopeDetailsVisible_ = !microscopeDetailsVisible_;
    dragMode_ = DragMode::None;
    dragMicroscopeMarker_.reset(); dragMicroscopePitchMark_.reset();
  } else if (!microscopeDetailsVisible_ ||
      (action == 1U && microscopeDetailsPage_ == 0U) ||
      (action == 2U && (microscopeDetailsPage_ + 1U) * microscopeDetailsRows_ >= microscopeDetailsLines_.size())) {
    return core::failure(core::ErrorCode::Conflict, "Sample details page is unavailable");
  } else if (action == 1U) --microscopeDetailsPage_;
  else ++microscopeDetailsPage_;
  rebuildAccessibilityTree(); repaint(); return core::success();
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
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before opening sample inspection");
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
  constexpr std::size_t maximumContextBytes = 65536U;
  if (data.unit.id.size() > maximumContextBytes || data.destinationContext.size() > maximumContextBytes - data.unit.id.size())
    return core::failure(core::ErrorCode::Unsupported, "Sample inspection context exceeds 64 KiB");
  const auto detailText = "Captured at open; reopen after edits or bank refresh.\n\nUnit: " + data.unit.id +
      "\n\n" + (data.destinationContext.empty() ? std::string{"Destination unknown"} : data.destinationContext);
  const auto validText = text::wrapUtf8ToDisplayWidth(detailText, 32U, 65536U);
  if (!validText) return core::Result<void>{validText.error()};
  microscopeUnit_ = std::move(data.unit);
  microscopeAudio_ = std::move(data.audio);
  microscopeUnitId_ = microscopeUnit_->id;
  microscopeDestinationContext_ = std::move(data.destinationContext);
  microscopeDetailsText_ = detailText;
  microscopeDetailsLines_.clear(); microscopeDetailsPage_ = 0U; microscopeDetailsVisible_ = false;
  const auto details = rebuildMicroscopeDetails();
  if (!details) { closeSampleMicroscope(); return details; }
  microscopeKey_ = key;
  const auto rebuilt = rebuildSampleMicroscope();
  if (!rebuilt) {
    closeSampleMicroscope();
    return rebuilt;
  }
  rebuildAccessibilityTree();
  static_cast<void>(accessibilityTree_.setFocus("microscope.details"));
  repaint();
  return core::success();
}

std::size_t NativeEditorController::reviewEditCount() const {
  return (phonemeReview_ ? phonemeReview_->retainedEdits.size() : 0U) +
      (renderEditReview_ ? renderEditReview_->units.size() + renderEditReview_->seams.size() : 0U);
}

domain::PhonemeKey NativeEditorController::reviewEditKey() const {
  if (reviewedEdit_ < phonemeReview_->retainedEdits.size()) return phonemeReview_->retainedEdits[reviewedEdit_].key;
  const auto index = reviewedEdit_ - phonemeReview_->retainedEdits.size();
  return index < renderEditReview_->units.size() ? renderEditReview_->units[index].startKey
      : renderEditReview_->seams[index - renderEditReview_->units.size()].incomingStartKey;
}

const std::vector<domain::PhonemeToken>& NativeEditorController::reviewTargets() const {
  return renderEditReview_ && reviewedEdit_ >= phonemeReview_->retainedEdits.size()
      ? renderEditReview_->tokens : phonemeReview_->targets;
}

core::Result<void> NativeEditorController::openPhonemeReview() {
  if (composition_.active()) {
    return core::failure(core::ErrorCode::Conflict, "Finish or cancel text editing before opening phoneme review");
  }
  if (!callbacks_.reviewPhonemeBindings || !callbacks_.rebindPhonemeOverride) {
    return core::failure(core::ErrorCode::Unsupported, "Phoneme review is not connected");
  }
  auto review = callbacks_.reviewPhonemeBindings();
  if (!review) return core::Result<void>{review.error()};
  std::optional<authoring::RetainedRenderEditReview> renderReview;
  if (callbacks_.reviewRenderEdits && callbacks_.rebindUnitOverride && callbacks_.rebindSeamOverride) {
    auto loaded = callbacks_.reviewRenderEdits();
    if (!loaded) return core::Result<void>{loaded.error()};
    renderReview = std::move(loaded).value();
  }
  closeSampleMicroscope();
  dragMode_ = DragMode::None;
  phonemeReview_ = std::move(review).value();
  renderEditReview_ = std::move(renderReview);
  reviewedEdit_ = 0U;
  reviewedTarget_.reset();
  reviewStatus_ = phonemeReview_->warnings.empty() ? "Apply only after reviewing the chosen sound. Undo restores the edit."
      : phonemeReview_->warnings.front().message;
  rebuildAccessibilityTree();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::activatePhonemeReview(std::size_t action) {
  if (!phonemeReview_ || action >= 6U) return core::failure(core::ErrorCode::InvalidArgument, "Invalid phoneme review action");
  if (!sceneState().phonemeReview.enabled[action]) return core::failure(core::ErrorCode::Conflict, "Phoneme review action is unavailable");
  if (action == 2U) {
    phonemeReview_.reset();
    renderEditReview_.reset();
  } else if (action < 2U) {
    if (action == 0U) --reviewedEdit_; else ++reviewedEdit_;
    reviewedTarget_.reset();
  } else if (action == 3U) {
    --*reviewedTarget_;
  } else if (action == 4U) {
    if (reviewedTarget_) {
      ++*reviewedTarget_;
    } else {
      const auto key = reviewEditKey();
      const auto& targets = reviewTargets();
      auto nearby = std::find_if(targets.begin(), targets.end(), [key](const auto& token) { return token.key == key; });
      if (nearby == targets.end()) nearby = std::find_if(targets.begin(), targets.end(),
          [key](const auto& token) { return token.key.noteId == key.noteId; });
      reviewedTarget_ = nearby == targets.end() ? 0U : static_cast<std::size_t>(nearby - targets.begin());
    }
  } else {
    if (phonemeReview_->regionId != regionId_) return core::failure(core::ErrorCode::Conflict, "Review region changed");
    const auto& target = reviewTargets()[*reviewedTarget_];
    const auto result = [&]() -> core::Result<void> {
      if (reviewedEdit_ < phonemeReview_->retainedEdits.size()) {
        return callbacks_.rebindPhonemeOverride(phonemeReview_->retainedEdits[reviewedEdit_], target.key, target.contextId);
      }
      const auto index = reviewedEdit_ - phonemeReview_->retainedEdits.size();
      const auto& review = *renderEditReview_;
      if (index < review.units.size()) return callbacks_.rebindUnitOverride(review, review.units[index], target.key);
      return callbacks_.rebindSeamOverride(review, review.seams[index - review.units.size()], target.key);
    }();
    if (!result) {
      reviewStatus_ = result.error().message + ". Close and reopen to refresh.";
      repaint();
      return result;
    }
    setDirty(true);
    const auto refreshed = openPhonemeReview();
    if (!refreshed) { phonemeReview_.reset(); repaint(); return refreshed; }
    reviewStatus_ = "Binding applied. Close this panel to use Undo.";
  }
  rebuildAccessibilityTree();
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
  microscopeDetailsText_.clear(); microscopeDetailsLines_.clear();
  microscopeDetailsPage_ = 0U; microscopeDetailsVisible_ = false;
  repaint();
}

void NativeEditorController::setDiagnostics(
    std::vector<authoring::Diagnostic> diagnostics) {
  // A host sets its list once per painted frame. The panel is made of this list and the notices
  // alone, and every change to either rebuilds it, so a list that is what it was leaves the panel as
  // it is and asks for no frame: a frame that asked for the next one for nothing would never let the
  // window idle.
  if (diagnostics == ownerDiagnostics_) return;
  ownerDiagnostics_ = std::move(diagnostics);
  rebuildDiagnosticPanel();
}

void NativeEditorController::noteRefusal(const core::Error& error) {
  if (error.code != core::ErrorCode::Conflict || error.message.empty()) return;
  raiseNotice(makeEditorNotice(kEditRefusedCode, "editor.edit-refused", error.message));
}

void NativeEditorController::rebuildDiagnosticPanel() {
  // The panel is rebuilt from the owner's diagnostics and this editor's notices, and it is drawn from
  // that alone: a rebuild that produces the same panel asks for no frame, as setDiagnostics and
  // setAudioState do not. A device that fails to start again and again raises the same notice, and a
  // rebuild that asked for a frame each time would keep a window that paints only on request painting
  // at the display's rate, for a panel that has not changed.
  const auto before = diagnosticPanel_.entries();
  diagnosticPanel_.clear();
  // The owner's diagnostics keep the order the owner gave them: the first entry is the toast, and the
  // owner decides what leads. The editor's notices follow, so a refused key never hides a failure the
  // creator has to act on, and the toast counts them among the rest. With nothing else to show, the
  // first notice is the toast.
  for (const auto& diagnostic : ownerDiagnostics_) diagnosticPanel_.add(diagnostic);
  for (const auto& notice : notices_) diagnosticPanel_.add(notice);
  if (diagnosticPanel_.entries() != before) repaint();
}

void NativeEditorController::raiseNotice(authoring::Diagnostic notice) {
  if (!authoring::DiagnosticRegistry::validate(notice)) return;
  const auto same = std::find_if(notices_.begin(), notices_.end(), [&notice](const auto& held) {
    return held.sameIssueAs(notice);
  });
  if (same != notices_.end()) {
    same->addOccurrences(notice.occurrenceCount);
  } else {
    notices_.push_back(std::move(notice));
    // Only refusals are bounded, and the oldest of them goes: the notice that reports the host is
    // the one failure here that must not be pushed out by a run of refused keys.
    const auto refusals = static_cast<std::size_t>(std::count_if(
        notices_.begin(), notices_.end(),
        [](const auto& held) { return held.code == kEditRefusedCode; }));
    if (refusals > kMaximumRefusalNotices) {
      notices_.erase(std::find_if(notices_.begin(), notices_.end(), [](const auto& held) {
        return held.code == kEditRefusedCode;
      }));
    }
  }
  rebuildDiagnosticPanel();
}

void NativeEditorController::removeNotice(const authoring::Diagnostic& notice) {
  // The argument may be an entry of the panel that the rebuild replaces, so what is removed is
  // decided first.
  std::erase_if(notices_, [&notice](const auto& held) { return held.sameIssueAs(notice); });
  rebuildDiagnosticPanel();
}

void NativeEditorController::removeNoticesWithCode(std::string_view code) {
  const auto removed = std::erase_if(notices_, [code](const auto& held) { return held.code == code; });
  if (removed != 0U) rebuildDiagnosticPanel();
}

core::Result<void> NativeEditorController::activateDiagnostic(
    std::size_t index, authoring::DiagnosticAction action) const {
  return diagnosticPanel_.activate(index, action);
}

void NativeEditorController::dismissDiagnostic(std::size_t index) {
  if (index >= diagnosticPanel_.entries().size()) return;
  // A copy: rebuilding the panel replaces its entries.
  const auto dismissed = diagnosticPanel_.entries()[index].diagnostic;
  if (isEditorNotice(dismissed)) {
    removeNotice(dismissed);
    return;
  }
  // The owner's list is the source of the panel, so it is the owner's entry that has to go, or the
  // next rebuild would bring it back.
  std::erase_if(ownerDiagnostics_,
                [&dismissed](const auto& held) { return held.sameIssueAs(dismissed); });
  rebuildDiagnosticPanel();
}

void NativeEditorController::setRecoverySupportView(RecoverySupportView view) {
  if (view.visible) {
    voicebankBrowserVisible_ = false;
    audioSettings_.visible = false;
  }
  recoverySupportPanel_.update(std::move(view));
  repaint();
}

core::Result<void> NativeEditorController::selectSupportReport(
    std::size_t index) {
  const auto& current = recoverySupportPanel_.view();
  if (!current.visible || current.mode != RecoverySupportMode::Reports ||
      index >= current.items.size()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Support report selection is unavailable");
  }
  if (!callbacks_.selectSupportReport) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Support report selection is not connected");
  }
  const auto selected = callbacks_.selectSupportReport(index);
  if (!selected) return selected;
  auto view = recoverySupportPanel_.view();
  if (index < view.items.size()) {
    for (std::size_t item = 0U; item < view.items.size(); ++item) {
      view.items[item].selected = item == index;
    }
    recoverySupportPanel_.update(std::move(view));
  }
  repaint();
  return core::success();
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
    static_cast<void>(rebuildMicroscopeDetails());
  }
  repaint();
}

ui::Point NativeEditorController::modelPoint(ui::Point windowPoint) const noexcept {
  return ui::Point{windowPoint.x, windowPoint.y - layout_.contentTop()};
}

std::uint64_t NativeEditorController::documentRevision() const noexcept {
  return session_.revision();
}

bool NativeEditorController::pointerGestureActive() const noexcept {
  return dragMode_ != DragMode::None || vibratoHandleDrag_.has_value() || dynamicsGainDragging_;
}

void NativeEditorController::cancelPointerGesture() {
  if (!pointerGestureActive()) return;
  if (dragMode_ == DragMode::MoveExpressionPoint && expressionDraft_ && expressionGestureSnapshot_) {
    const auto restored = expressionDraft_->replacePoints(*expressionGestureSnapshot_);
    // A cancelled gesture leaves no draft behind unless edits were pending before it began. A
    // leftover untouched draft goes stale as soon as another path (a knob nudge, undo) edits the
    // curve, and would then shadow the stored curve and refuse the next gesture's release.
    if (!restored || !expressionDraft_->hasChanges()) expressionDraft_.reset();
  }
  expressionGestureSnapshot_.reset();
  expressionDragTick_.reset();
  pitchGesture_.reset();
  vibratoHandleDrag_.reset();
  dynamicsGainDragging_ = false;
  dynamicsTimeDragging_ = false;
  dragMode_ = DragMode::None;
  dragPhoneme_.reset();
  dragPitchTick_.reset();
  dragMicroscopeMarker_.reset();
  dragMicroscopePitchMark_.reset();
  repaint();
}

double NativeEditorController::timelineOriginX() const noexcept {
  if (!hosted_) return layout_.keyboardWidth;
  const auto& viewport = pianoRoll_.viewport();
  return viewport.bounds.x + viewport.keyboardWidth;
}

// Region automation (pitch and the timbral channels) is stored in region-local ticks; the timeline,
// the notes and the playhead are absolute song ticks. Every lane conversion goes through these.
time::Tick NativeEditorController::automationOriginTick() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? time::Tick{0} : region->startTick;
}

double NativeEditorController::laneX(time::Tick regionTick) const noexcept {
  return timelineOriginX() +
         pianoRoll_.timeline().tickToPixel(automationOriginTick() + regionTick);
}

time::Tick NativeEditorController::laneTickAt(double x) const {
  const auto* region = session_.project().findRegion(regionId_);
  auto absolute = pianoRoll_.timeline().pixelToTick(std::max(0.0, x - timelineOriginX()));
  // Snap on the song grid the notes use, then convert into the region.
  if (session_.project().settings().snapEnabled)
    absolute = time::Quantizer(session_.project().settings().snapGrid).snap(absolute);
  if (region == nullptr) return absolute < time::Tick{0} ? time::Tick{0} : absolute;
  return std::clamp(absolute - region->startTick, time::Tick{0}, region->durationTick);
}

time::Tick NativeEditorController::regionPlayheadClamped() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return playheadTick_;
  return std::clamp(playheadTick_ - region->startTick, time::Tick{0}, region->durationTick);
}

core::Result<time::Tick> NativeEditorController::regionPlayheadForEdit() const {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return playheadTick_;
  const auto local = playheadTick_ - region->startTick;
  if (local < time::Tick{0} || local > region->durationTick)
    return core::failure<time::Tick>(
        core::ErrorCode::InvalidArgument,
        "The playhead is outside the selected region. Move the playhead into the region to edit "
        "this control at the playhead.");
  return local;
}

void NativeEditorController::repaint() const {
  if (callbacks_.requestRepaint) callbacks_.requestRepaint();
}

void NativeEditorController::finishTextInput() const {
  if (callbacks_.endTextInput) callbacks_.endTextInput();
}

core::Result<void> NativeEditorController::commitTimbralEdit(core::Result<void> result) {
  if (!result) return result;
  // The edit is in the project, but nothing has asked for it to be rendered. markDocumentChanged is
  // the hook every other application-level edit reaches the renderer through, and the renderer
  // debounces it, so a burst of nudges coalesces into one render rather than one per keystroke.
  markDocumentChanged();
  repaint();
  return result;
}

void NativeEditorController::markDocumentChanged() {
  dirty_ = true;
  if (callbacks_.documentChanged) callbacks_.documentChanged();
}

core::Result<TempoMeterModel> NativeEditorController::timeMapEvents() const {
  return TempoMeterModel::capture(session_.project(), session_.revision());
}

core::Result<void> NativeEditorController::selectTimeMapRow(std::size_t pageRow) {
  if (!timeMapPanel_) return core::failure(core::ErrorCode::Conflict, "Time maps are not open");
  if (pageRow >= timeMapPanel_->page(timeMapPage_).size())
    return core::failure(core::ErrorCode::InvalidArgument, "Time-map row is unavailable");
  const auto selected = timeMapPanel_->select(timeMapPage_ * TempoMeterModel::pageSize + pageRow);
  repaint();
  return selected;
}

core::Result<void> NativeEditorController::navigateTimeMapRow(int direction) {
  if (!timeMapPanel_) return core::failure(core::ErrorCode::Conflict, "Time maps are not open");
  auto index = timeMapPanel_->selectedIndex();
  if (direction < 0 && index > 0U) --index;
  if (direction > 0 && index + 1U < timeMapPanel_->size()) ++index;
  const auto selected = timeMapPanel_->select(index);
  timeMapPage_ = index / TempoMeterModel::pageSize;
  repaint();
  return selected;
}

core::Result<void> NativeEditorController::selectOverlapMemberRow(std::size_t index) {
  if (!overlapDetail_) return core::failure(core::ErrorCode::Conflict, "No overlap detail is open");
  if (index >= overlapDetail_->members.size())
    return core::failure(core::ErrorCode::InvalidArgument, "Overlap row is unavailable");
  // Selecting a row selects that note and marks it in the popover, exactly as the group's own
  // activation walks its members.
  const auto noteId = overlapDetail_->members[index].noteId;
  if (session_.project().findRegion(regionId_) == nullptr ||
      session_.project().findRegion(regionId_)->findNote(noteId) == nullptr)
    return core::failure(core::ErrorCode::NotFound, "The overlapping note is gone");
  session_.selection().selectOnly(noteId);
  for (std::size_t i = 0U; i < overlapDetail_->members.size(); ++i)
    overlapDetail_->members[i].selected = i == index;
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::closeOverlapDetail() {
  if (!overlapDetail_) return core::failure(core::ErrorCode::InvalidState, "No overlap detail is open");
  overlapDetail_.reset();
  rebuildAccessibilityTree();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::openOverlapDetail(std::size_t groupIndex) {
  const auto visuals = pianoRoll_.visibleNotes();
  const auto member = std::find_if(
      visuals.begin(), visuals.end(), [groupIndex](const ui::NoteVisual& note) {
        return note.overlapGroup == groupIndex && note.overlapMemberCount > 1U;
      });
  if (member == visuals.end())
    return core::failure(core::ErrorCode::NotFound, "Overlap group is no longer visible");
  const auto candidates = pianoRoll_.overlapCandidatesAt(
      ui::Point{member->bounds.x + member->bounds.width * 0.5,
                member->bounds.y + member->bounds.height * 0.5});
  if (candidates.empty())
    return core::failure(core::ErrorCode::NotFound, "Overlap group has no selectable notes");
  // Opening from the badge shows the group's own members without moving the selection: the member
  // the creator has selected is the one marked, and a row click is what changes it.
  const auto selected = session_.selection().noteIds();
  const auto current = std::find_first_of(candidates.begin(), candidates.end(), selected.begin(),
                                          selected.end());
  const auto marked = current == candidates.end() ? candidates.front() : *current;
  EditorSceneState::OverlapDetail detail{.groupIndex = groupIndex};
  detail.members.reserve(candidates.size());
  const auto* region = session_.project().findRegion(regionId_);
  for (const auto candidate : candidates) {
    const auto* note = region == nullptr ? nullptr : region->findNote(candidate);
    const auto* lyric = note == nullptr || region == nullptr ? nullptr
                                                            : region->findLyric(note->lyricTokenId);
    detail.members.push_back(EditorSceneState::OverlapDetailMember{
        .noteId = candidate,
        .lyric = lyric == nullptr ? std::string{} : domain::toUtf8(lyric->surface),
        .midiKey = static_cast<std::uint8_t>(note == nullptr ? 0U : note->midiKey),
        .selected = candidate == marked,
    });
  }
  overlapDetail_ = std::move(detail);
  rebuildAccessibilityTree();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::openTimeMapPanel() {
  if (timeMapInteraction_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(core::ErrorCode::Conflict, "Time-map interaction identity exhausted");
  if (composition_.active() || dragMode_ != DragMode::None || phonemeReview_ || sampleMicroscopeOpen() || replacementOpen_)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before opening time maps");
  auto model = timeMapEvents(); if (!model) return core::Result<void>{model.error()};
  if (timeMapPanel_ && timeMapPanel_->projectId() == session_.project().id()) {
    const auto& selected = timeMapPanel_->selected();
    model.value().selectNearest(selected.tick, selected.meter);
  }
  ++timeMapInteraction_; timeMapPanel_ = std::move(model.value());
  timeMapPage_ = timeMapPanel_->selectedIndex() / TempoMeterModel::pageSize;
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::timeMapPanelAction(std::size_t action) {
  if (!timeMapPanel_ || composition_.active()) return core::failure(core::ErrorCode::Conflict, "Finish event text editing first");
  if (action == 5U) { timeMapPanel_.reset(); repaint(); return core::success(); }
  if (action == 4U) return openTimeMapPanel();
  if (action == 6U || action == 7U) {
    if (timeMapInteraction_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(core::ErrorCode::Conflict, "Time-map interaction identity exhausted");
    if (timeMapPanel_->size() >= TempoMeterModel::maximumEvents)
      return core::failure(core::ErrorCode::Conflict, "Time-map editor event limit reached; remove an event before inserting");
    if (!timeMapPanel_->matches(session_.project(), session_.revision()))
      return core::failure(core::ErrorCode::Conflict, "Refresh the changed time-map list before insertion");
    if (!callbacks_.beginTextInput) return core::failure(core::ErrorCode::Unsupported, "Native event text input is not connected");
    const auto begun = composition_.begin(externalTextTarget(), U"0"); if (!begun) return begun;
    renameTrackTarget_.reset(); renameRegionTarget_.reset(); batchLyricTarget_.reset();
    ++timeMapInteraction_; tempoEdit_ = TempoEditContext{session_.revision(), time::Tick{0}, action == 7U, true, true};
    callbacks_.beginTextInput({externalTextTarget(), layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, true), U"0", TextInputAnchor::TimeMapPanel});
    repaint(); return core::success();
  }
  if (action <= 1U) {
    if (action == 0U && timeMapPage_ > 0U) --timeMapPage_;
    if (action == 1U && (timeMapPage_ + 1U) * TempoMeterModel::pageSize < timeMapPanel_->size()) ++timeMapPage_;
    const auto selected = timeMapPanel_->select(timeMapPage_ * TempoMeterModel::pageSize);
    repaint(); return selected;
  }
  if (action == 2U) return beginSelectedTimeMapEdit(*timeMapPanel_);
  if (action == 3U) {
    const auto removed = removeSelectedTimeMapEvent(*timeMapPanel_);
    if (!removed) return removed;
    return openTimeMapPanel();
  }
  return core::failure(core::ErrorCode::InvalidArgument, "Unknown time-map action");
}

core::Result<void> NativeEditorController::beginSelectedTimeMapEdit(const TempoMeterModel& model) {
  if (!model.matches(session_.project(), session_.revision()))
    return core::failure(core::ErrorCode::Conflict, "Refresh the changed time-map event list");
  const auto& row = model.selected();
  return row.meter ? beginMeterEdit(row.tick) : beginTempoEdit(row.tick);
}

core::Result<void> NativeEditorController::removeSelectedTimeMapEvent(const TempoMeterModel& model) {
  if (!model.matches(session_.project(), session_.revision()))
    return core::failure(core::ErrorCode::Conflict, "Refresh the changed time-map event list");
  const auto& row = model.selected();
  if (!row.removable()) return core::failure(core::ErrorCode::Conflict, "Initial tempo and meter events cannot be removed");
  return row.meter ? editMeter(model.revision(), row.tick, std::nullopt)
                   : editTempo(model.revision(), row.tick, std::nullopt);
}

core::Result<void> NativeEditorController::beginTempoEdit(time::Tick tick) {
  if (timeMapInteraction_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(core::ErrorCode::Conflict, "Time-map interaction identity exhausted");
  if (tick < time::Tick{0})
    return core::failure(core::ErrorCode::InvalidArgument, "Tempo event tick must not be negative");
  if (composition_.active() || dragMode_ != DragMode::None || phonemeReview_ || sampleMicroscopeOpen() || replacementOpen_)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before changing tempo");
  if (!callbacks_.beginTextInput)
    return core::failure(core::ErrorCode::Unsupported, "Native tempo text input is not connected");
  const auto text = domain::fromUtf8(tempoEditText(session_.project().tempoMap().bpmAt(tick)));
  if (!text) return core::Result<void>{text.error()};
  const auto begun = composition_.begin(externalTextTarget(), text.value()); if (!begun) return begun;
  renameTrackTarget_.reset(); renameRegionTarget_.reset(); batchLyricTarget_.reset();
  ++timeMapInteraction_; tempoEdit_ = TempoEditContext{session_.revision(), tick};
  callbacks_.beginTextInput({externalTextTarget(), layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, timeMapPanel_.has_value()), text.value(),
      timeMapPanel_ ? TextInputAnchor::TimeMapPanel : TextInputAnchor::Transport});
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::editTempo(std::uint64_t expectedRevision,
    time::Tick tick, std::optional<double> bpm) {
  if (expectedRevision != session_.revision() || composition_.active() ||
      dragMode_ != DragMode::None || phonemeReview_)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit or refresh the tempo selection");
  const auto result = session_.execute(std::make_unique<application::EditTempoCommand>(tick, bpm));
  if (result) { markDocumentChanged(); repaint(); }
  return result;
}

core::Result<void> NativeEditorController::beginMeterEdit(time::Tick tick) {
  if (timeMapInteraction_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(core::ErrorCode::Conflict, "Time-map interaction identity exhausted");
  if (tick < time::Tick{0}) return core::failure(core::ErrorCode::InvalidArgument, "Meter tick must not be negative");
  if (composition_.active() || dragMode_ != DragMode::None || phonemeReview_ || sampleMicroscopeOpen() || replacementOpen_)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before changing meter");
  if (!callbacks_.beginTextInput) return core::failure(core::ErrorCode::Unsupported, "Native meter text input is not connected");
  const auto text = domain::fromUtf8(meterEditText(session_.project().meterMap().meterAt(tick)));
  if (!text) return core::Result<void>{text.error()};
  const auto begun = composition_.begin(externalTextTarget(), text.value()); if (!begun) return begun;
  renameTrackTarget_.reset(); renameRegionTarget_.reset(); batchLyricTarget_.reset();
  ++timeMapInteraction_; tempoEdit_ = TempoEditContext{session_.revision(), tick, true};
  callbacks_.beginTextInput({externalTextTarget(), layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, timeMapPanel_.has_value()), text.value(),
      timeMapPanel_ ? TextInputAnchor::TimeMapPanel : TextInputAnchor::Transport});
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::editMeter(std::uint64_t expectedRevision,
    time::Tick tick, std::optional<application::EditMeterCommand::Signature> signature) {
  if (expectedRevision != session_.revision() || composition_.active() ||
      dragMode_ != DragMode::None || phonemeReview_)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit or refresh the meter selection");
  const auto result = session_.execute(std::make_unique<application::EditMeterCommand>(tick, signature));
  if (result) { markDocumentChanged(); repaint(); }
  return result;
}

core::Result<void> NativeEditorController::beginBatchLyricEdit() {
  if (replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Close the active review before distributing lyrics");
  if (!callbacks_.beginTextInput)
    return core::failure(core::ErrorCode::Unsupported, "Native batch lyric input is not connected");
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
  std::unordered_set<domain::NoteId> regionNotes;
  for (const auto& note : region->notes) regionNotes.insert(note.id);
  if (selected.size() > ui::NoteSearchModel::maximumNotes ||
      std::any_of(selected.begin(), selected.end(), [&regionNotes](auto id) { return !regionNotes.contains(id); }))
    return core::failure(core::ErrorCode::Conflict, "Select at most 10000 notes entirely within the active region");
  auto context = session_.capturePerformanceJob(); if (!context) return core::Result<void>{context.error()};
  auto captured = selected; std::sort(captured.begin(), captured.end());
  if (composition_.active()) composition_.cancel();
  tempoEdit_.reset(); hintEdit_.reset(); replacementInput_.reset();
  auto begun = composition_.begin(externalTextTarget(), {});
  if (!begun) return begun;
  batchLyricTarget_.emplace(BatchLyricContext{std::move(context.value()), regionId_, session_.revision(), std::move(captured)});
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = externalTextTarget(),
        .logicalBounds = noteWindowBounds(*first).value_or(
            ui::Rect{layout_.keyboardWidth, layout_.contentTop(), 240.0, 30.0}),
        .currentText = {},
        .anchor = TextInputAnchor::NoteGrid,
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerDown(
    const PointerEvent& event) {
  retryHostSelectionBeforeInput();
  vibratoKeyboardFocus_.reset();
  // Pointer input arrives only from the SING shell, which forwards presses in its grid and lane (in
  // this controller's hosted geometry) and the plots of its microscope and dynamics sheets (in the
  // controller's own plot coordinates). Every other control -- toolbar, panels, review rows and
  // buttons, fields -- is the shell's own and reaches the controller through its commands and
  // accessibility actions, so a modal surface here only has to keep a press off the score.
  if (replacementOpen_) {
    if (event.button == PointerButton::Left) {
      const auto view = replacementReviewView();
      if (view.dynamicsPlot && view.dynamicsPlot->editable) {
        for (std::size_t i = 0U; i < 4U; ++i) if (view.dynamicsPlot->navigation[i].contains(event.position))
          return i == 3U ? cycleMeasuredChannel() : navigateDynamics(static_cast<ui::DynamicsPlotViewport::Action>(i));
        const auto near = [&](ui::Point point) { return std::hypot(point.x - event.position.x, point.y - event.position.y) <= 7.0; };
        if (view.dynamicsPlot->candidate && near(*view.dynamicsPlot->candidate)) {
          dynamicsTimeDragging_ = event.modifiers.shift;
          dynamicsDragRange_ = {view.dynamicsPlot->startTick, view.dynamicsPlot->endTick};
          dynamicsGainDragging_ = true; dynamicsDragBounds_ = view.dynamicsPlot->bounds; return core::success();
        }
        const auto& handles = view.dynamicsPlot->handles;
        const auto closest = std::min_element(handles.begin(), handles.end(), [&](const auto& a, const auto& b) {
          return std::hypot(a.position.x - event.position.x, a.position.y - event.position.y) <
              std::hypot(b.position.x - event.position.x, b.position.y - event.position.y);
        });
        if (closest != handles.end() && near(closest->position)) {
          const auto opened = openReplacementRow(closest->pageRow); if (!opened) return opened;
          dynamicsTimeDragging_ = event.modifiers.shift;
          dynamicsDragRange_ = {view.dynamicsPlot->startTick, view.dynamicsPlot->endTick};
          dynamicsGainDragging_ = true; dynamicsDragBounds_ = view.dynamicsPlot->bounds; return core::success();
        }
      }
    }
    return core::success();
  }
  if (hintEdit_ || replacementInput_ || timeMapPanel_ || phonemeReview_) return core::success();
  if (sampleMicroscopeOpen()) {
    if (event.button == PointerButton::Right) {
      closeSampleMicroscope();
      return core::success();
    }
    if (event.button != PointerButton::Left || microscopeDetailsVisible_) return core::success();
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
  if (event.position.y >= layout_.toolbarHeight &&
      event.position.y < layout_.contentTop()) {
    const auto tick = pianoRoll_.timeline().pixelToTick(
        std::max(0.0, event.position.x - timelineOriginX()));
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
  const auto technical = resolveEditorTechnicalLaneHeights(
      state, layout_, logicalHeight_ - layout_.statusHeight - overlayInset);
  // A hosted grid has only the lanes its host put in its lane band: an expression curve, or the
  // phoneme, unit and seam lanes stacked in that order.
  const auto pianoBottom = hosted_ ? hosted_->pianoBottom : technical.pianoBottom;
  const auto phonemeHeight = hosted_ ? hosted_->phonemeHeight : technical.values[0U];
  const auto unitHeight = hosted_ ? hosted_->unitHeight : technical.values[1U];
  const auto seamHeight = hosted_ ? hosted_->seamHeight : technical.values[2U];
  const auto automationHeight = hosted_ ? hosted_->laneHeight : technical.values[3U];
  const auto phonemeTop = pianoBottom;
  const auto unitTop = phonemeTop + phonemeHeight;
  const auto seamTop = unitTop + unitHeight;
  const auto automationTop = seamTop + seamHeight;
  if (event.position.y >= phonemeTop && event.position.y < unitTop) {
    if (callbacks_.movePhonemeBoundary) {
      ui::PhonemeLaneModel lane;
      const auto current = session_.project().findRegion(regionId_);
      if (current != nullptr) {
        lane.rebuild(pianoRoll_, regionPronunciation(*current),
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
      ui::PhonemeLaneModel lane;
      const auto current = session_.project().findRegion(regionId_);
      if (current != nullptr) {
        lane.rebuild(pianoRoll_, regionPronunciation(*current),
                     layout_.phonemeContentTop(unitTop),
                     layout_.phonemeContentHeight(unitHeight));
        std::optional<domain::PhonemeKey> key;
        for (const auto& visual : lane.visuals()) {
          if (event.position.x >= visual.bounds.x && event.position.x <= visual.bounds.right()) key = visual.key;
        }
        if (key.has_value()) {
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
    if (expressionLaneVisible_) {
      if (event.button != PointerButton::Left) return core::success();
      const auto result = beginExpressionGesture(event.position, automationTop, automationHeight, event);
      repaint();
      return result;
    }
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
    const auto tick = laneTickAt(event.position.x);
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
    const auto localX = event.position.x - timelineOriginX();
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
  const auto selectedNotes = session_.selection().noteIds();
  if (selectedNotes.size() == 1U) {
    const auto selectedId = selectedNotes.front();
    const auto* region = session_.project().findRegion(regionId_);
    const auto* selectedNote = session_.project().findNote(selectedId);
    if (region != nullptr && selectedNote != nullptr && selectedNote->vibrato.enabled) {
      const auto visuals = pianoRoll_.visibleNotes();
      const auto visual = std::find_if(visuals.begin(), visuals.end(),
          [selectedId](const ui::NoteVisual& candidate) {
            return candidate.noteId == selectedId;
          });
      if (visual != visuals.end()) {
        auto bounds = visual->bounds;
        bounds.y += layout_.contentTop();
        const auto handles = vibratoHandlePositions(
            *selectedNote, region->startTick, session_.project().tempoMap(), bounds);
        if (handles) {
          constexpr double hitRadius = 9.0;
          auto selectedHandle = VibratoHandleKind::Onset;
          auto nearestDistance = hitRadius;
          const auto consider = [&](VibratoHandleKind kind, ui::Point point) {
            const auto distance = std::hypot(
                point.x - event.position.x, point.y - event.position.y);
            if (distance < nearestDistance) {
              nearestDistance = distance;
              selectedHandle = kind;
            }
          };
          consider(VibratoHandleKind::Onset, handles->onset);
          consider(VibratoHandleKind::Depth, handles->depth);
          if (handles->fadeIn)
            consider(VibratoHandleKind::FadeIn, *handles->fadeIn);
          if (handles->fadeOut)
            consider(VibratoHandleKind::FadeOut, *handles->fadeOut);
          if (handles->period)
            consider(VibratoHandleKind::Period, *handles->period);
          if (handles->phase)
            consider(VibratoHandleKind::Phase, *handles->phase);
          if (nearestDistance < hitRadius) {
            constexpr double vibratoSampleRate = 48000.0;
            const auto startFrame = session_.project().tempoMap().sampleFrameAt(
                region->startTick + selectedNote->startTick,
                vibratoSampleRate);
            const auto endFrame = session_.project().tempoMap().sampleFrameAt(
                region->startTick + selectedNote->endTick(),
                vibratoSampleRate);
            const auto activeDurationMilliseconds =
                (static_cast<double>(endFrame) - static_cast<double>(startFrame)) /
                48.0 * (1.0 - selectedNote->vibrato.startFraction);
            vibratoHandleDrag_ = VibratoHandleDrag{
                .noteId = selectedId,
                .kind = selectedHandle,
                .bounds = bounds,
                .handleStart = event.position,
                .source = selectedNote->vibrato,
                .preview = selectedNote->vibrato,
                .revision = session_.revision(),
                .logicalWidth = logicalWidth_,
                .logicalHeight = logicalHeight_,
                .activeDurationMilliseconds = activeDurationMilliseconds,
            };
            repaint();
            return core::success();
          }
        }
      }
    }
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

core::Result<void> NativeEditorController::cycleMeasuredChannel() {
  if (!measurementCoordinator_ || !dynamicsDraft_ || !dynamicsDraft_->matches(session_, regionId_) ||
      replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Measured output is unavailable");
  const auto audio = measurementCoordinator_->acquireCurrent();
  const auto* focused = accessibilityTree_.focusedNode();
  const bool retainFocus = focused && focused->id.ends_with("zoom.3");
  const auto channels = audio ? std::max(std::size_t{1}, static_cast<std::size_t>(audio->result.channelCount)) : 1U;
  measuredChannel_ = measuredChannel_ >= channels ? 0U : measuredChannel_ + 1U;
  dynamicsGainDragging_ = false; ++replacementInteraction_;
  if (measuredChannel_ == 0U) { measurementJob_.cancel(); measurementWindow_.reset(); }
  else pollAudioMeasurement();
  if (retainFocus) { rebuildAccessibilityTree(); static_cast<void>(accessibilityTree_.setFocus(replacementSemanticPrefix() + "zoom.3")); }
  repaint(); return core::success();
}

void NativeEditorController::pollAudioMeasurement() {
  if (!measurementCoordinator_) return;
  if (measurementJob_.preparing()) {
    const auto polled = measurementJob_.poll(session_, *measurementCoordinator_);
    if (!polled) measurementStatus_ = polled.error().message;
    repaint();
  }
  if (!replacementOpen_ || !dynamicsDraft_ || measuredChannel_ == 0U || !dynamicsDraft_->matches(session_, regionId_)) {
    measurementJob_.cancel(); measurementWindow_.reset(); measurementStatus_ = "Measurement inactive or document changed"; return;
  }
  const auto audio = measurementCoordinator_->acquireCurrent();
  if (!audio || !audio->sourceProject || audio->projectRevision != session_.revision() || *audio->sourceProject != session_.project()) {
    measurementJob_.cancel(); measurementWindow_.reset(); measurementStatus_ = "Render the current document to measure output"; return;
  }
  const auto* region = session_.project().findRegion(regionId_);
  if (!region || audio->result.channelCount == 0U) return;
  const auto window = dynamicsViewport_.resolve(region->durationTick.value());
  const auto firstFrame = session_.project().tempoMap().sampleFrameAt(region->startTick + time::Tick{window.start}, audio->result.sampleRate);
  const auto lastFrame = session_.project().tempoMap().sampleFrameAt(region->startTick + time::Tick{window.end}, audio->result.sampleRate);
  const auto frames = audio->result.interleaved.size() / audio->result.channelCount;
  if (firstFrame < 0 || lastFrame <= firstFrame || static_cast<std::uint64_t>(firstFrame) >= frames) {
    measurementJob_.cancel(); measurementWindow_.reset(); measurementStatus_ = "No rendered frames in this interval"; return;
  }
  const auto first = static_cast<std::size_t>(firstFrame);
  const auto end = static_cast<std::size_t>(std::min(static_cast<std::uint64_t>(lastFrame), static_cast<std::uint64_t>(frames)));
  const MeasurementWindow desired{audio->sourceIdentity, audio->requestId, regionId_, first, end - first};
  if (measurementWindow_ && *measurementWindow_ == desired) return;
  if (measurementJob_.preparing()) { measurementJob_.cancel(); return; }
  measurementWindow_ = desired;
  const auto started = measurementJob_.start(session_, *measurementCoordinator_, desired.first, desired.count, 256U);
  measurementStatus_ = started ? "Measuring rendered output..." : started.error().message;
  if (started) repaint();
}

core::Result<void> NativeEditorController::navigateDynamics(ui::DynamicsPlotViewport::Action action, double anchor) {
  if (!replacementOpen_ || !dynamicsDraft_ || !dynamicsDraft_->matches(session_, regionId_) ||
      replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Dynamics navigation is stale or unavailable");
  const auto view = replacementReviewView(); if (!view.dynamicsPlot) return core::failure(core::ErrorCode::Conflict, "Dynamics plot is unavailable");
  dynamicsViewport_.navigate(action, view.dynamicsPlot->fullEndTick, anchor);
  measurementJob_.cancel(); measurementWindow_.reset(); measurementStatus_ = "Measuring visible rendered output...";
  const auto window = dynamicsViewport_.resolve(view.dynamicsPlot->fullEndTick);
  dynamicsDraft_->refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{time::Tick{window.start}, time::Tick{window.end}});
  dynamicsGainDragging_ = false; ++replacementInteraction_; repaint(); return core::success();
}

core::Result<void> NativeEditorController::dragDynamicsPoint(ui::Point position) {
  if (!dynamicsGainDragging_ || !dynamicsDraft_ || !dynamicsPointEdit_ || !dynamicsDraft_->matches(session_, regionId_) ||
      !std::isfinite(position.x) || !std::isfinite(position.y)) {
    dynamicsGainDragging_ = false;
    return core::failure(core::ErrorCode::Conflict, "Dynamics drag is stale, closed or invalid");
  }
  const auto bounds = layout_.dynamicsPlotBounds(logicalWidth_, logicalHeight_);
  if (bounds.height < 16.0 || bounds.x != dynamicsDragBounds_.x || bounds.y != dynamicsDragBounds_.y ||
      bounds.width != dynamicsDragBounds_.width || bounds.height != dynamicsDragBounds_.height ||
      replacementInteraction_ == std::numeric_limits<std::uint64_t>::max()) {
    dynamicsGainDragging_ = false; return core::failure(core::ErrorCode::Conflict, "Dynamics drag geometry is unavailable");
  }
  if (dynamicsTimeDragging_) {
    const auto tick = ui::DynamicsPlotViewport::tickAtFraction(dynamicsDragRange_, (position.x - bounds.x) / bounds.width);
    if (!tick) { dynamicsGainDragging_ = false; return core::failure(core::ErrorCode::Conflict, "Dynamics drag tick is invalid"); }
    dynamicsPointEdit_->tickText = std::to_string(*tick);
  } else {
    const auto gain = static_cast<float>(std::clamp(1.0 - (position.y - bounds.y) / bounds.height, 0.0, 1.0) * domain::kMaximumDynamicsGain);
    std::array<char, 32> buffer{}; const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), gain);
    dynamicsPointEdit_->gainText = std::string(buffer.data(), result.ptr);
  }
  ++replacementInteraction_; replacementError_.clear(); repaint(); return core::success();
}

core::Result<void> NativeEditorController::dragVibratoHandle(ui::Point position) {
  if (!vibratoHandleDrag_ || !std::isfinite(position.x) ||
      !std::isfinite(position.y)) {
    vibratoHandleDrag_.reset();
    repaint();
    return core::failure(core::ErrorCode::Conflict,
                         "Vibrato handle drag is closed or invalid");
  }
  auto& drag = *vibratoHandleDrag_;
  const auto selected = session_.selection().noteIds();
  const auto* region = session_.project().findRegion(regionId_);
  const auto* note = session_.project().findNote(drag.noteId);
  const auto visuals = pianoRoll_.visibleNotes();
  const auto visual = std::find_if(visuals.begin(), visuals.end(),
      [&drag](const ui::NoteVisual& candidate) {
        return candidate.noteId == drag.noteId;
      });
  auto currentBounds = visual == visuals.end() ? ui::Rect{} : visual->bounds;
  currentBounds.y += layout_.contentTop();
  if (session_.revision() != drag.revision || selected.size() != 1U ||
      selected.front() != drag.noteId || region == nullptr || note == nullptr ||
      note->vibrato != drag.source || visual == visuals.end() ||
      currentBounds.x != drag.bounds.x || currentBounds.y != drag.bounds.y ||
      currentBounds.width != drag.bounds.width ||
      currentBounds.height != drag.bounds.height ||
      logicalWidth_ != drag.logicalWidth || logicalHeight_ != drag.logicalHeight) {
    vibratoHandleDrag_.reset();
    repaint();
    return core::failure(core::ErrorCode::Conflict,
                         "Vibrato target changed; restart the handle edit");
  }

  auto preview = drag.source;
  if (drag.kind == VibratoHandleKind::Onset) {
    const auto maximumStart = std::max(0.0, 1.0 - 8.0 / drag.bounds.width);
    preview.startFraction = static_cast<float>(std::clamp(
        static_cast<double>(drag.source.startFraction) +
            (position.x - drag.handleStart.x) / drag.bounds.width,
        0.0, maximumStart));
  } else if (drag.kind == VibratoHandleKind::FadeIn ||
             drag.kind == VibratoHandleKind::FadeOut) {
    const auto activeFraction = 1.0 - static_cast<double>(drag.source.startFraction);
    const auto activeWidth = drag.bounds.width * activeFraction;
    if (activeWidth <= 0.0) {
      vibratoHandleDrag_.reset();
      repaint();
      return core::failure(core::ErrorCode::Conflict,
                           "Vibrato fade handle has no active span");
    }
    const auto pointerDelta = position.x - drag.handleStart.x;
    if (drag.kind == VibratoHandleKind::FadeIn) {
      preview.fadeInFraction = static_cast<float>(std::clamp(
          static_cast<double>(drag.source.fadeInFraction) +
              pointerDelta / activeWidth,
          0.0,
          1.0 - static_cast<double>(drag.source.fadeOutFraction)));
    } else {
      preview.fadeOutFraction = static_cast<float>(std::clamp(
          static_cast<double>(drag.source.fadeOutFraction) -
              pointerDelta / activeWidth,
          0.0,
          1.0 - static_cast<double>(drag.source.fadeInFraction)));
    }
  } else if (drag.kind == VibratoHandleKind::Period) {
    const auto activeFraction = 1.0 - static_cast<double>(drag.source.startFraction);
    const auto activeWidth = drag.bounds.width * activeFraction;
    if (activeWidth <= 0.0 || drag.activeDurationMilliseconds < 5.0) {
      vibratoHandleDrag_.reset();
      repaint();
      return core::failure(core::ErrorCode::Conflict,
                           "Vibrato period handle has no valid span");
    }
    preview.periodMilliseconds = static_cast<float>(std::clamp(
        static_cast<double>(drag.source.periodMilliseconds) +
            (position.x - drag.handleStart.x) / activeWidth *
                drag.activeDurationMilliseconds,
        5.0,
        std::min(500.0, drag.activeDurationMilliseconds)));
  } else if (drag.kind == VibratoHandleKind::Phase) {
    const auto activeFraction = 1.0 - static_cast<double>(drag.source.startFraction);
    const auto periodWidth = drag.bounds.width * activeFraction *
        (static_cast<double>(drag.source.periodMilliseconds) /
         drag.activeDurationMilliseconds);
    if (periodWidth <= 0.0 || drag.activeDurationMilliseconds < 5.0) {
      vibratoHandleDrag_.reset();
      repaint();
      return core::failure(core::ErrorCode::Conflict,
                           "Vibrato phase handle has no valid cycle");
    }
    preview.phaseTurns = static_cast<float>(std::clamp(
        static_cast<double>(drag.source.phaseTurns) +
            (position.x - drag.handleStart.x) / periodWidth,
        0.0, 0.999999));
  } else {
    const auto visualAmplitude = drag.bounds.height * 0.28;
    const auto depthDelta = visualAmplitude > 0.0
        ? (drag.handleStart.y - position.y) / visualAmplitude * 200.0 : 0.0;
    preview.depthCents = static_cast<float>(std::clamp(
        static_cast<double>(drag.source.depthCents) + depthDelta, 0.0, 200.0));
  }
  const auto valid = preview.validate();
  if (!valid) return core::Result<void>{valid.error()};
  drag.preview = preview;
  repaint();
  return core::success();
}

std::vector<VibratoHandleKind>
NativeEditorController::availableVibratoHandles() const {
  std::vector<VibratoHandleKind> result;
  const auto selected = session_.selection().noteIds();
  if (selected.size() != 1U) return result;
  const auto* region = session_.project().findRegion(regionId_);
  const auto* note = session_.project().findNote(selected.front());
  if (region == nullptr || note == nullptr || !note->vibrato.enabled) return result;
  const auto visuals = pianoRoll_.visibleNotes();
  const auto visual = std::find_if(visuals.begin(), visuals.end(),
      [&selected](const ui::NoteVisual& candidate) {
        return candidate.noteId == selected.front() && !candidate.hiddenByOverlapDensity;
      });
  if (visual == visuals.end()) return result;
  auto bounds = visual->bounds;
  bounds.y += layout_.contentTop();
  const auto handles = vibratoHandlePositions(
      *note, region->startTick, session_.project().tempoMap(), bounds);
  if (!handles) return result;
  result = {VibratoHandleKind::Onset, VibratoHandleKind::Depth};
  if (handles->fadeIn) result.push_back(VibratoHandleKind::FadeIn);
  if (handles->fadeOut) result.push_back(VibratoHandleKind::FadeOut);
  if (handles->period) result.push_back(VibratoHandleKind::Period);
  if (handles->phase) result.push_back(VibratoHandleKind::Phase);
  return result;
}

core::Result<void> NativeEditorController::focusVibratoHandle(int direction) {
  const auto handles = availableVibratoHandles();
  if (handles.empty()) {
    vibratoKeyboardFocus_.reset();
    repaint();
    return core::failure(core::ErrorCode::Unsupported,
                         "Select one visible note with vibrato to focus its handles");
  }
  const auto current = std::find(handles.begin(), handles.end(),
                                 vibratoKeyboardFocus_.value_or(handles.front()));
  const auto index = current == handles.end()
      ? 0U : static_cast<std::size_t>(std::distance(handles.begin(), current));
  if (direction == 0) {
    vibratoKeyboardFocus_ = handles.front();
    repaint();
    return core::success();
  }
  const auto count = static_cast<std::ptrdiff_t>(handles.size());
  auto next = static_cast<std::ptrdiff_t>(index) +
      (direction < 0 ? -1 : 1);
  next = (next % count + count) % count;
  vibratoKeyboardFocus_ = handles[static_cast<std::size_t>(next)];
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::adjustFocusedVibratoHandle(
    int direction) {
  if (!vibratoKeyboardFocus_) return core::success();
  const auto selected = session_.selection().noteIds();
  const auto* region = session_.project().findRegion(regionId_);
  const auto* note = selected.size() == 1U
      ? session_.project().findNote(selected.front()) : nullptr;
  if (note == nullptr || region == nullptr || !note->vibrato.enabled) {
    vibratoKeyboardFocus_.reset();
    repaint();
    return core::failure(core::ErrorCode::Conflict,
                         "Vibrato keyboard target is no longer selected");
  }
  const auto visuals = pianoRoll_.visibleNotes();
  const auto visual = std::find_if(visuals.begin(), visuals.end(),
      [&selected](const ui::NoteVisual& candidate) {
        return candidate.noteId == selected.front() && !candidate.hiddenByOverlapDensity;
      });
  if (visual == visuals.end()) return core::failure(
      core::ErrorCode::NotFound, "Vibrato keyboard target is not visible");
  auto bounds = visual->bounds;
  bounds.y += layout_.contentTop();
  const auto handles = vibratoHandlePositions(
      *note, region->startTick, session_.project().tempoMap(), bounds);
  if (!handles) return core::failure(core::ErrorCode::Conflict,
                                      "Vibrato handles are unavailable");
  std::optional<ui::Point> currentPoint;
  switch (*vibratoKeyboardFocus_) {
    case VibratoHandleKind::Onset: currentPoint = handles->onset; break;
    case VibratoHandleKind::Depth: currentPoint = handles->depth; break;
    case VibratoHandleKind::FadeIn: currentPoint = handles->fadeIn; break;
    case VibratoHandleKind::FadeOut: currentPoint = handles->fadeOut; break;
    case VibratoHandleKind::Period: currentPoint = handles->period; break;
    case VibratoHandleKind::Phase: currentPoint = handles->phase; break;
  }
  if (!currentPoint) return core::failure(core::ErrorCode::Conflict,
                                          "Focused vibrato handle is unavailable");

  constexpr double sampleRate = 48000.0;
  const auto startFrame = session_.project().tempoMap().sampleFrameAt(
      region->startTick + note->startTick, sampleRate);
  const auto endFrame = session_.project().tempoMap().sampleFrameAt(
      region->startTick + note->endTick(), sampleRate);
  const auto activeDurationMilliseconds =
      (static_cast<double>(endFrame) - static_cast<double>(startFrame)) / 48.0 *
      (1.0 - static_cast<double>(note->vibrato.startFraction));
  if (!std::isfinite(activeDurationMilliseconds) ||
      (activeDurationMilliseconds < 5.0 &&
       (*vibratoKeyboardFocus_ == VibratoHandleKind::Period ||
        *vibratoKeyboardFocus_ == VibratoHandleKind::Phase))) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vibrato keyboard target has no active duration");
  }
  auto target = *currentPoint;
  const auto activeFraction = 1.0 -
      static_cast<double>(note->vibrato.startFraction);
  const auto activeWidth = bounds.width * activeFraction;
  const auto onsetX = bounds.x + bounds.width *
      static_cast<double>(note->vibrato.startFraction);
  switch (*vibratoKeyboardFocus_) {
    case VibratoHandleKind::Onset: {
      const auto value = std::clamp(
          static_cast<double>(note->vibrato.startFraction) + direction * 0.01,
          0.0, std::max(0.0, 1.0 - 8.0 / bounds.width));
      target.x = bounds.x + bounds.width * value;
      break;
    }
    case VibratoHandleKind::Depth:
      target.y -= direction * bounds.height * 0.28 * (5.0 / 200.0);
      break;
    case VibratoHandleKind::FadeIn: {
      const auto value = std::clamp(
          static_cast<double>(note->vibrato.fadeInFraction) + direction * 0.01,
          0.0, 1.0 - static_cast<double>(note->vibrato.fadeOutFraction));
      target.x = onsetX + activeWidth * value;
      break;
    }
    case VibratoHandleKind::FadeOut: {
      const auto value = std::clamp(
          static_cast<double>(note->vibrato.fadeOutFraction) + direction * 0.01,
          0.0, 1.0 - static_cast<double>(note->vibrato.fadeInFraction));
      target.x = onsetX + activeWidth * (1.0 - value);
      break;
    }
    case VibratoHandleKind::Period: {
      const auto value = std::clamp(
          static_cast<double>(note->vibrato.periodMilliseconds) +
              direction * 5.0,
          5.0, std::min(500.0, activeDurationMilliseconds));
      target.x = onsetX + activeWidth * value / activeDurationMilliseconds;
      break;
    }
    case VibratoHandleKind::Phase: {
      const auto value = std::clamp(
          static_cast<double>(note->vibrato.phaseTurns) + direction * 0.02,
          0.0, 0.999999);
      const auto periodWidth = activeWidth *
          static_cast<double>(note->vibrato.periodMilliseconds) /
          activeDurationMilliseconds;
      target.x = onsetX + periodWidth * value;
      break;
    }
  }
  vibratoHandleDrag_ = VibratoHandleDrag{
      .noteId = note->id,
      .kind = *vibratoKeyboardFocus_,
      .bounds = bounds,
      .handleStart = *currentPoint,
      .source = note->vibrato,
      .preview = note->vibrato,
      .revision = session_.revision(),
      .logicalWidth = logicalWidth_,
      .logicalHeight = logicalHeight_,
      .activeDurationMilliseconds = activeDurationMilliseconds,
  };
  const auto preview = dragVibratoHandle(target);
  if (!preview) {
    vibratoHandleDrag_.reset();
    repaint();
    return preview;
  }
  return finishVibratoHandleDrag();
}

core::Result<void> NativeEditorController::finishVibratoHandleDrag() {
  if (!vibratoHandleDrag_) return core::success();
  const auto drag = *vibratoHandleDrag_;
  vibratoHandleDrag_.reset();
  repaint();
  if (drag.source == drag.preview) return core::success();
  if (session_.revision() != drag.revision ||
      session_.selection().noteIds() != std::vector<domain::NoteId>{drag.noteId}) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vibrato target changed before the handle edit committed");
  }
  ui::VibratoFields patch;
  switch (drag.kind) {
    case VibratoHandleKind::Onset:
      patch.startFraction = drag.preview.startFraction;
      break;
    case VibratoHandleKind::FadeIn:
      patch.fadeInFraction = drag.preview.fadeInFraction;
      break;
    case VibratoHandleKind::FadeOut:
      patch.fadeOutFraction = drag.preview.fadeOutFraction;
      break;
    case VibratoHandleKind::Period:
      patch.periodMilliseconds = drag.preview.periodMilliseconds;
      break;
    case VibratoHandleKind::Phase:
      patch.phaseTurns = drag.preview.phaseTurns;
      break;
    case VibratoHandleKind::Depth:
      patch.depthCents = drag.preview.depthCents;
      break;
  }
  auto model = ui::VibratoModel::prepare(session_, regionId_, patch);
  if (!model) return core::Result<void>{model.error()};
  const auto applied = model.value().apply(session_, regionId_);
  if (applied) markDocumentChanged();
  repaint();
  return applied;
}

core::Result<void> NativeEditorController::pointerMove(
    const PointerEvent& event) {
  if (vibratoHandleDrag_) {
    if (event.button != PointerButton::Left) {
      vibratoHandleDrag_.reset();
      repaint();
      return core::success();
    }
    return dragVibratoHandle(event.position);
  }
  if (replacementOpen_ && dynamicsGainDragging_) {
    if (event.button != PointerButton::Left) { dynamicsGainDragging_ = false; return core::success(); }
    return dragDynamicsPoint(event.position);
  }
  if (replacementOpen_) return core::success();
  if (phonemeReview_) return core::success();
  if (dragMode_ == DragMode::None) {
    const auto point = modelPoint(event.position);
    const auto hovered = pianoRoll_.hitTest(point);
    bool changed = false;
    if (hovered.has_value()) {
      const auto visuals = pianoRoll_.visibleNotes();
      const auto visual = std::find_if(
          visuals.begin(), visuals.end(), [hovered](const ui::NoteVisual& note) {
            return note.noteId == *hovered;
          });
      changed = interaction_.updateHoveredNote(
          *hovered, visual == visuals.end() ? std::string{} : visual->lyric);
    } else {
      changed = interaction_.clearHover();
    }
    if (changed) {
      repaint();
    }
    return core::success();
  }
  if (dragMode_ == DragMode::RulerSeek) {
    const auto tick = pianoRoll_.timeline().pixelToTick(
        std::max(0.0, event.position.x - timelineOriginX()));
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
  // A TUNE pitch gesture is driven by dragPitchPoint; lane motion never moves it.
  if (dragMode_ == DragMode::EditPitchPoint) return core::success();
  if (dragMode_ == DragMode::MoveExpressionPoint) {
    const auto expressionState = sceneState();
    const auto overlayInset = layout_.diagnosticHeight(!expressionState.diagnostics.empty()) +
        layout_.exportHeight(expressionState.exportProgress.totalFiles != 0U);
    const auto technical = resolveEditorTechnicalLaneHeights(
        expressionState, layout_, logicalHeight_ - layout_.statusHeight - overlayInset);
    if (hosted_)
      return updateExpressionGesture(event.position, hosted_->pianoBottom, hosted_->laneHeight);
    const auto automationTop = technical.pianoBottom + technical.values[0U] +
        technical.values[1U] + technical.values[2U];
    return updateExpressionGesture(event.position, automationTop, technical.values[3U]);
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
  if (vibratoHandleDrag_) {
    if (event.button != PointerButton::Left) {
      vibratoHandleDrag_.reset();
      repaint();
      return core::success();
    }
    const auto updated = dragVibratoHandle(event.position);
    if (!updated) return updated;
    return finishVibratoHandleDrag();
  }
  if (replacementOpen_ && dynamicsGainDragging_) {
    if (event.button != PointerButton::Left) { dynamicsGainDragging_ = false; return core::success(); }
    const auto result = dragDynamicsPoint(event.position); dynamicsGainDragging_ = false; return result;
  }
  if (replacementOpen_) return core::success();
  if (phonemeReview_) return core::success();
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
      const auto hostedTop = hosted_ ? hosted_->pianoBottom : automationTop;
      const auto hostedHeight = hosted_ ? hosted_->laneHeight : automationHeight;
      const auto* region = session_.project().findRegion(regionId_);
      if (region == nullptr) {
        result = core::failure(core::ErrorCode::NotFound,
                               "Pitch automation region is missing");
      } else {
        const auto tick = laneTickAt(dragCurrent_.x);
        const auto normalized = std::clamp(
            (hostedTop +
             hostedHeight * layout_.automationCenterFraction -
             dragCurrent_.y) /
                (hostedHeight * layout_.pitchAutomationVerticalScale),
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
  if (dragMode_ == DragMode::EditPitchPoint) return releasePitchPoint();
  if (dragMode_ == DragMode::MoveExpressionPoint) {
    return endExpressionGesture();
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
            std::max(0.0, dragCurrent_.x - timelineOriginX()));
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
  retryHostSelectionBeforeInput();
  if (replacementOpen_) {
    if (dynamicsDraft_) {
      using Action = ui::DynamicsPlotViewport::Action;
      if (event.key == NativeKey::Plus) return navigateDynamics(Action::ZoomIn);
      if (event.key == NativeKey::Minus) return navigateDynamics(Action::ZoomOut);
      if (event.key == NativeKey::Left) return navigateDynamics(Action::Left);
      if (event.key == NativeKey::Right) return navigateDynamics(Action::Right);
      if (event.key == NativeKey::R) return navigateDynamics(Action::Fit);
    }
    if (event.key == NativeKey::Escape) return replacementReviewAction(replacementDetail_ && !findMode_ ? 3U : 4U);
    if (event.key == NativeKey::Tab) { rebuildAccessibilityTree(); const auto focused = accessibilityTree_.focusNext(event.modifiers.shift); repaint(); return focused; }
    if (event.key == NativeKey::Enter) {
      rebuildAccessibilityTree(); const auto* focused = accessibilityTree_.focusedNode();
      if (focused) { const auto id = focused->id; return dispatchAccessibility(id, SemanticAction::Activate); }
    }
    return core::success();
  }
  if (timeMapPanel_) {
    if (composition_.active()) {
      if (event.key == NativeKey::Escape) { cancelTextComposition(); return core::success(); }
      if (event.key == NativeKey::Enter || event.key == NativeKey::Tab) return commitTextComposition(composition_.compositionText());
      return core::success();
    }
    if (event.key == NativeKey::Escape) return timeMapPanelAction(5U);
    if (event.key == NativeKey::Tab) {
      rebuildAccessibilityTree(); const auto focused = accessibilityTree_.focusNext(event.modifiers.shift);
      repaint(); return focused;
    }
    if (event.key == NativeKey::Enter) {
      if (const auto* focused = accessibilityTree_.focusedNode(); focused && focused->id.starts_with(timeMapSemanticPrefix())) {
        const auto id = focused->id;
        const bool row = id.find(".row.") != std::string::npos;
        const auto activated = dispatchAccessibility(id, SemanticAction::Activate);
        if (!activated || !row) return activated;
      }
      return timeMapPanelAction(2U);
    }
    if (event.key == NativeKey::Delete || event.key == NativeKey::Backspace) return timeMapPanelAction(3U);
    if (event.key == NativeKey::R) return timeMapPanelAction(4U);
    if (event.key == NativeKey::N) return timeMapPanelAction(event.modifiers.shift ? 7U : 6U);
    if (event.key == NativeKey::Left || event.key == NativeKey::Right) return timeMapPanelAction(event.key == NativeKey::Left ? 0U : 1U);
    if (event.key == NativeKey::Up || event.key == NativeKey::Down) {
      auto index = timeMapPanel_->selectedIndex();
      if (event.key == NativeKey::Up && index > 0U) --index;
      if (event.key == NativeKey::Down && index + 1U < timeMapPanel_->size()) ++index;
      const auto selected = timeMapPanel_->select(index); timeMapPage_ = index / TempoMeterModel::pageSize;
      repaint(); return selected;
    }
    return core::success();
  }
  if (phonemeReview_) {
    if (event.key == NativeKey::Escape) return activatePhonemeReview(2U);
    if (event.key == NativeKey::Tab) {
      rebuildAccessibilityTree();
      const auto focused = accessibilityTree_.focusNext(event.modifiers.shift);
      repaint();
      return focused;
    }
    if (event.key == NativeKey::Enter) {
      if (const auto* focused = accessibilityTree_.focusedNode()) {
        const auto id = focused->id;
        return dispatchAccessibility(id, SemanticAction::Activate);
      }
    }
    return core::success();
  }
  if (sampleMicroscopeOpen()) {
    if (event.key == NativeKey::Escape) {
      if (microscopeDetailsVisible_) return microscopeDetailsAction(0U);
      closeSampleMicroscope(); return core::success();
    }
    if (event.key == NativeKey::Tab) {
      rebuildAccessibilityTree();
      const auto focused = accessibilityTree_.focusNext(event.modifiers.shift);
      repaint(); return focused;
    }
    if (event.key == NativeKey::Enter) {
      if (const auto* focused = accessibilityTree_.focusedNode()) {
        const auto id = focused->id;
        return dispatchAccessibility(id, SemanticAction::Activate);
      }
    }
    if (event.key == NativeKey::D) return microscopeDetailsAction(0U);
    if (microscopeDetailsVisible_ && (event.key == NativeKey::Left || event.key == NativeKey::Right))
      return microscopeDetailsAction(event.key == NativeKey::Left ? 1U : 2U);
    return core::success();
  }
  if (recoverySupportPanel_.view().visible &&
      event.key == NativeKey::Escape) {
    if (!diagnosticPanel_.entries().empty()) {
      const auto& diagnostic = diagnosticPanel_.entries().front().diagnostic;
      const auto dismiss = std::find(diagnostic.actions.begin(),
                                     diagnostic.actions.end(),
                                     authoring::DiagnosticAction::Dismiss);
      if (dismiss != diagnostic.actions.end()) {
        return activateDiagnostic(0U, *dismiss);
      }
    }
    recoverySupportPanel_.update({});
    repaint();
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
      const bool auxiliaryInput = tempoEdit_.has_value() || hintEdit_.has_value() || replacementInput_.has_value() || batchLyricTarget_.has_value();
      const auto committed = commitTextComposition(composition_.compositionText());
      if (!committed) return committed;
      if (auxiliaryInput) return core::success();
      return navigateLyricEdit(event.modifiers.shift ? -1 : 1);
    }
    if (hintEdit_ || replacementInput_ || batchLyricTarget_) return core::success(); // Native text input owns other keys, not score shortcuts.
  }

  if (recoverySupportPanel_.view().visible && event.modifiers.alt &&
      !event.modifiers.shift && !event.modifiers.primaryShortcut() &&
      (event.key == NativeKey::Left || event.key == NativeKey::Right)) {
    if (arrangementPanel_.tracks().size() < 2U) return core::success();
    return selectAdjacentVocalTrack(event.key == NativeKey::Right ? 1 : -1);
  }

  if (event.key == NativeKey::V && event.modifiers.alt &&
      !event.modifiers.shift && !event.modifiers.primaryShortcut()) {
    if (vibratoKeyboardFocus_) {
      vibratoKeyboardFocus_.reset();
      repaint();
      return core::success();
    }
    return focusVibratoHandle(0);
  }
  if (vibratoKeyboardFocus_) {
    if (event.key == NativeKey::Escape) {
      vibratoKeyboardFocus_.reset();
      repaint();
      return core::success();
    }
    if (!event.modifiers.alt && !event.modifiers.primaryShortcut() &&
        (event.key == NativeKey::Left || event.key == NativeKey::Right)) {
      return focusVibratoHandle(event.key == NativeKey::Left ? -1 : 1);
    }
    if (!event.modifiers.alt && !event.modifiers.primaryShortcut() &&
        (event.key == NativeKey::Up || event.key == NativeKey::Down)) {
      return adjustFocusedVibratoHandle(event.key == NativeKey::Up ? 1 : -1);
    }
  }

  if (event.key == NativeKey::Tab) {
    vibratoKeyboardFocus_.reset();
    rebuildAccessibilityTree();
    const auto focused = accessibilityTree_.focusNext(event.modifiers.shift);
    if (focused) {
      if (const auto* node = accessibilityTree_.focusedNode(); node != nullptr) {
        const auto selected = session_.selection().noteIds();
        if (selected.size() == 1U) {
          for (const auto candidate : availableVibratoHandles()) {
            if (node->id == vibratoHandleSemanticId(selected.front(), candidate)) {
              vibratoKeyboardFocus_ = candidate;
              break;
            }
          }
        }
      }
      syncInteractionToAccessibilityFocus();
      repaint();
    }
    return focused;
  }

  if (voicebankBrowserVisible_) {
    if (event.key == NativeKey::Escape || event.key == NativeKey::V) {
      closeVoicebankBrowser();
      return core::success();
    }
    if (event.key == NativeKey::R) return refreshVoicebanks();
    if (event.key == NativeKey::O) return openVoicebankInstaller();
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
      closeAudioSettings();
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

  if (event.key == NativeKey::Enter && event.modifiers.alt &&
      !event.modifiers.primaryShortcut()) {
    return beginSelectedHintEdit();
  }

  // While the expression lane is drawn it owns the two channel gestures that would otherwise belong to
  // the whole document: Alt+Up/Down is a nudge of the selected channel, and Shift+Alt+Left/Right walks
  // the channel picker. Both are undoable as single commands.
  if (expressionLaneVisible_ && !event.modifiers.primaryShortcut()) {
    if (event.modifiers.alt && !event.modifiers.shift &&
        (event.key == NativeKey::Up || event.key == NativeKey::Down)) {
      const auto nudged = nudgeExpressionLane(event.key == NativeKey::Up ? 1 : -1);
      repaint();
      return nudged;
    }
    if (event.modifiers.alt && event.modifiers.shift &&
        (event.key == NativeKey::Left || event.key == NativeKey::Right)) {
      const auto cycled = cycleExpressionLane(event.key == NativeKey::Right ? 1 : -1);
      repaint();
      return cycled;
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
      reconcileWithProject();
      markDocumentChanged();
    }
  } else if (event.modifiers.primaryShortcut() && event.key == NativeKey::Y) {
    result = session_.redo();
    if (result) {
      reconcileWithProject();
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
    const auto mode = session_.project().settings().characterDisplay;
    setCharacterDisplay(mode == domain::CharacterDisplayMode::Full
                            ? domain::CharacterDisplayMode::Minimal
                        : mode == domain::CharacterDisplayMode::Minimal
                            ? domain::CharacterDisplayMode::Off
                            : domain::CharacterDisplayMode::Full);
  } else if (event.key == NativeKey::V) {
    if (recoverySupportPanel_.view().visible) return core::success();
    voicebankBrowserVisible_ = !voicebankBrowserVisible_;
    if (voicebankBrowserVisible_) {
      voicebankBrowserFirstCard_ = 0U;
      audioSettings_.visible = false;
    }
    if (callbacks_.viewChanged) {
      callbacks_.viewChanged();
    } else {
      repaint();
    }
  } else if (event.key == NativeKey::I) {
    if (recoverySupportPanel_.view().visible) return core::success();
    audioSettings_.visible = !audioSettings_.visible;
    if (audioSettings_.visible) {
      voicebankBrowserVisible_ = false;
    }
    if (callbacks_.viewChanged) {
      callbacks_.viewChanged();
    } else {
      repaint();
    }
  } else if (event.key == NativeKey::Plus || event.key == NativeKey::Minus) {
    pianoRoll_.timeline().zoomAround(
        (hosted_ ? pianoRoll_.viewport().bounds.right() - timelineOriginX()
                 : logicalWidth_ - layout_.keyboardWidth) * 0.5,
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
  if (replacementOpen_ && dynamicsDraft_) {
    const auto view = replacementReviewView();
    if (!view.dynamicsPlot || !view.dynamicsPlot->editable || !view.dynamicsPlot->bounds.contains(anchor) ||
        !std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
    const bool zoom = modifiers.primaryShortcut();
    if (zoom != dynamicsScrollZoom_) { dynamicsScrollRemainder_ = 0.0; dynamicsScrollZoom_ = zoom; }
    const auto delta = zoom ? deltaY : std::abs(deltaX) > std::abs(deltaY) ? deltaX : deltaY;
    dynamicsScrollRemainder_ = std::clamp(dynamicsScrollRemainder_ + delta, -240.0, 240.0);
    if (std::abs(dynamicsScrollRemainder_) < 20.0) return;
    const bool positive = dynamicsScrollRemainder_ > 0.0; dynamicsScrollRemainder_ = 0.0;
    using Action = ui::DynamicsPlotViewport::Action;
    static_cast<void>(navigateDynamics(zoom ? (positive ? Action::ZoomOut : Action::ZoomIn) :
        (positive ? Action::Right : Action::Left), (anchor.x - view.dynamicsPlot->bounds.x) / view.dynamicsPlot->bounds.width));
    return;
  }
  if (phonemeReview_ || timeMapPanel_ || replacementOpen_ || replacementInput_ || sampleMicroscopeOpen()) return;
  const auto& support = recoverySupportPanel_.view();
  if (support.visible && !support.items.empty()) {
    const auto panelX = std::max(
        layout_.keyboardWidth + layout_.minimumTimelineWidth,
        logicalWidth_ - layout_.characterDockWidth);
    if (anchor.x >= panelX) {
      auto view = support;
      if (deltaY < 0.0) {
        view.firstVisibleItem = std::min(
            view.firstVisibleItem + 1U, view.items.size() - 1U);
      } else if (deltaY > 0.0 && view.firstVisibleItem > 0U) {
        --view.firstVisibleItem;
      }
      recoverySupportPanel_.update(std::move(view));
      repaint();
      return;
    }
  }
  if (modifiers.control || modifiers.command) {
    pianoRoll_.timeline().zoomAround(
        std::max(0.0, anchor.x - timelineOriginX()),
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
    const auto x = laneX(candidate.tick);
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

void NativeEditorController::setAudioState(bool online, std::string backend) {
  // Setting what was set asks for no frame, as setDiagnostics does not: a start of the audio device
  // that fails again and again sets the same state each time, and a frame that asked for the next one
  // for nothing would keep a window that paints only on request painting at the display's rate.
  if (audioOnline_ == online && audioBackend_ == backend) return;
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

float NativeEditorController::formantShiftAtPlayhead() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? 0.0F : region->formantAutomation.valueAt(regionPlayheadClamped());
}

core::Result<void> NativeEditorController::nudgeFormantShift(int steps) {
  const auto* region = session_.project().findRegion(regionId_);
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (region == nullptr || track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Formant edit has no region or track");
  if (steps == 0) return core::success();
  // The host can resolve renderer-specific sample capabilities. Without that
  // context remain conservative; the sample-bank family alone proves nothing.
  const auto carrier = synthesis::rendererCarrierFor(*track);
  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Formant);
  auto allowed = core::success();
  if (callbacks_.validateSingerControl) {
    allowed = callbacks_.validateSingerControl(selectedTrackId_, synthesis::RendererControl::Formant);
  } else {
    const auto decision = synthesis::validateRendererCapabilities(carrier, request);
    if (!decision) allowed = core::Result<void>{decision.error()};
  }
  if (!allowed) {
    return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
        std::string{"The selected singer route cannot apply a formant shift. "} +
            allowed.error().message +
            ". Select a Spectral Classic sample route or a source-filter (voice designer) singer."}};
  }
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = region->formantAutomation.valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(current + static_cast<float>(steps),
                                                  -domain::kMaximumFormantShiftSemitones,
                                                  domain::kMaximumFormantShiftSemitones));
  auto next = region->formantAutomation;
  if (!(target != 0.0F))
    next = domain::FormantAutomation{};
  else {
    const auto inserted = next.upsert(domain::FormantAutomationPoint{playheadInRegion, target});
    if (!inserted) return inserted;
  }
  // A nudge that lands on the value already in force is not an edit, and a no-op must not fill the
  // undo stack with an entry that changes nothing.
  if (next == region->formantAutomation) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{{regionId_, std::move(next)}}));
  return commitTimbralEdit(applied);
}

core::Result<void> NativeEditorController::resetFormantCurve() {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Formant edit has no region");
  if (region->formantAutomation.points().empty()) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{
              {regionId_, domain::FormantAutomation{}}}));
  return commitTimbralEdit(applied);
}

float NativeEditorController::breathinessAtPlayhead() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? 0.0F : region->breathinessAutomation.valueAt(regionPlayheadClamped());
}

core::Result<void> NativeEditorController::nudgeBreathiness(int steps) {
  // One step is a tenth of the channel, which is the unit a creator actually hears: the channel is
  // normalized, so a step is a share of the balance rather than a level in decibels.
  constexpr float kBreathinessStep = 0.1F;
  const auto* region = session_.project().findRegion(regionId_);
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (region == nullptr || track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Breathiness edit has no region or track");
  if (steps == 0) return core::success();
  // Only a carrier that generates its own excitation has a balance to move. Everything else is refused
  // with the reason and the change that would make it possible, and whatever curve is already stored
  // stays exactly as it was.
  const auto carrier = synthesis::rendererCarrierFor(*track);
  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Breathiness);
  auto allowed = core::success();
  if (callbacks_.validateSingerControl) {
    allowed = callbacks_.validateSingerControl(
        selectedTrackId_, synthesis::RendererControl::Breathiness);
  } else {
    const auto decision = synthesis::validateRendererCapabilities(carrier, request);
    if (!decision) allowed = core::Result<void>{decision.error()};
  }
  if (!allowed) {
    if (callbacks_.validateSingerControl) {
      return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
          std::string{"The selected singer cannot apply a breathiness curve. "} +
              allowed.error().message +
              ". Select a singer whose admitted route declares breathiness conditioning."}};
    }
    return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
        std::string{"The selected singer does not generate the excitation a breathiness curve would "
                    "rebalance, so it cannot apply one. "} +
            allowed.error().message +
            ". Select a source-filter (voice designer) singer to edit this channel."}};
  }
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = region->breathinessAutomation.valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(
      current + kBreathinessStep * static_cast<float>(steps), 0.0F, domain::kMaximumBreathiness));
  auto next = region->breathinessAutomation;
  if (!(target != 0.0F))
    next = domain::BreathinessAutomation{};
  else {
    const auto inserted =
        next.upsert(domain::BreathinessAutomationPoint{playheadInRegion, target});
    if (!inserted) return inserted;
  }
  // A nudge that lands on the value already in force is not an edit, and a no-op must not fill the undo
  // stack with an entry that changes nothing.
  if (next == region->breathinessAutomation) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{{regionId_, std::move(next)}}));
  return commitTimbralEdit(applied);
}

core::Result<void> NativeEditorController::resetBreathinessCurve() {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Breathiness edit has no region");
  if (region->breathinessAutomation.points().empty()) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{
              {regionId_, domain::BreathinessAutomation{}}}));
  return commitTimbralEdit(applied);
}

float NativeEditorController::tensionAtPlayhead() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? 0.0F : region->tensionAutomation.valueAt(regionPlayheadClamped());
}

core::Result<void> NativeEditorController::nudgeTension(int steps) {
  // One step is a tenth of the channel, matching the share-based channels beside it.
  constexpr float kTensionStep = 0.1F;
  const auto* region = session_.project().findRegion(regionId_);
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (region == nullptr || track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Tension edit has no region or track");
  if (steps == 0) return core::success();
  const auto carrier = synthesis::rendererCarrierFor(*track);
  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Tension);
  const auto allowed = synthesis::validateRendererCapabilities(carrier, request);
  if (!allowed) {
    return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
        std::string{"The selected singer does not generate the harmonic source a tension curve would "
                    "shape, so it cannot apply one. "} +
            allowed.error().message +
            ". Select a source-filter (voice designer) singer to edit this channel."}};
  }
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = region->tensionAutomation.valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(
      current + kTensionStep * static_cast<float>(steps), 0.0F, domain::kMaximumTension));
  auto next = region->tensionAutomation;
  if (!(target != 0.0F))
    next = domain::TensionAutomation{};
  else {
    const auto inserted = next.upsert(domain::TensionAutomationPoint{playheadInRegion, target});
    if (!inserted) return inserted;
  }
  if (next == region->tensionAutomation) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{{regionId_, std::move(next)}}));
  return commitTimbralEdit(applied);
}

core::Result<void> NativeEditorController::resetTensionCurve() {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Tension edit has no region");
  if (region->tensionAutomation.points().empty()) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{{regionId_, domain::TensionAutomation{}}}));
  return commitTimbralEdit(applied);
}

float NativeEditorController::airinessAtPlayhead() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? 0.0F : region->airinessAutomation.valueAt(regionPlayheadClamped());
}

core::Result<void> NativeEditorController::nudgeAiriness(int steps) {
  constexpr float kAirinessStep = 0.1F;
  const auto* region = session_.project().findRegion(regionId_);
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (region == nullptr || track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Airiness edit has no region or track");
  if (steps == 0) return core::success();
  const auto carrier = synthesis::rendererCarrierFor(*track);
  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Airiness);
  const auto allowed = synthesis::validateRendererCapabilities(carrier, request);
  if (!allowed) {
    return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
        std::string{"The selected singer does not generate the noise band an airiness curve would add, "
                    "so it cannot apply one. "} +
            allowed.error().message +
            ". Select a source-filter (voice designer) singer to edit this channel."}};
  }
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = region->airinessAutomation.valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(
      current + kAirinessStep * static_cast<float>(steps), 0.0F, domain::kMaximumAiriness));
  auto next = region->airinessAutomation;
  if (!(target != 0.0F))
    next = domain::AirinessAutomation{};
  else {
    const auto inserted = next.upsert(domain::AirinessAutomationPoint{playheadInRegion, target});
    if (!inserted) return inserted;
  }
  if (next == region->airinessAutomation) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{{regionId_, std::move(next)}}));
  return commitTimbralEdit(applied);
}

core::Result<void> NativeEditorController::resetAirinessCurve() {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Airiness edit has no region");
  if (region->airinessAutomation.points().empty()) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{
              {regionId_, domain::AirinessAutomation{}}}));
  return commitTimbralEdit(applied);
}

float NativeEditorController::genderAtPlayhead() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? 0.0F : region->genderAutomation.valueAt(regionPlayheadClamped());
}

core::Result<void> NativeEditorController::nudgeGender(int steps) {
  constexpr float kGenderStep = 0.1F;
  const auto* region = session_.project().findRegion(regionId_);
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (region == nullptr || track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Gender edit has no region or track");
  if (steps == 0) return core::success();
  const auto carrier = synthesis::rendererCarrierFor(*track);
  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Gender);
  const auto allowed = synthesis::validateRendererCapabilities(carrier, request);
  if (!allowed) {
    return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
        std::string{"The selected singer does not own both the vocal tract and the source that a gender "
                    "curve moves together, so it cannot apply one. "} +
            allowed.error().message +
            ". Select a source-filter (voice designer) singer to edit this channel."}};
  }
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = region->genderAutomation.valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(
      current + kGenderStep * static_cast<float>(steps),
      -domain::kMaximumGender, domain::kMaximumGender));
  auto next = region->genderAutomation;
  if (!(target != 0.0F))
    next = domain::GenderAutomation{};
  else {
    const auto inserted = next.upsert(domain::GenderAutomationPoint{playheadInRegion, target});
    if (!inserted) return inserted;
  }
  if (next == region->genderAutomation) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{},
          std::vector<application::RegionGenderEdit>{{regionId_, std::move(next)}}));
  return commitTimbralEdit(applied);
}

core::Result<void> NativeEditorController::resetGenderCurve() {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Gender edit has no region");
  if (region->genderAutomation.points().empty()) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{},
          std::vector<application::RegionGenderEdit>{{regionId_, domain::GenderAutomation{}}}));
  return commitTimbralEdit(applied);
}

float NativeEditorController::growlAtPlayhead() const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  return region == nullptr ? 0.0F : region->growlAutomation.valueAt(regionPlayheadClamped());
}

core::Result<void> NativeEditorController::nudgeGrowl(int steps) {
  constexpr float kGrowlStep = 0.1F;
  const auto* region = session_.project().findRegion(regionId_);
  const auto* track = session_.project().findVocalTrack(selectedTrackId_);
  if (region == nullptr || track == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Growl edit has no region or track");
  if (steps == 0) return core::success();
  const auto carrier = synthesis::rendererCarrierFor(*track);
  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Growl);
  const auto allowed = synthesis::validateRendererCapabilities(carrier, request);
  if (!allowed) {
    return core::Result<void>{core::Error{core::ErrorCode::Unsupported,
        std::string{"The selected singer does not generate the excitation a growl curve adds roughness "
                    "to, so it cannot apply one. "} +
            allowed.error().message +
            ". Select a source-filter (voice designer) singer to edit this channel."}};
  }
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = region->growlAutomation.valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(
      current + kGrowlStep * static_cast<float>(steps), 0.0F, domain::kMaximumGrowl));
  auto next = region->growlAutomation;
  const auto inserted = next.upsert(domain::GrowlAutomationPoint{playheadInRegion, target});
  if (!inserted) return inserted;
  // A neutral point can shape the ramp to another non-neutral point. Only collapse a wholly neutral
  // curve; clearing the region here would destroy edits outside the playhead.
  if (std::all_of(next.points().begin(), next.points().end(),
                  [](const auto& point) { return point.amount == 0.0F; }))
    next = domain::GrowlAutomation{};
  if (next == region->growlAutomation) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{},
          std::vector<application::RegionGenderEdit>{},
          std::vector<application::RegionGrowlEdit>{{regionId_, std::move(next)}}));
  return commitTimbralEdit(applied);
}

core::Result<void> NativeEditorController::resetGrowlCurve() {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Growl edit has no region");
  if (region->growlAutomation.points().empty()) return core::success();
  auto context = session_.capturePerformanceJob();
  if (!context) return core::Result<void>{context.error()};
  const auto applied = session_.executePerformanceResult(
      context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{},
          std::vector<application::RegionGenderEdit>{},
          std::vector<application::RegionGrowlEdit>{{regionId_, domain::GrowlAutomation{}}}));
  return commitTimbralEdit(applied);
}

}  // namespace seam::native_ui

namespace seam::native_ui {

core::Result<void> NativeEditorController::openExpressionLane(ui::ExpressionChannel channel) {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || sampleMicroscopeOpen())
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before opening the expression lane");
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Select a region before editing expression");
  expressionChannel_ = channel;
  expressionLaneVisible_ = true;
  expressionDraft_.reset();
  expressionDragTick_.reset();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::cycleExpressionLane(int direction) {
  if (direction == 0) return core::success();
  return openExpressionLane(ui::nextExpressionChannel(expressionChannel_, direction));
}

core::Result<void> NativeEditorController::closeExpressionLane() {
  expressionDraft_.reset();
  expressionDragTick_.reset();
  expressionLaneVisible_ = false;
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::toggleBounceTiming() {
  if (!callbacks_.setBounceTiming)
    return core::failure(core::ErrorCode::Unsupported, "Bounce timing is not connected");
  const auto result = callbacks_.setBounceTiming(!bounceFollowHost_);
  if (result) bounceFollowHost_ = !bounceFollowHost_;
  repaint();
  return result;
}

core::Result<void> NativeEditorController::setTechnicalLaneCollapsed(domain::TechnicalLane lane,
                                                                     bool collapsed) {
  const auto index = static_cast<std::size_t>(lane);
  if (index >= domain::kTechnicalLaneCount)
    return core::failure(core::ErrorCode::InvalidArgument, "Unknown technical lane");
  auto presentation = session_.project().settings().technicalLanes[index];
  const auto mode = collapsed ? domain::TechnicalLaneMode::Collapsed : domain::TechnicalLaneMode::Auto;
  if (presentation.mode == mode) return core::success();
  presentation.mode = mode;
  const auto fromState = sceneState();
  const auto changed = session_.execute(
      std::make_unique<application::SetTechnicalLanePresentationCommand>(lane, presentation));
  if (changed) {
    beginLayoutTransition(fromState);
    if (callbacks_.viewChanged) callbacks_.viewChanged();
    repaint();
  }
  return changed;
}

core::Result<ui::ExpressionLaneModel*> NativeEditorController::ensureExpressionDraft() {
  if (expressionDraft_) {
    // A draft with pending edits is reused as is; if the document moved on, its apply reports the
    // conflict instead of overwriting the newer curve. An untouched draft holds nothing to keep:
    // once the document, region or channel moved on it is re-read from the stored curve.
    if (expressionDraft_->hasChanges() ||
        (expressionDraft_->channel() == expressionChannel_ &&
         expressionDraft_->matches(session_, regionId_)))
      return &expressionDraft_.value();
    expressionDraft_.reset();
  }
  const bool resolved = static_cast<bool>(callbacks_.validateSingerControl);
  if (resolved) {
    const auto allowed = callbacks_.validateSingerControl(
        selectedTrackId_, ui::describeExpressionChannel(expressionChannel_).control);
    if (!allowed) return core::Result<ui::ExpressionLaneModel*>{allowed.error()};
  }
  auto prepared = ui::ExpressionLaneModel::prepare(
      session_, regionId_, expressionChannel_, {}, resolved);
  if (!prepared) return core::Result<ui::ExpressionLaneModel*>{prepared.error()};
  expressionDraft_.emplace(std::move(prepared.value()));
  return &expressionDraft_.value();
}

core::Result<void> NativeEditorController::commitExpressionDraft() {
  if (!expressionDraft_) return core::success();
  const auto applied = expressionDraft_->apply(session_, regionId_);
  if (!applied) { expressionDraft_.reset(); expressionDragTick_.reset(); return applied; }
  expressionDraft_.reset();
  expressionDragTick_.reset();
  markDocumentChanged();
  return core::success();
}

float NativeEditorController::expressionValueAtPlayhead() const {
  const auto playhead = regionPlayheadClamped();
  if (expressionDraft_ && expressionDraft_->current(session_, regionId_))
    return expressionDraft_->valueAt(playhead);
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return ui::describeExpressionChannel(expressionChannel_).neutral;
  const auto points = ui::readExpressionPoints(*region, expressionChannel_);
  const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
  if (points.empty()) return descriptor.neutral;
  const auto after = std::lower_bound(points.begin(), points.end(), playhead,
      [](const ui::ExpressionPoint& point, time::Tick value) { return point.tick < value; });
  if (after == points.begin()) return after->amount;
  if (after == points.end()) return points.back().amount;
  if (after->tick == playhead) return after->amount;
  const auto before = after - 1;
  const auto span = (after->tick - before->tick).value();
  if (span <= 0) return after->amount;
  const auto position = static_cast<double>((playhead - before->tick).value()) /
                        static_cast<double>(span);
  return static_cast<float>(static_cast<double>(before->amount) +
                            static_cast<double>(after->amount - before->amount) * position);
}

time::Tick NativeEditorController::expressionTickAt(double x) const { return laneTickAt(x); }

float NativeEditorController::expressionAmountAt(double y, double automationTop,
                                                double automationHeight) const {
  const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
  const auto centerY = automationTop + automationHeight * layout_.automationCenterFraction;
  const auto span = std::max(std::abs(descriptor.maximum - descriptor.neutral),
                             std::abs(descriptor.neutral - descriptor.minimum));
  const auto scale = automationHeight * (hosted_ ? HostedGeometry::kExpressionVerticalScale : layout_.pitchAutomationVerticalScale) * 0.5;
  if (scale <= 0.0 || span <= 0.0F) return descriptor.neutral;
  const auto normalized = (centerY - y) / scale;
  const auto value = static_cast<double>(descriptor.neutral) + normalized * span;
  return std::clamp(static_cast<float>(value), descriptor.minimum, descriptor.maximum);
}

std::optional<time::Tick> NativeEditorController::expressionPointAt(ui::Point point,
                                                                   double automationTop,
                                                                   double automationHeight) const {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return std::nullopt;
  const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
  const auto points = expressionDraft_ ? expressionDraft_->points()
                                       : ui::readExpressionPoints(*region, expressionChannel_);
  const auto centerY = automationTop + automationHeight * layout_.automationCenterFraction;
  const auto span = std::max(std::abs(descriptor.maximum - descriptor.neutral),
                             std::abs(descriptor.neutral - descriptor.minimum));
  const auto scale = automationHeight * (hosted_ ? HostedGeometry::kExpressionVerticalScale : layout_.pitchAutomationVerticalScale) * 0.5;
  std::optional<time::Tick> result;
  auto bestDistance = std::numeric_limits<double>::max();
  for (const auto& candidate : points) {
    const auto x = laneX(candidate.tick);
    const auto y = centerY - (span <= 0.0F ? 0.0 : (candidate.amount - descriptor.neutral) / span) * scale;
    const auto dx = point.x - x;
    const auto dy = point.y - y;
    const auto distance = dx * dx + dy * dy;
    if (distance > 64.0 || distance >= bestDistance) continue;
    bestDistance = distance;
    result = candidate.tick;
  }
  return result;
}

core::Result<void> NativeEditorController::beginExpressionGesture(
    ui::Point position, double automationTop, double automationHeight,
    const PointerEvent& event) {
  auto draft = ensureExpressionDraft();
  if (!draft) return core::Result<void>{draft.error()};
  expressionGestureSnapshot_ = draft.value()->points();
  const auto tick = expressionTickAt(position.x);
  const auto existing = expressionPointAt(position, automationTop, automationHeight);
  if (existing.has_value()) {
    if (event.modifiers.shift) {
      expressionDragTick_ = existing;
      const auto erased = draft.value()->erase(*existing);
      if (erased) return commitExpressionDraft();
      expressionDragTick_.reset();
      return erased;
    }
    expressionDragTick_ = existing;
    dragMode_ = DragMode::MoveExpressionPoint;
    return core::success();
  }
  const auto amount = expressionAmountAt(position.y, automationTop, automationHeight);
  const auto inserted = draft.value()->upsert(ui::ExpressionPoint{tick, amount});
  if (!inserted) return inserted;
  expressionDragTick_ = tick;
  dragMode_ = DragMode::MoveExpressionPoint;
  return core::success();
}

core::Result<void> NativeEditorController::updateExpressionGesture(
    ui::Point position, double automationTop, double automationHeight) {
  if (!expressionDraft_ || !expressionDragTick_.has_value())
    return core::failure(core::ErrorCode::InvalidState, "No expression point is being moved");
  const auto tick = expressionTickAt(position.x);
  const auto amount = expressionAmountAt(position.y, automationTop, automationHeight);
  if (tick == *expressionDragTick_) {
    const auto replaced = expressionDraft_->upsert(ui::ExpressionPoint{tick, amount});
    if (!replaced) return replaced;
  } else {
    const auto moved = expressionDraft_->move(*expressionDragTick_, ui::ExpressionPoint{tick, amount});
    if (!moved) return moved;
    expressionDragTick_ = tick;
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::endExpressionGesture() {
  dragMode_ = DragMode::None;
  expressionGestureSnapshot_.reset();
  return commitExpressionDraft();
}

core::Result<void> NativeEditorController::nudgeExpressionLane(int steps) {
  if (steps == 0) return core::success();
  auto draft = ensureExpressionDraft();
  if (!draft) return core::Result<void>{draft.error()};
  const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
  // A nudge edits the value at the playhead, which must lie inside the region.
  const auto editTick = regionPlayheadForEdit();
  if (!editTick) return core::Result<void>{editTick.error()};
  const auto playheadInRegion = editTick.value();
  const auto current = draft.value()->valueAt(playheadInRegion);
  const auto target = snappedToNeutral(std::clamp(
      current + descriptor.step * static_cast<float>(steps), descriptor.minimum, descriptor.maximum));
  auto next = draft.value()->points();
  const auto equalToNeutral = [&descriptor](float value) {
    return std::abs(value - descriptor.neutral) < 1.0e-6F;
  };
  const auto existing = std::lower_bound(next.begin(), next.end(), playheadInRegion,
      [](const ui::ExpressionPoint& point, time::Tick value) { return point.tick < value; });
  if (equalToNeutral(target)) {
    if (existing != next.end() && existing->tick == playheadInRegion) next.erase(existing);
    // A neutral point can still shape the ramp to another non-neutral point. Collapse only a curve
    // that is neutral everywhere, so a local nudge cannot erase expression elsewhere in the phrase.
    if (std::all_of(next.begin(), next.end(), [&](const ui::ExpressionPoint& point) {
          return equalToNeutral(point.amount); })) next.clear();
  } else if (existing != next.end() && existing->tick == playheadInRegion) {
    existing->amount = target;
  } else {
    next.insert(existing, ui::ExpressionPoint{playheadInRegion, target});
  }
  const auto replaced = draft.value()->replacePoints(std::move(next));
  if (!replaced) return replaced;
  if (!draft.value()->hasChanges()) { expressionDraft_.reset(); return core::success(); }
  return commitExpressionDraft();
}

core::Result<void> NativeEditorController::resetExpressionLaneDraft() {
  if (!expressionDraft_) return core::success();
  const auto reset = expressionDraft_->reset();
  if (!reset) return reset;
  expressionDraft_.reset();
  repaint();
  return core::success();
}

namespace {

// A song tick as the lane stores it: snapped on the song grid the notes use, then region-local.
time::Tick expressionRegionTick(const domain::Project& project, domain::RegionId regionId,
                                time::Tick songTick) {
  auto absolute = std::max(songTick, time::Tick{0});
  if (project.settings().snapEnabled)
    absolute = time::Quantizer(project.settings().snapGrid).snap(absolute);
  const auto* region = project.findRegion(regionId);
  if (region == nullptr) return absolute;
  return std::clamp(absolute - region->startTick, time::Tick{0}, region->durationTick);
}

}  // namespace

core::Result<void> NativeEditorController::pressExpressionPoint(std::optional<time::Tick> grab,
                                                               time::Tick songTick, float amount,
                                                               bool erase) {
  if (pointerGestureActive())
    return core::failure(core::ErrorCode::Conflict, "Finish the active gesture first");
  auto draft = ensureExpressionDraft();
  if (!draft) return core::Result<void>{draft.error()};
  const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
  const auto clamped = std::isfinite(amount)
                           ? std::clamp(amount, descriptor.minimum, descriptor.maximum)
                           : descriptor.neutral;
  if (grab.has_value()) {
    const auto& points = draft.value()->points();
    if (std::none_of(points.begin(), points.end(),
                     [&grab](const ui::ExpressionPoint& point) { return point.tick == *grab; }))
      return core::failure(core::ErrorCode::NotFound, "That expression point is no longer stored");
    if (erase) {
      const auto erased = draft.value()->erase(*grab);
      if (!erased) return erased;
      return commitExpressionDraft();
    }
    expressionGestureSnapshot_ = points;
    expressionDragTick_ = grab;
    dragMode_ = DragMode::MoveExpressionPoint;
    repaint();
    return core::success();
  }
  expressionGestureSnapshot_ = draft.value()->points();
  const auto tick = expressionRegionTick(session_.project(), regionId_, songTick);
  const auto inserted = draft.value()->upsert(ui::ExpressionPoint{tick, clamped});
  if (!inserted) {
    expressionGestureSnapshot_.reset();
    return inserted;
  }
  expressionDragTick_ = tick;
  dragMode_ = DragMode::MoveExpressionPoint;
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::dragExpressionPoint(time::Tick songTick, float amount) {
  if (dragMode_ != DragMode::MoveExpressionPoint || !expressionDraft_ ||
      !expressionDragTick_.has_value())
    return core::failure(core::ErrorCode::InvalidState, "No expression point is being moved");
  const auto descriptor = ui::describeExpressionChannel(expressionChannel_);
  const auto clamped = std::isfinite(amount)
                           ? std::clamp(amount, descriptor.minimum, descriptor.maximum)
                           : descriptor.neutral;
  const auto tick = expressionRegionTick(session_.project(), regionId_, songTick);
  if (tick == *expressionDragTick_) {
    const auto replaced = expressionDraft_->upsert(ui::ExpressionPoint{tick, clamped});
    if (!replaced) return replaced;
  } else {
    const auto moved = expressionDraft_->move(*expressionDragTick_, ui::ExpressionPoint{tick, clamped});
    if (!moved) return moved;
    expressionDragTick_ = tick;
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::releaseExpressionPoint() {
  if (dragMode_ != DragMode::MoveExpressionPoint) return core::success();
  return endExpressionGesture();
}

namespace {

// A region-local pitch tick as the lane stores it: snapped on the song grid the notes use.
time::Tick pitchRegionTick(const domain::Project& project, const domain::VocalRegion& region,
                           time::Tick regionTick) {
  auto absolute = region.startTick + std::clamp(regionTick, time::Tick{0}, region.durationTick);
  if (project.settings().snapEnabled)
    absolute = time::Quantizer(project.settings().snapGrid).snap(absolute);
  return std::clamp(absolute - region.startTick, time::Tick{0}, region.durationTick);
}

constexpr float kPitchCentsLimit = 4800.0F;

std::optional<domain::PitchAutomationPoint> storedPitchPoint(const domain::VocalRegion& region,
                                                             time::Tick tick) {
  const auto& points = region.pitchAutomation.points();
  const auto found = std::find_if(points.begin(), points.end(),
                                  [tick](const auto& point) { return point.tick == tick; });
  if (found == points.end()) return std::nullopt;
  return *found;
}

core::Result<void> missingPitchPoint() {
  return core::failure(core::ErrorCode::NotFound, "That pitch point is no longer stored");
}

}  // namespace

std::string NativeEditorController::pitchEditRefusal() const {
  if (!callbacks_.upsertPitchPoint || !callbacks_.movePitchPoint)
    return "This host cannot edit pitch points";
  return {};
}

core::Result<void> NativeEditorController::pressPitchPoint(std::optional<time::Tick> grab,
                                                          time::Tick regionTick, float cents) {
  if (pointerGestureActive())
    return core::failure(core::ErrorCode::Conflict, "Finish the active gesture first");
  if (!std::isfinite(cents))
    return core::failure(core::ErrorCode::InvalidArgument, "Pitch automation cents must be finite");
  if (grab.has_value() ? !callbacks_.movePitchPoint : !callbacks_.upsertPitchPoint)
    return core::failure(core::ErrorCode::Unsupported,
                         grab.has_value() ? "This host cannot move pitch points"
                                          : "This host cannot add pitch points");
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Pitch automation region is missing");
  PitchPointGesture gesture;
  if (grab.has_value()) {
    const auto stored = storedPitchPoint(*region, *grab);
    if (!stored) return missingPitchPoint();
    gesture.source = grab;
    gesture.point = *stored;
  } else {
    gesture.point = domain::PitchAutomationPoint{
        .tick = pitchRegionTick(session_.project(), *region, regionTick),
        .cents = std::clamp(cents, -kPitchCentsLimit, kPitchCentsLimit),
        .interpolation = domain::CurveInterpolation::Linear,
    };
  }
  pitchGesture_ = gesture;
  pitchGestureRevision_ = session_.revision();
  pitchGestureRegion_ = regionId_;
  dragMode_ = DragMode::EditPitchPoint;
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::dragPitchPoint(time::Tick regionTick, float cents) {
  if (dragMode_ != DragMode::EditPitchPoint || !pitchGesture_)
    return core::failure(core::ErrorCode::InvalidState, "No pitch point is being moved");
  const auto* region = session_.project().findRegion(pitchGestureRegion_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Pitch automation region is missing");
  if (std::isfinite(cents))
    pitchGesture_->point.cents = std::clamp(cents, -kPitchCentsLimit, kPitchCentsLimit);
  pitchGesture_->point.tick = pitchRegionTick(session_.project(), *region, regionTick);
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::releasePitchPoint() {
  if (dragMode_ != DragMode::EditPitchPoint) return core::success();
  const auto gesture = pitchGesture_;
  pitchGesture_.reset();
  dragMode_ = DragMode::None;
  repaint();
  if (!gesture) return core::success();
  // The release commits only against the document and region the press began on.
  if (session_.revision() != pitchGestureRevision_ || regionId_ != pitchGestureRegion_)
    return core::failure(core::ErrorCode::Conflict, "The pitch curve changed during the gesture");
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound, "Pitch automation region is missing");
  core::Result<void> result = core::success();
  if (gesture->source.has_value()) {
    const auto stored = storedPitchPoint(*region, *gesture->source);
    if (!stored) return missingPitchPoint();
    if (*stored == gesture->point) return core::success();
    if (!callbacks_.movePitchPoint)
      return core::failure(core::ErrorCode::Unsupported, "This host cannot move pitch points");
    result = callbacks_.movePitchPoint(*gesture->source, gesture->point);
  } else {
    if (!callbacks_.upsertPitchPoint)
      return core::failure(core::ErrorCode::Unsupported, "This host cannot add pitch points");
    result = callbacks_.upsertPitchPoint(gesture->point);
  }
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::removePitchPointAt(time::Tick regionTick) {
  if (pointerGestureActive())
    return core::failure(core::ErrorCode::Conflict, "Finish the active gesture first");
  if (!callbacks_.removePitchPoint)
    return core::failure(core::ErrorCode::Unsupported, "This host cannot remove pitch points");
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || !storedPitchPoint(*region, regionTick)) return missingPitchPoint();
  const auto result = callbacks_.removePitchPoint(regionTick);
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::cyclePitchInterpolationAt(time::Tick regionTick) {
  if (pointerGestureActive())
    return core::failure(core::ErrorCode::Conflict, "Finish the active gesture first");
  if (!callbacks_.cyclePitchInterpolation)
    return core::failure(core::ErrorCode::Unsupported,
                         "This host cannot change pitch interpolation");
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || !storedPitchPoint(*region, regionTick)) return missingPitchPoint();
  const auto result = callbacks_.cyclePitchInterpolation(regionTick);
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::nudgePitchPointAt(time::Tick regionTick, float cents) {
  if (pointerGestureActive())
    return core::failure(core::ErrorCode::Conflict, "Finish the active gesture first");
  if (!std::isfinite(cents))
    return core::failure(core::ErrorCode::InvalidArgument, "Pitch automation cents must be finite");
  if (!callbacks_.upsertPitchPoint)
    return core::failure(core::ErrorCode::Unsupported, "This host cannot edit pitch points");
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return missingPitchPoint();
  const auto stored = storedPitchPoint(*region, regionTick);
  if (!stored) return missingPitchPoint();
  auto next = *stored;
  next.cents = std::clamp(stored->cents + cents, -kPitchCentsLimit, kPitchCentsLimit);
  if (next.cents == stored->cents) return core::success();
  const auto result = callbacks_.upsertPitchPoint(next);
  if (result) markDocumentChanged();
  repaint();
  return result;
}

core::Result<void> NativeEditorController::applyVibratoToSelection(const ui::VibratoFields& patch) {
  auto model = ui::VibratoModel::prepare(session_, regionId_, patch);
  if (!model) return core::Result<void>{model.error()};
  const auto applied = model.value().apply(session_, regionId_);
  if (applied) markDocumentChanged();
  repaint();
  return applied;
}

}  // namespace seam::native_ui
