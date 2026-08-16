#include "seam/native_ui/voicebank_studio.hpp"

#include <algorithm>
#include <cmath>

namespace seam::native_ui {
namespace {

ui::Rect waveformRect(double width, double height) {
  const auto left = 270.0;
  const auto rightPanel = 250.0;
  return ui::Rect{left, 100.0, std::max(160.0, width - left - rightPanel - 18.0),
                  std::max(120.0, (height - 170.0) * 0.42)};
}

ui::Rect spectrogramRect(double width, double height) {
  auto wave = waveformRect(width, height);
  return ui::Rect{wave.x, wave.bottom() + 16.0, wave.width,
                  std::max(120.0, height - wave.bottom() - 76.0)};
}

}  // namespace

core::Result<void> VoicebankStudioController::openManifest(
    const std::filesystem::path& manifestPath, double logicalWidth,
    double logicalHeight) {
  auto loaded = codec_.load(manifestPath);
  if (!loaded) return core::Result<void>{loaded.error()};
  manifestPath_ = std::filesystem::absolute(manifestPath).lexically_normal();
  root_ = manifestPath_.parent_path();
  manifest_ = std::move(loaded.value());
  logicalWidth_ = std::max(720.0, logicalWidth);
  logicalHeight_ = std::max(520.0, logicalHeight);
  if (manifest_.units.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Voicebank manifest contains no units", manifestPath.string());
  }
  selectedIndex_ = 0U;
  dirty_ = false;
  status_ = "LOADED";
  return rebuildSelected();
}

core::Result<void> VoicebankStudioController::save() {
  if (manifestPath_.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Voicebank Studio has no manifest path");
  }
  const auto validation = manifest_.validate();
  if (!validation) return validation;
  auto result = codec_.save(manifest_, manifestPath_);
  if (result) {
    dirty_ = false;
    status_ = "SAVED";
  }
  return result;
}

core::Result<void> VoicebankStudioController::selectUnit(std::size_t index) {
  if (index >= manifest_.units.size()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Voicebank unit index is outside the manifest");
  }
  selectedIndex_ = index;
  status_ = "UNIT " + std::to_string(index + 1U);
  return rebuildSelected();
}

const voicebank::Unit* VoicebankStudioController::selectedUnit() const noexcept {
  return selectedIndex_ < manifest_.units.size() ? &manifest_.units[selectedIndex_]
                                                 : nullptr;
}

voicebank::Unit* VoicebankStudioController::selectedUnit() noexcept {
  return selectedIndex_ < manifest_.units.size() ? &manifest_.units[selectedIndex_]
                                                 : nullptr;
}

std::filesystem::path VoicebankStudioController::selectedAudioPath() const {
  const auto* unit = selectedUnit();
  return unit == nullptr ? std::filesystem::path{} : root_ / unit->audioPath;
}

core::Result<void> VoicebankStudioController::rebuildSelected() {
  const auto* unit = selectedUnit();
  if (unit == nullptr) {
    return core::failure(core::ErrorCode::NotFound, "No voicebank unit is selected");
  }
  auto audio = voicebank::readWav(selectedAudioPath());
  if (!audio) return core::Result<void>{audio.error()};
  audio_ = std::move(audio.value());
  const auto wave = waveformRect(logicalWidth_, logicalHeight_);
  const auto spectrogram = spectrogramRect(logicalWidth_, logicalHeight_);
  auto built = microscope_.rebuild(*unit, audio_, wave, spectrogram, 1200U,
                                   voicebank::SpectrogramConfig{
                                       .fftSize = 1024U,
                                       .hopSize = 256U,
                                   });
  if (!built) return built;
  return core::success();
}

core::Result<void> VoicebankStudioController::moveSelectedMarker(
    ui::AcousticMarkerKind marker, double x) {
  auto* unit = selectedUnit();
  if (unit == nullptr) return core::failure(core::ErrorCode::NotFound, "No unit selected");
  auto moved = microscope_.moveMarker(
      *unit, marker, x, static_cast<time::SampleFrame>(audio_.frameCount()));
  if (moved) {
    dirty_ = true;
    status_ = "MARKER EDITED";
  }
  return moved;
}

core::Result<void> VoicebankStudioController::moveSelectedPitchMark(
    std::size_t index, double x) {
  auto* unit = selectedUnit();
  if (unit == nullptr) return core::failure(core::ErrorCode::NotFound, "No unit selected");
  auto moved = microscope_.movePitchMark(*unit, index, x);
  if (moved) {
    dirty_ = true;
    status_ = "PITCH MARK EDITED";
  }
  return moved;
}

void VoicebankStudioController::resize(double logicalWidth, double logicalHeight) {
  logicalWidth_ = std::max(720.0, logicalWidth);
  logicalHeight_ = std::max(520.0, logicalHeight);
  if (selectedUnit() != nullptr && audio_.frameCount() != 0U) {
    static_cast<void>(rebuildSelected());
  }
}

void VoicebankStudioScenePainter::paint(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    bool recording, std::string_view recordingBackend) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  canvas.clear(theme_.background);
  canvas.fillRect(ui::Rect{0.0, 0.0, width, 72.0}, theme_.panel);
  canvas.drawText(ui::Point{18.0, 16.0}, "SEAM VOICEBANK STUDIO",
                  theme_.primaryText, 14.0);
  canvas.drawText(ui::Point{18.0, 42.0}, controller.manifest().displayName,
                  theme_.secondaryText, 8.0);
  if (!controller.manifest().characterId.empty()) {
    canvas.drawText(ui::Point{18.0, 57.0},
                    "CHARACTER " + controller.manifest().characterId + " @ " +
                        controller.manifest().characterVersion,
                    theme_.secondaryText, 6.0);
  }
  canvas.drawText(ui::Point{width - 360.0, 18.0},
                  recording ? "RECORDING" : controller.status(),
                  recording ? theme_.accent : theme_.secondaryText, 8.0);
  canvas.drawText(ui::Point{width - 360.0, 40.0},
                  "MIC " + std::string{recordingBackend}, theme_.secondaryText, 7.0);

  canvas.fillRect(ui::Rect{0.0, 72.0, 252.0, height - 72.0}, theme_.panelAlternate);
  canvas.drawText(ui::Point{12.0, 86.0}, "UNITS", theme_.secondaryText, 8.0);
  const auto& units = controller.manifest().units;
  const auto first = controller.selectedIndex() > 8U ? controller.selectedIndex() - 8U : 0U;
  const auto last = std::min(units.size(), first + 18U);
  for (std::size_t index = first; index < last; ++index) {
    const auto y = 108.0 + static_cast<double>(index - first) * 32.0;
    const ui::Rect row{8.0, y, 236.0, 28.0};
    canvas.fillRect(row, index == controller.selectedIndex() ? theme_.selected
                                                             : theme_.panelAlternate);
    if (index == controller.selectedIndex()) canvas.strokeRect(row, theme_.accent, 1.0);
    auto label = units[index].alias.empty() ? units[index].id : units[index].alias;
    if (label.size() > 24U) label.resize(24U);
    canvas.drawText(ui::Point{16.0, y + 7.0}, label, theme_.primaryText, 7.0);
  }

  const auto* unit = controller.selectedUnit();
  if (unit == nullptr) return;
  const auto& microscope = controller.microscope();
  const auto wave = microscope.waveformBounds();
  const auto spec = microscope.spectrogramBounds();
  canvas.fillRect(wave, Color{17, 16, 20, 255});
  canvas.fillRect(spec, Color{12, 12, 15, 255});
  canvas.strokeRect(wave, theme_.grid, 1.0);
  canvas.strokeRect(spec, theme_.grid, 1.0);
  const auto center = wave.y + wave.height * 0.5;
  canvas.line(ui::Point{wave.x, center}, ui::Point{wave.right(), center}, theme_.grid, 0.5);
  for (const auto& column : microscope.waveform()) {
    const auto y1 = center - static_cast<double>(column.maximum) * wave.height * 0.46;
    const auto y2 = center - static_cast<double>(column.minimum) * wave.height * 0.46;
    canvas.line(ui::Point{column.x, y1}, ui::Point{column.x, y2}, theme_.waveform, 1.0);
  }

  const auto& spectrogram = microscope.spectrogram();
  if (spectrogram.columns > 0U && spectrogram.bins > 0U &&
      !spectrogram.decibels.empty()) {
    const auto maxFrames = std::min<std::size_t>(spectrogram.columns, 420U);
    const auto maxBins = std::min<std::size_t>(spectrogram.bins, 96U);
    for (std::size_t frame = 0U; frame < maxFrames; ++frame) {
      const auto x = spec.x + static_cast<double>(frame) /
          static_cast<double>(std::max<std::size_t>(1U, maxFrames - 1U)) * spec.width;
      for (std::size_t bin = 0U; bin < maxBins; ++bin) {
        const auto sourceFrame = frame * spectrogram.columns / maxFrames;
        const auto sourceBin = bin * spectrogram.bins / maxBins;
        const auto value = spectrogram.decibels[
            std::min(sourceFrame, spectrogram.columns - 1U) * spectrogram.bins +
            std::min(sourceBin, spectrogram.bins - 1U)];
        const auto normalized = std::clamp((static_cast<double>(value) + 90.0) / 90.0,
                                           0.0, 1.0);
        if (normalized < 0.08) continue;
        const auto y = spec.bottom() - static_cast<double>(bin + 1U) /
            static_cast<double>(maxBins) * spec.height;
        canvas.fillRect(ui::Rect{x, y, std::max(1.0, spec.width / static_cast<double>(maxFrames) + 0.4),
                                 std::max(1.0, spec.height / static_cast<double>(maxBins) + 0.4)},
                        Color{static_cast<std::uint8_t>(70 + normalized * 120.0),
                              static_cast<std::uint8_t>(50 + normalized * 75.0),
                              static_cast<std::uint8_t>(80 + normalized * 100.0), 255});
      }
    }
  }

  for (const auto& marker : microscope.markers()) {
    canvas.line(ui::Point{marker.x, wave.y}, ui::Point{marker.x, spec.bottom()},
                marker.kind == ui::AcousticMarkerKind::VowelOnset ? theme_.accent
                                                                  : theme_.grid, 1.0);
    canvas.drawText(ui::Point{marker.x + 3.0, wave.y + 5.0}, marker.label,
                    theme_.secondaryText, 6.0);
  }
  for (const auto& mark : microscope.pitchMarks()) {
    canvas.line(ui::Point{mark.x, wave.y}, ui::Point{mark.x, wave.y + 18.0},
                mark.locked ? theme_.accent : theme_.pitch, 1.0);
  }

  const auto inspectorX = width - 238.0;
  canvas.fillRect(ui::Rect{inspectorX, 72.0, 238.0, height - 72.0}, theme_.panel);
  canvas.drawText(ui::Point{inspectorX + 12.0, 88.0}, "UNIT INSPECTOR",
                  theme_.secondaryText, 8.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 114.0}, unit->id,
                  theme_.primaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 138.0},
                  "ROOT MIDI " + std::to_string(unit->rootMidi), theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 157.0},
                  "RENDER " + std::string{voicebank::rendererHintName(unit->renderer)},
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 182.0}, "UP/DOWN UNIT",
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 199.0}, "DRAG MARKERS",
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 216.0}, "CTRL+S SAVE",
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 233.0}, "R RECORD TAKE",
                  theme_.secondaryText, 7.0);
}

}  // namespace seam::native_ui
