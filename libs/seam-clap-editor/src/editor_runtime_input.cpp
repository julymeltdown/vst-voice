#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>

namespace seam::clap_editor {
using namespace detail;

void EditorRuntime::requestRenderAfterEdit() {
  offlineRender_.invalidate("An edit invalidated the prepared final bounce");
  offlineAudioReady_.store(false, std::memory_order_release);
  authoring_->handleDocumentChanged();
  dirty_ = authoring_->document().dirty();
  controller_->setDirty(dirty_);
  requestRepaint();
}

// Pointer input reaches the editor only through the SING shell, the one surface on screen; with
// nothing presented (no vector backend) there is nothing to press.
void EditorRuntime::pointerDown(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  static_cast<void>(routeShellPointerLocked(ShellPointerPhase::Down, event));
}

void EditorRuntime::pointerMove(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  static_cast<void>(routeShellPointerLocked(ShellPointerPhase::Move, event));
}

void EditorRuntime::pointerUp(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  static_cast<void>(routeShellPointerLocked(ShellPointerPhase::Up, event));
}

void EditorRuntime::scroll(double deltaX, double deltaY, ui::Point anchor,
                           native_ui::InputModifiers modifiers) noexcept {
  std::lock_guard lock(mutex_);
  if (!shell_.scroll(*controller_, deltaX, deltaY, anchor, modifiers))
    controller_->scroll(deltaX, deltaY, anchor, modifiers);
}

void EditorRuntime::keyDown(const native_ui::KeyEvent& event) noexcept {
  if (std::lock_guard lock(mutex_); shellKeyLocked(event)) return;
  // Score interchange is dispatched before the editor lock is taken. Both operations can open a host
  // dialog through the handoff callbacks, which must not run while this mutex is held: the modal would
  // then deadlock against the conversion that tries to re-enter the same lock. Command-Shift-O opens a
  // score and Command-Shift-E writes one, matching the standalone menu's shortcuts.
  if (event.modifiers.command && event.modifiers.shift && !event.repeat) {
    if (event.key == native_ui::NativeKey::O) {
      const auto result = requestInterchangeImport();
      if (!result) {
        InterchangeErrorHandoff report;
        { std::lock_guard lock(mutex_); report = interchangeErrorHandoff_; }
        if (report) report("Could not import score", result.error());
      }
      return;
    }
    if (event.key == native_ui::NativeKey::E) {
      const auto result = requestInterchangeExport();
      if (!result) {
        InterchangeErrorHandoff report;
        { std::lock_guard lock(mutex_); report = interchangeErrorHandoff_; }
        if (report) report("Could not export score", result.error());
      }
      return;
    }
  }
  std::lock_guard lock(mutex_);
  if (controller_->replacementReviewOpen() || controller_->sampleMicroscopeOpen()) {
    static_cast<void>(controller_->keyDown(event)); return;
  }
  // Without the vector backend the view shows a notice, not an editor: no key edits a score nobody
  // can see. A unit or seam the shell's Phonemes lane targeted takes S, R and the seam keys in the
  // controller itself.
  if (!shell_.available()) return;
  const auto before = session_.revision();
  static_cast<void>(controller_->keyDown(event));
  if (session_.revision() != before) requestRenderAfterEdit();
}

void EditorRuntime::textComposition(
    std::u32string text, ui::CompositionSelection selection) noexcept {
  std::lock_guard lock(mutex_);
  static_cast<void>(controller_->updateTextComposition(std::move(text), selection));
}

void EditorRuntime::textCommit(std::u32string text) noexcept {
  std::lock_guard lock(mutex_);
  const auto before = session_.revision();
  static_cast<void>(controller_->commitTextComposition(std::move(text)));
  if (session_.revision() != before) requestRenderAfterEdit();
}

void EditorRuntime::textCancel() noexcept {
  std::lock_guard lock(mutex_);
  controller_->cancelTextComposition();
}

phonemizer::Result EditorRuntime::phonemesLocked() const {
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return {};
  return phonemizer::inspectPronunciation(*region);
}

core::Result<void> EditorRuntime::movePhonemeBoundary(
    domain::PhonemeKey key, bool startBoundary,
    time::Microseconds offset) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().movePhonemeBoundary(
      key, startBoundary, offset);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::selectUnitVariant(
    domain::PhonemeKey key, std::string unitId,
    domain::UnitRendererKind renderer) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().selectUnitVariant(
      key, std::move(unitId), renderer);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::cycleUnitVariant(
    domain::PhonemeKey key) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().cycleUnitVariant(key);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::cycleUnitRenderer(
    domain::PhonemeKey key) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().cycleUnitRenderer(key);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::upsertPitchPoint(
    domain::PitchAutomationPoint point) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().upsertPitchPoint(point);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::movePitchPoint(
    time::Tick from, domain::PitchAutomationPoint point) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().movePitchPoint(from, point);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::removePitchPoint(
    time::Tick tick) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().removePitchPoint(tick);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::cyclePitchInterpolation(
    time::Tick tick) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->technicalEdits().cyclePitchInterpolation(tick);
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<native_ui::SampleMicroscopeData> EditorRuntime::loadSampleMicroscope(
    domain::PhonemeKey key) {
  std::lock_guard lock(mutex_);
  auto phonemes = phonemesLocked();
  const auto preview = renderedPreview();
  if (!preview) return core::failure<native_ui::SampleMicroscopeData>(core::ErrorCode::NotFound, "Rendered unit decision is unavailable");
  const synthesis::UnitPlanEntry* entry = nullptr;
  for (const auto& candidate : preview->unitPlan) {
    if (candidate.tokenStart < phonemes.tokens.size() &&
        phonemes.tokens[candidate.tokenStart].key == key) {
      entry = &candidate;
      break;
    }
  }
  if (entry == nullptr || !voicebankResolution_.resolved()) {
    return core::failure<native_ui::SampleMicroscopeData>(core::ErrorCode::NotFound,
                         "Unit sample is unavailable for inspection");
  }
  const auto& bank = *voicebankResolution_.candidate;
  const auto* unit = bank.manifest.findUnit(entry->unitId);
  if (unit == nullptr) {
    return core::failure<native_ui::SampleMicroscopeData>(core::ErrorCode::NotFound,
                         "Voicebank Unit is missing", entry->unitId);
  }
  auto audio = voicebank::readWav(bank.bankRoot / unit->audioPath);
  if (!audio) return core::Result<native_ui::SampleMicroscopeData>{audio.error()};
  return native_ui::SampleMicroscopeData{
      .unit = *unit, .audio = std::move(audio.value()),
      .destinationContext = synthesis::describeUnitSelection(*entry)};
}

core::Result<void> EditorRuntime::openSampleMicroscope(domain::PhonemeKey key) {
  std::lock_guard lock(mutex_);
  const auto opened = controller_->openSampleMicroscope(key);
  return opened;
}

void EditorRuntime::closeSampleMicroscope() noexcept {
  std::lock_guard lock(mutex_);
  controller_->closeSampleMicroscope();
}

bool EditorRuntime::sampleMicroscopeOpen() const noexcept {
  std::lock_guard lock(mutex_);
  return controller_->sampleMicroscopeOpen();
}

const ui::SampleMicroscopeModel* EditorRuntime::sampleMicroscope() const noexcept {
  std::lock_guard lock(mutex_);
  return controller_->sampleMicroscope();
}

std::optional<std::string> EditorRuntime::selectedUnitId() const {
  std::lock_guard lock(mutex_);
  return controller_->sampleMicroscopeOpen()
      ? std::optional<std::string>{controller_->sampleMicroscopeUnitId()} : std::nullopt;
}

void EditorRuntime::setHostTimelineState(HostTimelineState state) noexcept {
  std::lock_guard lock(mutex_);
  const auto previousContent = hostTimelineCapture_.contentHash();
  hostTimelineState_ = state;
  hostTimelineCapture_.observe(state, renderSampleRate_);
  if (offlineTimingAuthority_ != OfflineTimingAuthority::FollowHost) {
    // A Fixed Audio bounce is defined by the document's own map. What a host does with its
    // transport cannot invalidate it.
    return;
  }
  // The host reports on every audio block. A transport that is merely advancing is not a
  // change to the authority a prepared bounce rests on, so it must not throw that bounce
  // away; a tempo, meter, loop or sample-rate change is.
  if (preparedHostTimeline_.has_value()) {
    if (hostTimelineCapture_.describes(*preparedHostTimeline_)) return;
    preparedHostTimeline_.reset();
  } else if (hostTimelineCapture_.contentHash() == previousContent) {
    return;
  }
  offlineRender_.invalidate("Host timing changed the offline render identity");
  offlineAudioReady_.store(false, std::memory_order_release);
}

HostTimelineState EditorRuntime::hostTimelineState() const noexcept {
  std::lock_guard lock(mutex_);
  return hostTimelineState_;
}

HostTempoMap EditorRuntime::hostTempoMap() const {
  std::lock_guard lock(mutex_);
  return hostTimelineCapture_.tempoMap();
}

std::optional<PreparedHostTimeline> EditorRuntime::preparedHostTimeline() const {
  std::lock_guard lock(mutex_);
  if (offlineTimingAuthority_ != OfflineTimingAuthority::FollowHost ||
      !preparedHostTimeline_.has_value()) {
    return std::nullopt;
  }
  const auto& prepared = *preparedHostTimeline_;
  // The authority describes one project revision at one render rate, frozen from one host
  // content. Anything else means the caller is looking at a preparation that no longer
  // describes what a bounce would do now.
  if (prepared.projectRevision() != session_.revision() ||
      prepared.sampleRate() != renderSampleRate_ ||
      !hostTimelineCapture_.describes(prepared)) {
    return std::nullopt;
  }
  return prepared;
}

core::Result<void> EditorRuntime::setPrimarySeamAmount(float value) {
  std::lock_guard lock(mutex_);
  const auto key = primaryPhonemeKey(session_, regionId_);
  if (!key.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "No note is available for seam editing");
  }
  auto result = authoring_->technicalEdits().upsertSeam(domain::SeamOverride{
      .incomingStartKey = *key,
      .seamAmount = std::clamp(value, 0.0F, 1.0F),
      .overlap = time::Microseconds{9000},
      .phaseReset = 0.65F,
      .envelopeBlend = 0.20F,
      .curve = domain::SeamCurve::HardCharacter,
      .locked = true,
  });
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

float EditorRuntime::primarySeamAmount() const noexcept {
  std::lock_guard lock(mutex_);
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || region->notes.empty()) return 0.55F;
  const auto key = primaryPhonemeKey(session_, regionId_);
  if (!key.has_value()) return 0.55F;
  const auto* seam = region->findSeamOverride(*key);
  return seam != nullptr && seam->seamAmount.has_value()
             ? std::clamp(*seam->seamAmount, 0.0F, 1.0F)
             : 0.55F;
}

void EditorRuntime::requestRepaint() const {
  std::function<void()> repaint;
  std::function<void()> changed;
  {
    std::lock_guard lock(mutex_);
    repaint = repaintCallback_;
    if (const auto revision = session_.revision();
        revision != lastSignalledRevision_) {
      lastSignalledRevision_ = revision;
      changed = persistentStateChangeCallback_;
    }
  }
  if (changed) changed();
  if (repaint) repaint();
}

}  // namespace seam::clap_editor
