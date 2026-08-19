#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
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
  authoring_->handleDocumentChanged();
  dirty_ = authoring_->document().dirty();
  controller_->setDirty(dirty_);
  requestRepaint();
}

void EditorRuntime::pointerDown(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  rebuildTechnicalModelsLocked();
  if (microscopeUnitId_.has_value()) {
    if (event.button == native_ui::PointerButton::Right || event.clickCount >= 2) {
      closeSampleMicroscope();
    }
    return;
  }

  const auto layout = painter_.layout();
  const auto statusTop = logicalHeight_ - layout.statusHeight;
  const auto pianoBottom = std::max(layout.contentTop() + 100.0,
                                    statusTop - layout.lanesHeight());
  const auto unitTop = pianoBottom + layout.phonemeLaneHeight;
  const auto seamTop = unitTop + layout.unitLaneHeight;
  const auto automationTop = seamTop + layout.seamLaneHeight;

  if (event.button == native_ui::PointerButton::Left && event.position.y <= 50.0 &&
      event.position.x >= 565.0 && event.position.x <= 925.0) {
    const auto tracks = vocalTrackIds();
    if (event.position.x < 650.0 && !tracks.empty()) {
      const auto it = std::find(tracks.begin(), tracks.end(), trackId_);
      const auto next = it == tracks.end() || std::next(it) == tracks.end()
                            ? tracks.front()
                            : *std::next(it);
      static_cast<void>(selectTrack(next));
    } else if (event.position.x < 735.0) {
      const auto regions = regionIds(trackId_);
      if (!regions.empty()) {
        const auto it = std::find(regions.begin(), regions.end(), regionId_);
        const auto next = it == regions.end() || std::next(it) == regions.end()
                              ? regions.front()
                              : *std::next(it);
        static_cast<void>(selectRegion(next));
      }
    } else if (const auto* track = session_.project().findVocalTrack(trackId_)) {
      if (event.position.x < 800.0) {
        static_cast<void>(setTrackMix(trackId_, track->gainDb, track->pan,
                                      !track->muted, track->solo));
      } else if (event.position.x < 865.0) {
        static_cast<void>(setTrackMix(trackId_, track->gainDb, track->pan,
                                      track->muted, !track->solo));
      } else {
        const auto current = session_.project().routing().deviceOutputChannels;
        const auto next = current == 1U ? 2U : current == 2U ? 4U
                                        : current == 4U ? 6U
                                        : current == 6U ? 8U : 1U;
        static_cast<void>(configureOutputChannels(static_cast<std::uint8_t>(next)));
      }
    }
    return;
  }

  if (event.position.y >= pianoBottom && event.position.y < unitTop) {
    if (const auto* visual = phonemeVisualAt(event.position)) {
      const auto leftDistance = std::abs(event.position.x - visual->bounds.x);
      const auto rightDistance = std::abs(event.position.x - visual->bounds.right());
      if (event.button == native_ui::PointerButton::Left &&
          std::min(leftDistance, rightDistance) <= 7.0) {
        draggingPhonemeKey_ = visual->key;
        draggingPhonemeStart_ = leftDistance <= rightDistance;
        selectedUnitKey_ = visual->key;
        return;
      }
    }
  }

  if (event.position.y >= unitTop && event.position.y < seamTop) {
    if (const auto* visual = unitVisualAt(event.position)) {
      selectedUnitKey_ = visual->startKey;
      if (event.clickCount >= 2) {
        static_cast<void>(openSampleMicroscope(visual->startKey));
      } else if (event.modifiers.shift) {
        static_cast<void>(cycleUnitVariant(visual->startKey));
      } else if (event.modifiers.alt) {
        static_cast<void>(cycleUnitRenderer(visual->startKey));
      }
      requestRepaint();
      return;
    }
  }

  if (event.position.y >= automationTop &&
      event.position.y < automationTop + layout.automationLaneHeight) {
    const auto existing = pitchPointAt(event.position);
    if (event.button == native_ui::PointerButton::Right && existing.has_value()) {
      static_cast<void>(removePitchPoint(*existing));
      return;
    }
    if (event.modifiers.alt && existing.has_value()) {
      static_cast<void>(cyclePitchInterpolation(*existing));
      return;
    }
    if (event.button == native_ui::PointerButton::Left && existing.has_value()) {
      draggingPitchTick_ = existing;
      return;
    }
    if (event.button == native_ui::PointerButton::Left && event.clickCount >= 2) {
      const auto* region = session_.project().findRegion(regionId_);
      if (region == nullptr) return;
      auto tick = controller_->pianoRoll().timeline().pixelToTick(
          event.position.x - layout.keyboardWidth);
      tick = std::clamp(tick, time::Tick{0}, region->durationTick);
      const auto centerY = automationTop + layout.automationLaneHeight * 0.5;
      const auto cents = static_cast<float>(std::clamp(
          (centerY - event.position.y) /
              (layout.automationLaneHeight * 0.38) * 600.0,
          -1200.0, 1200.0));
      static_cast<void>(upsertPitchPoint(domain::PitchAutomationPoint{
          .tick = tick,
          .cents = cents,
          .interpolation = domain::CurveInterpolation::Linear}));
      return;
    }
  }

  const auto seamLeft = std::max(520.0, logicalWidth_ - 248.0);
  if (event.button == native_ui::PointerButton::Left &&
      event.position.y >= 16.0 && event.position.y <= 42.0 &&
      event.position.x >= seamLeft && event.position.x <= seamLeft + 190.0) {
    const auto value = static_cast<float>(
        std::clamp((event.position.x - seamLeft) / 190.0, 0.0, 1.0));
    static_cast<void>(setPrimarySeamAmount(value));
    return;
  }
  static_cast<void>(controller_->pointerDown(event));
}

void EditorRuntime::pointerMove(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  if (draggingPhonemeKey_.has_value() || draggingPitchTick_.has_value()) {
    requestRepaint();
    return;
  }
  static_cast<void>(controller_->pointerMove(event));
}

void EditorRuntime::pointerUp(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  if (draggingPhonemeKey_.has_value()) {
    const auto key = *draggingPhonemeKey_;
    draggingPhonemeKey_.reset();
    static_cast<void>(movePhonemeBoundary(
        key, draggingPhonemeStart_, microsecondOffsetAt(key.noteId,
                                                        event.position.x)));
    return;
  }
  if (draggingPitchTick_.has_value()) {
    const auto from = *draggingPitchTick_;
    draggingPitchTick_.reset();
    const auto layout = painter_.layout();
    const auto statusTop = logicalHeight_ - layout.statusHeight;
    const auto pianoBottom = std::max(layout.contentTop() + 100.0,
                                      statusTop - layout.lanesHeight());
    const auto automationTop = pianoBottom + layout.phonemeLaneHeight +
                               layout.unitLaneHeight + layout.seamLaneHeight;
    const auto centerY = automationTop + layout.automationLaneHeight * 0.5;
    const auto* region = session_.project().findRegion(regionId_);
    if (region != nullptr) {
      auto tick = controller_->pianoRoll().timeline().pixelToTick(
          event.position.x - layout.keyboardWidth);
      tick = std::clamp(tick, time::Tick{0}, region->durationTick);
      const auto cents = static_cast<float>(std::clamp(
          (centerY - event.position.y) /
              (layout.automationLaneHeight * 0.38) * 600.0,
          -1200.0, 1200.0));
      const auto old = std::find_if(
          region->pitchAutomation.points().begin(),
          region->pitchAutomation.points().end(),
          [from](const auto& point) { return point.tick == from; });
      const auto interpolation = old == region->pitchAutomation.points().end()
                                     ? domain::CurveInterpolation::Linear
                                     : old->interpolation;
      static_cast<void>(movePitchPoint(
          from, domain::PitchAutomationPoint{
                    .tick = tick, .cents = cents,
                    .interpolation = interpolation}));
    }
    return;
  }
  const auto before = session_.revision();
  static_cast<void>(controller_->pointerUp(event));
  if (session_.revision() != before) requestRenderAfterEdit();
}

void EditorRuntime::scroll(double deltaX, double deltaY, ui::Point anchor,
                           native_ui::InputModifiers modifiers) noexcept {
  std::lock_guard lock(mutex_);
  controller_->scroll(deltaX, deltaY, anchor, modifiers);
}

void EditorRuntime::keyDown(const native_ui::KeyEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  if (event.key == native_ui::NativeKey::Escape &&
      microscopeUnitId_.has_value()) {
    closeSampleMicroscope();
    return;
  }
  if (selectedUnitKey_.has_value() && event.key == native_ui::NativeKey::S) {
    static_cast<void>(cycleUnitVariant(*selectedUnitKey_));
    return;
  }
  if (selectedUnitKey_.has_value() && event.key == native_ui::NativeKey::R) {
    static_cast<void>(cycleUnitRenderer(*selectedUnitKey_));
    return;
  }
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
  phonemizer::JapaneseKanaPhonemizer phonemizer;
  return phonemizer.phonemize(*region);
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
    selectedUnitKey_ = key;
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
    selectedUnitKey_ = key;
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
    selectedUnitKey_ = key;
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

core::Result<void> EditorRuntime::openSampleMicroscope(
    domain::PhonemeKey key) {
  std::lock_guard lock(mutex_);
  auto phonemes = phonemesLocked();
  const auto preview = renderedPreview();
  const synthesis::UnitPlanEntry* entry = nullptr;
  for (const auto& candidate : preview->unitPlan) {
    if (candidate.tokenStart < phonemes.tokens.size() &&
        phonemes.tokens[candidate.tokenStart].key == key) {
      entry = &candidate;
      break;
    }
  }
  if (entry == nullptr || !voicebankResolution_.resolved()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit sample is unavailable for inspection");
  }
  const auto& bank = *voicebankResolution_.candidate;
  const auto* unit = bank.manifest.findUnit(entry->unitId);
  if (unit == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Voicebank Unit is missing", entry->unitId);
  }
  auto audio = voicebank::readWav(bank.bankRoot / unit->audioPath);
  if (!audio) return core::Result<void>{audio.error()};
  microscopeAudio_ = std::move(audio.value());
  const auto width = std::max(520.0, logicalWidth_ - 180.0);
  const auto height = std::max(300.0, logicalHeight_ - 180.0);
  auto rebuilt = microscope_.rebuild(
      *unit, microscopeAudio_,
      ui::Rect{90.0, 118.0, width - 180.0, height * 0.38},
      ui::Rect{90.0, 130.0 + height * 0.38, width - 180.0, height * 0.38});
  if (!rebuilt) return rebuilt;
  microscopeUnitId_ = entry->unitId;
  selectedUnitKey_ = key;
  requestRepaint();
  return core::success();
}

void EditorRuntime::closeSampleMicroscope() noexcept {
  std::lock_guard lock(mutex_);
  microscopeUnitId_.reset();
  requestRepaint();
}

bool EditorRuntime::sampleMicroscopeOpen() const noexcept {
  std::lock_guard lock(mutex_);
  return microscopeUnitId_.has_value();
}

const ui::SampleMicroscopeModel* EditorRuntime::sampleMicroscope() const noexcept {
  std::lock_guard lock(mutex_);
  return microscopeUnitId_.has_value() ? &microscope_ : nullptr;
}

std::optional<std::string> EditorRuntime::selectedUnitId() const {
  std::lock_guard lock(mutex_);
  return microscopeUnitId_;
}

void EditorRuntime::setHostTimelineState(HostTimelineState state) noexcept {
  std::lock_guard lock(mutex_);
  hostTimelineState_ = state;
}

HostTimelineState EditorRuntime::hostTimelineState() const noexcept {
  std::lock_guard lock(mutex_);
  return hostTimelineState_;
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
  if (repaintCallback_) repaintCallback_();
}

}  // namespace seam::clap_editor
