#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"
#include "test_support.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
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

// A real published phrase must reach the plug-in's drawing surface. Only the card's character
// association is supplied by this fixture: the public-domain demo bank ships without an avatar.
bool verifyCharacterDock(seam::clap_editor::EditorRuntime& runtime,
                         const std::filesystem::path& packageRoot) {
  using namespace seam;
  native_ui::CharacterPresentation artwork;
  if (const auto loaded = artwork.load(packageRoot); !loaded) {
    std::cerr << "CLAP character package failed to load: " << loaded.error().message << '\n';
    return false;
  }
  const auto& manifest = artwork.package()->manifest;
  auto cards = runtime.controller().sceneState().voicebankCards;
  const auto project = runtime.projectCopy();
  const auto& bank = project.vocalTracks().front().voicebank;
  for (auto& card : cards) {
    if (card.id == bank.id && card.version == bank.version && card.contentHash == bank.contentHash) {
      card.characterId = manifest.characterId;
      card.characterVersion = manifest.version;
    }
  }
  runtime.controller().setVoicebankCards(std::move(cards));
  runtime.controller().setCharacterBinding({
      .id = manifest.characterId, .version = manifest.version,
      .voicebankId = manifest.voicebankId,
      .hasPerformance = artwork.hasPerformanceAssets()});
  if (runtime.controller().voicebankBrowserVisible())
    runtime.keyDown({.key = native_ui::NativeKey::V});
  runtime.resize(1100.0, 720.0);
  const native_ui::EditorSceneLayout layout;
  const native_ui::EditorScenePainter painter;
  const auto dockLeft = 1100.0 - layout.characterDockWidth;
  const auto check = [&](clap_editor::HostTimelineState host,
                          character::MouthShape mouth, bool performing) {
    runtime.setHostTimelineState(host);
    native_ui::PixelSurface actual{1100U, 720U};
    native_ui::RasterCanvas canvas{actual};
    runtime.paint(canvas);
    const auto state = runtime.controller().sceneState();
    if (!state.voiceIdentity.characterActive || !state.characterPerformance.has_value() ||
        state.characterPerformance->mouth != mouth ||
        state.characterPerformance->performing != performing) {
      std::cerr << "CLAP character performance mismatch at " << host.seconds << " seconds"
                << " active=" << state.voiceIdentity.characterActive
                << " snapshot=" << state.characterPerformance.has_value();
      if (state.characterPerformance.has_value())
        std::cerr << " mouth=" << character::mouthShapeName(state.characterPerformance->mouth)
                  << " performing=" << state.characterPerformance->performing;
      std::cerr << " expected=" << character::mouthShapeName(mouth) << '/' << performing << '\n';
      return false;
    }
    const auto& performance = *state.characterPerformance;
    const auto overlay = layout.diagnosticHeight(!state.diagnostics.empty()) +
                         layout.exportHeight(state.exportProgress.totalFiles != 0U);
    const auto portrait = layout.characterDockPortraitBounds(
        dockLeft, 720.0 - layout.statusHeight - overlay, 1100.0);
    const auto top = layout.characterDockMetadataTop(portrait) +
        layout.characterDockNameToRoleAdvance + layout.characterDockRoleToStateAdvance +
        layout.characterDockStateToModeAdvance + layout.characterDockPerformanceAdvance;
    ui::Rect mouthBounds{
        dockLeft + layout.characterDockTextInsetX + layout.characterDockPerformanceBarWidth,
        top - layout.characterDockMouthAssetHeight,
        layout.characterDockMouthAssetWidth, layout.characterDockMouthAssetHeight};
    const auto* portraitImage = artwork.portrait(character::State::Neutral);
    std::optional<ui::Rect> fittedPortrait;
    if (portraitImage != nullptr && portraitImage->width() > 0U &&
        portraitImage->height() > 0U) {
      const auto imageWidth = static_cast<double>(portraitImage->width());
      const auto imageHeight = static_cast<double>(portraitImage->height());
      const auto scale = std::min(portrait.width / imageWidth,
                                  portrait.height / imageHeight);
      const auto width = imageWidth * scale;
      const auto height = imageHeight * scale;
      fittedPortrait = ui::Rect{portrait.x + (portrait.width - width) * 0.5,
                                portrait.y + (portrait.height - height) * 0.5,
                                width, height};
    }
    const auto placement = artwork.mouthPlacement();
    if (placement && fittedPortrait) {
      mouthBounds = ui::Rect{
          fittedPortrait->x + placement->x * fittedPortrait->width,
          fittedPortrait->y + placement->y * fittedPortrait->height,
          placement->width * fittedPortrait->width,
          placement->height * fittedPortrait->height};
    }
    native_ui::PixelSurface expected{1100U, 720U};
    native_ui::RasterCanvas expectedCanvas{expected};
    expected.clear(painter.theme().characterBackground);
    if (placement && fittedPortrait) {
      expectedCanvas.drawImageNearest(*fittedPortrait, *portraitImage,
                                      layout.characterDockPortraitScale);
    }
    if (!performance.reducedMotion) {
      if (const auto* asset = artwork.mouth(mouth); asset != nullptr) {
        expectedCanvas.drawImageNearest(mouthBounds, *asset,
                                        placement ? 1.0 : layout.characterDockPortraitScale);
      } else {
        const auto height = layout.characterDockPerformanceGlyphHeight *
                            (0.2 + 0.8 * performance.energy);
        expectedCanvas.fillRect({mouthBounds.x, top - height,
                                  layout.characterDockPerformanceGlyphWidth, height},
            performing ? painter.theme().accent : painter.theme().gridStrong);
      }
    }
    for (auto y = static_cast<std::uint32_t>(mouthBounds.y);
         y < static_cast<std::uint32_t>(mouthBounds.bottom()); ++y) {
      for (auto x = static_cast<std::uint32_t>(mouthBounds.x);
           x < static_cast<std::uint32_t>(mouthBounds.right()); ++x) {
        if (actual.pixels()[y * 1100U + x] != expected.pixels()[y * 1100U + x]) {
          std::cerr << "CLAP mouth artwork/fallback pixel mismatch at " << x << ',' << y << '\n';
          return false;
        }
      }
    }
    return true;
  };
  const auto firstVowel = project.tempoMap().secondsAt(time::Tick{500});
  const auto secondVowel = project.tempoMap().secondsAt(time::Tick{1000});
  if (!check({.playing = true, .hasSeconds = true, .seconds = firstVowel},
             character::MouthShape::Round, true) ||
      !check({.playing = true, .hasSeconds = true, .seconds = secondVowel},
             character::MouthShape::Wide, true) ||
      !check({.playing = false, .hasSeconds = true, .seconds = secondVowel},
             character::MouthShape::Closed, false) ||
      !check({.playing = true, .hasSeconds = true, .seconds = firstVowel + 1.0,
              .loopActive = true, .loopHasSeconds = true,
              .loopStartSeconds = 0.0, .loopEndSeconds = 1.0},
             character::MouthShape::Round, true) ||
      !check({.playing = true, .hasSeconds = true, .seconds = 20.0},
             character::MouthShape::Closed, false)) return false;
  runtime.setHostTimelineState({});
  return true;
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

  auto dockProject = runtime.projectCopy();
  dockProject.settings().characterDisplay = seam::domain::CharacterDisplayMode::Full;
  // The checked-in development artwork belongs to official.voice.01, not the demo bank. Keep a
  // refusal check against that real package, then make a process-private package whose manifest is
  // explicitly paired to the synthetic test voice for the positive paint mechanics below.
  {
    seam::clap_editor::EditorRuntime mismatchedRuntime{dockProject, "assets/character-01", roots};
    if (!awaitRender(mismatchedRuntime, [&] { mismatchedRuntime.requestRender(48000U); }, [&] {
          return mismatchedRuntime.renderedPreview()->status == seam::clap_editor::PreviewStatus::Ready;
        })) return 47;
    auto cards = mismatchedRuntime.controller().sceneState().voicebankCards;
    seam::native_ui::CharacterPresentation originalArtwork;
    if (!originalArtwork.load("assets/character-01")) return 47;
    const auto& originalManifest = originalArtwork.package()->manifest;
    for (auto& card : cards) {
      if (card.id == dockProject.vocalTracks().front().voicebank.id) {
        card.characterId = originalManifest.characterId;
        card.characterVersion = originalManifest.version;
      }
    }
    mismatchedRuntime.controller().setVoicebankCards(std::move(cards));
    mismatchedRuntime.resize(1100.0, 720.0);
    seam::native_ui::PixelSurface actual{1100U, 720U};
    seam::native_ui::RasterCanvas canvas{actual};
    mismatchedRuntime.paint(canvas);
    const auto state = mismatchedRuntime.controller().sceneState();
    if (state.voiceIdentity.characterActive || state.characterPerformance.has_value()) return 47;
  }
  const auto performanceRoot = seam::test::support::temporaryDirectory("clap-matched-character");
  std::filesystem::copy("assets/character-01/runtime", performanceRoot / "runtime",
                        std::filesystem::copy_options::recursive);
  const auto manifestText = seam::core::readTextFileLimited("assets/character-01/manifest.json", 65536U);
  if (!manifestText) return 48;
  auto performanceManifest = seam::formats::parseJson(manifestText.value());
  if (!performanceManifest) return 48;
  performanceManifest.value().asObject()["voicebankId"] =
      seam::formats::JsonValue{dockProject.vocalTracks().front().voicebank.id};
  if (!seam::core::durableAtomicWriteText(performanceRoot / "manifest.json",
          seam::formats::stringifyJson(performanceManifest.value()))) return 48;
  {
    seam::clap_editor::EditorRuntime performanceRuntime{dockProject, performanceRoot, roots};
    if (!awaitRender(performanceRuntime, [&] { performanceRuntime.requestRender(48000U); }, [&] {
          return performanceRuntime.renderedPreview()->status == seam::clap_editor::PreviewStatus::Ready;
        }) || !verifyCharacterDock(performanceRuntime, performanceRoot)) return 47;
  }
  // Legacy status-only packages still draw their fallback glyph, without borrowing a mouth from
  // another package. The source package is copied into a process-private temporary directory.
  const auto statusRoot = seam::test::support::temporaryDirectory("clap-status-character");
  std::filesystem::copy(performanceRoot / "runtime", statusRoot / "runtime",
                        std::filesystem::copy_options::recursive);
  auto statusManifest = seam::formats::parseJson(
      seam::formats::stringifyJson(performanceManifest.value()));
  if (!statusManifest) return 48;
  statusManifest.value().asObject().erase("mouths");
  statusManifest.value().asObject().erase("mouthPlacement");
  statusManifest.value().asObject().erase("developmentOnly");
  statusManifest.value().asObject()["schemaVersion"] = seam::formats::JsonValue{std::int64_t{1}};
  if (!seam::core::durableAtomicWriteText(statusRoot / "manifest.json",
          seam::formats::stringifyJson(statusManifest.value()))) return 48;
  {
    seam::clap_editor::EditorRuntime statusRuntime{dockProject, statusRoot, roots};
    if (!awaitRender(statusRuntime, [&] { statusRuntime.requestRender(48000U); }, [&] {
          return statusRuntime.renderedPreview()->status == seam::clap_editor::PreviewStatus::Ready;
        }) || !verifyCharacterDock(statusRuntime, statusRoot)) return 49;
  }
  std::filesystem::remove_all(statusRoot);
  std::filesystem::remove_all(performanceRoot);

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
