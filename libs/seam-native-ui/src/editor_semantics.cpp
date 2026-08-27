#include "seam/native_ui/editor_semantics.hpp"

#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"

#include <array>
#include <algorithm>
#include <cmath>

namespace seam::native_ui {

namespace {

bool exportCancellable(authoring::ExportState state) noexcept {
  return state == authoring::ExportState::Preflight ||
         state == authoring::ExportState::Staging ||
         state == authoring::ExportState::Prepared;
}

}

std::string_view semanticRoleName(SemanticRole role) noexcept {
  switch (role) {
    case SemanticRole::Window: return "window";
    case SemanticRole::Panel: return "panel";
    case SemanticRole::Toolbar: return "toolbar";
    case SemanticRole::Button: return "button";
    case SemanticRole::Timeline: return "timeline";
    case SemanticRole::Note: return "note";
    case SemanticRole::Lane: return "lane";
    case SemanticRole::Status: return "status";
    case SemanticRole::TextField: return "text-field";
  }
  return "unknown";
}

std::string_view semanticActionName(SemanticAction action) noexcept {
  switch (action) {
    case SemanticAction::Activate: return "activate";
    case SemanticAction::SetFocus: return "set-focus";
    case SemanticAction::EditText: return "edit-text";
    case SemanticAction::Toggle: return "toggle";
  }
  return "unknown";
}

SemanticNode EditorSemanticTree::noteNode(const ui::NoteVisual& note,
                                          EditorSceneLayout layout) {
  auto noteBounds = note.bounds;
  noteBounds.y += layout.contentTop();
  return SemanticNode{
      .id = "note." + note.noteId.toString(),
      .role = SemanticRole::Note,
      .name = note.lyric.empty() ? "Note" : "Note " + note.lyric,
      .value = std::to_string(note.midiKey),
      .bounds = noteBounds,
      .enabled = true,
      .focused = false,
      .selected = note.selected,
      .actions = {SemanticAction::Activate, SemanticAction::SetFocus,
                  SemanticAction::EditText},
      .children = {},
      .editableValue = note.lyric,
      .description = "MIDI " + std::to_string(note.midiKey) + " / " +
                    std::to_string(note.duration.value()) +
                    " ticks; Enter edits lyric",
  };
}

SemanticNode EditorSemanticTree::build(const EditorSceneState& state,
                                       const ui::PianoRollModel& model,
                                       EditorSceneLayout layout,
                                       bool includeOffscreenNotes,
                                       bool includeNotes) {
  RenderStatusPanelModel renderStatus;
  renderStatus.update(state.renderStatus);
  const auto diagnosticInset =
      layout.diagnosticHeight(!state.diagnostics.empty());
  const auto exportInset =
      layout.exportHeight(state.exportProgress.totalFiles != 0U);
  const auto overlayInset = diagnosticInset + exportInset;
  const auto technical = resolveTechnicalLaneHeights(TechnicalLaneLayoutInput{
      .presentation = state.technicalLanes,
      .populated = { !state.phonemes.tokens.empty(), !state.unitOverrides.empty(),
                     !state.seamOverrides.empty(), !state.pitchAutomation.empty() },
      .previewHeights = { layout.phonemeLaneHeight, layout.unitLaneHeight,
                          layout.seamLaneHeight, layout.automationLaneHeight },
      .contentTop = layout.contentTop(),
      .contentBottom = state.logicalHeight - layout.statusHeight - overlayInset,
  });
  const auto contentHeight = std::max(1.0, technical.pianoBottom - layout.contentTop());
  const auto laneGeometry = EditorSceneLayout::TechnicalLaneGeometry{
      .pianoBottom = technical.pianoBottom,
      .phonemeTop = technical.pianoBottom,
      .phonemeHeight = technical.values[0U],
      .unitTop = technical.pianoBottom + technical.values[0U],
      .unitHeight = technical.values[1U],
      .seamTop = technical.pianoBottom + technical.values[0U] + technical.values[1U],
      .seamHeight = technical.values[2U],
      .pitchTop = technical.pianoBottom + technical.values[0U] + technical.values[1U] + technical.values[2U],
      .pitchHeight = technical.values[3U],
      .bottom = state.logicalHeight - layout.statusHeight - overlayInset,
  };
  const auto characterFull =
      state.characterMode == domain::CharacterDisplayMode::Full &&
      state.voiceIdentity.characterActive &&
      state.characterPortrait != nullptr;
  const auto portraitVisible =
      !layout.compactToolbar(state.logicalWidth) &&
      state.characterMode == domain::CharacterDisplayMode::Minimal &&
      state.voiceIdentity.characterActive &&
      state.characterPortrait != nullptr;
  const auto audioSettingsVisible = state.audioSettings.visible;
  const auto arrangementVisible =
      !state.voicebankBrowserVisible &&
      !audioSettingsVisible &&
      state.characterMode == domain::CharacterDisplayMode::Off &&
      !state.arrangementTracks.empty();
  const auto dockVisible = audioSettingsVisible || characterFull || arrangementVisible ||
                           state.voicebankBrowserVisible;
  const auto editorRight = std::max(
      layout.keyboardWidth,
      std::max(layout.keyboardWidth + layout.minimumTimelineWidth,
               state.logicalWidth -
                   (dockVisible ? layout.characterDockWidth : 0.0)));
  SemanticNode root{
      .id = "editor",
      .role = SemanticRole::Window,
      .name = "Project SEAM editor",
      .value = state.projectName,
      .bounds = ui::Rect{0.0, 0.0, state.logicalWidth, state.logicalHeight},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
      .children = {},
      .description = "Use Tab and Shift-Tab to move keyboard focus",
  };
  if (state.sampleMicroscope.has_value() &&
      state.sampleMicroscope->model != nullptr) {
    const auto& microscope = *state.sampleMicroscope;
    const auto& microscopeModel = *microscope.model;
    const auto panelBounds =
        layout.microscopePanelBounds(root.bounds.width, root.bounds.height);
    SemanticNode panel{
        .id = "microscope.panel",
        .role = SemanticRole::Panel,
        .name = "Sample microscope",
        .value = microscope.unitId + " / " +
                 (microscope.destinationContext.empty()
                      ? std::string{"Destination unknown"}
                      : microscope.destinationContext),
        .bounds = panelBounds,
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    };
    const auto closeBounds =
        layout.microscopeCloseBounds(root.bounds.width, root.bounds.height);
    panel.children.push_back(SemanticNode{
        .id = "microscope.close",
        .role = SemanticRole::Button,
        .name = "Close sample microscope",
        .value = "Escape or double-click",
        .bounds = closeBounds,
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
        .children = {},
    });
    panel.children.push_back(SemanticNode{
        .id = "microscope.waveform",
        .role = SemanticRole::Timeline,
        .name = "Sample waveform",
        .value = std::to_string(microscopeModel.waveform().size()) +
                 " waveform columns",
        .bounds = microscopeModel.waveformBounds(),
        .enabled = true,
        .focused = false,
        .actions = microscope.canPlay
                       ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                     SemanticAction::SetFocus}
                       : std::vector<SemanticAction>{SemanticAction::SetFocus},
        .children = {},
    });
    const auto& spectrogram = microscopeModel.spectrogram();
    panel.children.push_back(SemanticNode{
        .id = "microscope.spectrogram",
        .role = SemanticRole::Timeline,
        .name = "Sample spectrogram",
        .value = std::to_string(spectrogram.columns) + " columns / " +
                 std::to_string(spectrogram.bins) + " bins",
        .bounds = microscopeModel.spectrogramBounds(),
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    });
    panel.children.push_back(SemanticNode{
        .id = "microscope.markers",
        .role = SemanticRole::Status,
        .name = "Acoustic markers",
        .value = std::to_string(microscopeModel.markers().size()) +
                 " markers",
        .bounds = microscopeModel.waveformBounds(),
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    });
    panel.children.push_back(SemanticNode{
        .id = "microscope.pitch-marks",
        .role = SemanticRole::Status,
        .name = "Pitch marks",
        .value = std::to_string(microscopeModel.pitchMarks().size()) +
                 " pitch marks",
        .bounds = microscopeModel.waveformBounds(),
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    });
    root.children.push_back(std::move(panel));
    return root;
  }
  SemanticNode transportToolbar{
      .id = "toolbar.controls",
      .role = SemanticRole::Toolbar,
      .name = "Transport controls",
      .value = {},
      .bounds = ui::Rect{0.0, 0.0, root.bounds.width, layout.toolbarHeight},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
      .children = {},
  };
  transportToolbar.children.push_back(SemanticNode{
      .id = "toolbar.transport",
      .role = SemanticRole::Button,
      .name = state.playing ? "Pause playback" : "Play playback",
      .value = state.playing ? "Playing" : "Stopped",
      .bounds = layout.transportBoundsForWidth(state.logicalWidth),
      .enabled = state.renderStatus.hasAudibleAudio,
      .focused = false,
      .actions = state.renderStatus.hasAudibleAudio
                     ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                   SemanticAction::Toggle,
                                                   SemanticAction::SetFocus}
                     : std::vector<SemanticAction>{SemanticAction::SetFocus},
      .children = {},
  });
  const auto stopBounds = layout.stopBoundsForWidth(state.logicalWidth);
  transportToolbar.children.push_back(SemanticNode{
      .id = "toolbar.stop",
      .role = SemanticRole::Button,
      .name = "Stop playback",
      .value = {},
      .bounds = stopBounds,
      .enabled = state.renderStatus.hasAudibleAudio,
      .focused = false,
      .actions = state.renderStatus.hasAudibleAudio
                     ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                   SemanticAction::SetFocus}
                     : std::vector<SemanticAction>{SemanticAction::SetFocus},
      .children = {},
  });
  transportToolbar.children.push_back(SemanticNode{
      .id = "toolbar.tempo",
      .role = SemanticRole::Status,
      .name = "Project tempo",
      .value = "BPM " + std::to_string(
                              static_cast<int>(std::lround(state.tempoBpm))),
      .bounds = layout.bpmBoundsForWidth(state.logicalWidth),
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
      .children = {},
  });
  transportToolbar.children.push_back(SemanticNode{
      .id = "voice.identity",
      .role = SemanticRole::Status,
      .name = "Active voice identity",
      .value = state.voiceIdentity.name + " / " +
               std::string{voiceIdentityStateName(state.voiceIdentity.state)},
      .bounds = ui::Rect{std::max(0.0, state.logicalWidth - 206.0), 4.0,
                         132.0, 34.0},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
      .children = {},
      .description = state.voiceIdentity.identity +
                     (state.voiceIdentity.recovery.empty()
                          ? ""
                          : " / " + state.voiceIdentity.recovery),
  });
  root.children.push_back(std::move(transportToolbar));
  const auto batchLyricsBounds =
      layout.batchLyricsBoundsForWidth(state.logicalWidth, portraitVisible);
  if (batchLyricsBounds.width > 0.0) {
    root.children.push_back(SemanticNode{
        .id = "toolbar.batch-lyrics",
        .role = SemanticRole::Button,
        .name = "Distribute lyrics",
        .value = std::to_string(state.selectedNoteCount) + " selected notes",
        .bounds = batchLyricsBounds,
        .enabled = state.selectedNoteCount > 0U,
        .focused = false,
        .selected = false,
        .actions = state.selectedNoteCount > 0U
                       ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                     SemanticAction::SetFocus}
                       : std::vector<SemanticAction>{SemanticAction::SetFocus},
        .children = {},
        .description = "Shift-L opens text input and distributes whitespace-separated syllables across selected notes",
    });
  }
  const auto loopBounds =
      layout.loopBoundsForWidth(state.logicalWidth, portraitVisible);
  if (state.loopAvailable && loopBounds.width > 0.0) {
    root.children.push_back(SemanticNode{
        .id = "toolbar.loop",
        .role = SemanticRole::Button,
        .name = state.loopEnabled ? "Disable loop" : "Enable loop",
        .value = state.loopEnabled ? "On" : "Off",
        .bounds = loopBounds,
        .enabled = state.renderStatus.hasAudibleAudio,
        .focused = false,
        .selected = state.loopEnabled,
        .actions = state.renderStatus.hasAudibleAudio
                       ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                     SemanticAction::Toggle,
                                                     SemanticAction::SetFocus}
                       : std::vector<SemanticAction>{SemanticAction::SetFocus},
        .children = {},
        .description = "Loop the published audio timeline; press L to toggle",
    });
  }
  root.children.push_back(SemanticNode{
      .id = "timeline",
      .role = SemanticRole::Timeline,
      .name = "Arrangement timeline",
      .value = std::to_string(state.revision),
      .bounds = ui::Rect{0.0, layout.contentTop(), editorRight,
                         contentHeight},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
  });
  const auto noteVisuals = includeNotes
                               ? (includeOffscreenNotes ? model.allNotes()
                                                        : model.visibleNotes())
                               : std::vector<ui::NoteVisual>{};
  for (const auto& note : noteVisuals) {
    if (!includeOffscreenNotes) {
      auto noteBounds = note.bounds;
      noteBounds.y += layout.contentTop();
      const auto clip = ui::Rect{0.0, layout.contentTop(), editorRight,
                                 contentHeight};
      const auto left = std::max(noteBounds.x, clip.x);
      const auto top = std::max(noteBounds.y, clip.y);
      const auto right = std::min(noteBounds.right(), clip.right());
      const auto bottom = std::min(noteBounds.bottom(), clip.bottom());
      if (right <= left || bottom <= top) continue;
      const auto clipped = ui::Rect{left, top, right - left, bottom - top};
      auto node = noteNode(note, layout);
      node.bounds = clipped;
      root.children.push_back(std::move(node));
      continue;
    }
    root.children.push_back(noteNode(note, layout));
  }
  struct OverlapGroup final {
    std::size_t index{0U};
    std::size_t memberCount{0U};
    ui::Rect bounds;
    bool initialized{false};
  };
  std::vector<OverlapGroup> overlapGroups;
  const auto overlapVisuals = noteVisuals.empty() ? model.allNotes()
                                                   : noteVisuals;
  for (const auto& note : overlapVisuals) {
    if (note.overlapMemberCount <= 1U) continue;
    auto group = std::find_if(
        overlapGroups.begin(), overlapGroups.end(), [&note](const OverlapGroup& value) {
          return value.index == note.overlapGroup;
        });
    if (group == overlapGroups.end()) {
      overlapGroups.push_back(OverlapGroup{.index = note.overlapGroup,
                                           .memberCount = note.overlapMemberCount});
      group = std::prev(overlapGroups.end());
    }
    auto bounds = note.bounds;
    bounds.y += layout.contentTop();
    if (!group->initialized) {
      group->bounds = bounds;
      group->initialized = true;
    } else {
      const auto left = std::min(group->bounds.x, bounds.x);
      const auto top = std::min(group->bounds.y, bounds.y);
      const auto right = std::max(group->bounds.right(), bounds.right());
      const auto bottom = std::max(group->bounds.bottom(), bounds.bottom());
      group->bounds = ui::Rect{left, top, right - left, bottom - top};
    }
  }
  for (const auto& group : overlapGroups) {
    if (!group.initialized) continue;
    const auto clip = ui::Rect{0.0, layout.contentTop(), editorRight,
                               contentHeight};
    const auto left = std::max(group.bounds.x, clip.x);
    const auto top = std::max(group.bounds.y, clip.y);
    const auto right = std::min(group.bounds.right(), clip.right());
    const auto bottom = std::min(group.bounds.bottom(), clip.bottom());
    if (right <= left || bottom <= top) continue;
    root.children.push_back(SemanticNode{
        .id = "overlap-group." + std::to_string(group.index),
        .role = SemanticRole::Panel,
        .name = "Overlapping notes",
        .value = std::to_string(group.memberCount) + " notes",
        .bounds = ui::Rect{left, top, right - left, bottom - top},
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
        .children = {},
        .description = "Activate to select the next overlapping note",
    });
  }
  const auto detailNote = state.hoveredNote.has_value() ? state.hoveredNote
                                                        : state.focusedNote;
  if (state.detail.has_value() && detailNote.has_value() &&
      state.detail->kind == EditorDetailKind::Note) {
    const auto visibleNotes = model.visibleNotes();
    const auto hovered = std::find_if(
        visibleNotes.begin(), visibleNotes.end(), [detailNote](const ui::NoteVisual& note) {
          return note.noteId == *detailNote;
        });
    if (hovered != visibleNotes.end()) {
      auto noteBounds = hovered->bounds;
      noteBounds.y += layout.contentTop();
      root.children.push_back(SemanticNode{
          .id = "detail.note." + state.detail->stableId,
          .role = SemanticRole::Status,
          .name = "Note detail",
          .value = state.detail->value,
          .bounds = layout.noteDetailBounds(noteBounds, editorRight),
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::SetFocus},
          .children = {},
          .description = "Full note lyric",
      });
    }
  }
  auto laneTop = laneGeometry.phonemeTop;
  const auto statusTop = root.bounds.bottom() - layout.statusHeight;
  const auto laneBottom = statusTop - overlayInset;
  const auto laneValue = [&state](std::string_view name) {
    if (name == "phoneme") {
      return std::to_string(state.phonemes.tokens.size()) + " tokens / " +
             std::to_string(state.phonemes.warnings.size()) + " warnings";
    }
    if (name == "unit") {
      return std::to_string(state.unitOverrides.size()) + " overrides";
    }
    if (name == "seam") {
      return std::to_string(state.seamOverrides.size()) + " overrides" +
             (state.selectedSeam.has_value() ? " / boundary selected" :
                                                " / no boundary selected");
    }
    return std::to_string(state.pitchAutomation.size()) + " automation points";
  };
  const auto laneDescription = [](std::string_view name) {
    if (name == "phoneme") {
      return std::string{"Generated phoneme tokens and diagnostics"};
    }
    if (name == "unit") {
      return std::string{"Source unit and renderer overrides"};
    }
    if (name == "seam") {
      return std::string{"Seam overlap, phase, and envelope overrides"};
    }
    return std::string{"Pitch automation points and interpolation"};
  };
  const auto clipToRoot = [&root](ui::Rect bounds) {
    const auto left = std::max(bounds.x, root.bounds.x);
    const auto top = std::max(bounds.y, root.bounds.y);
    const auto right = std::min(bounds.right(), root.bounds.right());
    const auto bottom = std::min(bounds.bottom(), root.bounds.bottom());
    return ui::Rect{left, top, std::max(0.0, right - left),
                    std::max(0.0, bottom - top)};
  };
  for (const auto& lane : {std::pair<std::string, double>{
                               "phoneme", laneGeometry.phonemeHeight},
                           {"unit", laneGeometry.unitHeight},
                           {"seam", laneGeometry.seamHeight},
                           {"pitch", laneGeometry.pitchHeight}}) {
    const auto visibleHeight = std::max(
        0.0, std::min(lane.second, laneBottom - laneTop));
    if (visibleHeight <= 0.0) {
      laneTop += lane.second;
      continue;
    }
    const auto laneBounds = clipToRoot(
        ui::Rect{0.0, laneTop, editorRight, visibleHeight});
    if (laneBounds.width <= 0.0 || laneBounds.height <= 0.0) {
      laneTop += lane.second;
      continue;
    }
    root.children.push_back(SemanticNode{
        .id = "lane." + lane.first,
        .role = SemanticRole::Lane,
        .name = lane.first + " lane",
        .value = laneValue(lane.first),
        .bounds = laneBounds,
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus, SemanticAction::Toggle},
        .description = laneDescription(lane.first),
    });
    laneTop += lane.second;
  }
  if (characterFull && editorRight < root.bounds.width) {
    root.children.push_back(SemanticNode{
        .id = "character.dock",
        .role = SemanticRole::Panel,
        .name = state.characterName.empty() ? "Character dock"
                                            : "Character " + state.characterName,
        .value = "Full character presentation",
        .bounds = ui::Rect{editorRight, layout.toolbarHeight,
                           root.bounds.width - editorRight,
                           std::max(0.0, laneBottom - layout.toolbarHeight)},
        .enabled = true,
        .focused = false,
      .actions = {SemanticAction::SetFocus},
    });
  }
  if (state.voicebankBrowserVisible && editorRight < root.bounds.width) {
    SemanticNode panel{
        .id = "voicebank.panel",
        .role = SemanticRole::Panel,
        .name = "Voicebank browser",
        .value = std::to_string(state.voicebankCards.size()) + " voicebanks",
        .bounds = ui::Rect{editorRight, layout.toolbarHeight,
                           root.bounds.width - editorRight,
                           std::max(0.0, laneBottom - layout.toolbarHeight)},
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    };
    const auto cardX = editorRight + layout.voicebankCardInsetX;
    const auto cardWidth = std::max(
        1.0, root.bounds.width - editorRight -
                  layout.voicebankCardInsetX * 2.0);
    for (std::size_t index = 0U; index < state.voicebankCards.size(); ++index) {
      const auto& card = state.voicebankCards[index];
      const auto cardY = layout.toolbarHeight + layout.voicebankCardTop +
                         static_cast<double>(index) *
                             (layout.voicebankCardHeight +
                              layout.voicebankCardGap);
      const auto cardBottom = std::min(
          cardY + layout.voicebankCardHeight, laneBottom);
      if (cardY >= laneBottom || cardBottom <= cardY) continue;
      const auto selected = state.inspector.valid && state.inspector.vocal &&
                            state.inspector.voicebank.id == card.id &&
                            state.inspector.voicebank.version == card.version &&
                            state.inspector.voicebank.contentHash == card.contentHash;
      panel.children.push_back(SemanticNode{
          .id = "voicebank.card." + std::to_string(index),
          .role = SemanticRole::Button,
          .name = card.displayName,
          .value = card.version + " / " + card.trustLabel +
                   (selected ? " / Selected" : ""),
          .bounds = ui::Rect{cardX, cardY, cardWidth, cardBottom - cardY},
          .enabled = card.selectable,
          .focused = false,
          .selected = selected,
          .actions = card.selectable
                         ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                       SemanticAction::SetFocus}
                         : std::vector<SemanticAction>{SemanticAction::SetFocus},
          .children = {},
      });
    }
    root.children.push_back(std::move(panel));
  }
  if (audioSettingsVisible && editorRight < root.bounds.width) {
    SemanticNode panel{
        .id = "audio.settings",
        .role = SemanticRole::Panel,
        .name = "Audio settings",
        .value = state.audioSettings.current.deviceId,
        .bounds = ui::Rect{editorRight, layout.toolbarHeight,
                           root.bounds.width - editorRight,
                           std::max(0.0, laneBottom - layout.toolbarHeight)},
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    };
    const auto panelTop = layout.toolbarHeight;
    const auto panelBottom = laneBottom;
    const auto panelWidth = std::max(0.0, root.bounds.width - editorRight);
    const auto rowX = editorRight + layout.audioSettingsInsetX;
    const auto rowWidth = std::max(
        1.0, panelWidth - layout.audioSettingsInsetX * 2.0);
    const auto clippedBounds = [editorRight, panelTop, panelBottom,
                                rootRight = root.bounds.right()](ui::Rect bounds) {
      const auto right = std::min(bounds.right(), rootRight);
      const auto bottom = std::min(bounds.bottom(), panelBottom);
      bounds.x = std::max(bounds.x, editorRight);
      bounds.y = std::max(bounds.y, panelTop);
      bounds.width = std::max(0.0, right - bounds.x);
      bounds.height = std::max(0.0, bottom - bounds.y);
      return bounds;
    };
    const auto rowY = layout.toolbarHeight + layout.audioSettingsRowTop;
    for (std::size_t index = 0U; index < state.audioSettings.devices.size();
         ++index) {
      const auto& device = state.audioSettings.devices[index];
      const auto bounds = clippedBounds(ui::Rect{
          rowX,
          rowY + static_cast<double>(index) *
                     (layout.audioSettingsRowHeight + layout.audioSettingsRowGap),
          rowWidth,
          layout.audioSettingsRowHeight,
      });
      if (bounds.width <= 0.0 || bounds.height <= 0.0) continue;
      panel.children.push_back(SemanticNode{
          .id = "audio.device." + std::to_string(index),
          .role = SemanticRole::Button,
          .name = device.name.empty() ? device.id : device.name,
          .value = device.physical ? "Physical device" : "Fallback device",
          .bounds = bounds,
          .enabled = true,
          .focused = false,
          .selected = device.selected,
          .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
          .children = {},
      });
    }
    const auto fieldsTop = rowY + static_cast<double>(state.audioSettings.devices.size()) *
                                          (layout.audioSettingsRowHeight +
                                           layout.audioSettingsRowGap);
    const std::array<std::string, 3> fieldIds{
        "audio.sample-rate", "audio.block-frames", "audio.channels"};
    const std::array<std::string, 3> fieldNames{
        "Sample rate", "Block size", "Output channels"};
    const std::array<std::string, 3> fieldValues{
        std::to_string(state.audioSettings.current.sampleRate) + " Hz",
        std::to_string(state.audioSettings.current.blockFrames) + " frames",
        std::to_string(state.audioSettings.current.outputChannels) + " channels"};
    for (std::size_t index = 0U; index < fieldIds.size(); ++index) {
      const auto bounds = clippedBounds(ui::Rect{
          rowX,
          fieldsTop + static_cast<double>(index) *
                          (layout.audioSettingsRowHeight +
                           layout.audioSettingsRowGap),
          rowWidth,
          layout.audioSettingsRowHeight,
      });
      if (bounds.width <= 0.0 || bounds.height <= 0.0) continue;
      panel.children.push_back(SemanticNode{
          .id = fieldIds[index],
          .role = SemanticRole::Button,
          .name = fieldNames[index],
          .value = fieldValues[index],
          .bounds = bounds,
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
          .children = {},
      });
    }
    const auto statsBounds = clippedBounds(ui::Rect{
        rowX,
        panelBottom - layout.audioSettingsStatsBottomInset,
        rowWidth,
        layout.audioSettingsStatsHeight,
    });
    if (statsBounds.width > 0.0 && statsBounds.height > 0.0) {
      panel.children.push_back(SemanticNode{
          .id = "audio.diagnostics",
          .role = SemanticRole::Status,
          .name = "Audio diagnostics",
          .value = "Underflow " +
                   std::to_string(state.audioSettings.underflowFrames) +
                   " / XRun " + std::to_string(state.audioSettings.xruns) +
                   (state.audioSettings.diagnostic.empty()
                        ? std::string{}
                        : " / " + state.audioSettings.diagnostic),
          .bounds = statsBounds,
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::SetFocus},
          .children = {},
      });
    }
    root.children.push_back(std::move(panel));
  }
  if (arrangementVisible && editorRight < root.bounds.width) {
    SemanticNode panel{
        .id = "arrangement.panel",
        .role = SemanticRole::Panel,
        .name = "Arrangement and track inspector",
        .value = state.inspector.valid ? state.inspector.name : "No track selected",
        .bounds = ui::Rect{editorRight, layout.toolbarHeight,
                           root.bounds.width - editorRight,
                           std::max(0.0, laneBottom - layout.toolbarHeight)},
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    };
    const std::array<std::string_view, 5> actionIds{
        "arrangement.add-track", "arrangement.add-region",
        "arrangement.rename", "arrangement.move-up",
        "arrangement.move-down"};
    const std::array<std::string_view, 5> actionNames{
        "Add vocal track", "Add vocal region", "Rename selected arrangement item",
        "Move selected track up", "Move selected track down"};
    const auto panelWidth = std::max(0.0, root.bounds.width - editorRight);
    const auto rowWidth = std::max(
        0.0, panelWidth - layout.trackRowInsetX * 2.0);
    const auto panelTop = layout.toolbarHeight;
    const auto panelBottom = laneBottom;
    const auto clippedBounds = [editorRight, panelTop, panelBottom,
                                rootRight = root.bounds.right()](ui::Rect bounds) {
      const auto right = std::min(bounds.right(), rootRight);
      const auto bottom = std::min(bounds.bottom(), panelBottom);
      bounds.x = std::max(bounds.x, editorRight);
      bounds.y = std::max(bounds.y, panelTop);
      bounds.width = std::max(0.0, right - bounds.x);
      bounds.height = std::max(0.0, bottom - bounds.y);
      return bounds;
    };
    for (std::size_t index = 0U; index < actionIds.size(); ++index) {
      const auto enabled = index == 0U ||
                           (state.inspector.valid &&
                            (index == 1U ? state.inspector.vocal : true));
      panel.children.push_back(SemanticNode{
          .id = std::string{actionIds[index]},
          .role = SemanticRole::Button,
          .name = std::string{actionNames[index]},
          .value = {},
          .bounds = layout.arrangementActionBoundsForWidth(
              state.logicalWidth, index),
          .enabled = enabled,
          .focused = false,
          .actions = enabled
                         ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                       SemanticAction::SetFocus}
                         : std::vector<SemanticAction>{SemanticAction::SetFocus},
          .children = {},
      });
    }
    double trackY = layout.toolbarHeight + layout.trackListTop;
    for (const auto& track : state.arrangementTracks) {
      const auto trackBounds = clippedBounds(ui::Rect{
          editorRight + layout.trackRowInsetX,
          trackY + layout.trackRowTopOffset,
          rowWidth,
          layout.trackRowHeight,
      });
      SemanticNode trackNode{
          .id = "arrangement.track." + track.id.toString(),
          .role = SemanticRole::Panel,
          .name = track.name,
          .value = track.vocal ? "Vocal track" : "Audio track",
          .bounds = trackBounds,
          .enabled = true,
          .focused = false,
          .selected = track.selected,
          .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
          .children = {},
      };
      trackY += layout.trackRowAdvance;
      for (const auto& region : track.regions) {
        const auto regionBounds = clippedBounds(ui::Rect{
            editorRight + layout.trackRowInsetX,
            trackY,
            rowWidth,
            std::max(1.0, layout.regionAdvance -
                                layout.regionBottomPadding),
        });
        trackNode.children.push_back(SemanticNode{
            .id = "arrangement.region." + region.id.toString(),
            .role = SemanticRole::Panel,
            .name = region.name,
            .value = std::to_string(region.noteCount) + " notes",
            .bounds = regionBounds,
            .enabled = true,
            .focused = false,
            .selected = region.selected,
            .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
            .children = {},
        });
        trackY += layout.regionAdvance;
      }
      panel.children.push_back(std::move(trackNode));
    }
    if (state.inspector.valid) {
      const auto inspectorTop = std::max(
          layout.toolbarHeight, panelBottom - layout.inspectorHeight);
      const auto firstFieldBaseline =
          inspectorTop + layout.inspectorNameBaseline +
          layout.inspectorNameToFirstFieldAdvance;
      const auto toggleTop = firstFieldBaseline +
                             layout.inspectorFieldAdvance * 3.0 -
                             layout.inspectorFontSize;
      const auto toggleWidth = std::max(
          1.0, (panelWidth - layout.inspectorTextInsetX * 2.0) * 0.5);
      panel.children.push_back(SemanticNode{
          .id = "inspector.mute",
          .role = SemanticRole::Button,
          .name = "Mute selected track",
          .value = state.inspector.muted ? "On" : "Off",
          .bounds = clippedBounds(ui::Rect{
              editorRight + layout.inspectorTextInsetX,
              toggleTop,
              toggleWidth,
              layout.inspectorFieldAdvance,
          }),
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::Activate, SemanticAction::Toggle,
                      SemanticAction::SetFocus},
          .children = {},
      });
      panel.children.push_back(SemanticNode{
          .id = "inspector.solo",
          .role = SemanticRole::Button,
          .name = "Solo selected track",
          .value = state.inspector.solo ? "On" : "Off",
          .bounds = clippedBounds(ui::Rect{
              editorRight + layout.inspectorTextInsetX + toggleWidth,
              toggleTop,
              toggleWidth,
              layout.inspectorFieldAdvance,
          }),
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::Activate, SemanticAction::Toggle,
                      SemanticAction::SetFocus},
          .children = {},
      });
      panel.children.push_back(SemanticNode{
          .id = "inspector.route",
          .role = SemanticRole::Button,
          .name = "Cycle selected track output bus",
          .value = "Bus " + state.inspector.outputRoute.bus.toString(),
          .bounds = clippedBounds(ui::Rect{
              editorRight + layout.inspectorTextInsetX,
              firstFieldBaseline + layout.inspectorFieldAdvance * 4.0 -
                  layout.inspectorFontSize,
              panelWidth - layout.inspectorTextInsetX * 2.0,
              layout.inspectorFieldAdvance,
          }),
          .enabled = true,
          .focused = false,
          .selected = false,
          .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
          .children = {},
          .description = "Activate to cycle the selected track through project output buses",
      });
    }
    root.children.push_back(std::move(panel));
  }
  if (!state.diagnostics.empty()) {
    SemanticNode diagnostics{
        .id = "diagnostics.panel",
        .role = SemanticRole::Panel,
        .name = "Diagnostics",
        .value = std::to_string(state.diagnostics.size()) + " active issues",
        .bounds = layout.diagnosticBounds(root.bounds.width, root.bounds.height,
                                          state.exportProgress.totalFiles != 0U),
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    };
    for (std::size_t index = 0U; index < state.diagnostics.size(); ++index) {
      const auto& diagnostic = state.diagnostics[index];
      const auto presentation = presentDiagnostic(diagnostic);
      diagnostics.children.push_back(SemanticNode{
          .id = "diagnostic." + std::to_string(index) + "." + diagnostic.code,
          .role = SemanticRole::Status,
          .name = presentation.title,
          .value = presentation.impact,
          .bounds = ui::Rect{0.0, diagnostics.bounds.y,
                             root.bounds.width, diagnostics.bounds.height},
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::SetFocus},
          .children = {},
          .description = presentation.technicalDetail,
      });
      const auto actionCount = presentation.primaryActionKinds.size();
      for (std::size_t actionIndex = 0U; actionIndex < actionCount; ++actionIndex) {
        const auto action = presentation.primaryActionKinds[actionIndex];
        const auto actionName = std::string{authoring::toString(action)};
        diagnostics.children.push_back(SemanticNode{
            .id = "diagnostic-action." + std::to_string(index) + "." +
                 actionName,
            .role = SemanticRole::Button,
            .name = diagnosticActionLabel(action),
            .value = diagnosticActionLabel(action),
            .bounds = layout.diagnosticActionBounds(
                root.bounds.width, root.bounds.height,
                state.exportProgress.totalFiles != 0U, actionCount, actionIndex),
            .enabled = true,
            .focused = false,
            .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
            .children = {},
            .description = "Activate to run this diagnostic recovery action",
        });
      }
    }
    root.children.push_back(std::move(diagnostics));
  }
  if (state.exportProgress.totalFiles > 0U) {
    auto exportValue = std::string{authoring::exportStateName(
        state.exportProgress.state)} +
                       " " + std::to_string(state.exportProgress.completedFiles) +
                       "/" + std::to_string(state.exportProgress.totalFiles);
    if (state.exportProgress.state != authoring::ExportState::Committed &&
        state.exportProgress.state != authoring::ExportState::Recovered &&
        !state.exportProgress.currentOutput.empty()) {
      exportValue += " / " + state.exportProgress.currentOutput;
    }
    if (state.lastExport.has_value()) {
      const auto& result = *state.lastExport;
      const auto destination = result.setPath.empty()
                                   ? result.masterPath
                                   : result.setPath;
      exportValue += " / " + destination.string();
      if (!result.receiptPath.empty()) {
        exportValue += " / receipt " + result.receiptPath.string();
      }
    }
    SemanticNode exportProgress{
        .id = "export.progress",
        .role = SemanticRole::Status,
        .name = "Export progress",
        .value = std::move(exportValue),
        .bounds = ui::Rect{0.0, root.bounds.height - layout.statusHeight -
                                      layout.exportStripHeight,
                           root.bounds.width, layout.exportStripHeight},
        .enabled = state.exportProgress.state != authoring::ExportState::Committed,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
        .children = {},
    };
    if (exportCancellable(state.exportProgress.state)) {
      exportProgress.children.push_back(SemanticNode{
          .id = "export.cancel",
          .role = SemanticRole::Button,
          .name = "Cancel export",
          .value = {},
          .bounds = layout.exportCancelBounds(root.bounds.width,
                                               root.bounds.height),
          .enabled = true,
          .focused = false,
          .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
          .children = {},
          .description = "Cancel the active export without publishing a partial destination",
      });
    }
    root.children.push_back(std::move(exportProgress));
  }
  root.children.push_back(SemanticNode{
      .id = "status.render",
      .role = SemanticRole::Status,
      .name = "Render and audio status",
      .value = renderStatus.label() + " / " +
               (state.audioDeviceOnline ? state.audioBackend
                                        : "Audio unavailable"),
      .bounds = ui::Rect{0.0, root.bounds.height - layout.statusHeight,
                         root.bounds.width, layout.statusHeight},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
  });
  return root;
}

bool EditorSemanticTree::containsId(const SemanticNode& root,
                                    std::string_view id) noexcept {
  if (root.id == id) return true;
  return std::any_of(root.children.begin(), root.children.end(),
                     [id](const auto& child) {
                       return EditorSemanticTree::containsId(child, id);
                     });
}

}
