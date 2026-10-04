// Opening and refreshing the editor overlay panels: the vibrato inspector, the style coverage
// sheet, the Japanese reading review, the dynamics inspector, the find and diagnostic-find
// reviews, the clear-dynamics / note-cleanup / clear-vibrato reviews, and the lyric replacement
// and distribution reviews.
//
// SEAM-BETA-P2-01 names overlay/panel coordination as one of the boundaries worth taking out of
// editor_controller.cpp. This is that boundary: the twenty-five methods that decide which overlay
// is open, what it shows, and when it is refreshed. They moved verbatim -- the controller diff is
// deletions only and the block was diffed byte-for-byte -- so nothing about their behaviour changed
// with their location.
//
// What stays behind is deliberate. The views these panels render (replacementReviewView in
// particular, a single 434-line method) still live in the controller, as does the scene model and
// the edit commands the panels run. Moving a view means moving the state that fills it, and a
// boundary that has to drag the whole controller behind it is not a boundary. The oversized view
// builder is a separate piece of work and is named as open in the readiness register.

#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_text_target.hpp"

#include "seam/application/lyric_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/view_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::native_ui {

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
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), textValue.value(), TextInputAnchor::BoundedField});
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
  styleBlendMode_ = false; styleBlendChoosingSecondary_ = false;
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
    if constexpr (std::is_floating_point_v<std::remove_reference_t<decltype(value)>>) {
      return core::parseFiniteDecimal(textValue, value);
    } else {
      const auto result = std::from_chars(textValue.data(), textValue.data() + textValue.size(), value);
      return result.ec == std::errc{} && result.ptr == textValue.data() + textValue.size();
    }
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
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), value.value(), TextInputAnchor::BoundedField});
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
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), U"", TextInputAnchor::BoundedField});
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


}  // namespace seam::native_ui
