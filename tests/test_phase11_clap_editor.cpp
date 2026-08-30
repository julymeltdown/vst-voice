#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for Phase 11 regression tests
#endif

template <typename Trigger, typename Ready>
bool awaitRender(seam::clap_editor::EditorRuntime& runtime, Trigger trigger,
                 Ready ready) {
  std::mutex mutex;
  std::condition_variable condition;
  std::uint64_t notifications = 0U;
  runtime.setRenderReadyCallback([&] {
    {
      std::scoped_lock lock{mutex};
      ++notifications;
    }
    condition.notify_one();
  });
  trigger();
  std::unique_lock lock{mutex};
  const auto completed = condition.wait_for(
      lock, std::chrono::seconds{30},
      [&] { return notifications > 0U && ready(); });
  lock.unlock();
  runtime.setRenderReadyCallback({});
  return completed;
}

int main() {
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }};
  seam::clap_editor::EditorRuntime runtime(
      std::nullopt, std::filesystem::path{"assets/character-01"}, roots);
  const auto initial = runtime.projectCopy();
  if (initial.noteCount() < 4U || !initial.validate()) return 1;
  if (initial.vocalTracks().empty() ||
      initial.vocalTracks().front().voicebank.contentHash.empty()) {
    return 43;
  }
  const auto initialVoicebank = initial.vocalTracks().front().voicebank;
  if (runtime.selectVoicebank(initialVoicebank.id, initialVoicebank.version,
                              "wrong-content-hash") ||
      runtime.projectCopy().vocalTracks().front().voicebank != initialVoicebank) {
    return 44;
  }
  runtime.controller().showVoicebankBrowser();
  if (runtime.controller().sceneState().voicebankCards.empty()) return 45;
  std::size_t installerRequests = 0U;
  runtime.setVoicebankInstallerHandoff([&installerRequests] {
    ++installerRequests;
    return seam::core::success();
  });
  runtime.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::O,
      .modifiers = {},
      .repeat = false,
  });
  if (installerRequests != 1U ||
      !runtime.controller().voicebankBrowserVisible()) {
    return 46;
  }
  const auto accessibility = runtime.accessibilitySnapshot();
  const auto toolbar = std::find_if(
      accessibility.children.begin(), accessibility.children.end(),
      [](const auto& child) { return child.id == "toolbar.controls"; });
  if (toolbar == accessibility.children.end()) return 22;
  if (accessibility.virtualizedNoteCount == 0U ||
      accessibility.virtualizedNoteCount > initial.noteCount()) {
    return 23;
  }
  if (!runtime.dispatchAccessibility(
          "toolbar.tempo", seam::native_ui::SemanticAction::SetFocus)) {
    return 24;
  }
  runtime.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Tab,
      .modifiers = {},
      .repeat = false,
  });
  const auto focusedAfterTab = runtime.accessibilityFocusedNode();
  if (!focusedAfterTab.has_value() || focusedAfterTab->id == "toolbar.tempo") {
    return 25;
  }
  runtime.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Tab,
      .modifiers = {.shift = true},
      .repeat = false,
  });
  const auto focusedAfterReverseTab = runtime.accessibilityFocusedNode();
  if (!focusedAfterReverseTab.has_value() ||
      focusedAfterReverseTab->id != "toolbar.tempo") {
    return 26;
  }

  const auto before = runtime.primarySeamAmount();
  const auto seamResult = runtime.setPrimarySeamAmount(0.82F);
  if (!seamResult || runtime.primarySeamAmount() < 0.81F || before == 0.82F) {
    return 2;
  }

  std::shared_ptr<const seam::clap_editor::RenderedPreview> preview;
  const auto initialRendered = awaitRender(
      runtime, [&] { runtime.requestRender(48000U); }, [&] {
        preview = runtime.renderedPreview();
        return preview != nullptr && !preview->stereo.empty() &&
               preview->revision == runtime.revision();
      });
  if (!initialRendered || preview == nullptr || preview->stereo.empty()) return 3;
  double energy = 0.0;
  for (const auto sample : preview->stereo) {
    if (!std::isfinite(sample)) return 4;
    energy += std::abs(static_cast<double>(sample));
  }
  if (energy <= 1.0) return 5;

  runtime.resize(480.0, 320.0);
  const seam::native_ui::EditorSceneLayout compactLayout;
  const auto compactTick = seam::time::Tick{480};
  if (!runtime.upsertPitchPoint(seam::domain::PitchAutomationPoint{
          .tick = compactTick,
          .cents = 0.0F,
          .interpolation = seam::domain::CurveInterpolation::Linear})) {
    return 30;
  }
  const auto compactProject = runtime.projectCopy();
  const auto* compactLayoutRegion = compactProject.findRegion(runtime.regionId());
  if (compactLayoutRegion == nullptr) return 31;
  const auto compactTechnical = seam::native_ui::resolveTechnicalLaneHeights(
      seam::native_ui::TechnicalLaneLayoutInput{
          .presentation = compactProject.settings().technicalLanes,
          .populated = {true, !compactLayoutRegion->unitSelectionOverrides.empty(),
                        !compactLayoutRegion->seamOverrides.empty(), true},
          .previewHeights = {compactLayout.phonemeLaneHeight,
                             compactLayout.unitLaneHeight,
                             compactLayout.seamLaneHeight,
                             compactLayout.automationLaneHeight},
          .contentTop = compactLayout.contentTop(),
          .contentBottom = 320.0 - compactLayout.statusHeight,
      });
  const auto compactX = compactLayout.keyboardWidth +
                        runtime.controller().pianoRoll().timeline().tickToPixel(
                            compactTick);
  const auto compactPitchTop = compactTechnical.pianoBottom +
                               compactTechnical.values[0U] +
                               compactTechnical.values[1U] +
                               compactTechnical.values[2U];
  const auto compactY = compactPitchTop +
                        compactTechnical.values[3U] *
                            compactLayout.automationCenterFraction;
  runtime.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{compactX, compactY},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  runtime.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{compactX, compactY + 4.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  const auto compactAfter = runtime.projectCopy();
  const auto* compactRegion = compactAfter.findRegion(runtime.regionId());
  if (compactRegion == nullptr) return 31;
  const auto compactPoint = std::find_if(
      compactRegion->pitchAutomation.points().begin(),
      compactRegion->pitchAutomation.points().end(),
      [compactTick](const auto& point) { return point.tick == compactTick; });
  if (compactPoint == compactRegion->pitchAutomation.points().end() ||
      std::abs(compactPoint->cents) < 1.0F) {
    return 32;
  }

  if (compactRegion->notes.empty() ||
      !runtime.openSampleMicroscope(seam::domain::PhonemeKey{
          .noteId = compactRegion->notes.front().id, .ordinal = 0U})) {
    return 33;
  }
  seam::native_ui::PixelSurface microscopeSurface{480U, 320U};
  seam::native_ui::RasterCanvas microscopeCanvas{microscopeSurface, 1.0};
  runtime.paint(microscopeCanvas);
  const auto* microscope = runtime.sampleMicroscope();
  const auto expectedLayout = seam::native_ui::EditorSceneLayout{};
  const auto expectedWave =
      expectedLayout.microscopeWaveformBounds(480.0, 320.0);
  const auto expectedSpectrogram =
      expectedLayout.microscopeSpectrogramBounds(480.0, 320.0);
  const auto sameRect = [](const seam::ui::Rect& left,
                           const seam::ui::Rect& right) {
    return std::abs(left.x - right.x) < 1e-9 &&
           std::abs(left.y - right.y) < 1e-9 &&
           std::abs(left.width - right.width) < 1e-9 &&
           std::abs(left.height - right.height) < 1e-9;
  };
  if (!runtime.sampleMicroscopeOpen() || microscope == nullptr ||
      microscopeSurface.checksum() == 0U ||
      !sameRect(microscope->waveformBounds(), expectedWave) ||
      !sameRect(microscope->spectrogramBounds(), expectedSpectrogram)) {
    return 34;
  }
  const auto microscopeAccessibility = runtime.accessibilitySnapshot();
  const auto microscopePanel = std::find_if(
      microscopeAccessibility.children.begin(),
      microscopeAccessibility.children.end(),
      [](const auto& child) { return child.id == "microscope.panel"; });
  if (microscopePanel == microscopeAccessibility.children.end() ||
      !seam::native_ui::EditorSemanticTree::containsId(
          *microscopePanel, "microscope.close") ||
      seam::native_ui::EditorSemanticTree::containsId(
          *microscopePanel, "toolbar.controls")) {
    return 35;
  }
  if (!runtime.dispatchAccessibility(
          "microscope.close", seam::native_ui::SemanticAction::SetFocus)) {
    return 38;
  }
  const auto focusedMicroscope = runtime.accessibilityFocusedNode();
  if (!focusedMicroscope.has_value() ||
      focusedMicroscope->id != "microscope.close" ||
      !focusedMicroscope->focused) {
    return 39;
  }
  const auto modalHeaderPixel = microscopeSurface.pixels()[68U * 480U + 430U];
  if (!runtime.dispatchAccessibility(
          "microscope.close", seam::native_ui::SemanticAction::Activate) ||
      runtime.sampleMicroscopeOpen()) {
    return 36;
  }
  seam::native_ui::PixelSurface normalSurface{480U, 320U};
  seam::native_ui::RasterCanvas normalCanvas{normalSurface, 1.0};
  runtime.paint(normalCanvas);
  if (modalHeaderPixel == normalSurface.pixels()[68U * 480U + 430U]) {
    return 37;
  }

  if (!runtime.openSampleMicroscope(seam::domain::PhonemeKey{
          .noteId = compactRegion->notes.front().id, .ordinal = 0U})) {
    return 40;
  }
  auto replacementProject = runtime.projectCopy();
  replacementProject.setName("Phase 11 replacement closes microscope");
  if (!runtime.replaceProject(std::move(replacementProject)) ||
      runtime.sampleMicroscopeOpen() || runtime.sampleMicroscope() != nullptr) {
    return 41;
  }

  const auto encoded = seam::clap_editor::encodeEditorState(runtime.projectCopy());
  if (!encoded || encoded.value().empty()) return 6;
  const auto decoded = seam::clap_editor::decodeEditorState(encoded.value());
  if (!decoded || decoded.value().noteCount() != initial.noteCount()) return 7;
  auto replacement = decoded.value();
  replacement.setName("Phase 11 restored while GUI survives");
  bool replacementAccepted = false;
  const auto replacementRendered = awaitRender(
      runtime,
      [&] {
        replacementAccepted = static_cast<bool>(
            runtime.replaceProject(std::move(replacement)));
      },
      [&] {
        const auto current = runtime.renderedPreview();
        return current != nullptr && current->revision == runtime.revision() &&
               current->status == seam::clap_editor::PreviewStatus::Ready;
      });
  if (!replacementAccepted ||
      runtime.projectCopy().name() != "Phase 11 restored while GUI survives") {
    return 8;
  }
  if (!replacementRendered) return 42;
  const auto beforeControllerEditRevision = runtime.revision();
  const auto beforeControllerEditSubmissions = runtime.renderStats().submitted;
  bool controllerEditAccepted = false;
  const auto controllerEditRendered = awaitRender(
      runtime,
      [&] {
        controllerEditAccepted = static_cast<bool>(
            runtime.controller().renameSelectedTrack("CLAP UI callback"));
      },
      [&] {
        const auto current = runtime.renderedPreview();
        const auto currentStats = runtime.renderStats();
        return currentStats.submitted > beforeControllerEditSubmissions &&
               current != nullptr && current->revision == runtime.revision() &&
               current->status == seam::clap_editor::PreviewStatus::Ready;
      });
  if (!controllerEditAccepted ||
      runtime.revision() != beforeControllerEditRevision + 1U) {
    return 20;
  }
  if (!controllerEditRendered) return 21;
  auto corrupted = encoded.value();
  corrupted.back() ^= std::byte{0x01};
  if (seam::clap_editor::decodeEditorState(corrupted)) return 9;

  seam::live_voice::VoiceEngine semantics;
  if (!semantics.publishResource(seam::phase12c::makeEmbeddedHumanResource())) {
    return 19;
  }
  semantics.setOutputSampleRate(48000.0);
  semantics.noteOn(101, 67, 0.0F);
  if (semantics.activeVoiceCount() != 1U) return 10;
  semantics.noteOn(102, 69, 0.8F);
  if (semantics.activeVoiceCount() != 2U) return 11;
  semantics.choke(102, 69);
  if (semantics.activeVoiceCount() != 1U) return 12;
  semantics.choke(101, 67);
  if (semantics.activeVoiceCount() != 0U) return 13;

  runtime.noteOn(1, 67, 0.9F);
  double liveEnergy = 0.0;
  for (int frame = 0; frame < 4000; ++frame) {
    const auto sample = runtime.renderLiveSample();
    if (!std::isfinite(sample)) return 14;
    liveEnergy += std::abs(static_cast<double>(sample));
  }
  runtime.noteOff(1, 67);
  for (int frame = 0; frame < 4000; ++frame) {
    liveEnergy += std::abs(static_cast<double>(runtime.renderLiveSample()));
  }
  if (liveEnergy <= 1.0) return 15;

  const auto stats = runtime.renderStats();
  if (stats.submitted == 0U || stats.completed == 0U) return 16;

  if (!awaitRender(runtime, [&] { runtime.requestRender(48000U); },
                   [] { return true; })) {
    return 17;
  }

  seam::clap_editor::RealtimePreviewPublication publication;
  std::atomic<bool> publicationOk{true};
  std::jthread reader([&](std::stop_token token) {
    while (!token.stop_requested()) {
      auto handle = publication.acquire();
      if (!handle || handle->sampleRate < 8000U ||
          handle->sampleRate > 192000U) {
        publicationOk.store(false, std::memory_order_relaxed);
        return;
      }
      for (const auto value : handle->stereo) {
        if (!std::isfinite(value)) {
          publicationOk.store(false, std::memory_order_relaxed);
          return;
        }
      }
    }
  });
  for (std::uint64_t revision = 1U; revision <= 500U; ++revision) {
    seam::clap_editor::RenderedPreview value;
    value.sampleRate = 48000U;
    value.revision = revision;
    value.stereo.assign(256U, static_cast<float>(revision % 17U) / 17.0F);
    while (!publication.publish(std::move(value))) {
      std::this_thread::yield();
      value.sampleRate = 48000U;
      value.revision = revision;
      value.stereo.assign(256U, static_cast<float>(revision % 17U) / 17.0F);
    }
  }
  reader.request_stop();
  reader.join();
  if (!publicationOk.load(std::memory_order_relaxed)) return 18;

  const auto beforeAccessibilityEdit = runtime.projectCopy();
  const auto* beforeRegion = beforeAccessibilityEdit.findRegion(runtime.regionId());
  if (beforeRegion == nullptr || beforeRegion->notes.empty()) return 27;
  const auto accessibilityNoteId = beforeRegion->notes.front().id;
  const auto accessibilityEdit = runtime.setAccessibilityValue(
      "note." + accessibilityNoteId.toString(), "い");
  if (!accessibilityEdit) return 28;
  const auto afterAccessibilityEdit = runtime.projectCopy();
  const auto* afterRegion = afterAccessibilityEdit.findRegion(runtime.regionId());
  const auto* afterNote = afterRegion == nullptr
                              ? nullptr
                              : afterRegion->findNote(accessibilityNoteId);
  const auto* afterLyric = afterNote == nullptr || afterRegion == nullptr
                               ? nullptr
                               : afterRegion->findLyric(afterNote->lyricTokenId);
  if (afterLyric == nullptr || afterLyric->surface != U"い") return 29;

  std::cout << "Phase 11 tests PASS: notes=" << initial.noteCount()
            << " previewEnergy=" << energy
            << " liveEnergy=" << liveEnergy << '\n';
  return 0;
}
