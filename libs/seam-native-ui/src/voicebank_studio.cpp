#include "seam/native_ui/voicebank_studio.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/text/unicode.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

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

std::vector<ui::Rect> voicebankStudioMarkerLabelBounds(
    std::span<const ui::AcousticMarkerVisual> markers,
    ui::Rect waveformBounds) {
  std::vector<ui::Rect> result;
  result.reserve(markers.size());
  std::vector<double> rowRights;
  for (const auto& marker : markers) {
    const auto displayWidth =
        static_cast<double>(text::utf8DisplayWidth(marker.label));
    const auto estimatedWidth = std::max(
        12.0, displayWidth * 3.8 + 4.0);
    const auto width = std::min(
        estimatedWidth, std::max(1.0, waveformBounds.width - 4.0));
    const auto leftLimit = waveformBounds.x + 2.0;
    const auto rightLimit = waveformBounds.right() - width - 2.0;
    const auto x = std::clamp(marker.x + 3.0, leftLimit, rightLimit);
    std::size_t row = 0U;
    while (row < rowRights.size() && x < rowRights[row] + 2.0) ++row;
    if (row == rowRights.size()) {
      rowRights.push_back(x + width);
    } else {
      rowRights[row] = x + width;
    }
    result.push_back(ui::Rect{x, waveformBounds.y + 2.0 +
                                      static_cast<double>(row) * 9.0,
                              width, 7.0});
  }
  return result;
}

core::Result<std::filesystem::path> nextVoicebankRecordingPath(
    const std::filesystem::path& directory, std::string_view unitId) {
  std::error_code error;
  const auto directoryStatus = std::filesystem::symlink_status(directory, error);
  if (error || std::filesystem::is_symlink(directoryStatus) ||
      !std::filesystem::is_directory(directoryStatus)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Recording directory must be a real directory", directory.string());
  }
  std::string stem;
  stem.reserve(std::min<std::size_t>(unitId.size(), 80U));
  for (const auto character : unitId) {
    if (stem.size() == 80U) break;
    const auto byte = static_cast<unsigned char>(character);
    stem.push_back((byte < 128U && (std::isalnum(byte) != 0 || character == '-' ||
                                   character == '_'))
                       ? character
                       : '_');
  }
  if (stem.empty()) stem = "take";
  auto upper = stem;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](char value) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  });
  const auto reserved = upper == "CON" || upper == "PRN" || upper == "AUX" ||
                        upper == "NUL" ||
                        (upper.size() == 4U &&
                         (upper.starts_with("COM") || upper.starts_with("LPT")) &&
                         upper.back() >= '1' && upper.back() <= '9');
  if (reserved) stem.insert(stem.begin(), '_');
  const auto resolvedDirectory = std::filesystem::canonical(directory, error);
  if (error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to resolve recording directory", error.message());
  }
  for (std::size_t index = 1U; index <= 10000U; ++index) {
    std::ostringstream name;
    name << stem << "-take-" << std::setw(4) << std::setfill('0') << index
         << ".wav";
    const auto candidate = directory / name.str();
    if (candidate.parent_path() != directory ||
        std::filesystem::canonical(candidate.parent_path(), error) !=
            resolvedDirectory ||
        error) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::Conflict,
          "Recording destination is not a direct child", candidate.string());
    }
    const auto status = std::filesystem::symlink_status(candidate, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
      return candidate;
    }
    if (error) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::IoError,
          "Unable to inspect recording destination", error.message());
    }
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::Conflict,
          "Recording destination is not a regular file", candidate.string());
    }
  }
  return core::failure<std::filesystem::path>(
      core::ErrorCode::Conflict,
      "Voicebank Studio has no available take filename");
}

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
  takeInspection_.reset();
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
  takeInspection_.reset();
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

core::Result<void> VoicebankStudioController::inspectTake(
    const std::filesystem::path& path, std::int32_t expectedRootMidi) {
  auto inspected = voicebank::inspectDryTake(path, expectedRootMidi);
  if (!inspected) {
    takeInspection_.reset();
    status_ = "TAKE ERROR";
    return core::Result<void>{inspected.error()};
  }
  takeInspection_ = std::move(inspected.value());
  status_ = takeInspection_->accepted() ? "TAKE ACCEPTED" : "TAKE REVIEW";
  return core::success();
}

core::Result<std::filesystem::path>
VoicebankStudioController::persistTakeInspection(
    const std::filesystem::path& takePath) const {
  if (!takeInspection_.has_value()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidState,
        "Voicebank Studio has no take inspection to persist");
  }
  const auto digest = core::sha256File(
      takePath, voicebank::kMaximumSupportedWavBytes);
  if (!digest) return core::Result<std::filesystem::path>{digest.error()};
  const auto& inspection = *takeInspection_;
  if (inspection.sourceSha256.empty() ||
      digest.value() != inspection.sourceSha256) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Recorded take changed after inspection");
  }
  auto sidecar = takePath;
  sidecar += ".inspection.json";
  std::error_code error;
  const auto status = std::filesystem::symlink_status(sidecar, error);
  if (error != std::errc::no_such_file_or_directory && error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to inspect take inspection destination", error.message());
  }
  if (!error && status.type() != std::filesystem::file_type::not_found) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Take inspection destination already exists", sidecar.string());
  }
  formats::JsonValue::Object quality{
      {"formatValid", formats::JsonValue{inspection.formatValid}},
      {"finite", formats::JsonValue{inspection.finite}},
      {"clippingFree", formats::JsonValue{inspection.clippingFree}},
      {"silenceFree", formats::JsonValue{inspection.silenceFree}},
      {"dcOffsetFree", formats::JsonValue{inspection.dcOffsetFree}},
      {"rootPitchValid", formats::JsonValue{inspection.rootPitchValid}},
      {"peak", formats::JsonValue{static_cast<double>(inspection.peak)}},
      {"rms", formats::JsonValue{inspection.rms}},
      {"dcOffset", formats::JsonValue{inspection.dcOffset}},
  };
  if (inspection.analyzedRootMidi.has_value()) {
    quality.emplace("analyzedRootMidi", formats::JsonValue{
        static_cast<std::int64_t>(*inspection.analyzedRootMidi)});
  }
  formats::JsonValue::Object record{
      {"schemaVersion", formats::JsonValue{std::int64_t{1}}},
      {"takeFile", formats::JsonValue{takePath.filename().generic_string()}},
      {"takeSha256", formats::JsonValue{inspection.sourceSha256}},
      {"status", formats::JsonValue{
          inspection.accepted() ? "ACCEPTED" : "REVIEW"}},
      {"sampleRate", formats::JsonValue{
          static_cast<std::int64_t>(inspection.sampleRate)}},
      {"channels", formats::JsonValue{
          static_cast<std::int64_t>(inspection.channels)}},
      {"bitsPerSample", formats::JsonValue{
          static_cast<std::int64_t>(inspection.bitsPerSample)}},
      {"expectedRootMidi", formats::JsonValue{
          static_cast<std::int64_t>(inspection.expectedRootMidi)}},
      {"quality", formats::JsonValue{std::move(quality)}},
  };
  const auto written = core::durableAtomicWriteText(
      sidecar, formats::stringifyJson(
                   formats::JsonValue{std::move(record)}, true));
  if (!written) return core::Result<std::filesystem::path>{written.error()};
  return sidecar;
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

  const auto labelBounds = voicebankStudioMarkerLabelBounds(
      microscope.markers(), wave);
  for (std::size_t index = 0U; index < microscope.markers().size(); ++index) {
    const auto& marker = microscope.markers()[index];
    canvas.line(ui::Point{marker.x, wave.y}, ui::Point{marker.x, spec.bottom()},
                marker.kind == ui::AcousticMarkerKind::VowelOnset ? theme_.accent
                                                                  : theme_.grid, 1.0);
    canvas.drawText(ui::Point{labelBounds[index].x, labelBounds[index].y + 5.0},
                    marker.label,
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
