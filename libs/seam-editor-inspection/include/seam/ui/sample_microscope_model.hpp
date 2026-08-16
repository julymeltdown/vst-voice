#pragma once

#include "seam/core/result.hpp"
#include "seam/ui/geometry.hpp"
#include "seam/voicebank/marker_editor.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/waveform.hpp"

#include <optional>
#include <string>
#include <vector>

namespace seam::ui {

enum class AcousticMarkerKind {
  AudioOffset,
  ConsonantEnd,
  VowelOnset,
  StableStart,
  LoopStart,
  LoopEnd,
  ReleaseStart,
  AudioEnd,
};

struct AcousticMarkerVisual final {
  AcousticMarkerKind kind{AcousticMarkerKind::AudioOffset};
  std::string label;
  time::SampleFrame frame{0};
  double x{0.0};
};

struct PitchMarkVisual final {
  std::size_t index{0};
  time::SampleFrame frame{0};
  float confidence{1.0F};
  bool locked{false};
  double x{0.0};
};

struct WaveformColumn final {
  double x{0.0};
  float minimum{0.0F};
  float maximum{0.0F};
  float rms{0.0F};
};

class SampleMicroscopeModel final {
public:
  [[nodiscard]] core::Result<void> rebuild(
      const voicebank::Unit& unit,
      const voicebank::AudioBuffer& source,
      Rect waveformBounds,
      Rect spectrogramBounds,
      std::size_t maximumWaveformColumns = 1024U,
      voicebank::SpectrogramConfig spectrogramConfig = {});

  [[nodiscard]] const voicebank::Unit* unit() const noexcept { return unit_; }
  [[nodiscard]] const std::vector<WaveformColumn>& waveform() const noexcept {
    return waveform_;
  }
  [[nodiscard]] const voicebank::Spectrogram& spectrogram() const noexcept {
    return spectrogram_;
  }
  [[nodiscard]] const std::vector<AcousticMarkerVisual>& markers() const noexcept {
    return markers_;
  }
  [[nodiscard]] const std::vector<PitchMarkVisual>& pitchMarks() const noexcept {
    return pitchMarks_;
  }
  [[nodiscard]] Rect waveformBounds() const noexcept { return waveformBounds_; }
  [[nodiscard]] Rect spectrogramBounds() const noexcept { return spectrogramBounds_; }
  [[nodiscard]] double frameToPixel(time::SampleFrame frame) const noexcept;
  [[nodiscard]] time::SampleFrame pixelToFrame(double x) const noexcept;
  [[nodiscard]] std::optional<AcousticMarkerKind> hitTestMarker(
      Point point, double tolerancePixels = 5.0) const noexcept;
  [[nodiscard]] std::optional<std::size_t> hitTestPitchMark(
      Point point, double tolerancePixels = 4.0) const noexcept;
  [[nodiscard]] core::Result<void> moveMarker(
      voicebank::Unit& unit,
      AcousticMarkerKind marker,
      double x,
      time::SampleFrame totalFrames);
  [[nodiscard]] core::Result<void> movePitchMark(
      voicebank::Unit& unit,
      std::size_t index,
      double x);

private:
  void refreshMarkers(const voicebank::Unit& unit);
  void refreshPitchMarks(const voicebank::Unit& unit);

  const voicebank::Unit* unit_{nullptr};
  time::SampleFrame totalFrames_{0};
  Rect waveformBounds_{};
  Rect spectrogramBounds_{};
  std::vector<WaveformColumn> waveform_;
  voicebank::Spectrogram spectrogram_;
  std::vector<AcousticMarkerVisual> markers_;
  std::vector<PitchMarkVisual> pitchMarks_;
};

[[nodiscard]] std::string_view acousticMarkerKindName(
    AcousticMarkerKind kind) noexcept;

}  // namespace seam::ui
