#include "seam/native_ui/editor_controller.hpp"

#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"

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
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

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

std::optional<std::pair<domain::TechnicalLane, std::size_t>> technicalLaneForId(
    std::string_view id) noexcept {
  if (id == "lane.phoneme") return std::pair{domain::TechnicalLane::Phoneme, 0U};
  if (id == "lane.unit") return std::pair{domain::TechnicalLane::Unit, 1U};
  if (id == "lane.seam") return std::pair{domain::TechnicalLane::Seam, 2U};
  if (id == "lane.pitch") return std::pair{domain::TechnicalLane::Pitch, 3U};
  return std::nullopt;
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
  };
  state.selectedNoteCount = session_.selection().noteIds().size();
  state.hoveredNote = interaction_.hoveredNote();
  state.focusedNote = interaction_.focusedNote();
  state.detail = interaction_.detail();
  state.overlapDetail = overlapDetail_;
  state.arrangementTracks = arrangementPanel_.tracks();
  state.inspector = TrackInspectorModel::snapshot(session_.project(),
                                                  selectedTrackId_);
  state.vibratoEditable = state.inspector.vocal && session_.project().findRegion(regionId_) &&
      !session_.selection().empty() && session_.selection().noteIds().size() <= 10000U;
  const auto* dynamicsRegion = session_.project().findRegion(regionId_);
  state.dynamicsEditable = state.inspector.vocal && dynamicsRegion && dynamicsRegion->notes.size() <= 10000U;
  state.styleEditable = state.dynamicsEditable && (styleBankResolver_ || styleSnapshotResolver_);
  state.voicebankBrowserVisible = voicebankBrowserVisible_;
  state.voicebankCards = voicebankCards_;
  state.audioSettings = audioSettings_;
  state.recoverySupport = recoverySupportPanel_.view();
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
    // Character artwork follows the verified voice/render state instead of
    // remaining permanently neutral/focused.  Native hosts select the actual
    // state asset from this semantic value after the scene is built.
    switch (state.voiceIdentity.state) {
      case VoiceIdentityState::Missing:
        state.characterState = character::State::Warning;
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
      state.boundedInputLabel = "PHONE HINT (EMPTY = CLEAR)";
      if (replacementInput_) state.boundedInputLabel = replacementInputError_.empty() ?
          (replacementInput_->diagnostics ? "FIND ACTIVE DIAGNOSTICS (LITERAL)" : replacementInput_->findOnly ? "FIND NOTES (LITERAL; CHOOSE FIELD IN RESULTS)" :
           replacementInput_->query ? "REPLACE WITH (EMPTY ALLOWED)" : "FIND LYRIC (LITERAL)") : replacementInputError_;
      if (replacementInput_ && replacementInput_->vibratoField) state.boundedInputLabel = "VIBRATO: " + std::string{VibratoInspectorDraft::label(*replacementInput_->vibratoField)};
      if (replacementInput_ && replacementInput_->dynamicsTickField) state.boundedInputLabel = *replacementInput_->dynamicsTickField ? "DYNAMICS: REGION TICK" : "DYNAMICS: LINEAR GAIN (UNITY = 1)";
      state.lyricEditor = layout_.hintTextBounds(logicalWidth_, logicalHeight_);
    }
  }
  if (const auto* region = session_.project().findRegion(regionId_); region != nullptr) {
    state.phonemes = phonemizer::inspectPronunciation(*region);
    state.unitOverrides = region->unitSelectionOverrides;
    state.seamOverrides = region->seamOverrides;
    state.pitchAutomation = region->pitchAutomation.points();
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
  if (const auto* focused = accessibilityTree_.focusedNode(); focused != nullptr) {
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

core::Result<void> NativeEditorController::openVibratoInspector() {
  if (composition_.active() || (replacementOpen_ && !vibratoDraft_) || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before editing vibrato");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
  auto draft = VibratoInspectorDraft::prepare(session_, regionId_); if (!draft) return core::Result<void>{draft.error()};
  replacementJob_.cancel(); findJob_.cancel(); diagnosticFindJob_.cancel();
  findMode_ = false; diagnosticFindMode_ = false; findNavigation_.reset(); diagnosticFindReview_.reset();
  clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.reset();
  vibratoDraft_.emplace(std::move(draft.value())); selectedVibratoField_ = VibratoField::Enabled;
  dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  replacementDependencies_ = false; replacementDistribution_ = false;
  replacementOpen_ = true; replacementPage_ = 0U; replacementRegion_ = regionId_; replacementDetail_.reset();
  replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_; repaint(); return core::success();
}

core::Result<void> NativeEditorController::beginVibratoFieldInput(VibratoField field) {
  if (!vibratoDraft_ || !vibratoDraft_->current(session_, regionId_) || composition_.active() || !callbacks_.beginTextInput)
    return core::failure(core::ErrorCode::Conflict, "Vibrato field input is unavailable or stale");
  if (replacementInputSerial_ == std::numeric_limits<std::uint64_t>::max() || replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Inspector input identity exhausted");
  auto textValue = domain::fromUtf8(vibratoDraft_->mixed(field) ? "" : vibratoDraft_->text(field)); if (!textValue) return core::Result<void>{textValue.error()};
  auto context = session_.capturePerformanceJob(); if (!context) return core::Result<void>{context.error()};
  const auto begun = composition_.begin(externalTextTarget(), textValue.value()); if (!begun) return begun;
  replacementInput_.emplace(ReplacementInput{std::move(context.value()), regionId_, session_.revision(), std::nullopt, false, false, field});
  selectedVibratoField_ = field; replacementOpen_ = false; ++replacementInputSerial_; ++replacementInteraction_;
  replacementInputError_.clear();
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), textValue.value()});
  repaint(); return core::success();
}
bool NativeEditorController::styleSourceCurrent() const {
  if (!styleDraft_) return false;
  if (styleSnapshotResolver_) return styleDraft_->matches(session_, selectedTrackId_, regionId_, styleSnapshotResolver_(selectedTrackId_));
  return styleBankResolver_ && styleDraft_->matches(session_, selectedTrackId_, regionId_, styleBankResolver_(selectedTrackId_));
}
core::Result<void> NativeEditorController::openStyleCoverageSheet() {
  if (composition_.active() || (replacementOpen_ && !styleDraft_) || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before choosing a style");
  if (!styleBankResolver_ && !styleSnapshotResolver_) return core::failure(core::ErrorCode::Unsupported, "Style bank resolver is not connected");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
  auto draft = styleSnapshotResolver_ ? StyleCoverageSheet::prepare(session_, selectedTrackId_, regionId_, styleSnapshotResolver_(selectedTrackId_)) :
      StyleCoverageSheet::prepare(session_, selectedTrackId_, regionId_, styleBankResolver_(selectedTrackId_));
  if (!draft) return core::Result<void>{draft.error()};
  replacementJob_.cancel(); findJob_.cancel(); diagnosticFindJob_.cancel(); measurementJob_.cancel();
  findMode_ = false; diagnosticFindMode_ = false; findNavigation_.reset(); diagnosticFindReview_.reset();
  clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.reset(); vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  styleDraft_.emplace(std::move(draft.value()));
  styleIssues_ = false; styleIssue_.reset(); styleDetailLines_.clear();
  replacementDependencies_ = false; replacementDistribution_ = false;
  replacementOpen_ = true; replacementPage_ = 0U; replacementRegion_ = regionId_; replacementDetail_.reset();
  replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_; repaint(); return core::success();
}

core::Result<void> NativeEditorController::openJapaneseReadingReview() {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before resolving Japanese reading");
  if (japaneseReadingJob_.preparing()) return core::failure(core::ErrorCode::Conflict, "Wait for the previous Japanese reading worker to retire");
  const auto prepareResource = japaneseReadingResourceResolver_ ? japaneseReadingResourceResolver_ : callbacks_.prepareJapaneseReadingResource;
  if (!prepareResource) return core::failure(core::ErrorCode::Unsupported, "Japanese reading resource is not connected");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Reading interaction identity exhausted");
  const auto* region = session_.project().findRegion(regionId_);
  if (!region || region->notes.empty()) return core::failure(core::ErrorCode::NotFound, "Active region has no notes to read");
  std::vector<domain::NoteId> notes;
  if (session_.selection().empty()) {
    notes.reserve(region->notes.size()); for (const auto& note : region->notes) notes.push_back(note.id);
  } else {
    notes.reserve(session_.selection().noteIds().size());
    for (const auto id : session_.selection().noteIds()) {
      if (!region->findNote(id)) return core::failure(core::ErrorCode::Conflict, "Selected notes must belong to the active region");
      notes.push_back(id);
    }
  }
  auto resource = prepareResource(); if (!resource) return core::Result<void>{resource.error()};
  japaneseReadingIdentity_ = resource.value().resource().identity();
  const auto started = japaneseReadingJob_.start(session_, regionId_, notes, std::move(resource.value()));
  if (!started) { japaneseReadingIdentity_.reset(); return started; }
  replacementJob_.cancel(); findJob_.cancel(); diagnosticFindJob_.cancel(); measurementJob_.cancel();
  findMode_ = false; diagnosticFindMode_ = false; findNavigation_.reset(); diagnosticFindReview_.reset();
  clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.reset(); vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  styleDraft_.reset(); styleIssues_ = false; styleIssue_.reset(); styleDetailLines_.clear();
  japaneseReadingMode_ = true; japaneseReadingDetail_.reset(); japaneseReadingDetailLines_.clear();
  replacementDependencies_ = false; replacementDistribution_ = false; replacementOpen_ = true; replacementPage_ = 0U;
  replacementRegion_ = regionId_; replacementDetail_.reset(); replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_;
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::openDynamicsInspector() {
  if (composition_.active() || (replacementOpen_ && !dynamicsDraft_) || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before editing region dynamics");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
  auto draft = ui::DynamicsLaneModel::prepare(session_, regionId_); if (!draft) return core::Result<void>{draft.error()};
  replacementJob_.cancel(); findJob_.cancel(); diagnosticFindJob_.cancel();
  findMode_ = false; diagnosticFindMode_ = false; findNavigation_.reset(); diagnosticFindReview_.reset();
  clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.reset(); vibratoDraft_.reset();
  dynamicsDraft_.emplace(std::move(draft.value())); dynamicsPointEdit_.reset();
  dynamicsDraft_->refreshTargetPreview();
  dynamicsViewport_.reset();
  measurementJob_.cancel(); measurementWindow_.reset(); measuredChannel_ = 0U;
  dynamicsScrollRemainder_ = 0.0;
  dynamicsGainDragging_ = false;
  replacementDependencies_ = false; replacementDistribution_ = false;
  replacementOpen_ = true; replacementPage_ = 0U; replacementRegion_ = regionId_; replacementDetail_.reset();
  replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_; repaint(); return core::success();
}

core::Result<domain::DynamicsAutomationPoint> NativeEditorController::dynamicsPointValue() const {
  if (!dynamicsPointEdit_) return core::failure<domain::DynamicsAutomationPoint>(core::ErrorCode::Conflict, "No dynamics point is open");
  std::int64_t tick{}; float gain{};
  const auto parse = [](const std::string& textValue, auto& value) {
    if (textValue.empty() || textValue.size() > 64U) return false;
    const auto result = std::from_chars(textValue.data(), textValue.data() + textValue.size(), value);
    return result.ec == std::errc{} && result.ptr == textValue.data() + textValue.size();
  };
  if (!parse(dynamicsPointEdit_->tickText, tick) || !parse(dynamicsPointEdit_->gainText, gain))
    return core::failure<domain::DynamicsAutomationPoint>(core::ErrorCode::InvalidArgument, "Enter an integer tick and a finite linear gain");
  domain::DynamicsAutomationPoint point{time::Tick{tick}, gain};
  const auto valid = dynamicsDraft_ ? dynamicsDraft_->validatePoint(point) : point.validate();
  if (!valid) return core::Result<domain::DynamicsAutomationPoint>{valid.error()};
  return point;
}

core::Result<void> NativeEditorController::beginDynamicsFieldInput(bool tick) {
  if (!dynamicsDraft_ || !dynamicsPointEdit_ || !dynamicsDraft_->matches(session_, regionId_) || composition_.active() || !callbacks_.beginTextInput)
    return core::failure(core::ErrorCode::Conflict, "Dynamics field input is unavailable or stale");
  if (replacementInputSerial_ == std::numeric_limits<std::uint64_t>::max() || replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Inspector input identity exhausted");
  auto value = domain::fromUtf8(tick ? dynamicsPointEdit_->tickText : dynamicsPointEdit_->gainText);
  if (!value) return core::Result<void>{value.error()};
  auto context = session_.capturePerformanceJob(); if (!context) return core::Result<void>{context.error()};
  const auto begun = composition_.begin(externalTextTarget(), value.value()); if (!begun) return begun;
  replacementInput_.emplace(ReplacementInput{std::move(context.value()), regionId_, session_.revision(), std::nullopt, false, false, {}, tick});
  replacementOpen_ = false; ++replacementInputSerial_; ++replacementInteraction_; replacementInputError_.clear();
  dynamicsGainDragging_ = false;
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), value.value()});
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::beginSearchInput(bool findOnly, bool diagnostics) {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before finding lyrics");
  if (!callbacks_.beginTextInput) return core::failure(core::ErrorCode::Unsupported, "Native replacement input is not connected");
  if (replacementInputSerial_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Replacement input identity exhausted");
  if (!diagnostics && !session_.project().findRegion(regionId_)) return core::failure(core::ErrorCode::NotFound, "Select a region before finding lyrics");
  auto context = session_.capturePerformanceJob(); if (!context) return core::Result<void>{context.error()};
  const auto begun = composition_.begin(externalTextTarget(), U""); if (!begun) return begun;
  replacementInput_.emplace(ReplacementInput{std::move(context.value()), regionId_, session_.revision(), std::nullopt, findOnly, diagnostics});
  ++replacementInputSerial_; replacementInputError_.clear();
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), U""});
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::beginFindInput() {
  return beginSearchInput(true, false);
}
core::Result<void> NativeEditorController::beginDiagnosticFindInput() {
  return beginSearchInput(true, true);
}

core::Result<void> NativeEditorController::openDiagnosticFindReview(std::string query) {
  if (composition_.active() || (replacementOpen_ && !findMode_) || timeMapPanel_ || phonemeReview_ ||
      sampleMicroscopeOpen() || voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before finding diagnostics");
  if (findPreparing()) return core::failure(core::ErrorCode::Conflict, "Wait for the previous Find worker to retire");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Find interaction identity exhausted");
  const auto started = diagnosticFindJob_.start(session_, diagnosticPanel_, query);
  replacementJob_.cancel(); findJob_.cancel(); clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.reset();
  findMode_ = true; diagnosticFindMode_ = true; diagnosticFindReview_.reset(); findNavigation_.reset(); findDetailIndex_.reset();
  vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  replacementQuery_ = std::move(query); replacementRegion_ = regionId_; replacementPage_ = 0U;
  replacementDetail_.reset(); replacementDependencies_ = false; replacementDistribution_ = false;
  replacementOpen_ = true; ++replacementInteraction_; replacementError_.clear(); replacementErrorContext_.clear();
  if (!started) { replacementError_ = started.error().message; replacementErrorContext_ = started.error().context; }
  repaint(); return started;
}

core::Result<void> NativeEditorController::openFindReview(std::string query, ui::NoteSearchField field) {
  if (composition_.active() || (replacementOpen_ && !findMode_) || timeMapPanel_ || phonemeReview_ ||
      sampleMicroscopeOpen() || voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before finding notes");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Find interaction identity exhausted");
  if (findPreparing()) return core::failure(core::ErrorCode::Conflict, "Wait for the previous Find worker to retire");
  const auto prepared = findJob_.start(session_, regionId_, query, field);
  replacementJob_.cancel(); clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.reset();
  findMode_ = true; findField_ = field; findNavigation_.reset(); findDetailIndex_.reset();
  vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  diagnosticFindMode_ = false; diagnosticFindJob_.cancel(); diagnosticFindReview_.reset();
  replacementQuery_ = std::move(query); replacementRegion_ = regionId_; replacementPage_ = 0U;
  replacementDetail_.reset(); replacementDependencies_ = false; replacementDistribution_ = false;
  replacementOpen_ = true; ++replacementInteraction_;
  replacementError_.clear(); replacementErrorContext_.clear();
  if (!prepared) { replacementError_ = prepared.error().message; replacementErrorContext_ = prepared.error().context; }
  repaint(); return prepared ? core::success() : core::Result<void>{prepared.error()};
}

core::Result<void> NativeEditorController::repeatFind(bool backwards) {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before repeating Find");
  if (!findNavigation_)
    return core::failure(core::ErrorCode::NotFound, "Use Find Notes and select a result first");
  const auto selected = findNavigation_->step(session_, regionId_, backwards);
  if (!selected) return core::Result<void>{selected.error()};
  return revealFindNote(selected.value());
}

core::Result<void> NativeEditorController::revealFindNote(domain::NoteId noteId) {
  const auto* region = session_.project().findRegion(regionId_);
  const auto* note = region ? region->findNote(noteId) : nullptr;
  if (!note) return core::failure(core::ErrorCode::NotFound, "Find note is no longer available");
  pianoRoll_.timeline().setOriginTick(note->startTick);
  pianoRoll_.pitch().setTopMidiKey(static_cast<std::int32_t>(note->midiKey) + 2);
  rebuildAccessibilityTree();
  const auto focused = accessibilityTree_.setFocus("note." + noteId.toString());
  repaint(); return focused;
}

core::Result<void> NativeEditorController::openClearDynamicsReview() {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before clearing the region dynamics curve");
  return refreshClearDynamicsReview();
}
core::Result<void> NativeEditorController::refreshClearDynamicsReview() {
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Review interaction identity exhausted");
  auto preview = ui::DynamicsClearPreview::prepare(session_, regionId_);
  if (!preview) { replacementError_ = preview.error().message; repaint(); return core::Result<void>{preview.error()}; }
  findJob_.cancel(); findMode_ = false; findNavigation_.reset();
  diagnosticFindJob_.cancel(); diagnosticFindMode_ = false; diagnosticFindReview_.reset();
  replacementJob_.cancel(); clearVibrato_.reset(); noteCleanup_.reset(); clearDynamics_.emplace(std::move(preview.value()));
  vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  replacementOpen_ = true; replacementPage_ = 0U; replacementRegion_ = regionId_;
  replacementDetail_.reset(); replacementDependencies_ = false; replacementDistribution_ = false;
  replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_;
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::openNoteCleanupReview(ui::NoteCleanupKind kind) {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before note cleanup");
  return refreshNoteCleanupReview(kind);
}
core::Result<void> NativeEditorController::refreshNoteCleanupReview(ui::NoteCleanupKind kind) {
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Review interaction identity exhausted");
  auto preview = ui::NoteCleanupPreview::prepare(session_, regionId_, kind);
  if (!preview) { replacementError_ = preview.error().message; repaint(); return core::Result<void>{preview.error()}; }
  findJob_.cancel(); findMode_ = false; findNavigation_.reset();
  diagnosticFindJob_.cancel(); diagnosticFindMode_ = false; diagnosticFindReview_.reset();
  replacementJob_.cancel(); clearVibrato_.reset(); clearDynamics_.reset(); noteCleanup_.emplace(std::move(preview.value()));
  vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  replacementOpen_ = true; replacementPage_ = 0U; replacementRegion_ = regionId_;
  replacementDetail_.reset(); replacementDependencies_ = false; replacementDistribution_ = false;
  replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_;
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::openClearVibratoReview() {
  if (composition_.active() || replacementOpen_ || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before clearing vibrato");
  return refreshClearVibratoReview();
}

core::Result<void> NativeEditorController::refreshClearVibratoReview() {
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Review interaction identity exhausted");
  auto preview = ui::VibratoClearPreview::prepare(session_, regionId_);
  if (!preview) { replacementError_ = preview.error().message; repaint(); return core::Result<void>{preview.error()}; }
  findJob_.cancel(); findMode_ = false; findNavigation_.reset();
  diagnosticFindJob_.cancel(); diagnosticFindMode_ = false; diagnosticFindReview_.reset();
  replacementJob_.cancel(); noteCleanup_.reset(); clearDynamics_.reset(); clearVibrato_.emplace(std::move(preview.value()));
  vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  replacementOpen_ = true; replacementPage_ = 0U; replacementRegion_ = regionId_;
  replacementDetail_.reset(); replacementDependencies_ = false; replacementDistribution_ = false;
  replacementError_.clear(); replacementErrorContext_.clear(); ++replacementInteraction_;
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::openReplacementReview(std::string query, std::string replacement) {
  return openLyricReview(std::move(query), std::move(replacement), false);
}
core::Result<void> NativeEditorController::openDistributionReview(std::string text) {
  return openLyricReview({}, std::move(text), true);
}
core::Result<void> NativeEditorController::openLyricReview(std::string query, std::string replacement, bool distribution) {
  if (composition_.active() || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before reviewing replacement");
  if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Replacement interaction identity exhausted");
  const auto started = replacementJob_.start(session_, regionId_, query, replacement,
      distribution ? std::optional{session_.selection().noteIds()} : std::nullopt);
  if (!started) return started;
  vibratoDraft_.reset(); dynamicsDraft_.reset(); dynamicsPointEdit_.reset();
  findJob_.cancel(); findMode_ = false; findNavigation_.reset();
  diagnosticFindJob_.cancel(); diagnosticFindMode_ = false; diagnosticFindReview_.reset();
  clearVibrato_.reset();
  clearDynamics_.reset();
  noteCleanup_.reset();
  replacementQuery_ = std::move(query); replacementText_ = std::move(replacement);
  replacementRegion_ = regionId_; replacementPage_ = 0U; replacementDependencies_ = false;
  replacementDistribution_ = distribution;
  replacementDetail_.reset();
  replacementError_.clear(); replacementErrorContext_.clear(); replacementOpen_ = true; ++replacementInteraction_;
  repaint(); return core::success();
}

void NativeEditorController::pollReplacementReview() {
  pollAudioMeasurement();
  if (japaneseReadingJob_.preparing()) {
    if (!japaneseReadingMode_ || !japaneseReadingIdentity_) {
      japaneseReadingJob_.cancel();
      static_cast<void>(japaneseReadingJob_.pollCancelled());
    } else {
      const auto polled = japaneseReadingJob_.poll(session_, regionId_, *japaneseReadingIdentity_);
      if (!polled) { replacementError_ = polled.error().message; replacementErrorContext_ = polled.error().context; }
      else if (polled.value() && japaneseReadingJob_.state() == authoring::JapaneseReadingJob::State::Ready) {
        replacementError_.clear(); replacementErrorContext_.clear(); japaneseReadingDetail_.reset(); japaneseReadingDetailLines_.clear();
      }
    }
    repaint();
  }
  if (diagnosticFindJob_.preparing()) {
    const auto polled = diagnosticFindJob_.poll(session_, diagnosticPanel_);
    if (findMode_ && diagnosticFindMode_ && replacementOpen_) {
      if (!polled) { replacementError_ = polled.error().message; replacementErrorContext_ = polled.error().context; }
      else if (polled.value()) diagnosticFindReview_ = diagnosticFindJob_.takeReady();
    } else diagnosticFindJob_.cancel();
    repaint();
  }
  if (findJob_.preparing()) {
    const auto polledFind = findJob_.poll(session_, regionId_);
    if (findMode_ && !diagnosticFindMode_ && replacementOpen_) {
      if (!polledFind) { replacementError_ = polledFind.error().message; replacementErrorContext_ = polledFind.error().context; }
      else if (polledFind.value()) findNavigation_ = findJob_.takeReady();
    } else { findJob_.cancel(); }
    repaint();
  }
  if (replacementJob_.state() != ui::LyricReplacementJob::State::Preparing) return;
  const auto polled = replacementJob_.poll(session_, regionId_);
  if (!polled && !findMode_) { replacementError_ = polled.error().message; replacementErrorContext_ = polled.error().context; }
  // Both native hosts schedule the next frame through this callback. Keep
  // polling until a cancelled or completed worker has actually retired.
  repaint();
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
    view.rowsInspectable = current;
    view.labels[2] = "Draft only"; view.labels[3] = "Apply track style"; view.labels[4] = "Cancel draft"; view.labels[5] = "Refresh";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < styles.size();
    view.enabled[3] = current && styleDraft_->hasChanges() && !styleDraft_->selection().styleId.empty(); view.enabled[5] = true;
    view.labels[2] = "Coverage details"; view.enabled[2] = true;
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
        pageRow >= 6U || replacementPage_ * 6U + pageRow >= styleDraft_->styles().size())
      return core::failure(core::ErrorCode::Conflict, "Style row is stale or unavailable");
    if (replacementInteraction_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Inspector interaction identity exhausted");
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
      if (styleIssue_) { replacementPage_ = *styleIssue_ / 6U; styleIssue_.reset(); styleDetailLines_.clear(); }
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

void NativeEditorController::rebuildAccessibilityTree() {
  if (replacementOpen_) {
    const auto prefix = replacementSemanticPrefix(); const auto view = replacementReviewView();
    SemanticNode root; root.id = prefix + "panel"; root.role = SemanticRole::Panel;
    root.name = clearVibrato_ ? "Clear selected vibrato review" : (replacementDistribution_ ? "Lyric distribution review" : "Lyric replacement review"); root.value = view.status + " " + view.summary;
    if (noteCleanup_) root.name = "Note cleanup review";
    if (clearDynamics_) root.name = "Clear entire region dynamics curve review";
    if (findMode_) root.name = "Find notes and inspect complete matching text";
    if (findMode_ && diagnosticFindMode_) root.name = "Find active diagnostics; read-only inspection";
    if (vibratoDraft_) root.name = "Vibrato inspector; Apply to Selection";
    if (styleDraft_) root.name = "Style and structural coverage; Apply track style";
    if (japaneseReadingMode_) root.name = "Japanese contextual reading; Apply reading";
    if (dynamicsDraft_) root.name = "Region dynamics inspector; draft only until Apply region curve";
    root.bounds = layout_.reviewPanelBounds(logicalWidth_, logicalHeight_, view.dockedInspector);
    for (std::size_t i = 0U; i < view.rows.size(); ++i) {
      SemanticNode row; row.id = prefix + "row." + std::to_string(i); row.role = SemanticRole::Status;
      if (view.rowsInspectable) row.role = SemanticRole::Button;
      row.name = "Review row " + std::to_string(replacementPage_ * (dynamicsDraft_ ? 2U : 6U) + i + 1U); row.value = view.rows[i];
      row.bounds = layout_.reviewRowBounds(logicalWidth_, logicalHeight_, i, view.dockedInspector); row.actions = {SemanticAction::SetFocus};
      if (view.rowsInspectable) { row.actions.push_back(SemanticAction::Activate); row.description = "Open complete before and after text";
        if (findMode_) { row.actions.push_back(SemanticAction::SetFocus); row.description = "Inspect complete matching text before selecting this note"; }
        if (findMode_ && diagnosticFindMode_) row.description = "Inspect this diagnostic without selecting notes or running recovery";
        if (vibratoDraft_) row.description = "Edit this vibrato draft field; does not apply to notes";
        if (styleDraft_) row.description = styleIssues_ ? "Inspect the complete coverage issue without selecting notes or editing the score" :
            "Choose a draft track style; only Apply track style changes the score. Coverage is structural, not audio qualification.";
        if (dynamicsDraft_) row.description = dynamicsPointEdit_ ? "Edit dynamics point field; score remains unchanged" : "Inspect or edit this region dynamics point";
        if (japaneseReadingMode_) row.description = "Inspect the contextual dictionary reading and its note ownership; Apply is enabled only for complete single-note readings without explicit hints.";
      }
      root.children.push_back(std::move(row));
    }
    for (std::size_t i = 0U; i < view.enabled.size(); ++i) {
      SemanticNode button; button.id = prefix + "action." + std::to_string(i); button.role = SemanticRole::Button;
      button.name = view.labels[i]; button.enabled = view.enabled[i];
      button.bounds = layout_.reviewButtonBounds(logicalWidth_, logicalHeight_, i, view.dockedInspector);
      if (button.enabled) button.actions = {SemanticAction::SetFocus, SemanticAction::Activate};
      root.children.push_back(std::move(button));
    }
    SemanticNode status; status.id = prefix + "status"; status.role = SemanticRole::Status;
    if (view.dynamicsPlot) {
      const auto& plot = *view.dynamicsPlot;
      const std::array<const char*, 4U> names{"Zoom in dynamics", "Zoom out dynamics", "Fit region dynamics", "Measured output: next channel, then return to controls"};
      for (std::size_t i = 0U; i < 4U; ++i) {
        SemanticNode nav; nav.id = prefix + "zoom." + std::to_string(i); nav.role = SemanticRole::Button;
        nav.name = names[i]; nav.bounds = plot.navigation[i]; nav.enabled = plot.editable && (i != 3U || plot.measurementAvailable);
        if (nav.enabled) nav.actions = {SemanticAction::SetFocus, SemanticAction::Activate};
        root.children.push_back(std::move(nav));
      }
      SemanticNode chart; chart.id = prefix + "curve"; chart.role = SemanticRole::Status;
      chart.name = "Region dynamics: applied score, staged curve and unsaved point";
      chart.value = "Visible region ticks " + std::to_string(plot.startTick) + " to " + std::to_string(plot.endTick) + "; linear gain zero to 3.9810717, unity one. Applied score gray, draft pink, unsaved point yellow. Unselected proposals and measured curves are not shown.";
      chart.description = "Visible ticks " + std::to_string(plot.startTick) + " to " + std::to_string(plot.endTick) +
          ". Plus/minus zoom, left/right pan, R fits the region. Wheel pans; Command or Control plus wheel zooms at the pointer. " + plot.influenceDescription + " " + plot.targetStatus;
      if (plot.measuredMode) {
        chart.name = "Read-only measured project-render output"; chart.value = plot.measurementLabel + "; " + view.summary;
        chart.description = "Per-output-channel RMS dBFS from committed rendered PCM, not the unsaved point or staged curve. Silence and values below -96 dBFS are displayed at the floor. FS counts samples at or above full scale, not proof of audible clipping. This is not LUFS, microphone input or a single singer's isolated level.";
      }
      chart.bounds = plot.bounds; chart.actions = {SemanticAction::SetFocus}; root.children.push_back(std::move(chart));
      for (const auto& handle : plot.handles) {
        SemanticNode point; point.id = prefix + "point." + std::to_string(handle.pageRow); point.role = SemanticRole::Button;
        point.name = view.rows[handle.pageRow]; point.description = "Drag vertically for gain; Shift-drag horizontally for time. Axis locks at gesture start. Activate for exact tick and gain fields. Apply region curve is required to change the score.";
        point.bounds = {handle.position.x - 4.0, handle.position.y - 4.0, 8.0, 8.0}; point.enabled = plot.editable;
        if (point.enabled) point.actions = {SemanticAction::SetFocus, SemanticAction::Activate};
        root.children.push_back(std::move(point));
      }
    }
    status.name = "Review status and counts"; status.value = view.status + " / " + view.summary;
    status.bounds = {root.bounds.x + 12.0, root.bounds.y + 8.0, root.bounds.width - 24.0, 38.0};
    status.actions = {SemanticAction::SetFocus}; root.children.push_back(std::move(status));
    const auto* old = accessibilityTree_.focusedNode();
    const auto focus = old && EditorSemanticTree::containsId(root, old->id) ? old->id : prefix + "action.4";
    accessibilityTree_.rebuildCustom(std::move(root), focus); return;
  }
  if ((hintEdit_ || replacementInput_) && composition_.active()) {
    const auto prefix = hintSemanticPrefix();
    SemanticNode root; root.id = prefix + "panel"; root.role = SemanticRole::Panel;
    root.name = "Japanese pronunciation hint";
    if (replacementInput_) root.name = replacementInput_->diagnostics ? "Find active diagnostics" : replacementInput_->findOnly ? "Find notes" : "Find and replace lyrics";
    if (replacementInput_ && replacementInput_->vibratoField) root.name = "Edit vibrato draft field";
    if (replacementInput_ && replacementInput_->dynamicsTickField) root.name = "Edit region dynamics point field";
    root.bounds = layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, false);
    SemanticNode input; input.id = prefix + "input"; input.role = SemanticRole::TextField;
    input.name = "Japanese phone symbols; empty clears hint";
    if (replacementInput_) input.name = replacementInput_->diagnostics ? "Nonempty literal diagnostic search query" : replacementInput_->findOnly ? "Nonempty literal note search query" :
        replacementInput_->query ? "Replacement text; empty is allowed" : "Nonempty literal lyric query";
    input.description = replacementInput_ ? replacementInputError_ : std::string{};
    if (replacementInput_ && replacementInput_->vibratoField) input.name = std::string{VibratoInspectorDraft::label(*replacementInput_->vibratoField)};
    if (replacementInput_ && replacementInput_->dynamicsTickField) input.name = *replacementInput_->dynamicsTickField ? "Nonnegative region tick" : "Linear gain: zero to 3.9810717, unity is one";
    input.value = domain::toUtf8(composition_.compositionText()); input.editableValue = input.value;
    input.bounds = layout_.hintTextBounds(logicalWidth_, logicalHeight_);
    input.actions = {SemanticAction::SetFocus, SemanticAction::EditText};
    root.children.push_back(std::move(input));
    SemanticNode cancel; cancel.id = prefix + "cancel"; cancel.role = SemanticRole::Button;
    cancel.name = "Cancel pronunciation hint";
    if (replacementInput_) cancel.name = replacementInput_->findOnly ? "Cancel Find" : "Cancel find and replace";
    if (replacementInput_ && replacementInput_->vibratoField) cancel.name = "Cancel vibrato field edit";
    if (replacementInput_ && replacementInput_->dynamicsTickField) cancel.name = "Cancel dynamics field edit";
    cancel.bounds = layout_.hintCancelBounds(logicalWidth_, logicalHeight_);
    cancel.actions = {SemanticAction::SetFocus, SemanticAction::Activate};
    root.children.push_back(std::move(cancel));
    const auto* focused = accessibilityTree_.focusedNode();
    const auto focus = focused && EditorSemanticTree::containsId(root, focused->id) ? focused->id : prefix + "input";
    accessibilityTree_.rebuildCustom(std::move(root), focus); return;
  }
  if (timeMapPanel_) {
    const auto prefix = timeMapSemanticPrefix();
    const auto oldFocus = accessibilityTree_.focusedNode() ? accessibilityTree_.focusedNode()->id : std::string{};
    SemanticNode root; root.id = prefix + "panel"; root.role = SemanticRole::Panel;
    root.name = "Tempo and meter events"; root.bounds = layout_.timeMapPanelBounds(logicalWidth_, logicalHeight_);
    const bool stale = !timeMapPanel_->matches(session_.project(), session_.revision());
    root.value = stale ? "Changed. Refresh before editing." : "Current event list";
    const auto rows = timeMapPanel_->page(timeMapPage_);
    for (std::size_t i = 0U; i < rows.size(); ++i) {
      SemanticNode row; row.id = prefix + "row." + std::to_string(i); row.role = SemanticRole::Button;
      row.name = (rows[i].meter ? "Meter at tick " : "Tempo at tick ") + std::to_string(rows[i].tick.value());
      row.value = rows[i].value + (rows[i].removable() ? "" : " initial, cannot remove");
      row.bounds = layout_.timeMapRowBounds(logicalWidth_, logicalHeight_, i);
      row.enabled = !composition_.active(); row.selected = timeMapPanel_->selectedIndex() == timeMapPage_ * TempoMeterModel::pageSize + i;
      row.actions = {SemanticAction::SetFocus, SemanticAction::Activate};
      if (!row.enabled) row.actions.clear();
      root.children.push_back(std::move(row));
    }
    constexpr std::array<const char*, 8> names{"Previous page", "Next page", "Edit selected event", "Remove selected event", "Refresh events", "Close time maps", "Add tempo", "Add meter"};
    for (std::size_t i = 0U; i < names.size(); ++i) {
      SemanticNode button; button.id = prefix + "action." + std::to_string(i); button.role = SemanticRole::Button;
      button.name = names[i]; button.bounds = layout_.timeMapActionBounds(logicalWidth_, logicalHeight_, i);
      button.enabled = !composition_.active() && (!stale || i == 0U || i == 1U || i == 4U || i == 5U);
      if (i == 0U) button.enabled = button.enabled && timeMapPage_ > 0U;
      if (i == 1U) button.enabled = button.enabled && (timeMapPage_ + 1U) * TempoMeterModel::pageSize < timeMapPanel_->size();
      if (i == 3U) button.enabled = button.enabled && timeMapPanel_->selected().removable();
      if (i >= 6U) button.enabled = button.enabled && timeMapPanel_->size() < TempoMeterModel::maximumEvents;
      button.actions = {SemanticAction::SetFocus, SemanticAction::Activate};
      if (!button.enabled) button.actions.clear();
      root.children.push_back(std::move(button));
    }
    if (tempoEdit_ && composition_.active()) {
      SemanticNode input; input.id = prefix + "input"; input.role = SemanticRole::TextField;
      input.name = tempoEdit_->chooseTick ? "New event tick" : (tempoEdit_->meter ? "Time signature numerator slash denominator" : "Tempo in BPM");
      input.value = domain::toUtf8(composition_.compositionText()); input.editableValue = input.value;
      input.bounds = layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, true); input.actions = {SemanticAction::SetFocus, SemanticAction::EditText};
      root.children.push_back(std::move(input));
      SemanticNode cancel; cancel.id = prefix + "cancel"; cancel.role = SemanticRole::Button; cancel.name = "Cancel event input";
      cancel.actions = {SemanticAction::SetFocus, SemanticAction::Activate}; root.children.push_back(std::move(cancel));
    }
    const auto focus = EditorSemanticTree::containsId(root, oldFocus) ? oldFocus : prefix +
        (composition_.active() ? "input" : "row." + std::to_string(timeMapPanel_->selectedIndex() % TempoMeterModel::pageSize));
    accessibilityTree_.rebuildCustom(std::move(root), focus); return;
  }
  accessibilityTree_.rebuild(sceneState(), pianoRoll_);
}

core::Result<void> NativeEditorController::dispatchAccessibility(
    std::string_view id, SemanticAction action) {
  if (replacementOpen_) {
    const std::string target{id}; const auto prefix = replacementSemanticPrefix();
    if (!target.starts_with(prefix)) return core::failure(core::ErrorCode::Conflict, "Stale or background replacement action");
    rebuildAccessibilityTree();
    return accessibilityTree_.dispatch(target, action,
        [&](std::string_view element, SemanticAction requested) -> core::Result<void> {
      if (requested == SemanticAction::SetFocus) {
        const auto focused = accessibilityTree_.setFocus(element); repaint(); return focused;
      }
      const auto suffix = element.substr(prefix.size());
      if (dynamicsDraft_ && requested == SemanticAction::Activate && suffix.size() == 6U && suffix.starts_with("zoom.") &&
          suffix.back() >= '0' && suffix.back() <= '3') return suffix.back() == '3' ? cycleMeasuredChannel() : navigateDynamics(static_cast<ui::DynamicsPlotViewport::Action>(suffix.back() - '0'));
      if (dynamicsDraft_ && requested == SemanticAction::Activate && suffix.size() == 7U && suffix.starts_with("point.") &&
          suffix.back() >= '0' && suffix.back() <= '2') return openReplacementRow(static_cast<std::size_t>(suffix.back() - '0'));
      if (requested == SemanticAction::Activate && suffix.size() == 5U && suffix.starts_with("row.") &&
          suffix.back() >= '0' && suffix.back() <= '5') return openReplacementRow(static_cast<std::size_t>(suffix.back() - '0'));
      if (requested == SemanticAction::Activate && suffix.size() == 8U && suffix.starts_with("action.") &&
          suffix.back() >= '0' && suffix.back() <= '5') return replacementReviewAction(static_cast<std::size_t>(suffix.back() - '0'));
      return core::failure(core::ErrorCode::Unsupported, "Unsupported replacement review action");
    });
  }
  if (id.starts_with("replacement.")) return core::failure(core::ErrorCode::Conflict, "Replacement review has ended");
  if (hintEdit_ || replacementInput_) {
    const std::string target{id}; const auto prefix = hintSemanticPrefix();
    if (target != prefix + "input" && target != prefix + "cancel")
      return core::failure(core::ErrorCode::Conflict, "Stale or background pronunciation-hint action");
    rebuildAccessibilityTree();
    return accessibilityTree_.dispatch(target, action,
        [&](std::string_view element, SemanticAction requested) -> core::Result<void> {
      if (requested == SemanticAction::SetFocus) {
        const auto focused = accessibilityTree_.setFocus(element); repaint(); return focused;
      }
      if (element == prefix + "input" && requested == SemanticAction::EditText) return core::success();
      if (element == prefix + "cancel" && requested == SemanticAction::Activate) {
        cancelTextComposition(); return core::success();
      }
      return core::failure(core::ErrorCode::Unsupported, "Unsupported pronunciation-hint action");
    });
  }
  if (id.starts_with("phone-hint.") || id.starts_with("lyric-find."))
    return core::failure(core::ErrorCode::Conflict, "Pronunciation-hint input has ended");
  if (timeMapPanel_) {
    const std::string target{id}; const auto prefix = timeMapSemanticPrefix();
    if (!target.starts_with(prefix)) return core::failure(core::ErrorCode::Conflict, "Stale or background time-map action");
    rebuildAccessibilityTree();
    const auto& nodes = accessibilityTree_.root().children;
    const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const auto& item) { return item.id == target; });
    if (node == nodes.end() || !node->enabled) return core::failure(core::ErrorCode::Conflict, "Time-map control is unavailable");
    return accessibilityTree_.dispatch(target, action, [&](std::string_view element, SemanticAction requested) -> core::Result<void> {
      if (requested == SemanticAction::SetFocus) { const auto focused = accessibilityTree_.setFocus(element); repaint(); return focused; }
      const auto suffix = element.substr(prefix.size());
      if (suffix == "input" && requested == SemanticAction::EditText) return core::success();
      if (requested != SemanticAction::Activate) return core::failure(core::ErrorCode::Unsupported, "Unsupported time-map action");
      if (suffix == "cancel") { cancelTextComposition(); return core::success(); }
      const bool row = suffix.starts_with("row."); const auto marker = row ? std::string_view{"row."} : std::string_view{"action."};
      if (!suffix.starts_with(marker)) return core::failure(core::ErrorCode::InvalidArgument, "Unknown time-map target");
      std::size_t index = 0U; const auto number = suffix.substr(marker.size());
      const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
      if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) return core::failure(core::ErrorCode::InvalidArgument, "Invalid time-map target");
      if (!row) return timeMapPanelAction(index);
      const auto selected = timeMapPanel_->select(timeMapPage_ * TempoMeterModel::pageSize + index); repaint(); return selected;
    });
  }
  if (phonemeReview_ && !id.starts_with("phoneme.review.action.")) {
    return core::failure(core::ErrorCode::Conflict, "Close phoneme review before using background controls");
  }
  return accessibilityTree_.dispatch(
      id, action,
      [this](std::string_view element, SemanticAction requested) {
        if (element == "toolbar.time-map") {
          if (requested == SemanticAction::Activate) return openTimeMapPanel();
        }
        if (requested == SemanticAction::Activate && element == "phoneme.review.open") return openPhonemeReview();
        if (requested == SemanticAction::Activate && element.starts_with("phoneme.review.action.")) {
          const auto suffix = element.substr(std::string_view{"phoneme.review.action."}.size());
          if (suffix.size() == 1U && suffix.front() >= '0' && suffix.front() <= '5') {
            return activatePhonemeReview(static_cast<std::size_t>(suffix.front() - '0'));
          }
        }
        if (requested == SemanticAction::SetFocus) {
          const auto focused = accessibilityTree_.setFocus(element);
          if (!focused) return focused;
          syncInteractionToAccessibilityFocus();
          repaint();
          if (element.starts_with("phoneme.review.action.")) return core::success();
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
        if (element == "toolbar.bounce" &&
            (requested == SemanticAction::Activate ||
             requested == SemanticAction::Toggle)) {
          if (!callbacks_.setBounceTiming) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Bounce timing is not connected");
          }
          const auto result = callbacks_.setBounceTiming(!bounceFollowHost_);
          if (result) bounceFollowHost_ = !bounceFollowHost_;
          repaint();
          return result;
        }
        if (element == "toolbar.batch-lyrics" &&
            requested == SemanticAction::Activate) {
          return beginBatchLyricEdit();
        }
        if (element == "toolbar.tempo") {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested == SemanticAction::Activate || requested == SemanticAction::EditText)
            return beginTempoEdit();
        }
        if (element == "toolbar.meter") {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested == SemanticAction::Activate || requested == SemanticAction::EditText)
            return beginMeterEdit();
        }
        if ((element == "audio.settings" || element == "audio.diagnostics" ||
             element == "audio.sample-rate" ||
             element == "audio.block-frames" ||
             element == "audio.channels" ||
             element.rfind("audio.device.", 0U) == 0U) &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element == "support.panel" &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        constexpr auto supportItemPrefix = std::string_view{"support.item."};
        if (element.starts_with(supportItemPrefix)) {
          std::size_t index = 0U;
          const auto suffix = element.substr(supportItemPrefix.size());
          const auto parsed = std::from_chars(
              suffix.data(), suffix.data() + suffix.size(), index);
          if (parsed.ec != std::errc{} ||
              parsed.ptr != suffix.data() + suffix.size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Support report accessibility index is invalid");
          }
          const auto& support = recoverySupportPanel_.view();
          if (index >= support.items.size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Support report accessibility index is unavailable");
          }
          if (requested == SemanticAction::SetFocus) {
            auto view = support;
            view.firstVisibleItem = index;
            recoverySupportPanel_.update(std::move(view));
            rebuildAccessibilityTree();
            repaint();
            return core::success();
          }
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Support report only supports activation");
          }
          return selectSupportReport(index);
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
        if (element == "inspector.vibrato" && requested == SemanticAction::Activate) return openVibratoInspector();
        if (element == "inspector.dynamics" && requested == SemanticAction::Activate) return openDynamicsInspector();
        if (element == "inspector.style" && requested == SemanticAction::Activate) return openStyleCoverageSheet();
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
        if (const auto lane = technicalLaneForId(element); lane.has_value()) {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Toggle) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Technical lane only supports toggle");
          }
          const auto fromState = sceneState();
          auto presentation =
              session_.project().settings().technicalLanes[lane->second];
          presentation.mode =
              presentation.mode == domain::TechnicalLaneMode::Expanded
                  ? domain::TechnicalLaneMode::Collapsed
                  : domain::TechnicalLaneMode::Expanded;
          const auto changed = session_.execute(std::make_unique<
              application::SetTechnicalLanePresentationCommand>(
              lane->first, presentation));
          if (changed) {
            beginLayoutTransition(fromState);
            if (callbacks_.viewChanged) callbacks_.viewChanged();
            repaint();
          }
          return changed;
        }
        constexpr auto overlapGroupPrefix = std::string_view{"overlap-group."};
        if (element.starts_with(overlapGroupPrefix)) {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Overlap group only supports activation");
          }
          std::size_t groupIndex = 0U;
          const auto suffix = element.substr(overlapGroupPrefix.size());
          const auto parsed = std::from_chars(
              suffix.data(), suffix.data() + suffix.size(), groupIndex);
          if (parsed.ec != std::errc{} ||
              parsed.ptr != suffix.data() + suffix.size()) {
            return core::failure(core::ErrorCode::InvalidArgument,
                                 "Overlap group accessibility id is invalid");
          }
          const auto visuals = pianoRoll_.visibleNotes();
          const auto member = std::find_if(
              visuals.begin(), visuals.end(), [groupIndex](const ui::NoteVisual& note) {
                return note.overlapGroup == groupIndex &&
                       note.overlapMemberCount > 1U;
              });
          if (member == visuals.end()) {
            return core::failure(core::ErrorCode::NotFound,
                                 "Overlap group is no longer visible");
          }
          const auto candidates = pianoRoll_.overlapCandidatesAt(ui::Point{
              member->bounds.x + member->bounds.width * 0.5,
              member->bounds.y + member->bounds.height * 0.5});
          if (candidates.empty()) {
            return core::failure(core::ErrorCode::NotFound,
                                 "Overlap group has no selectable notes");
          }
          const auto selected = session_.selection().noteIds();
          const auto current = std::find_first_of(
              candidates.begin(), candidates.end(), selected.begin(), selected.end());
          const auto next = current == candidates.end()
                                ? candidates.front()
                                : candidates[(static_cast<std::size_t>(
                                                  std::distance(candidates.begin(), current)) +
                                              1U) %
                                             candidates.size()];
          session_.selection().selectOnly(next);
          EditorSceneState::OverlapDetail detail{.groupIndex = groupIndex};
          detail.members.reserve(candidates.size());
          const auto* region = session_.project().findRegion(regionId_);
          for (const auto candidate : candidates) {
            const auto* note = region == nullptr ? nullptr : region->findNote(candidate);
            const auto* lyric = note == nullptr || region == nullptr
                                    ? nullptr
                                    : region->findLyric(note->lyricTokenId);
            detail.members.push_back(EditorSceneState::OverlapDetailMember{
                .noteId = candidate,
                .lyric = lyric == nullptr ? std::string{}
                                          : domain::toUtf8(lyric->surface),
                .midiKey = static_cast<std::uint8_t>(
                    note == nullptr ? 0U : note->midiKey),
                .selected = candidate == next,
            });
          }
          overlapDetail_ = std::move(detail);
          repaint();
          return core::success();
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
  if (replacementOpen_ || id.starts_with("replacement."))
    return core::failure(core::ErrorCode::Conflict, "Replacement review does not edit background values");
  if (hintEdit_ || replacementInput_) {
    if (!composition_.active() || id != hintSemanticPrefix() + "input")
      return core::failure(core::ErrorCode::Conflict, "Stale or background pronunciation-hint input");
    if (value.size() > 4096U)
      return core::failure(core::ErrorCode::InvalidArgument, "Phone hint is too long");
    const auto decoded = domain::fromUtf8(std::string{value});
    if (!decoded) return core::Result<void>{decoded.error()};
    return commitTextComposition(decoded.value());
  }
  if (id.starts_with("phone-hint.") || id.starts_with("lyric-find."))
    return core::failure(core::ErrorCode::Conflict, "Pronunciation-hint input has ended");
  if (timeMapPanel_) {
    if (!tempoEdit_ || !composition_.active() || id != timeMapSemanticPrefix() + "input")
      return core::failure(core::ErrorCode::Conflict, "Stale or background time-map input");
    if (value.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Time-map input is too long");
    const auto decoded = domain::fromUtf8(std::string{value}); if (!decoded) return core::Result<void>{decoded.error()};
    return commitTextComposition(decoded.value());
  }
  if (phonemeReview_) return core::failure(core::ErrorCode::Conflict, "Close phoneme review before editing background values");
  if (id == "toolbar.tempo" || id == "toolbar.meter") {
    const bool meter = id == "toolbar.meter";
    if (value.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Tempo text is too long");
    const auto decoded = domain::fromUtf8(std::string{value});
    if (!decoded) return core::Result<void>{decoded.error()};
    if (tempoEdit_ && (tempoEdit_->tick != time::Tick{0} || tempoEdit_->meter != meter))
      return core::failure(core::ErrorCode::Conflict, "Finish the selected tempo event before editing initial BPM");
    if (!tempoEdit_) {
      const auto begun = meter ? beginMeterEdit() : beginTempoEdit(); if (!begun) return begun;
    }
    return commitTextComposition(decoded.value());
  }
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
                                                 std::optional<domain::Language> language) {
  const auto result = pianoRoll_.distributeSelectedLyrics(std::move(text), language);
  if (result && result.value().committed && result.value().changedLyrics > 0U) markDocumentChanged();
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

core::Result<TempoMeterModel> NativeEditorController::timeMapEvents() const {
  return TempoMeterModel::capture(session_.project(), session_.revision());
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
    callbacks_.beginTextInput({externalTextTarget(), layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, true), U"0"});
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
  callbacks_.beginTextInput({externalTextTarget(), layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, timeMapPanel_.has_value()), text.value()});
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
  callbacks_.beginTextInput({externalTextTarget(), layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, timeMapPanel_.has_value()), text.value()});
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
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerDown(
    const PointerEvent& event) {
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
      if (view.rowsInspectable) for (std::size_t i = 0U; i < view.rows.size(); ++i)
        if (layout_.reviewRowBounds(logicalWidth_, logicalHeight_, i, view.dockedInspector).contains(event.position)) return openReplacementRow(i);
      for (std::size_t i = 0U; i < view.enabled.size(); ++i)
        if (view.enabled[i] && layout_.reviewButtonBounds(logicalWidth_, logicalHeight_, i, view.dockedInspector).contains(event.position)) return replacementReviewAction(i);
    }
    return core::success();
  }
  if (hintEdit_ || replacementInput_) {
    if (event.button == PointerButton::Left &&
        layout_.hintCancelBounds(logicalWidth_, logicalHeight_).contains(event.position)) cancelTextComposition();
    return core::success();
  }
  if (timeMapPanel_) {
    if (event.button != PointerButton::Left || composition_.active()) return core::success();
    const auto rows = timeMapPanel_->page(timeMapPage_);
    for (std::size_t i = 0U; i < rows.size(); ++i) {
      if (layout_.timeMapRowBounds(logicalWidth_, logicalHeight_, i).contains(event.position)) {
        const auto selected = timeMapPanel_->select(timeMapPage_ * TempoMeterModel::pageSize + i);
        repaint(); if (!selected) return selected;
        return event.clickCount >= 2 ? timeMapPanelAction(2U) : core::success();
      }
    }
    for (std::size_t i = 0U; i < 8U; ++i)
      if (layout_.timeMapActionBounds(logicalWidth_, logicalHeight_, i).contains(event.position)) return timeMapPanelAction(i);
    return core::success();
  }
  if (event.button == PointerButton::Left && layout_.timeMapOpenBounds().contains(event.position)) return openTimeMapPanel();
  if (!phonemeReview_ && !sampleMicroscopeOpen() && event.button == PointerButton::Left &&
      layout_.tempoInputBoundsForWidth(logicalWidth_).contains(event.position)) return beginTempoEdit();
  if (!phonemeReview_ && !sampleMicroscopeOpen() && event.button == PointerButton::Left &&
      layout_.meterInputBoundsForWidth(logicalWidth_).contains(event.position)) return beginMeterEdit();
  if (phonemeReview_) {
    if (event.button == PointerButton::Left) {
      for (std::size_t i = 0U; i < 6U; ++i) {
        if (layout_.phonemeReviewButtonBounds(logicalWidth_, logicalHeight_, i).contains(event.position)) {
          return sceneState().phonemeReview.enabled[i] ? activatePhonemeReview(i) : core::success();
        }
      }
    }
    return core::success();
  }
  if (!sampleMicroscopeOpen() && callbacks_.reviewPhonemeBindings && callbacks_.rebindPhonemeOverride &&
      event.button == PointerButton::Left && layout_.phonemeReviewOpenBounds(logicalWidth_, logicalHeight_).contains(event.position)) {
    return openPhonemeReview();
  }
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
  const auto& supportView = recoverySupportPanel_.view();
  if (supportView.visible) {
    const auto panelX = std::max(
        layout_.keyboardWidth + layout_.minimumTimelineWidth,
        logicalWidth_ - layout_.characterDockWidth);
    if (event.position.x >= panelX &&
        event.position.y >= layout_.toolbarHeight &&
        event.position.y < layout_.pianoBottom(logicalHeight_)) {
      const auto first = std::min(supportView.firstVisibleItem,
                                  supportView.items.size());
      for (std::size_t index = first; index < supportView.items.size(); ++index) {
        const auto row = layout_.supportItemBounds(panelX, logicalWidth_,
                                                   index - first);
        if (row.y >= layout_.pianoBottom(logicalHeight_)) break;
        if (!row.contains(event.position)) continue;
        return supportView.mode == RecoverySupportMode::Reports
                   ? selectSupportReport(index)
                   : core::success();
      }
      return core::success();
    }
  }
  if (!voicebankBrowserVisible_ && !audioSettings_.visible &&
      !supportView.visible &&
      session_.project().settings().characterDisplay ==
          domain::CharacterDisplayMode::Off &&
      !arrangementPanel_.tracks().empty()) {
    const auto arrangementState = sceneState();
    const auto dockWidth = resolveEditorDockWidth(arrangementState, layout_);
    const auto panelBottom = logicalHeight_ - layout_.statusHeight -
        layout_.diagnosticHeight(!arrangementState.diagnostics.empty()) -
        layout_.exportHeight(arrangementState.exportProgress.totalFiles != 0U);
    const auto panelX = std::max(
        layout_.keyboardWidth + layout_.minimumTimelineWidth,
        logicalWidth_ - dockWidth);
    if (dockWidth > 0.0 && event.position.x >= panelX &&
        event.position.y >= layout_.toolbarHeight &&
        event.position.y < panelBottom) {
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
        if (y + layout_.trackRowHeight > panelBottom) break;
        if (event.position.y >= y + layout_.trackRowTopOffset &&
            event.position.y < y + layout_.trackRowTopOffset + layout_.trackRowHeight) {
          if (event.clickCount >= 2) return beginSelectedTrackRename();
          return selectTrack(track.id);
        }
        y += layout_.trackRowAdvance;
        for (const auto& region : track.regions) {
          if (y + layout_.regionAdvance - layout_.regionBottomPadding > panelBottom) break;
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
      if (const auto resolvedTop = resolveArrangementInspectorTop(arrangementState, layout_, panelBottom)) {
        const auto inspectorTop = *resolvedTop;
        const auto firstFieldBaseline =
            inspectorTop + layout_.inspectorNameBaseline +
            layout_.inspectorNameToFirstFieldAdvance;
        const auto toggleTop = firstFieldBaseline +
                               layout_.inspectorFieldAdvance * 3.0;
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
            firstFieldBaseline + layout_.inspectorFieldAdvance * 4.0,
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
        const ui::Rect vibratoBounds{panelX + layout_.inspectorTextInsetX, firstFieldBaseline + layout_.inspectorFieldAdvance * 5.0,
            (logicalWidth_ - panelX - layout_.inspectorTextInsetX * 2.0 - 8.0) / 3.0, 22.0};
        if (arrangementState.vibratoEditable && vibratoBounds.contains(event.position)) return openVibratoInspector();
        const ui::Rect dynamicsBounds{vibratoBounds.x + vibratoBounds.width + 4.0, vibratoBounds.y, vibratoBounds.width, vibratoBounds.height};
        if (arrangementState.dynamicsEditable && dynamicsBounds.contains(event.position)) return openDynamicsInspector();
        const ui::Rect styleBounds{dynamicsBounds.x + dynamicsBounds.width + 4.0, dynamicsBounds.y, dynamicsBounds.width, dynamicsBounds.height};
        if (arrangementState.styleEditable && styleBounds.contains(event.position)) return openStyleCoverageSheet();
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
    const auto bounceBounds =
        layout_.bounceTimingBoundsForWidth(logicalWidth_, portraitVisible);
    if (callbacks_.setBounceTiming && bounceBounds.width > 0.0 &&
        bounceBounds.contains(event.position)) {
      const auto result = callbacks_.setBounceTiming(!bounceFollowHost_);
      if (result) bounceFollowHost_ = !bounceFollowHost_;
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
  const auto technical = resolveEditorTechnicalLaneHeights(
      state, layout_, logicalHeight_ - layout_.statusHeight - overlayInset);
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
      ui::PhonemeLaneModel lane;
      const auto current = session_.project().findRegion(regionId_);
      if (current != nullptr) {
        lane.rebuild(pianoRoll_, phonemizer::inspectPronunciation(*current),
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
        lane.rebuild(pianoRoll_, phonemizer::inspectPronunciation(*current),
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

core::Result<void> NativeEditorController::pointerMove(
    const PointerEvent& event) {
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
  if (sampleMicroscopeOpen() && event.key == NativeKey::Escape) {
    closeSampleMicroscope();
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

  if (event.key == NativeKey::Tab) {
    rebuildAccessibilityTree();
    const auto focused = accessibilityTree_.focusNext(event.modifiers.shift);
    if (focused) {
      syncInteractionToAccessibilityFocus();
      repaint();
    }
    return focused;
  }

  if (voicebankBrowserVisible_) {
    if (event.key == NativeKey::Escape || event.key == NativeKey::V) {
      voicebankBrowserVisible_ = false;
      repaint();
      return core::success();
    }
    if (event.key == NativeKey::R) {
      if (!callbacks_.refreshVoicebanks) {
        return core::failure(core::ErrorCode::Unsupported,
                             "Voicebank refresh is not connected");
      }
      const auto refreshed = callbacks_.refreshVoicebanks();
      repaint();
      return refreshed;
    }
    if (event.key == NativeKey::O) {
      if (!callbacks_.openVoicebankInstaller) {
        return core::failure(core::ErrorCode::Unsupported,
                             "Standalone voicebank installation is not connected");
      }
      const auto opened = callbacks_.openVoicebankInstaller();
      repaint();
      return opened;
    }
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

  if (event.key == NativeKey::Enter && event.modifiers.alt &&
      !event.modifiers.primaryShortcut()) {
    return beginSelectedHintEdit();
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
    const auto fromState = sceneState();
    auto& mode = session_.project().settings().characterDisplay;
    if (mode == domain::CharacterDisplayMode::Full) {
      mode = domain::CharacterDisplayMode::Minimal;
    } else if (mode == domain::CharacterDisplayMode::Minimal) {
      mode = domain::CharacterDisplayMode::Off;
    } else {
      mode = domain::CharacterDisplayMode::Full;
    }
    beginLayoutTransition(fromState);
    if (callbacks_.viewChanged) {
      callbacks_.viewChanged();
    } else {
      repaint();
    }
  } else if (event.key == NativeKey::V) {
    if (recoverySupportPanel_.view().visible) return core::success();
    voicebankBrowserVisible_ = !voicebankBrowserVisible_;
    if (voicebankBrowserVisible_) {
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
  if (phonemeReview_ || timeMapPanel_ || replacementOpen_ || replacementInput_) return;
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

core::Result<void> NativeEditorController::beginSelectedHintEdit() {
  const auto selected = session_.selection().noteIds();
  if (selected.size() != 1U)
    return core::failure(core::ErrorCode::Conflict, "Select exactly one note to edit its phone hint");
  return beginHintEdit(selected.front());
}

core::Result<void> NativeEditorController::beginHintEdit(domain::NoteId noteId) {
  if (hintInteraction_ == std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict, "Phone-hint interaction identity exhausted");
  if (composition_.active() || timeMapPanel_ || phonemeReview_ || sampleMicroscopeOpen() || replacementOpen_ ||
      voicebankBrowserVisible_ || audioSettings_.visible || dragMode_ != DragMode::None)
    return core::failure(core::ErrorCode::Conflict, "Finish the active edit before changing a phone hint");
  const auto* region = session_.project().findRegion(regionId_);
  const auto* note = region ? region->findNote(noteId) : nullptr;
  const auto* lyric = note ? region->findLyric(note->lyricTokenId) : nullptr;
  if (!note || !lyric) return core::failure(core::ErrorCode::NotFound, "Hint target is missing from the selected region");
  if (lyric->language != domain::Language::Japanese && lyric->language != domain::Language::Unspecified)
    return core::failure(core::ErrorCode::Unsupported, "No registered phone-hint editor for this language");
  if (!callbacks_.beginTextInput) return core::failure(core::ErrorCode::Unsupported, "Native hint text input is not connected");
  const auto text = domain::fromUtf8(note->phoneticHint.value_or("")); if (!text) return core::Result<void>{text.error()};
  const auto begun = composition_.begin(externalTextTarget(), text.value()); if (!begun) return begun;
  tempoEdit_.reset(); renameTrackTarget_.reset(); renameRegionTarget_.reset(); batchLyricTarget_.reset();
  hintEdit_ = HintEditContext{session_.project().id(), regionId_, noteId, session_.revision(), note->phoneticHint};
  ++hintInteraction_;
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), text.value()});
  repaint(); return core::success();
}

core::Result<void> NativeEditorController::beginLyricEdit(domain::NoteId noteId) {
  if (replacementOpen_) return core::failure(core::ErrorCode::Conflict, "Close replacement review before editing lyrics");
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
  tempoEdit_.reset(); hintEdit_.reset(); replacementInput_.reset();
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  batchLyricTarget_.reset();
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
  auto commit = composition_.commit(hintEdit_.has_value() || replacementInput_.has_value());
  if (!commit) return core::Result<void>{commit.error()};

  core::Result<void> result = core::success();
  if (replacementInput_) {
    auto input = std::move(*replacementInput_); replacementInput_.reset();
    finishTextInput();
    if (input.vibratoField && vibratoDraft_) replacementOpen_ = true;
    if (input.dynamicsTickField && dynamicsDraft_) replacementOpen_ = true;
    const auto valid = session_.validatePerformanceJob(input.context);
    if (!valid || input.revision != session_.revision() || input.regionId != regionId_) {
      if (input.vibratoField) replacementError_ = "Vibrato source changed; Refresh or Cancel";
      if (input.dynamicsTickField) replacementError_ = "Dynamics source changed; Back then Refresh, or Cancel";
      repaint(); return core::failure(core::ErrorCode::Conflict, "Find/replace input belongs to a changed document or region");
    }
    const auto textValue = domain::toUtf8(commit.value().text);
    if (input.dynamicsTickField) {
      if (!dynamicsDraft_ || !dynamicsPointEdit_ || !dynamicsDraft_->matches(session_, regionId_))
        return core::failure(core::ErrorCode::Conflict, "Dynamics draft is stale or closed");
      if (textValue.size() > 64U) {
        replacementError_ = "Dynamics fields accept at most 64 bytes"; repaint();
        return core::failure(core::ErrorCode::InvalidArgument, replacementError_);
      }
      (*input.dynamicsTickField ? dynamicsPointEdit_->tickText : dynamicsPointEdit_->gainText) = textValue;
      const auto point = dynamicsPointValue(); replacementError_ = point ? std::string{} : point.error().message;
      repaint(); return point ? core::success() : core::Result<void>{point.error()};
    }
    if (input.vibratoField) {
      if (!vibratoDraft_) return core::failure(core::ErrorCode::Conflict, "Vibrato draft is no longer open");
      const auto edited = vibratoDraft_->set(session_, regionId_, *input.vibratoField, textValue);
      replacementError_ = edited ? std::string{} : edited.error().message; repaint(); return edited;
    }
    if (commit.value().text.size() > ui::NoteSearchModel::maximumQueryScalars || (!input.query && textValue.empty())) {
      replacementInputError_ = !input.query && textValue.empty() ? "ENTER A NONEMPTY QUERY" : "MAXIMUM 256 CHARACTERS";
      replacementInput_.emplace(std::move(input));
      const auto begun = composition_.begin(externalTextTarget(), commit.value().text);
      if (!begun) { replacementInput_.reset(); return begun; }
      callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), commit.value().text});
      repaint(); return core::failure(core::ErrorCode::InvalidArgument, replacementInputError_);
    }
    replacementInputError_.clear();
    if (input.diagnostics) return openDiagnosticFindReview(textValue);
    if (input.findOnly) return openFindReview(textValue);
    if (!input.query) {
      input.query = textValue; replacementInput_.emplace(std::move(input));
      const auto begun = composition_.begin(externalTextTarget(), U"");
      if (!begun) { replacementInput_.reset(); return begun; }
      callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), U""});
      repaint(); return core::success();
    }
    return openReplacementReview(*input.query, textValue);
  }
  if (hintEdit_) {
    const auto expected = *hintEdit_; hintEdit_.reset();
    const auto* region = session_.project().findRegion(expected.regionId);
    const auto* note = region ? region->findNote(expected.noteId) : nullptr;
    if (expected.projectId != session_.project().id() || expected.regionId != regionId_ ||
        expected.revision != session_.revision() || !note || note->phoneticHint != expected.before)
      result = core::failure(core::ErrorCode::Conflict, "Phone-hint edit is stale");
    else {
      const auto value = domain::toUtf8(commit.value().text);
      const std::optional<std::string> hint = value.empty() ? std::nullopt : std::optional{value};
      if (hint != expected.before) {
        result = session_.execute(std::make_unique<application::SetNoteHintsCommand>(
            std::vector<application::NoteHintEdit>{{expected.noteId, expected.before, hint}}));
        if (result) markDocumentChanged();
      }
    }
    finishTextInput(); repaint(); return result;
  }
  if (tempoEdit_) {
    const auto expected = *tempoEdit_; tempoEdit_.reset();
    const auto value = domain::toUtf8(commit.value().text);
    if (expected.insert) {
      const auto tick = expected.chooseTick ? parseTimeMapTick(value) : core::success(expected.tick);
      if (!tick) { finishTextInput(); repaint(); return core::Result<void>{tick.error()}; }
      const auto target = tick.value();
      const bool occupied = expected.meter
          ? std::any_of(session_.project().meterMap().events().begin(), session_.project().meterMap().events().end(),
                        [&](const auto& event) { return event.tick == target; })
          : std::any_of(session_.project().tempoMap().events().begin(), session_.project().tempoMap().events().end(),
                        [&](const auto& event) { return event.tick == target; });
      if (expected.revision != session_.revision() || occupied || !timeMapPanel_ ||
          !timeMapPanel_->matches(session_.project(), session_.revision())) {
        finishTextInput(); repaint();
        return core::failure(core::ErrorCode::Conflict, "Event already exists or the time map changed; refresh and select Edit");
      }
      if (expected.chooseTick) {
        finishTextInput();
        if (expected.revision != session_.revision()) return core::failure(core::ErrorCode::Conflict, "Document changed while advancing event input");
        const auto begun = expected.meter ? beginMeterEdit(target) : beginTempoEdit(target);
        if (begun && tempoEdit_) tempoEdit_->insert = true;
        return begun;
      }
    }
    if (expected.meter) {
      const auto signature = parseMeterEditText(value);
      if (!signature) result = core::Result<void>{signature.error()};
      else result = editMeter(expected.revision, expected.tick,
          application::EditMeterCommand::Signature{signature.value().numerator, signature.value().denominator});
      finishTextInput(); repaint(); return result;
    }
    const auto bpm = parseTempoEditText(value);
    if (!bpm) result = core::Result<void>{bpm.error()};
    else result = editTempo(expected.revision, expected.tick, bpm.value());
    finishTextInput(); repaint(); return result;
  }
  if (renameTrackTarget_.has_value()) {
    result = renameSelectedTrack(domain::toUtf8(commit.value().text));
    renameTrackTarget_.reset();
    renameRegionTarget_.reset();
    batchLyricTarget_.reset();
    finishTextInput();
    repaint();
    return result;
  }
  if (renameRegionTarget_.has_value()) {
    result = renameSelectedRegion(domain::toUtf8(commit.value().text));
    renameTrackTarget_.reset();
    renameRegionTarget_.reset();
    batchLyricTarget_.reset();
    finishTextInput();
    repaint();
    return result;
  }

  if (batchLyricTarget_) {
    auto expected = std::move(*batchLyricTarget_); batchLyricTarget_.reset();
    auto selected = session_.selection().noteIds(); std::sort(selected.begin(), selected.end());
    const auto valid = session_.validatePerformanceJob(expected.context);
    if (!valid || expected.revision != session_.revision() || expected.regionId != regionId_ || selected != expected.notes) {
      finishTextInput(); repaint();
      return core::failure(core::ErrorCode::Conflict, "Batch lyric targets changed; reopen entry for the current selection");
    }
    finishTextInput();
    repaint();
    return openDistributionReview(domain::toUtf8(commit.value().text));
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
  const bool returnToVibrato = replacementInput_ && replacementInput_->vibratoField && vibratoDraft_;
  const bool returnToDynamics = replacementInput_ && replacementInput_->dynamicsTickField && dynamicsDraft_;
  composition_.cancel();
  replacementInput_.reset(); replacementInputError_.clear();
  hintEdit_.reset();
  tempoEdit_.reset();
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  batchLyricTarget_.reset();
  finishTextInput();
  if (returnToVibrato) replacementOpen_ = true;
  if (returnToDynamics) replacementOpen_ = true;
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
