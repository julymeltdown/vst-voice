#include "seam/voicebank/voicebank.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace seam::voicebank {

core::Result<void> UnitMarkers::validate(time::SampleFrame totalFrames) const {
  if (totalFrames <= 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Voicebank sample must contain audio frames");
  }
  if (audioOffset < 0 || audioEnd <= audioOffset || audioEnd > totalFrames) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Audio offset/end markers are outside the sample");
  }
  if (consonantEnd < audioOffset || vowelOnset < consonantEnd ||
      stableStart < vowelOnset || stableStart > audioEnd) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Consonant and vowel markers are not monotonic");
  }
  if (loopStart.has_value() != loopEnd.has_value()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Loop start and loop end must either both exist or both be absent");
  }
  if (loopStart.has_value()) {
    if (*loopStart < stableStart || *loopEnd <= *loopStart || *loopEnd > audioEnd) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Sustain loop markers are invalid");
    }
  }
  if (releaseStart.has_value()) {
    const auto minimum = loopEnd.value_or(stableStart);
    if (*releaseStart < minimum || *releaseStart >= audioEnd) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Release marker is invalid");
    }
  }
  return core::success();
}

core::Result<void> Unit::validate() const {
  if (id.empty() || alias.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank unit ID and alias must not be empty");
  }
  if (phones.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank unit must contain at least one phoneme",
                         id);
  }
  if (std::any_of(phones.begin(), phones.end(),
                  [](const std::string& value) { return value.empty(); })) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank unit phonemes must not be empty",
                         id);
  }
  if (audioPath.empty() || audioPath.is_absolute()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank unit audio path must be relative",
                         id);
  }
  for (const auto& part : audioPath) {
    if (part == "..") {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Voicebank audio path must not escape the package",
                           id);
    }
  }
  if (rootMidi < 0 || rootMidi > 127 || take <= 0 || style.empty() ||
      !std::isfinite(gainDb) || gainDb < -96.0F || gainDb > 24.0F) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank unit metadata is outside supported ranges",
                         id);
  }
  return core::success();
}

const Unit* Manifest::findUnit(std::string_view unitId) const noexcept {
  const auto iterator = std::find_if(units.begin(), units.end(),
      [unitId](const Unit& unit) { return unit.id == unitId; });
  return iterator == units.end() ? nullptr : &*iterator;
}

Unit* Manifest::findUnit(std::string_view unitId) noexcept {
  const auto iterator = std::find_if(units.begin(), units.end(),
      [unitId](const Unit& unit) { return unit.id == unitId; });
  return iterator == units.end() ? nullptr : &*iterator;
}

core::Result<void> Manifest::validate() const {
  if (id.empty() || version.empty() || displayName.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank identity fields must not be empty");
  }
  if (language == domain::Language::Unspecified) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank language must be specified");
  }
  if (expectedSampleRate < 8000 || expectedSampleRate > 384000) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank sample rate is unsupported");
  }
  if (styles.empty() || std::any_of(styles.begin(), styles.end(),
                                    [](const std::string& value) { return value.empty(); })) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank must declare at least one non-empty style");
  }
  std::set<std::string> styleSet(styles.begin(), styles.end());
  if (styleSet.size() != styles.size()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Voicebank styles must be unique");
  }

  std::set<std::string> ids;
  for (const auto& unit : units) {
    const auto result = unit.validate();
    if (!result) return result;
    if (!ids.insert(unit.id).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Voicebank unit IDs must be unique",
                           unit.id);
    }
    if (!styleSet.contains(unit.style)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Voicebank unit references an undeclared style",
                           unit.id);
    }
  }
  return core::success();
}

std::string_view unitKindName(UnitKind kind) noexcept {
  switch (kind) {
    case UnitKind::Cv: return "cv";
    case UnitKind::Vcv: return "vcv";
    case UnitKind::Vc: return "vc";
    case UnitKind::Vv: return "vv";
    case UnitKind::Cc: return "cc";
    case UnitKind::Sustain: return "sustain";
    case UnitKind::Release: return "release";
    case UnitKind::Breath: return "breath";
    case UnitKind::Glottal: return "glottal";
    case UnitKind::Special: return "special";
  }
  return "special";
}

UnitKind parseUnitKind(std::string_view value) noexcept {
  if (value == "cv") return UnitKind::Cv;
  if (value == "vcv") return UnitKind::Vcv;
  if (value == "vc") return UnitKind::Vc;
  if (value == "vv") return UnitKind::Vv;
  if (value == "cc") return UnitKind::Cc;
  if (value == "sustain") return UnitKind::Sustain;
  if (value == "release") return UnitKind::Release;
  if (value == "breath") return UnitKind::Breath;
  if (value == "glottal") return UnitKind::Glottal;
  return UnitKind::Special;
}

std::string_view rendererHintName(RendererHint hint) noexcept {
  switch (hint) {
    case RendererHint::Raw: return "raw";
    case RendererHint::ClassicPsola: return "classic-psola";
    case RendererHint::SpectralClassic: return "spectral-classic";
    case RendererHint::Stretch: return "stretch";
  }
  return "raw";
}

RendererHint parseRendererHint(std::string_view value) noexcept {
  if (value == "classic-psola") return RendererHint::ClassicPsola;
  if (value == "spectral-classic") return RendererHint::SpectralClassic;
  if (value == "stretch") return RendererHint::Stretch;
  return RendererHint::Raw;
}

}  // namespace seam::voicebank
