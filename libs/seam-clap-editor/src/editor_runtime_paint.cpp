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
  // The surface and its geometry are chosen before the scene state is derived from them.
  const auto shellFrame =
      shell_.prepareFrame(*controller_, canvas.logicalWidth(), canvas.logicalHeight());
  const auto state = sceneState();
  shellPresentedFrame_ =
      shellFrame && shell_.paint(canvas, *controller_, state, controller_->playheadTick());
  if (shellPresentedFrame_) return;
  // The shell is the only editor surface; without the vector backend the view says so.
  native_ui::paintEditorUnavailable(canvas);
}

native_ui::FrameDamage EditorRuntime::paintFrame(native_ui::RasterCanvas& canvas) noexcept {
  paint(canvas);
  std::lock_guard lock(mutex_);
  return shellPresentedFrame_ ? shell_.lastFrameDamage() : native_ui::FrameDamage::everything();
}

}  // namespace seam::clap_editor
