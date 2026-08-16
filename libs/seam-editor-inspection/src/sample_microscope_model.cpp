#include "seam/ui/sample_microscope_model.hpp"

#include <algorithm>
#include <cmath>

namespace seam::ui {
namespace {

void appendMarker(std::vector<AcousticMarkerVisual>& output,
                  AcousticMarkerKind kind,
                  time::SampleFrame frame,
                  double x) {
  output.push_back(AcousticMarkerVisual{
      .kind = kind,
      .label = std::string{acousticMarkerKindName(kind)},
      .frame = frame,
      .x = x,
  });
}

}  // namespace



std::string_view acousticMarkerKindName(AcousticMarkerKind kind) noexcept {
  switch (kind) {
    case AcousticMarkerKind::AudioOffset: return "offset";
    case AcousticMarkerKind::ConsonantEnd: return "consonant";
    case AcousticMarkerKind::VowelOnset: return "vowel";
    case AcousticMarkerKind::StableStart: return "stable";
    case AcousticMarkerKind::LoopStart: return "loop-start";
    case AcousticMarkerKind::LoopEnd: return "loop-end";
    case AcousticMarkerKind::ReleaseStart: return "release";
    case AcousticMarkerKind::AudioEnd: return "end";
  }
  return "marker";
}

core::Result<void> SampleMicroscopeModel::rebuild(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    Rect waveformBounds,
    Rect spectrogramBounds,
    std::size_t maximumWaveformColumns,
    voicebank::SpectrogramConfig spectrogramConfig) {
  if (source.frameCount() == 0U || source.channels == 0U ||
      waveformBounds.width <= 0.0 || waveformBounds.height <= 0.0 ||
      spectrogramBounds.width <= 0.0 || spectrogramBounds.height <= 0.0 ||
      maximumWaveformColumns == 0U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Sample microscope input is invalid", unit.id);
  }
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return markerValidation;

  unit_ = &unit;
  totalFrames_ = static_cast<time::SampleFrame>(source.frameCount());
  waveformBounds_ = waveformBounds;
  spectrogramBounds_ = spectrogramBounds;
  waveform_.clear();
  markers_.clear();
  pitchMarks_.clear();

  const auto mono = source.monoMix();
  const auto framesPerColumn = std::max<std::size_t>(
      1U, (mono.size() + maximumWaveformColumns - 1U) / maximumWaveformColumns);
  auto pyramid = voicebank::WaveformPyramid::build(mono, framesPerColumn, 1U);
  if (!pyramid) return core::Result<void>{pyramid.error()};
  const auto& level = pyramid.value().levels().front();
  waveform_.reserve(level.buckets.size());
  for (std::size_t index = 0U; index < level.buckets.size(); ++index) {
    const auto frame = static_cast<time::SampleFrame>(index * level.framesPerBucket);
    const auto& bucket = level.buckets[index];
    waveform_.push_back(WaveformColumn{
        .x = frameToPixel(frame),
        .minimum = bucket.minimum,
        .maximum = bucket.maximum,
        .rms = bucket.rms,
    });
  }

  auto builtSpectrogram = voicebank::buildSpectrogram(mono, spectrogramConfig);
  if (!builtSpectrogram) return core::Result<void>{builtSpectrogram.error()};
  spectrogram_ = std::move(builtSpectrogram).value();

  refreshMarkers(unit);
  refreshPitchMarks(unit);
  return core::success();
}

double SampleMicroscopeModel::frameToPixel(time::SampleFrame frame) const noexcept {
  if (totalFrames_ <= 1) return waveformBounds_.x;
  const auto normalized = std::clamp(
      static_cast<double>(frame) / static_cast<double>(totalFrames_ - 1),
      0.0, 1.0);
  return waveformBounds_.x + normalized * waveformBounds_.width;
}

time::SampleFrame SampleMicroscopeModel::pixelToFrame(double x) const noexcept {
  if (totalFrames_ <= 1 || waveformBounds_.width <= 0.0) return 0;
  const auto normalized = std::clamp(
      (x - waveformBounds_.x) / waveformBounds_.width, 0.0, 1.0);
  return static_cast<time::SampleFrame>(std::llround(
      normalized * static_cast<double>(totalFrames_ - 1)));
}

std::optional<AcousticMarkerKind> SampleMicroscopeModel::hitTestMarker(
    Point point, double tolerancePixels) const noexcept {
  if (point.y < waveformBounds_.y ||
      point.y > spectrogramBounds_.y + spectrogramBounds_.height) {
    return std::nullopt;
  }
  for (auto iterator = markers_.rbegin(); iterator != markers_.rend(); ++iterator) {
    if (std::abs(point.x - iterator->x) <= tolerancePixels) return iterator->kind;
  }
  return std::nullopt;
}

std::optional<std::size_t> SampleMicroscopeModel::hitTestPitchMark(
    Point point, double tolerancePixels) const noexcept {
  if (!waveformBounds_.contains(point)) return std::nullopt;
  for (auto iterator = pitchMarks_.rbegin(); iterator != pitchMarks_.rend(); ++iterator) {
    if (std::abs(point.x - iterator->x) <= tolerancePixels) return iterator->index;
  }
  return std::nullopt;
}


void SampleMicroscopeModel::refreshMarkers(const voicebank::Unit& unit) {
  markers_.clear();
  const auto& marker = unit.markers;
  appendMarker(markers_, AcousticMarkerKind::AudioOffset,
               marker.audioOffset, frameToPixel(marker.audioOffset));
  appendMarker(markers_, AcousticMarkerKind::ConsonantEnd,
               marker.consonantEnd, frameToPixel(marker.consonantEnd));
  appendMarker(markers_, AcousticMarkerKind::VowelOnset,
               marker.vowelOnset, frameToPixel(marker.vowelOnset));
  appendMarker(markers_, AcousticMarkerKind::StableStart,
               marker.stableStart, frameToPixel(marker.stableStart));
  if (marker.loopStart.has_value()) {
    appendMarker(markers_, AcousticMarkerKind::LoopStart,
                 *marker.loopStart, frameToPixel(*marker.loopStart));
  }
  if (marker.loopEnd.has_value()) {
    appendMarker(markers_, AcousticMarkerKind::LoopEnd,
                 *marker.loopEnd, frameToPixel(*marker.loopEnd));
  }
  if (marker.releaseStart.has_value()) {
    appendMarker(markers_, AcousticMarkerKind::ReleaseStart,
                 *marker.releaseStart, frameToPixel(*marker.releaseStart));
  }
  appendMarker(markers_, AcousticMarkerKind::AudioEnd,
               marker.audioEnd, frameToPixel(marker.audioEnd));
}

void SampleMicroscopeModel::refreshPitchMarks(const voicebank::Unit& unit) {
  pitchMarks_.clear();
  pitchMarks_.reserve(unit.pitchMarks.size());
  for (std::size_t index = 0U; index < unit.pitchMarks.size(); ++index) {
    const auto& mark = unit.pitchMarks[index];
    pitchMarks_.push_back(PitchMarkVisual{
        .index = index,
        .frame = mark.frame,
        .confidence = mark.confidence,
        .locked = mark.locked,
        .x = frameToPixel(mark.frame),
    });
  }
}

core::Result<void> SampleMicroscopeModel::moveMarker(
    voicebank::Unit& unit,
    AcousticMarkerKind marker,
    double pixelX,
    time::SampleFrame totalFrames) {
  if (&unit != unit_ || totalFrames <= 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Sample microscope marker edit target is invalid", unit.id);
  }
  voicebank::MarkerKind target = voicebank::MarkerKind::AudioOffset;
  switch (marker) {
    case AcousticMarkerKind::AudioOffset:
      target = voicebank::MarkerKind::AudioOffset; break;
    case AcousticMarkerKind::ConsonantEnd:
      target = voicebank::MarkerKind::ConsonantEnd; break;
    case AcousticMarkerKind::VowelOnset:
      target = voicebank::MarkerKind::VowelOnset; break;
    case AcousticMarkerKind::StableStart:
      target = voicebank::MarkerKind::StableStart; break;
    case AcousticMarkerKind::LoopStart:
      target = voicebank::MarkerKind::LoopStart; break;
    case AcousticMarkerKind::LoopEnd:
      target = voicebank::MarkerKind::LoopEnd; break;
    case AcousticMarkerKind::ReleaseStart:
      target = voicebank::MarkerKind::ReleaseStart; break;
    case AcousticMarkerKind::AudioEnd:
      target = voicebank::MarkerKind::AudioEnd; break;
  }
  auto changed = voicebank::MarkerEditor::set(
      unit.markers, target, pixelToFrame(pixelX), totalFrames);
  if (!changed) return core::Result<void>{changed.error()};
  unit.markers = std::move(changed).value();
  refreshMarkers(unit);
  return core::success();
}

core::Result<void> SampleMicroscopeModel::movePitchMark(
    voicebank::Unit& unit,
    std::size_t index,
    double pixelX) {
  if (&unit != unit_ || index >= unit.pitchMarks.size()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Sample microscope pitch-mark target is invalid", unit.id);
  }
  voicebank::PitchMarkEditor editor;
  const auto changed = editor.move(
      unit.pitchMarks, index, pixelToFrame(pixelX),
      unit.markers.stableStart,
      unit.markers.releaseStart.value_or(unit.markers.audioEnd));
  if (!changed) return changed;
  refreshPitchMarks(unit);
  return core::success();
}

}  // namespace seam::ui
