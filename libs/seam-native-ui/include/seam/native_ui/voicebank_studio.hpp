#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/sample_microscope_model.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui {

struct VoicebankStudioTheme final {
  Color background{15, 14, 18, 255};
  Color panel{24, 22, 28, 255};
  Color panelAlternate{20, 20, 24, 255};
  Color grid{58, 52, 64, 255};
  Color primaryText{239, 233, 241, 255};
  Color secondaryText{166, 154, 170, 255};
  Color accent{169, 79, 119, 255};
  Color pitch{101, 187, 184, 255};
  Color waveform{184, 136, 161, 255};
  Color selected{72, 52, 76, 255};
};

[[nodiscard]] std::vector<ui::Rect> voicebankStudioMarkerLabelBounds(
    std::span<const ui::AcousticMarkerVisual> markers,
    ui::Rect waveformBounds);

[[nodiscard]] core::Result<std::filesystem::path> nextVoicebankRecordingPath(
    const std::filesystem::path& directory, std::string_view unitId);

class VoicebankStudioController final {
public:
  [[nodiscard]] core::Result<void> openManifest(
      const std::filesystem::path& manifestPath,
      double logicalWidth = 1440.0, double logicalHeight = 900.0);
  [[nodiscard]] core::Result<void> save();
  [[nodiscard]] core::Result<void> selectUnit(std::size_t index);
  [[nodiscard]] core::Result<void> moveSelectedMarker(ui::AcousticMarkerKind marker,
                                                       double x);
  [[nodiscard]] core::Result<void> moveSelectedPitchMark(std::size_t index,
                                                          double x);
  [[nodiscard]] core::Result<void> inspectTake(
      const std::filesystem::path& path, std::int32_t expectedRootMidi);
  [[nodiscard]] core::Result<std::filesystem::path> persistTakeInspection(
      const std::filesystem::path& takePath) const;
  void resize(double logicalWidth, double logicalHeight);

  [[nodiscard]] const voicebank::Manifest& manifest() const noexcept { return manifest_; }
  [[nodiscard]] voicebank::Manifest& manifest() noexcept { return manifest_; }
  [[nodiscard]] const voicebank::Unit* selectedUnit() const noexcept;
  [[nodiscard]] voicebank::Unit* selectedUnit() noexcept;
  [[nodiscard]] const ui::SampleMicroscopeModel& microscope() const noexcept {
    return microscope_;
  }
  [[nodiscard]] std::size_t selectedIndex() const noexcept { return selectedIndex_; }
  [[nodiscard]] bool dirty() const noexcept { return dirty_; }
  [[nodiscard]] const std::filesystem::path& manifestPath() const noexcept {
    return manifestPath_;
  }
  [[nodiscard]] const std::string& status() const noexcept { return status_; }
  [[nodiscard]] const std::optional<voicebank::DryTakeInspection>&
  takeInspection() const noexcept {
    return takeInspection_;
  }

private:
  [[nodiscard]] core::Result<void> rebuildSelected();
  [[nodiscard]] std::filesystem::path selectedAudioPath() const;

  voicebank::ManifestJsonCodec codec_;
  std::filesystem::path manifestPath_;
  std::filesystem::path root_;
  voicebank::Manifest manifest_;
  voicebank::AudioBuffer audio_;
  ui::SampleMicroscopeModel microscope_;
  std::size_t selectedIndex_{0U};
  bool dirty_{false};
  std::string status_{"NO BANK"};
  std::optional<voicebank::DryTakeInspection> takeInspection_;
  double logicalWidth_{1440.0};
  double logicalHeight_{900.0};
};

class VoicebankStudioScenePainter final {
public:
  explicit VoicebankStudioScenePainter(VoicebankStudioTheme theme = {}) noexcept
      : theme_(theme) {}
  void paint(RasterCanvas& canvas,
             const VoicebankStudioController& controller,
             bool recording = false,
             std::string_view recordingBackend = "OFF") const noexcept;

private:
  VoicebankStudioTheme theme_;
};

}  // namespace seam::native_ui
