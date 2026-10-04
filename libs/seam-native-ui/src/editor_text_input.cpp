// Text entry: phone hints, lyrics, and the composition a creator types them through.
//
// SEAM-BETA-P2-01 names input mode as the last of the four boundaries worth taking out of
// editor_controller.cpp. This is that boundary: beginning a phone-hint or lyric edit, the IME
// composition updates and commits that carry typed text into the project, moving between lyric
// notes, and cancelling a composition back to whatever opened it.
//
// The methods moved verbatim -- the controller diff is deletions only and the block was diffed
// byte-for-byte -- so nothing about their behaviour changed with their location.
//
// What stays behind is the part that is not text entry. keyDown itself, the pointer gestures, the
// scroll and zoom handling and the audio-settings commands are input too, but they are input to the
// editor rather than input to a field, and they reach into so much of the controller that moving them
// would mean moving the controller. commitTextComposition is the one that reads most like a general
// commit and is deliberately still reached from the overlay file.

#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_notices.hpp"
#include "seam/native_ui/editor_text_target.hpp"

#include "seam/application/lyric_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/view_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::native_ui {

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
  if (lyric->language != domain::Language::Japanese &&
      lyric->language != domain::Language::English &&
      lyric->language != domain::Language::Korean &&
      lyric->language != domain::Language::Unspecified)
    return core::failure(core::ErrorCode::Unsupported,
                         "No registered phone-hint editor for this language");
  if (!callbacks_.beginTextInput) return core::failure(core::ErrorCode::Unsupported, "Native hint text input is not connected");
  const auto text = domain::fromUtf8(note->phoneticHint.value_or("")); if (!text) return core::Result<void>{text.error()};
  const auto begun = composition_.begin(externalTextTarget(), text.value()); if (!begun) return begun;
  tempoEdit_.reset(); renameTrackTarget_.reset(); renameRegionTarget_.reset(); batchLyricTarget_.reset();
  hintEditError_.clear();
  hintEdit_ = HintEditContext{session_.project().id(), regionId_, noteId,
                              session_.revision(), lyric->language,
                              note->phoneticHint};
  ++hintInteraction_;
  callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), text.value(), TextInputAnchor::BoundedField});
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
        .anchor = TextInputAnchor::NoteGrid,
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::updateTextComposition(
    std::u32string text, ui::CompositionSelection selection) {
  const auto result = composition_.update(std::move(text), selection);
  if (result && hintEdit_) hintEditError_.clear();
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
  if (hintEdit_ && hintEdit_->revision == session_.revision() &&
      !composition_.compositionText().empty()) {
    const auto* region = session_.project().findRegion(hintEdit_->regionId);
    const auto* note = region ? region->findNote(hintEdit_->noteId) : nullptr;
    const auto* lyric = note && region ? region->findLyric(note->lyricTokenId) : nullptr;
    if (!lyric)
      return core::failure(core::ErrorCode::Conflict,
                           "Phone-hint target changed; cancel and reopen the editor");
    const auto hint = domain::toUtf8(composition_.compositionText());
    const auto valid = phonemizer::validatePhoneHintForLanguage(hintEdit_->language, hint);
    if (!valid) {
      hintEditError_ = valid.error().message;
      repaint();
      return valid;
    }
    hintEditError_.clear();
  }
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
      callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), commit.value().text, TextInputAnchor::BoundedField});
      repaint(); return core::failure(core::ErrorCode::InvalidArgument, replacementInputError_);
    }
    replacementInputError_.clear();
    if (input.diagnostics) return openDiagnosticFindReview(textValue);
    if (input.findOnly) return openFindReview(textValue);
    if (!input.query) {
      input.query = textValue; replacementInput_.emplace(std::move(input));
      const auto begun = composition_.begin(externalTextTarget(), U"");
      if (!begun) { replacementInput_.reset(); return begun; }
      callbacks_.beginTextInput({externalTextTarget(), layout_.hintTextBounds(logicalWidth_, logicalHeight_), U"", TextInputAnchor::BoundedField});
      repaint(); return core::success();
    }
    return openReplacementReview(*input.query, textValue);
  }
  if (hintEdit_) {
    const auto expected = *hintEdit_; hintEdit_.reset();
    const auto* region = session_.project().findRegion(expected.regionId);
    const auto* note = region ? region->findNote(expected.noteId) : nullptr;
    const auto* lyric = note && region ? region->findLyric(note->lyricTokenId) : nullptr;
    if (expected.projectId != session_.project().id() || expected.regionId != regionId_ ||
        expected.revision != session_.revision() || !note || !lyric ||
        lyric->language != expected.language || note->phoneticHint != expected.before)
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
    hintEditError_.clear();
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

void NativeEditorController::cancelTextComposition() noexcept {
  const bool returnToVibrato = replacementInput_ && replacementInput_->vibratoField && vibratoDraft_;
  const bool returnToDynamics = replacementInput_ && replacementInput_->dynamicsTickField && dynamicsDraft_;
  composition_.cancel();
  replacementInput_.reset(); replacementInputError_.clear();
  hintEdit_.reset(); hintEditError_.clear();
  tempoEdit_.reset();
  renameTrackTarget_.reset();
  renameRegionTarget_.reset();
  batchLyricTarget_.reset();
  finishTextInput();
  if (returnToVibrato) replacementOpen_ = true;
  if (returnToDynamics) replacementOpen_ = true;
  repaint();
}


}  // namespace seam::native_ui
