// Keyboard handling, split by what the key means rather than by which key it is.
//
// keyDown was one 481-line method. Its first half asks what surface has focus -- the replacement
// panel, the time map, the phoneme review, the sample microscope, the recovery sheet, an active text
// composition, the voicebank browser, the audio settings, the expression lane -- and each of those
// returns as soon as it handles a key, because an open surface owns the keyboard. Its second half
// asks what the key means to the editor as a whole: undo, redo, play, save, nudge, delete.
//
// The split is at exactly that boundary, and every statement is unchanged. What changed is that the
// question is now asked in one place instead of being implied by the order of 481 lines.
//
// keyDownForOpenSurface returns nothing rather than a failure when no surface took the key. That is
// not a stylistic choice: a failure would propagate to the host as a refused key press, so every key
// the editor did not bind would read as an error rather than as unhandled.

#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_notices.hpp"
#include "seam/native_ui/editor_text_target.hpp"

#include "seam/application/arrangement_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/view_commands.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace seam::native_ui {

std::optional<core::Result<void>>
NativeEditorController::keyDownForOpenSurface(const KeyEvent& event) {
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
  return std::nullopt;
}

core::Result<void> NativeEditorController::keyDownGlobalShortcut(const KeyEvent& event) {
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

}  // namespace seam::native_ui
