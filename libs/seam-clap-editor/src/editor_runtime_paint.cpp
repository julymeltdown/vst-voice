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

void EditorRuntime::rebuildTechnicalModelsLocked() {
  if (controller_ == nullptr) return;
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr) return;
  auto phonemes = phonemesLocked();
  const auto layout = painter_.layout();
  const auto characterFull =
      session_.project().settings().characterDisplay ==
          domain::CharacterDisplayMode::Full &&
      character_.portrait(character::State::Neutral) != nullptr;
  const auto editorRight = std::max(
      layout.keyboardWidth + 180.0,
      logicalWidth_ - (characterFull ? layout.characterDockWidth : 0.0));
  const auto statusTop = logicalHeight_ - layout.statusHeight;
  const auto pianoBottom = std::max(layout.contentTop() + 100.0,
                                    statusTop - layout.lanesHeight());
  phonemeLane_.rebuild(controller_->pianoRoll(), phonemes,
                       pianoBottom + 4.0,
                       layout.phonemeLaneHeight - 8.0);

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
          pianoBottom + layout.phonemeLaneHeight + 5.0,
          layout.unitLaneHeight - 10.0, renderSampleRate_);
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
  const auto statusTop = logicalHeight_ - layout.statusHeight;
  const auto pianoBottom = std::max(layout.contentTop() + 100.0,
                                    statusTop - layout.lanesHeight());
  const auto automationTop = pianoBottom + layout.phonemeLaneHeight +
                             layout.unitLaneHeight + layout.seamLaneHeight;
  const auto centerY = automationTop + layout.automationLaneHeight * 0.5;
  for (const auto& value : region->pitchAutomation.points()) {
    const auto x = layout.keyboardWidth +
                   controller_->pianoRoll().timeline().tickToPixel(value.tick);
    const auto normalized =
        std::clamp(static_cast<double>(value.cents) / 600.0, -1.0, 1.0);
    const auto y = centerY - normalized * (layout.automationLaneHeight * 0.38);
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

void EditorRuntime::paintSampleMicroscope(
    native_ui::RasterCanvas& canvas) noexcept {
  if (!microscopeUnitId_.has_value()) return;
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  canvas.fillRect(ui::Rect{52.0, 74.0, width - 104.0, height - 122.0},
                  native_ui::Color{11, 10, 14, 248});
  canvas.strokeRect(ui::Rect{52.0, 74.0, width - 104.0, height - 122.0},
                    native_ui::Color{182, 104, 145, 255}, 2.0);
  canvas.drawText(ui::Point{72.0, 91.0},
                  "SAMPLE MICROSCOPE / " + *microscopeUnitId_,
                  native_ui::Color{241, 235, 242, 255}, 11.0);
  canvas.drawText(ui::Point{width - 190.0, 91.0}, "ESC / DOUBLE CLICK CLOSE",
                  native_ui::Color{170, 159, 171, 255}, 7.0);
  const auto wave = microscope_.waveformBounds();
  canvas.fillRect(wave, native_ui::Color{25, 23, 30, 255});
  const auto center = wave.y + wave.height * 0.5;
  for (const auto& column : microscope_.waveform()) {
    canvas.line(ui::Point{column.x, center - column.maximum * wave.height * 0.45},
                ui::Point{column.x, center - column.minimum * wave.height * 0.45},
                native_ui::Color{150, 102, 136, 255}, 1.0);
  }
  const auto specBounds = microscope_.spectrogramBounds();
  canvas.fillRect(specBounds, native_ui::Color{16, 18, 22, 255});
  const auto& spec = microscope_.spectrogram();
  if (spec.columns > 0U && spec.bins > 0U) {
    const auto columnStep = std::max<std::size_t>(1U, spec.columns / 280U);
    const auto binStep = std::max<std::size_t>(1U, spec.bins / 90U);
    for (std::size_t column = 0U; column < spec.columns; column += columnStep) {
      for (std::size_t bin = 0U; bin < spec.bins; bin += binStep) {
        const auto db = std::clamp((spec.at(column, bin) + 90.0F) / 84.0F,
                                   0.0F, 1.0F);
        if (db < 0.07F) continue;
        const auto x = specBounds.x +
                       static_cast<double>(column) / static_cast<double>(spec.columns) * specBounds.width;
        const auto y = specBounds.bottom() -
                       static_cast<double>(bin + binStep) / static_cast<double>(spec.bins) * specBounds.height;
        canvas.fillRect(ui::Rect{x, y,
                                 std::max(1.0, specBounds.width * static_cast<double>(columnStep) /
                                                   static_cast<double>(spec.columns)),
                                 std::max(1.0, specBounds.height * static_cast<double>(binStep) /
                                                   static_cast<double>(spec.bins))},
                        native_ui::Color{static_cast<std::uint8_t>(45 + 165 * db),
                                         static_cast<std::uint8_t>(35 + 80 * db),
                                         static_cast<std::uint8_t>(65 + 120 * db),
                                         255});
      }
    }
  }
  for (const auto& marker : microscope_.markers()) {
    canvas.line(ui::Point{marker.x, wave.y},
                ui::Point{marker.x, specBounds.bottom()},
                native_ui::Color{94, 192, 190, 220}, 1.0);
    canvas.drawText(ui::Point{marker.x + 2.0, wave.y + 4.0}, marker.label,
                    native_ui::Color{180, 210, 205, 255}, 6.0);
  }
  for (const auto& mark : microscope_.pitchMarks()) {
    canvas.line(ui::Point{mark.x, wave.y}, ui::Point{mark.x, wave.bottom()},
                mark.locked ? native_ui::Color{235, 175, 205, 255}
                            : native_ui::Color{118, 96, 142, 170},
                0.5);
  }
}

void EditorRuntime::paintPhase12BOverlay(
    native_ui::RasterCanvas& canvas) noexcept {
  const auto* track = session_.project().findVocalTrack(trackId_);
  const auto* region = track == nullptr ? nullptr : track->findRegion(regionId_);
  if (track != nullptr && region != nullptr) {
    const auto title = "TRACK " + track->name + " / REGION " + region->name +
                       " / OUT " +
                       std::to_string(session_.project().routing().deviceOutputChannels) +
                       "CH";
    canvas.fillRect(ui::Rect{565.0, 8.0, 360.0, 40.0},
                    native_ui::Color{20, 18, 24, 235});
    canvas.drawText(ui::Point{575.0, 16.0}, title,
                    native_ui::Color{229, 218, 228, 255}, 8.0);
    canvas.drawText(ui::Point{575.0, 32.0},
                    "GAIN " + std::to_string(track->gainDb).substr(0, 5) +
                        "  PAN " + std::to_string(track->pan).substr(0, 5) +
                        (track->muted ? "  MUTE" : "") +
                        (track->solo ? "  SOLO" : ""),
                    native_ui::Color{160, 150, 164, 255}, 7.0);
  }
  for (const auto& visual : phonemeLane_.visuals()) {
    const auto handle = selectedUnitKey_.has_value() &&
                                *selectedUnitKey_ == visual.key
                            ? native_ui::Color{242, 178, 208, 255}
                            : native_ui::Color{125, 105, 132, 210};
    canvas.fillRect(ui::Rect{visual.bounds.x - 2.0, visual.bounds.y,
                             4.0, visual.bounds.height}, handle);
    canvas.fillRect(ui::Rect{visual.bounds.right() - 2.0, visual.bounds.y,
                             4.0, visual.bounds.height}, handle);
  }
  for (const auto& visual : unitLane_.visuals()) {
    if (selectedUnitKey_.has_value() && *selectedUnitKey_ == visual.startKey) {
      canvas.strokeRect(visual.bounds,
                        native_ui::Color{242, 178, 208, 255}, 2.0);
    }
    if (!visual.alternatives.empty()) {
      canvas.drawText(ui::Point{visual.bounds.right() - 30.0,
                                visual.bounds.y + 6.0},
                      "+" + std::to_string(visual.alternatives.size()),
                      native_ui::Color{187, 176, 191, 255}, 6.0);
    }
  }
  const auto host = hostTimelineState_;
  const auto hostLabel = "HOST " + std::to_string(host.tempo).substr(0, 6) +
                         " BPM  " + std::to_string(host.numerator) + "/" +
                         std::to_string(host.denominator) +
                         (host.loopActive ? " LOOP" : "");
  canvas.drawText(ui::Point{330.0, 52.0}, hostLabel,
                  native_ui::Color{111, 193, 190, 255}, 7.0);
  paintSampleMicroscope(canvas);
}

native_ui::EditorSceneState EditorRuntime::sceneState() const {
  auto state = controller_->sceneState();
  state.characterMode = session_.project().settings().characterDisplay;
  state.characterPortrait = character_.portrait(state.characterState);
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  return state;
}

void EditorRuntime::paint(native_ui::RasterCanvas& canvas) noexcept {
  std::lock_guard lock(mutex_);
  rebuildTechnicalModelsLocked();
  painter_.paint(canvas, controller_->pianoRoll(), sceneState());
  const auto seam = primarySeamAmount();
  const auto width = canvas.logicalWidth();
  const auto left = std::max(520.0, width - 360.0);
  canvas.fillRect(ui::Rect{left, 9.0, 330.0, 38.0},
                  native_ui::Color{20, 18, 24, 230});
  canvas.drawText(ui::Point{left + 10.0, 18.0}, "SEAM BOUNDARY",
                  native_ui::Color{201, 190, 201, 255}, 8.0);
  canvas.fillRect(ui::Rect{left + 112.0, 20.0, 190.0, 10.0},
                  native_ui::Color{58, 52, 62, 255});
  canvas.fillRect(ui::Rect{left + 112.0, 20.0, 190.0 * seam, 10.0},
                  native_ui::Color{168, 82, 120, 255});
  const auto stats = renderStats();
  const auto preview = renderedPreview();
  const auto label = "PROD " + std::string{previewStatusName(preview->status)} +
                     " " + std::to_string(stats.completed) + "/" +
                     std::to_string(stats.submitted);
  canvas.drawText(ui::Point{left + 112.0, 34.0}, label,
                  preview->status == PreviewStatus::Ready
                      ? native_ui::Color{153, 178, 169, 255}
                      : native_ui::Color{205, 126, 126, 255},
                  7.0);
  paintPhase12BOverlay(canvas);
}

}  // namespace seam::clap_editor
