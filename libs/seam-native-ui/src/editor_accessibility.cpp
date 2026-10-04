// The editor accessibility tree and dispatch, extracted from editor_controller.cpp.
//
// SEAM-BETA-P2-01 records that this controller concentrated several unrelated state machines in
// one file, and names accessibility dispatch as one of the boundaries worth taking. This is that
// boundary and nothing more: the six methods below moved verbatim, with no behaviour change and no
// signature change, so what they do is still whatever the controller did. They are the accessibility
// cluster specifically -- rebuilding the semantic tree, dispatching a semantic action, following a
// focus move, refusing a list entry, and setting a value through one -- which is the part a screen
// reader actually exercises and therefore the part most worth reading on its own.
//
// What is deliberately NOT here: the scene model it reads, the panels it dispatches into, and the
// edit commands those panels run. Those stay in editor_controller.cpp for now, because moving them
// would mean moving state with them, and a boundary that drags the whole controller behind it is
// not a boundary.

#include "seam/native_ui/editor_controller.hpp"

#include "seam/native_ui/diagnostic_ids.hpp"
#include "seam/native_ui/list_entry_ids.hpp"

#include "seam/authoring/export_service.hpp"
#include "seam/domain/project.hpp"

#include "seam/application/lyric_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/view_commands.hpp"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::native_ui {

namespace {

// Both helpers moved here with the code that uses them: the export states a cancel is offered in,
// and the semantic id of a technical lane. Each had exactly one use, inside the block below.
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

}  // namespace

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
    root.name = "Pronunciation hint";
    if (replacementInput_) root.name = replacementInput_->diagnostics ? "Find active diagnostics" : replacementInput_->findOnly ? "Find notes" : "Find and replace lyrics";
    if (replacementInput_ && replacementInput_->vibratoField) root.name = "Edit vibrato draft field";
    if (replacementInput_ && replacementInput_->dynamicsTickField) root.name = "Edit region dynamics point field";
    root.bounds = layout_.timeMapTextBounds(logicalWidth_, logicalHeight_, false);
    SemanticNode input; input.id = prefix + "input"; input.role = SemanticRole::TextField;
    input.name = "Space-separated phone symbols; empty clears hint";
    if (replacementInput_) input.name = replacementInput_->diagnostics ? "Nonempty literal diagnostic search query" : replacementInput_->findOnly ? "Nonempty literal note search query" :
        replacementInput_->query ? "Replacement text; empty is allowed" : "Nonempty literal lyric query";
    input.description = replacementInput_ ? replacementInputError_ : hintEditError_;
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
  auto result = dispatchAccessibilityAction(id, action);
  if (result && action == SemanticAction::SetFocus) accessibilityFocusMoved(id);
  return result;
}

void NativeEditorController::accessibilityFocusMoved(std::string_view id) noexcept {
  if (!id.starts_with("editor.vibrato.handle.") && vibratoKeyboardFocus_.has_value()) {
    vibratoKeyboardFocus_.reset();
    repaint();
  }
}

std::optional<core::Error> NativeEditorController::listEntryRefusal(std::string_view element) const {
  // An id names an entry by where it stood in its list and by what it is. The list is rebuilt whenever
  // its owner's data or the editor's notices change, and an entry can go (an eviction, a dismissal, a
  // notice that clears) while the ones after it move up. So an id made for an earlier list is acted
  // on, or given focus, only while it still names the entry at that place; otherwise the click or the
  // key was meant for something that is no longer there, and nothing is touched.
  if (isDiagnosticElementId(element)) {
    const auto named = parseDiagnosticElementId(element);
    if (!named) return named.error();
    const auto& entries = diagnosticPanel_.entries();
    if (named.value().index >= entries.size() ||
        entries[named.value().index].diagnostic.issueIdentity() != named.value().identity) {
      return core::Error{core::ErrorCode::Conflict, "That diagnostic is no longer in the list"};
    }
    return std::nullopt;
  }
  if (element.starts_with(kVoicebankCardIdPrefix)) {
    const auto named = parseListEntryId(kVoicebankCardIdPrefix, element);
    if (!named) return named.error();
    if (named.value().index >= voicebankCards_.size() ||
        voicebankCardIdentity(voicebankCards_[named.value().index]) != named.value().identity) {
      return core::Error{core::ErrorCode::Conflict, "That voicebank is no longer in the list"};
    }
    return std::nullopt;
  }
  if (element.starts_with(kSupportItemIdPrefix)) {
    const auto named = parseListEntryId(kSupportItemIdPrefix, element);
    if (!named) return named.error();
    const auto& support = recoverySupportPanel_.view();
    const auto selectable = support.mode == RecoverySupportMode::Reports;
    const auto& items = support.items;
    if (named.value().index >= items.size() ||
        supportItemIdentity(items[named.value().index], selectable) != named.value().identity) {
      return core::Error{core::ErrorCode::Conflict, "That report is no longer in the list"};
    }
    return std::nullopt;
  }
  if (element.starts_with(kAudioDeviceIdPrefix)) {
    const auto named = parseListEntryId(kAudioDeviceIdPrefix, element);
    if (!named) return named.error();
    const auto& devices = audioSettings_.devices;
    if (named.value().index >= devices.size() ||
        audioDeviceIdentity(devices[named.value().index]) != named.value().identity) {
      return core::Error{core::ErrorCode::Conflict, "That audio device is no longer in the list"};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

core::Result<void> NativeEditorController::dispatchAccessibilityAction(
    std::string_view id, SemanticAction action) {
  constexpr std::string_view vibratoPrefix{"editor.vibrato.handle."};
  if (id.starts_with(vibratoPrefix)) {
    const auto selected = session_.selection().noteIds();
    if (selected.size() != 1U) {
      return core::failure(core::ErrorCode::Conflict,
                           "Vibrato handle target is no longer selected");
    }
    std::optional<VibratoHandleKind> kind;
    for (const auto candidate : availableVibratoHandles()) {
      if (vibratoHandleSemanticId(selected.front(), candidate) == id) {
        kind = candidate;
        break;
      }
    }
    if (!kind) {
      return core::failure(core::ErrorCode::Conflict,
                           "Vibrato handle target is no longer available");
    }
    if (action != SemanticAction::SetFocus &&
        action != SemanticAction::Activate) {
      return core::failure(core::ErrorCode::Unsupported,
                           "Vibrato handle supports focus and activate actions");
    }
    rebuildAccessibilityTree();
    return accessibilityTree_.dispatch(
        id, action,
        [this, kind](std::string_view element,
                     SemanticAction) -> core::Result<void> {
          const auto focused = accessibilityTree_.setFocus(element);
          if (!focused) return focused;
          vibratoKeyboardFocus_ = *kind;
          repaint();
          return core::success();
        });
  }
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
  if (sampleMicroscopeOpen()) {
    if (!id.starts_with("microscope."))
      return core::failure(core::ErrorCode::Conflict, "Close sample microscope before using background controls");
    rebuildAccessibilityTree();
  }
  return accessibilityTree_.dispatch(
      id, action,
      [this](std::string_view element, SemanticAction requested) {
        // Before anything acts, and before the generic request for focus below takes the focus for
        // the id and clears the editor's own: an id that names an entry of a list that is rebuilt
        // must still name the entry at its place.
        if (const auto refusal = listEntryRefusal(element)) return core::Result<void>{*refusal};
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
        if (requested == SemanticAction::Activate && element == "microscope.details")
          return microscopeDetailsAction(0U);
        if (requested == SemanticAction::Activate && element == "microscope.previous")
          return microscopeDetailsAction(1U);
        if (requested == SemanticAction::Activate && element == "microscope.next")
          return microscopeDetailsAction(2U);
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
          return toggleBounceTiming();
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
        if (element.starts_with(kAudioDeviceIdPrefix)) {
          // listEntryRefusal has shown the id to be whole and to name the device where it stands.
          const auto named = parseListEntryId(kAudioDeviceIdPrefix, element);
          if (!named) return core::Result<void>{named.error()};
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Accessibility action is not implemented");
          }
          return selectAudioDevice(named.value().index);
        }
        if ((element == "audio.settings" || element == "audio.diagnostics" ||
             element == "audio.sample-rate" ||
             element == "audio.block-frames" ||
             element == "audio.channels") &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element == "support.panel" &&
            requested == SemanticAction::SetFocus) {
          return core::success();
        }
        if (element.starts_with(kSupportItemIdPrefix)) {
          // listEntryRefusal has shown the id to be whole and to name the report where it stands.
          const auto named = parseListEntryId(kSupportItemIdPrefix, element);
          if (!named) return core::Result<void>{named.error()};
          const auto index = named.value().index;
          const auto& support = recoverySupportPanel_.view();
          if (index >= support.items.size()) {
            return core::failure(core::ErrorCode::Conflict,
                                 "That report is no longer in the list");
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
        if (element == "support.track.previous" ||
            element == "support.track.next") {
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Track navigation only supports activation");
          }
          return selectAdjacentVocalTrack(
              element == "support.track.next" ? 1 : -1);
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
          auto added = addRegionToSelectedTrack();
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
        if (element.starts_with(kVoicebankCardIdPrefix)) {
          // listEntryRefusal has shown the id to be whole and to name the card where it stands.
          const auto named = parseListEntryId(kVoicebankCardIdPrefix, element);
          if (!named) return core::Result<void>{named.error()};
          if (requested == SemanticAction::SetFocus) return core::success();
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Accessibility action is not implemented");
          }
          return selectVoicebankCard(named.value().index);
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
        if (isDiagnosticElementId(element)) {
          // listEntryRefusal has shown the id to be whole and to name the issue where it stands (see
          // diagnostic_ids.hpp), so what is left is to act on that issue.
          const auto parsed = parseDiagnosticElementId(element);
          if (!parsed) return core::Result<void>{parsed.error()};
          const auto& named = parsed.value();
          const auto& entries = diagnosticPanel_.entries();
          if (named.index >= entries.size()) {
            return core::failure(core::ErrorCode::Conflict,
                                 "That diagnostic is no longer in the list");
          }
          if (requested == SemanticAction::SetFocus) return core::success();
          const auto& actions = entries[named.index].diagnostic.actions;
          if (named.action) {
            if (requested != SemanticAction::Activate) {
              return core::failure(core::ErrorCode::Unsupported,
                                   "Diagnostic action only supports activation");
            }
            const auto matchingAction = std::find_if(
                actions.begin(), actions.end(), [&named](const auto candidate) {
                  return authoring::toString(candidate) == named.name;
                });
            if (matchingAction == actions.end()) {
              return core::failure(core::ErrorCode::InvalidArgument,
                                   "Diagnostic action is not available");
            }
            return activateDiagnostic(named.index, *matchingAction);
          }
          if (requested != SemanticAction::Activate) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Accessibility action is not implemented");
          }
          if (actions.empty()) {
            return core::failure(core::ErrorCode::Unsupported,
                                 "Diagnostic has no recovery action");
          }
          return activateDiagnostic(named.index, actions.front());
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
  if (sampleMicroscopeOpen())
    return core::failure(core::ErrorCode::Conflict, "Sample inspection does not edit background values");
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


}  // namespace seam::native_ui
