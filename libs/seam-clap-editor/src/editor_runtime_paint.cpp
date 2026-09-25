#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/native_ui/character_performance_binding.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace seam::clap_editor {
using namespace detail;

void EditorRuntime::rebuildTechnicalModelsLocked() {
  if (controller_ == nullptr) return;
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return;
  auto phonemes = phonemesLocked();
  const auto layout = painter_.layout();
  // One predicate, shared with the standalone surface, so both reserve the dock for the same package.
  // The previous test asked whether a portrait had decoded, which is a drawing question, and answered
  // the layout question with it.
  const auto characterFull =
      session_.project().settings().characterDisplay ==
          domain::CharacterDisplayMode::Full &&
      character_.dockVisible(session_.project().settings().characterDisplay);
  const auto editorRight = std::max(
      layout.keyboardWidth + layout.minimumTimelineWidth,
      logicalWidth_ - (characterFull ? layout.characterDockWidth : 0.0));
  const auto geometry = adaptiveTechnicalLaneGeometry(
      layout, session_.project(), *region, phonemes.tokens.size(), logicalHeight_);
  phonemeLane_.rebuild(controller_->pianoRoll(), phonemes,
                       layout.phonemeContentTop(geometry.phonemeTop),
                       layout.phonemeContentHeight(geometry.phonemeHeight));

  synthesis::UnitPlan plan;
  const auto preview = renderedPreview();
  if (preview != nullptr) plan.entries = preview->unitPlan;
  if (plan.entries.empty() && voicebankResolution_.resolved()) {
    synthesis::DeterministicUnitSelector selector;
    auto selected = selector.select(
        voicebankResolution_.candidate->manifest, *region, phonemes.tokens,
        voicebankResolution_.candidate->manifest.styles.empty()
            ? std::string_view{}
            : std::string_view{
                  voicebankResolution_.candidate->manifest.styles.front()},
        region->unitSelectionOverrides);
    if (selected) plan = std::move(selected.value());
  }
  if (!plan.entries.empty() && voicebankResolution_.resolved()) {
    synthesis::TimingSolver timingSolver;
    auto timing = timingSolver.solve(
        session_.project(), *region, phonemes.tokens, plan,
        voicebankResolution_.candidate->manifest, renderSampleRate_);
    if (timing) {
      unitLane_.rebuild(
          session_.project(), *region, phonemes, plan, timing.value(), nullptr,
          controller_->pianoRoll().timeline(), layout.keyboardWidth,
          layout.unitContentTop(geometry.unitTop),
          layout.unitContentHeight(geometry.unitHeight),
          renderSampleRate_);
    }
  }
  static_cast<void>(editorRight);
}

const ui::PhonemeVisual* EditorRuntime::phonemeVisualAt(
    ui::Point point) const noexcept {
  for (auto iterator = phonemeLane_.visuals().rbegin();
       iterator != phonemeLane_.visuals().rend(); ++iterator) {
    if (iterator->bounds.contains(point)) return &*iterator;
  }
  return nullptr;
}

const ui::UnitLaneVisual* EditorRuntime::unitVisualAt(
    ui::Point point) const noexcept {
  for (auto iterator = unitLane_.visuals().rbegin();
       iterator != unitLane_.visuals().rend(); ++iterator) {
    if (iterator->bounds.contains(point)) return &*iterator;
  }
  return nullptr;
}

std::optional<time::Tick> EditorRuntime::pitchPointAt(
    ui::Point point, double tolerance) const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || controller_ == nullptr) return std::nullopt;
  const auto layout = painter_.layout();
  const auto geometry = adaptiveTechnicalLaneGeometry(
      layout, session_.project(), *region, phonemesLocked().tokens.size(), logicalHeight_);
  const auto centerY = geometry.pitchTop +
                       geometry.pitchHeight * layout.automationCenterFraction;
  for (const auto& value : region->pitchAutomation.points()) {
    const auto x = layout.keyboardWidth +
                   controller_->pianoRoll().timeline().tickToPixel(value.tick);
    const auto normalized = std::clamp(
        static_cast<double>(value.cents) / layout.pitchAutomationCentsRange,
        -1.0, 1.0);
    const auto y = centerY - normalized *
                                      (geometry.pitchHeight *
                                       layout.pitchAutomationVerticalScale);
    const auto dx = x - point.x;
    const auto dy = y - point.y;
    if (dx * dx + dy * dy <= tolerance * tolerance) return value.tick;
  }
  return std::nullopt;
}

time::Microseconds EditorRuntime::microsecondOffsetAt(
    domain::NoteId noteId, double x) const noexcept {
  const auto* region = session_.project().findRegion(regionId_);
  const auto* note = region == nullptr ? nullptr : region->findNote(noteId);
  if (region == nullptr || note == nullptr || controller_ == nullptr) {
    return time::Microseconds{0};
  }
  const auto absoluteStart = region->startTick + note->startTick;
  const auto tick = controller_->pianoRoll().timeline().pixelToTick(
      x - painter_.layout().keyboardWidth);
  const auto seconds = session_.project().tempoMap().secondsAt(tick) -
                       session_.project().tempoMap().secondsAt(absoluteStart);
  const auto micros = std::clamp(seconds * 1'000'000.0, -10'000'000.0,
                                 10'000'000.0);
  return time::Microseconds{static_cast<std::int64_t>(std::llround(micros))};
}

void EditorRuntime::paintPhase12BOverlay(
    native_ui::RasterCanvas& canvas) noexcept {
  const auto layout = painter_.layout();
  const auto theme = painter_.theme();
  const auto* track = session_.project().findVocalTrack(trackId_);
  const auto* region = track == nullptr ? nullptr : track->findRegion(regionId_);
  if (track != nullptr && region != nullptr) {
    const auto title = "TRACK " + track->name + " / REGION " + region->name +
                       " / OUT " +
                       std::to_string(session_.project().routing().deviceOutputChannels) +
                       "CH";
    const auto overlay =
        layout.phase12BOverlayBoundsForWidth(canvas.logicalWidth());
    const auto overlayTop = overlay.y;
    canvas.fillRect(overlay, theme.runtimeOverlayBackground);
    canvas.drawText(
        ui::Point{overlay.x + layout.phase12BTitleInsetX,
                  overlayTop +
                      (layout.phase12BTitleBaseline - layout.phase12BOverlayTop)},
        title, theme.runtimeOverlayText, layout.phase12BTitleFontSize);
    canvas.drawText(
        ui::Point{overlay.x + layout.phase12BTitleInsetX,
                  overlayTop +
                      (layout.phase12BDetailBaseline - layout.phase12BOverlayTop)},
                    "GAIN " + std::to_string(track->gainDb).substr(0, 5) +
                        "  PAN " + std::to_string(track->pan).substr(0, 5) +
                        (track->muted ? "  MUTE" : "") +
                        (track->solo ? "  SOLO" : ""),
        theme.runtimeOverlaySecondaryText, layout.phase12BDetailFontSize);
  }
  for (const auto& visual : phonemeLane_.visuals()) {
    const auto handle = selectedUnitKey_.has_value() &&
                                *selectedUnitKey_ == visual.key
                            ? theme.microscopePitchMark
                            : theme.accentSecondary;
    canvas.fillRect(ui::Rect{visual.bounds.x - layout.phase12BHandleInset,
                             visual.bounds.y, layout.phase12BHandleWidth,
                             visual.bounds.height}, handle);
    canvas.fillRect(ui::Rect{visual.bounds.right() - layout.phase12BHandleInset,
                             visual.bounds.y, layout.phase12BHandleWidth,
                             visual.bounds.height}, handle);
  }
  for (const auto& visual : unitLane_.visuals()) {
    if (selectedUnitKey_.has_value() && *selectedUnitKey_ == visual.startKey) {
      canvas.strokeRect(visual.bounds,
                        theme.microscopePitchMark,
                        layout.phase12BSelectionStroke);
    }
    if (!visual.alternatives.empty()) {
      canvas.drawText(ui::Point{visual.bounds.right() -
                                    layout.phase12BAlternativeInset,
                                visual.bounds.y + layout.phase12BTitleInsetX},
                      "+" + std::to_string(visual.alternatives.size()),
                      theme.runtimeOverlaySecondaryText,
                      layout.phase12BAlternativeFontSize);
    }
  }
  const auto host = hostTimelineState_;
  const auto hostLabel = "HOST " + std::to_string(host.tempo).substr(0, 6) +
                         " BPM  " + std::to_string(host.numerator) + "/" +
                         std::to_string(host.denominator) +
                         (host.loopActive ? " LOOP" : "");
  canvas.drawText(ui::Point{layout.runtimeOverlayHostX,
                            layout.runtimeOverlayHostBaseline},
                  hostLabel, theme.runtimeOverlayHost,
                  layout.runtimeOverlayHostFontSize);
}

native_ui::EditorSceneState EditorRuntime::sceneState() {
  // Follow the same published phrase as the preview. The publication handle avoids copying the
  // cue list on every repaint; its request id changes even for a same-revision rerender.
  const auto published = authoring_->renderer().acquire();
  const auto preview = acquireRenderedPreview();
  const auto ready = published && published->state == authoring::RenderState::Ready &&
      preview && preview->status == PreviewStatus::Ready &&
      published->result.interleaved.storageIdentity() == preview->interleaved.storageIdentity();
  if (!ready) {
    characterPerformanceRequest_.reset();
    character_.clearPerformanceSnapshot();
  } else if (characterPerformanceRequest_ != published->requestId) {
    characterPerformanceRequest_ = published->requestId;
    character_.clearPerformanceSnapshot();
    const auto& result = published->result;
    if (result.performanceIdentity.has_value() && !result.performanceCues.empty()) {
      const auto& identity = *result.performanceIdentity;
      native_ui::CharacterPerformanceBindingRequest request;
      request.resourceId = identity.resourceId;
      request.resourceVersion = identity.resourceVersion;
      request.resourceContentHash = identity.resourceContentHash;
      request.style = identity.style;
      request.pronunciationIdentity = identity.pronunciationIdentity;
      request.renderRevision = identity.renderRevision;
      request.resourceKind = identity.resourceKind;
      if (identity.scorePitchRange.has_value())
        request.scorePitchRange = character::ScorePitchRange{
            identity.scorePitchRange->lowestMidiKey, identity.scorePitchRange->highestMidiKey};
      request.sampleRate = identity.sampleRate;
      request.channelCount = result.channelCount;
      request.interleaved = {result.interleaved.data(), result.interleaved.size()};
      request.cues = result.performanceCues;
      if (auto built = native_ui::buildPublishedCharacterPerformance(request); built) {
        const auto followed = character_.followSinger(
            character::performanceBindingKey(built.value()));
        if (followed) {
          static_cast<void>(character_.setPerformanceSnapshot(std::move(built).value()));
        }
      }
    }
  }
  controller_->clearCharacterPerformance();
  if (const auto* performance = character_.performanceSnapshot(); performance != nullptr) {
    const auto mapped = HostTimelineMapper::map(
        hostTimelineState_, session_.project(), static_cast<double>(performance->sampleRate));
    character::CharacterPerformanceFrame frame;
    if (mapped.audible && mapped.sourceFrame <=
            static_cast<std::uint64_t>(std::numeric_limits<time::SampleFrame>::max())) {
      frame = character_.performanceFrameAt(static_cast<time::SampleFrame>(mapped.sourceFrame));
    }
    const auto* snapshot = character_.performanceSnapshot();
    controller_->setCharacterPerformance({
        .mouth = frame.mouth,
        .energy = frame.energy,
        .expression = frame.expression,
        .performing = frame.performing,
        .audibleStale = authoring_->renderer().progress().audibleAudioStale,
        .voiceStyle = snapshot == nullptr ? std::string{} : snapshot->style,
        .scorePitchRange = snapshot == nullptr || !snapshot->scorePitchRange.has_value()
            ? std::string{}
            : character::scorePitchRangeLabel(*snapshot->scorePitchRange),
    });
  }
  auto state = controller_->sceneState();
  state.characterMode = session_.project().settings().characterDisplay;
  if (!state.voiceIdentity.characterActive) {
    // The render may be audible while the loaded artwork is not associated with this selected
    // voicebank. Do not publish a singing state in the paint or accessibility read model.
    state.characterPerformance.reset();
    controller_->clearCharacterPerformance();
  }
  state.characterMouthPlacement.reset();
  state.characterVoiceStyle.clear();
  state.characterScorePitchRange.clear();
  if (state.characterPerformance.has_value()) {
    state.characterMouth = character_.mouth(state.characterPerformance->mouth);
    if (state.characterMouth != nullptr)
      state.characterMouthPlacement = character_.mouthPlacement();
    if (const auto* snapshot = character_.performanceSnapshot(); snapshot != nullptr) {
      state.characterVoiceStyle = snapshot->style;
      if (snapshot->scorePitchRange.has_value())
        state.characterScorePitchRange = character::scorePitchRangeLabel(*snapshot->scorePitchRange);
    }
  }
  state.characterPortrait = character_.portrait(state.characterState);
  // The same predicate the standalone surface uses, so a package reserves the dock in both or in
  // neither. The portrait above is what the dock draws for the current render status; this is whether
  // there is a dock at all.
  state.characterDockReserved = character_.dockVisible(state.characterMode);
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  return state;
}

void EditorRuntime::paint(native_ui::RasterCanvas& canvas) noexcept {
  std::lock_guard lock(mutex_);
  controller_->pollReplacementReview();
  rebuildTechnicalModelsLocked();
  const auto state = sceneState();
  if (shell_.paint(canvas, controller_->pianoRoll(), state, controller_->playheadTick())) return;
  painter_.paint(canvas, controller_->pianoRoll(), state);
  if (state.sampleMicroscope.has_value() || state.replacementReview.visible) return;
  const auto seam = primarySeamAmount();
  const auto width = canvas.logicalWidth();
  const auto layout = painter_.layout();
  const auto theme = painter_.theme();
  const auto overlay = layout.runtimeOverlayBoundsForWidth(width);
  const auto meter = layout.runtimeOverlayMeterBoundsForWidth(width);
  const auto overlayTop = overlay.y;
  const auto left = overlay.x;
  canvas.fillRect(overlay, theme.runtimeOverlayBackground);
  canvas.drawText(
      ui::Point{left + layout.runtimeOverlayTitleInsetX,
                overlayTop +
                    (layout.runtimeOverlayTitleBaseline - layout.runtimeOverlayTop)},
      "SEAM BOUNDARY", theme.runtimeOverlayBorder,
      layout.runtimeOverlayTitleFontSize);
  canvas.fillRect(meter, theme.runtimeOverlayMeter);
  canvas.fillRect(ui::Rect{meter.x, meter.y, meter.width * seam, meter.height},
                  theme.runtimeOverlayAccent);
  const auto stats = renderStats();
  const auto preview = renderedPreview();
  const auto label = "PROD " + std::string{previewStatusName(preview->status)} +
                     " " + std::to_string(stats.completed) + "/" +
                     std::to_string(stats.submitted);
  canvas.drawText(ui::Point{left + layout.runtimeOverlayMeterInsetX,
                            overlayTop +
                                (layout.runtimeOverlayDetailBaseline -
                                 layout.runtimeOverlayTop)},
                  label,
                  preview->status == PreviewStatus::Ready
                      ? theme.runtimeOverlayReady
                      : theme.runtimeOverlayError,
                  layout.runtimeOverlayDetailFontSize);
  paintPhase12BOverlay(canvas);
}

}  // namespace seam::clap_editor
