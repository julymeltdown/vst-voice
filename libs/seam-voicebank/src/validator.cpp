#include "seam/voicebank/validator.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace seam::voicebank {
namespace {

void add(ValidationReport& report, IssueSeverity severity, IssueCode code,
         std::string unitId, std::string message) {
  report.issues.push_back(ValidationIssue{
      .severity = severity,
      .code = code,
      .unitId = std::move(unitId),
      .message = std::move(message),
  });
}

double midiToHz(std::int32_t midi) noexcept {
  return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

float loopJump(std::span<const float> samples, const UnitMarkers& markers) noexcept {
  if (!markers.loopStart.has_value() || !markers.loopEnd.has_value() || samples.empty()) {
    return 0.0F;
  }
  const auto start = static_cast<std::size_t>(*markers.loopStart);
  const auto end = static_cast<std::size_t>(*markers.loopEnd);
  if (start >= samples.size() || end == 0 || end > samples.size()) return 1.0F;
  return std::abs(samples[start] - samples[end - 1U]);
}

}  // namespace

core::Result<DryTakeInspection> inspectDryTake(
    const std::filesystem::path& path, std::int32_t expectedRootMidi) {
  if (expectedRootMidi < 0 || expectedRootMidi > 127) {
    return core::failure<DryTakeInspection>(
        core::ErrorCode::InvalidArgument,
        "Dry-take root MIDI must be between 0 and 127");
  }
  const auto beforeDigest = core::sha256File(path, kMaximumSupportedWavBytes);
  if (!beforeDigest) return core::Result<DryTakeInspection>{beforeDigest.error()};
  auto audio = readWav(path);
  if (!audio) return core::Result<DryTakeInspection>{audio.error()};
  const auto afterDigest = core::sha256File(path, kMaximumSupportedWavBytes);
  if (!afterDigest) return core::Result<DryTakeInspection>{afterDigest.error()};
  if (beforeDigest.value() != afterDigest.value()) {
    return core::failure<DryTakeInspection>(
        core::ErrorCode::Conflict,
        "Dry take changed while it was being inspected");
  }

  const auto mono = audio.value().monoMix();
  const auto statistics = analyzeAudio(mono);
  DryTakeInspection result{
      .sourceSha256 = std::move(afterDigest.value()),
      .sampleRate = audio.value().sampleRate,
      .channels = audio.value().channels,
      .bitsPerSample = audio.value().bitsPerSample,
      .expectedRootMidi = expectedRootMidi,
      .analyzedRootMidi = std::nullopt,
      .peak = statistics.peak,
      .rms = statistics.rms,
      .dcOffset = statistics.dcOffset,
      .formatValid = audio.value().sampleRate == 48000U &&
                     audio.value().channels == 1U &&
                     audio.value().bitsPerSample == 24U,
      .finite = std::all_of(mono.begin(), mono.end(),
                            [](const float sample) {
                              return std::isfinite(sample);
                            }),
      .clippingFree = statistics.clippedSamples == 0U,
      .silenceFree = statistics.rms > 1.0e-4,
      .dcOffsetFree = std::abs(statistics.dcOffset) <= 0.01,
      .rootPitchValid = false,
  };
  if (result.formatValid && result.finite && mono.size() >= 2048U) {
    const auto pitch = analyzePitch(mono, result.sampleRate);
    if (pitch) {
      const auto median = medianVoicedPitch(pitch.value());
      if (median > 0.0 && std::isfinite(median)) {
        const auto midi = 69.0 + 12.0 * std::log2(median / 440.0);
        result.analyzedRootMidi = static_cast<std::int32_t>(std::lround(midi));
        const auto cents = 1200.0 * std::log2(
            median / midiToHz(expectedRootMidi));
        result.rootPitchValid = std::isfinite(cents) && std::abs(cents) <= 80.0;
      }
    }
  }
  return result;
}

std::size_t ValidationReport::errorCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
      [](const auto& issue) { return issue.severity == IssueSeverity::Error; }));
}

std::size_t ValidationReport::warningCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
      [](const auto& issue) { return issue.severity == IssueSeverity::Warning; }));
}

ValidationReport BankValidator::validate(const Manifest& manifest,
                                         const std::filesystem::path& bankRoot) const {
  ValidationReport report;
  const auto structural = manifest.validate();
  if (!structural) {
    add(report, IssueSeverity::Error, IssueCode::ManifestInvalid, {},
        structural.error().message);
    return report;
  }

  std::map<std::string, std::size_t, std::less<>> aliasCounts;
  bool hasSustain = false;
  for (const auto& unit : manifest.units) {
    ++aliasCounts[unit.alias];
    hasSustain = hasSustain || unit.kind == UnitKind::Sustain;
    ++report.unitsChecked;
    auto resolved = resolveBankAsset(bankRoot, unit.audioPath);
    if (!resolved) {
      add(report, IssueSeverity::Error, IssueCode::MissingAudio, unit.id,
          resolved.error().message + ": " + unit.audioPath.generic_string());
      continue;
    }
    const auto& audioPath = resolved.value();
    auto audio = readWav(audioPath);
    if (!audio) {
      add(report, IssueSeverity::Error, IssueCode::AudioUnreadable, unit.id,
          audio.error().message);
      continue;
    }
    if (audio.value().channels != 1) {
      add(report, IssueSeverity::Error, IssueCode::ChannelMismatch, unit.id,
          "Voicebank source must be mono");
    }
    if (audio.value().sampleRate != manifest.expectedSampleRate) {
      add(report, IssueSeverity::Error, IssueCode::SampleRateMismatch, unit.id,
          "Audio sample rate does not match the voicebank manifest");
    }
    const auto markerResult = unit.markers.validate(
        static_cast<time::SampleFrame>(audio.value().frameCount()));
    if (!markerResult) {
      add(report, IssueSeverity::Error, IssueCode::MarkerInvalid, unit.id,
          markerResult.error().message);
      continue;
    }
    const auto mono = audio.value().monoMix();
    const auto statistics = analyzeAudio(mono);
    if (statistics.clippedSamples > 0) {
      add(report, IssueSeverity::Error, IssueCode::Clipping, unit.id,
          "Audio contains clipped samples");
    }
    if (std::abs(statistics.dcOffset) > 0.01) {
      add(report, IssueSeverity::Warning, IssueCode::DcOffset, unit.id,
          "Audio DC offset exceeds 0.01");
    }
    if (loopJump(mono, unit.markers) > 0.15F) {
      add(report, IssueSeverity::Warning, IssueCode::LoopDiscontinuity, unit.id,
          "Sustain loop boundary has a large waveform jump");
    }

    const auto analysisStart = static_cast<std::size_t>(unit.markers.stableStart);
    const auto analysisEnd = static_cast<std::size_t>(
        unit.markers.releaseStart.value_or(unit.markers.audioEnd));
    if (analysisEnd > analysisStart + 512U && analysisEnd <= mono.size()) {
      const auto pitch = analyzePitch(
          std::span<const float>(mono).subspan(analysisStart, analysisEnd - analysisStart),
          audio.value().sampleRate);
      if (pitch) {
        const auto median = medianVoicedPitch(pitch.value());
        const auto expected = midiToHz(unit.rootMidi);
        if (median > 0.0) {
          const auto cents = 1200.0 * std::log2(median / expected);
          if (std::abs(cents) > 80.0) {
            add(report, IssueSeverity::Warning, IssueCode::RootPitchMismatch, unit.id,
                "Analyzed pitch differs from root MIDI by " +
                    std::to_string(static_cast<int>(std::lround(cents))) + " cents");
          }
        }
      }
    }
  }

  for (const auto& [alias, count] : aliasCounts) {
    if (count > 1) {
      add(report, IssueSeverity::Info, IssueCode::DuplicateAlias, {},
          "Alias has multiple variants: " + alias);
    }
  }
  if (!hasSustain) {
    add(report, IssueSeverity::Warning, IssueCode::MissingSustain, {},
        "Voicebank does not contain a dedicated sustain unit");
  }
  return report;
}

std::string_view issueSeverityName(IssueSeverity severity) noexcept {
  switch (severity) {
    case IssueSeverity::Info: return "info";
    case IssueSeverity::Warning: return "warning";
    case IssueSeverity::Error: return "error";
  }
  return "error";
}

std::string_view issueCodeName(IssueCode code) noexcept {
  switch (code) {
    case IssueCode::ManifestInvalid: return "manifest-invalid";
    case IssueCode::DuplicateAlias: return "duplicate-alias";
    case IssueCode::MissingAudio: return "missing-audio";
    case IssueCode::AudioUnreadable: return "audio-unreadable";
    case IssueCode::ChannelMismatch: return "channel-mismatch";
    case IssueCode::SampleRateMismatch: return "sample-rate-mismatch";
    case IssueCode::MarkerInvalid: return "marker-invalid";
    case IssueCode::Clipping: return "clipping";
    case IssueCode::DcOffset: return "dc-offset";
    case IssueCode::RootPitchMismatch: return "root-pitch-mismatch";
    case IssueCode::LoopDiscontinuity: return "loop-discontinuity";
    case IssueCode::MissingSustain: return "missing-sustain";
  }
  return "unknown";
}

}  // namespace seam::voicebank
