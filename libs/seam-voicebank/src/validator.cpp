#include "seam/voicebank/validator.hpp"

#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <vector>

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

struct DryMixAnalysis final {
  AudioStatistics statistics;
  bool finite{true};
};

core::Result<DryMixAnalysis> analyzeDryMix(const AudioBuffer& audio, std::stop_token stopToken) {
  const auto cancelled = [] { return core::failure<DryMixAnalysis>(
      core::ErrorCode::Conflict, "Dry-take inspection cancelled"); };
  if (audio.channels == 0U) return core::failure<DryMixAnalysis>(
      core::ErrorCode::ParseError, "Dry take has no channels");
  const auto frames = audio.frameCount();
  const auto channelCount = static_cast<std::size_t>(audio.channels);
  long double squareSum = 0.0L;
  long double sum = 0.0L;
  DryMixAnalysis result;
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    if ((frame & 4095U) == 0U && stopToken.stop_requested()) return cancelled();
    double mixed = 0.0;
    for (std::size_t channel = 0U; channel < channelCount; ++channel)
      mixed += audio.interleaved[frame * channelCount + channel];
    const auto sample = static_cast<float>(mixed / static_cast<double>(channelCount));
    result.finite = result.finite && std::isfinite(sample);
    const auto finiteSample = std::isfinite(sample) ? sample : 0.0F;
    result.statistics.peak = std::max(result.statistics.peak, std::abs(finiteSample));
    squareSum += static_cast<long double>(finiteSample) * static_cast<long double>(finiteSample);
    sum += finiteSample;
    if (std::abs(finiteSample) >= 0.9999F) ++result.statistics.clippedSamples;
  }
  if (stopToken.stop_requested()) return cancelled();
  if (frames != 0U) {
    const auto sampleCount = static_cast<long double>(frames);
    result.statistics.rms = std::sqrt(static_cast<double>(squareSum / sampleCount));
    result.statistics.dcOffset = static_cast<double>(sum / sampleCount);
  }
  return result;
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

// Median spacing of a run of consecutive pitch marks, in samples. Stored marks
// are one per glottal period, so this is the period the manifest claims the
// audio had when the marks were written.
double medianMarkGap(std::span<const PitchMark> marks) {
  if (marks.size() < 2U) return 0.0;
  std::vector<double> gaps;
  gaps.reserve(marks.size() - 1U);
  for (std::size_t index = 1; index < marks.size(); ++index) {
    gaps.push_back(static_cast<double>(marks[index].frame - marks[index - 1U].frame));
  }
  const auto middle = gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2U);
  std::nth_element(gaps.begin(), middle, gaps.end());
  return *middle;
}

}  // namespace

core::Result<DryTakeInspection> inspectDryTake(
    const std::filesystem::path& path, std::int32_t expectedRootMidi) {
  return inspectDryTake(path, expectedRootMidi, {});
}

core::Result<DryTakeInspection> inspectDryTake(
    const std::filesystem::path& path, std::int32_t expectedRootMidi,
    std::stop_token stopToken) {
  const auto cancelled = [&path] { return core::failure<DryTakeInspection>(
      core::ErrorCode::Conflict, "Dry-take inspection cancelled", path.string()); };
  if (stopToken.stop_requested()) return cancelled();
  if (expectedRootMidi < 0 || expectedRootMidi > 127) {
    return core::failure<DryTakeInspection>(
        core::ErrorCode::InvalidArgument,
        "Dry-take root MIDI must be between 0 and 127");
  }
  const auto beforeDigest = core::sha256File(path, kMaximumSupportedWavBytes, stopToken);
  if (!beforeDigest) return core::Result<DryTakeInspection>{beforeDigest.error()};
  auto audio = readWav(path, WavReadLimits{}, stopToken);
  if (!audio) return core::Result<DryTakeInspection>{audio.error()};
  const auto afterDigest = core::sha256File(path, kMaximumSupportedWavBytes, stopToken);
  if (!afterDigest) return core::Result<DryTakeInspection>{afterDigest.error()};
  if (beforeDigest.value() != afterDigest.value()) {
    return core::failure<DryTakeInspection>(
        core::ErrorCode::Conflict,
        "Dry take changed while it was being inspected");
  }

  const auto mix = analyzeDryMix(audio.value(), stopToken);
  if (!mix) return core::Result<DryTakeInspection>{mix.error()};
  const auto& statistics = mix.value().statistics;
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
      .finite = mix.value().finite,
      .clippingFree = statistics.clippedSamples == 0U,
      .silenceFree = statistics.rms > 1.0e-4,
      .dcOffsetFree = std::abs(statistics.dcOffset) <= 0.01,
      .rootPitchValid = false,
  };
  if (stopToken.stop_requested()) return cancelled();
  if (result.formatValid && result.finite && audio.value().frameCount() >= 2048U) {
    const auto pitch = analyzePitch(audio.value().interleaved, result.sampleRate, {}, stopToken);
    if (pitch) {
      const auto median = medianVoicedPitch(pitch.value());
      if (stopToken.stop_requested()) return cancelled();
      if (median > 0.0 && std::isfinite(median)) {
        const auto midi = 69.0 + 12.0 * std::log2(median / 440.0);
        result.analyzedRootMidi = static_cast<std::int32_t>(std::lround(midi));
        const auto cents = 1200.0 * std::log2(
            median / midiToHz(expectedRootMidi));
        result.rootPitchValid = std::isfinite(cents) && std::abs(cents) <= 80.0;
      }
    }
  }
  if (stopToken.stop_requested()) return cancelled();
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

    // Stored pitch marks are a measurement of the audio, and until now nothing
    // recorded which audio they measured. Replacing a unit's take while keeping
    // its name and length therefore left marks that still satisfied every
    // structural rule -- ascending, in range, confidence in [0, 1] -- while
    // describing a take that no longer exists. The marks are what PSOLA cuts on
    // and what the persisted loop/release descriptions derive from, so the
    // damage is not visible until a render sounds wrong.
    //
    // Re-running the producer's own analysis over the audio that is present now
    // and comparing periods catches exactly that case. Marks spacing and the
    // analyser disagreeing by more than a few semitones cannot happen while the
    // two describe the same bytes.
    if (unit.pitchMarks.size() >= 6U && unit.renderer == RendererHint::ClassicPsola) {
      const auto storedPeriod = medianMarkGap(unit.pitchMarks);
      const auto generated = generatePitchMarks(
          mono, audio.value().sampleRate, unit.markers.audioOffset,
          unit.markers.audioEnd, producerPitchMarkConfig(), {},
          producerPitchLimits(mono.size()));
      if (generated && generated.value().size() >= 2U) {
        const auto measuredPeriod = medianMarkGap(generated.value());
        if (storedPeriod > 0.0 && measuredPeriod > 0.0) {
          const auto cents = std::abs(1200.0 * std::log2(storedPeriod / measuredPeriod));
          if (cents > 300.0) {
            add(report, IssueSeverity::Warning, IssueCode::PitchMarksStale, unit.id,
                "Stored pitch marks describe a different take: they imply " +
                    std::to_string(static_cast<int>(std::lround(
                        audio.value().sampleRate / storedPeriod))) +
                    " Hz while the audio present measures " +
                    std::to_string(static_cast<int>(std::lround(
                        audio.value().sampleRate / measuredPeriod))) +
                    " Hz, " + std::to_string(static_cast<int>(std::lround(cents))) +
                    " cents apart. Re-analyse or remove the marks before release.");
          }
        }
      }
    }

    // A stored acoustic analysis is consumed by QC and by anything that needs to
    // know where this take is voiced. When one is present it must be re-checked
    // against the audio the bank actually contains, not trusted because it parsed:
    // the same reasoning that makes stored pitch marks untrustworthy applies here,
    // and analysis spans additionally carry a digest that can be compared exactly.
    // Located relative to the bank root and resolved through the same containment
    // helper as audio, so a sidecar cannot sit outside the bank or behind a link.
    const auto analysisRelative = std::filesystem::path{acousticAnalysisSidecarPath(unit.id)};
    std::error_code analysisError;
    const auto analysisStatus =
        std::filesystem::symlink_status(bankRoot / analysisRelative, analysisError);
    const bool analysisPresent = !analysisError && std::filesystem::exists(analysisStatus);
    if (analysisPresent) {
      const auto analysisPath = resolveBankAsset(bankRoot, analysisRelative);
      if (!analysisPath) {
        add(report, IssueSeverity::Error, IssueCode::AcousticAnalysisStale, unit.id,
            "Stored acoustic analysis is not a contained bank asset: " +
                analysisPath.error().message);
      } else {
        auto bytes = core::readFileBytesLimited(analysisPath.value(), 512ULL * 1024ULL);
        auto digest = core::sha256File(resolved.value(), kMaximumSupportedWavBytes);
        if (!bytes || !digest) {
          add(report, IssueSeverity::Error, IssueCode::AcousticAnalysisStale, unit.id,
              "Stored acoustic analysis or its audio could not be read");
        } else {
          // The record binds to encoded audio bytes; the validator has the decoded
          // mix, so the digest is what ties the two together.
          const auto decoded = decodeAcousticAnalysis(
              std::string_view{reinterpret_cast<const char*>(bytes.value().data()),
                               bytes.value().size()},
              unit, digest.value(),
              static_cast<time::SampleFrame>(audio.value().frameCount()));
          if (!decoded) {
            add(report, IssueSeverity::Error, IssueCode::AcousticAnalysisStale, unit.id,
                "Stored acoustic analysis does not describe this audio: " +
                    decoded.error().message);
          } else {
            // Structure can be right while the conclusion is wrong. Re-measure and
            // compare, so a record left over from earlier audio is caught rather
            // than believed.
            const auto measured = analyzeUnitAcoustics(
                mono, audio.value().sampleRate, unit, digest.value(),
                static_cast<time::SampleFrame>(audio.value().frameCount()));
            if (measured) {
              const auto agree = std::equal(
                  decoded.value().spans.begin(), decoded.value().spans.end(),
                  measured.value().spans.begin(), measured.value().spans.end(),
                  [](const AcousticVoicingSpan& stored,
                     const AcousticVoicingSpan& fresh) {
                    // Booleans must match exactly: a voicing disagreement is the
                    // thing this check exists to find.
                    if (stored.voiced != fresh.voiced) return false;
                    // Frequencies are measurements of the same signal by the same
                    // algorithm, so exact equality is the honest comparison. Any
                    // tolerance here would be choosing how wrong is acceptable.
                    return stored.f0Hz == fresh.f0Hz;
                  });
              if (!agree) {
                add(report, IssueSeverity::Warning, IssueCode::AcousticAnalysisMismatch,
                    unit.id,
                    "Stored acoustic analysis disagrees with the analysis of the audio"
                    " present; regenerate it before relying on it");
              }
            }
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
    case IssueCode::PitchMarksStale: return "pitch-marks-stale";
    case IssueCode::AcousticAnalysisStale: return "acoustic-analysis-stale";
    case IssueCode::AcousticAnalysisMismatch: return "acoustic-analysis-mismatch";
  }
  return "unknown";
}

}  // namespace seam::voicebank
