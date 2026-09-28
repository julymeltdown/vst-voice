#include "seam/voicebank/acoustic_analysis.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace seam::voicebank {
namespace {

using seam::formats::JsonValue;
using Object = JsonValue::Object;
using Array = JsonValue::Array;

bool isDigest(std::string_view value) noexcept {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

// Median over the voiced frames that describe one span, [first, last] by frame
// index. Membership comes from the shared partition rather than from where a
// frame's window starts: a frame's origin lies half a window before the samples
// it describes, so selecting by origin would summarise a neighbour's frames.
// Frames without a usable fundamental are skipped rather than counted as zero.
double medianVoicedHz(std::span<const PitchFrame> frames, std::size_t first,
                      std::size_t last) {
  std::vector<double> values;
  for (std::size_t index = first; index <= last && index < frames.size(); ++index) {
    const auto& frame = frames[index];
    if (!frame.voiced || !std::isfinite(frame.f0Hz) || frame.f0Hz <= 0.0) continue;
    values.push_back(frame.f0Hz);
  }
  if (values.empty()) return 0.0;
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
  std::nth_element(values.begin(), middle, values.end());
  return *middle;
}

double meanConfidence(std::span<const PitchFrame> frames, std::size_t first,
                      std::size_t last) {
  double total = 0.0;
  std::size_t count = 0U;
  for (std::size_t index = first; index <= last && index < frames.size(); ++index) {
    total += frames[index].confidence;
    ++count;
  }
  return count == 0U ? 0.0 : total / static_cast<double>(count);
}

}  // namespace

PitchConfig producerPitchConfig() noexcept {
  return PitchConfig{
      .frameSize = kProducerFrameSize,
      .hopSize = kProducerHopSize,
      .minimumHz = kProducerMinimumHz,
      .maximumHz = kProducerMaximumHz,
      .voicingThreshold = kProducerVoicingThreshold,
      .correlationMethod = PitchCorrelationMethod::Fft,
  };
}

std::uint64_t producerAnalysisWork(std::size_t frames) noexcept {
  constexpr std::uint64_t workPerFrame = 4096ULL * 12ULL;
  const auto analysed = frames <= kProducerFrameSize
      ? 1ULL
      : 1ULL + (static_cast<std::uint64_t>(frames) - kProducerFrameSize) / kProducerHopSize;
  return analysed * workPerFrame;
}

PitchAnalysisLimits producerPitchLimits(std::size_t frames) noexcept {
  return PitchAnalysisLimits{
      .maximumFrames = 4096U,
      .maximumCorrelationTerms = 0U,
      .maximumTransformButterflies = producerAnalysisWork(frames),
  };
}

PitchMarkGenerationConfig producerPitchMarkConfig() noexcept {
  PitchMarkGenerationConfig config;
  config.pitch = producerPitchConfig();
  return config;
}

bool AcousticAnalysis::currentAlgorithm() const noexcept {
  return algorithmId == kAcousticAnalysisAlgorithmId &&
         algorithmVersion == kAcousticAnalysisAlgorithmVersion;
}

const AcousticVoicingSpan* AcousticAnalysis::spanAt(time::SampleFrame frame) const noexcept {
  const auto found = std::upper_bound(spans.begin(), spans.end(), frame,
      [](time::SampleFrame value, const AcousticVoicingSpan& span) {
        return value < span.start;
      });
  if (found == spans.begin()) return nullptr;
  const auto& candidate = *std::prev(found);
  return frame < candidate.end ? &candidate : nullptr;
}

core::Result<AcousticAnalysis> analyzeUnitAcoustics(
    std::span<const float> samples, std::uint32_t sampleRate, const Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames,
    AcousticAnalysisLimits limits, std::stop_token stopToken) {
  const auto config = producerPitchConfig();
  const auto validUnit = unit.validate();
  if (!validUnit) return core::Result<AcousticAnalysis>{validUnit.error()};
  if (!isDigest(verifiedAudioSha256)) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::InvalidArgument,
        "Acoustic analysis requires the digest of the audio it measured", unit.id);
  }
  if (sampleRate < 8000U || sampleRate > 384000U) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::InvalidArgument,
        "Acoustic analysis sample rate is unsupported", unit.id);
  }
  if (samples.empty() || decodedFrames <= 0 ||
      static_cast<std::uint64_t>(decodedFrames) !=
          static_cast<std::uint64_t>(samples.size())) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::InvalidArgument,
        "Acoustic analysis requires the exact decoded frame count", unit.id);
  }
  if (limits.maximumSpans == 0U) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::InvalidArgument,
        "Acoustic analysis requires a nonzero span budget", unit.id);
  }
  const auto markerCheck = unit.markers.validate(decodedFrames);
  if (!markerCheck) return core::Result<AcousticAnalysis>{markerCheck.error()};

  auto frames = analyzePitch(samples, sampleRate, config, stopToken, limits.pitch);
  if (!frames) return core::Result<AcousticAnalysis>{frames.error()};
  if (frames.value().empty()) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::NotFound,
        "Acoustic analysis produced no frames", unit.id);
  }

  // Analysis windows overlap: a frame at origin s covers [s, s + frameSize),
  // while frames are one hop apart, so up to frameSize/hop frames describe the
  // same sample. Voicing at a sample is therefore not a single frame's answer,
  // and emitting one span per frame would produce overlapping claims about the
  // same audio.
  //
  // Each sample is instead described by the frame whose window centre is nearest
  // (partitionPitchFrames). Pitch mark generation uses the same partition, so a
  // generated mark can only sit where a span of this record says the audio is
  // voiced. The regions tile [0, decodedFrames) exactly once; consecutive regions
  // that agree are merged, so a span means "the analysis found one state
  // throughout", not "one frame was analysed here".
  //
  // Algorithm version 1 gave each frame [origin_i, origin_{i+1}) instead, which
  // attributes a window's conclusion to its first hop. On the voiced/noise/voiced
  // engineering fixture in tests/test_audio_conditioning.cpp that put the
  // noise-to-voice boundary 1408 samples early. The version identity is what makes
  // a record written by that revision refuse to load rather than be reinterpreted.
  AcousticAnalysis analysis;
  analysis.unitId = unit.id;
  analysis.audioSha256 = std::string{verifiedAudioSha256};
  analysis.sampleRate = sampleRate;
  analysis.decodedFrames = decodedFrames;
  analysis.algorithmId = std::string{kAcousticAnalysisAlgorithmId};
  analysis.algorithmVersion = std::string{kAcousticAnalysisAlgorithmVersion};

  const auto& analysed = frames.value();
  const auto regions = partitionPitchFrames(analysed, config.frameSize,
                                            static_cast<std::size_t>(decodedFrames));
  // Inclusive frame-index range behind each span, so its fundamental and
  // confidence summarise exactly the frames that describe it.
  std::vector<std::pair<std::size_t, std::size_t>> spanFrames;
  for (const auto& region : regions) {
    const bool voiced = analysed[region.frameIndex].voiced;
    const auto start = static_cast<time::SampleFrame>(region.start);
    const auto end = static_cast<time::SampleFrame>(region.end);
    if (!analysis.spans.empty() && analysis.spans.back().voiced == voiced) {
      analysis.spans.back().end = end;
      spanFrames.back().second = region.frameIndex;
      continue;
    }
    if (analysis.spans.size() >= limits.maximumSpans) {
      return core::failure<AcousticAnalysis>(core::ErrorCode::Unsupported,
          "Acoustic analysis exceeds the span budget: the source alternates too often to describe compactly",
          unit.id);
    }
    analysis.spans.push_back(AcousticVoicingSpan{start, end, voiced, 0.0, 0.0});
    spanFrames.emplace_back(region.frameIndex, region.frameIndex);
  }
  if (analysis.spans.empty()) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::NotFound,
        "Acoustic analysis produced no spans to describe the audio", unit.id);
  }

  // Every frame behind a span reached the span's voicing conclusion, but a voiced
  // frame can still lack a usable fundamental, so the median skips those rather
  // than counting them. An unvoiced span reports none rather than inheriting a
  // neighbour's.
  for (std::size_t index = 0U; index < analysis.spans.size(); ++index) {
    auto& span = analysis.spans[index];
    const auto [first, last] = spanFrames[index];
    span.f0Hz = span.voiced ? medianVoicedHz(analysed, first, last) : 0.0;
    span.confidence = meanConfidence(analysed, first, last);
    if (span.voiced && span.f0Hz <= 0.0) {
      // A merged voiced run whose frames reported no usable fundamental is not a
      // measurement. Reporting it as voiced with a placeholder pitch would be.
      span.voiced = false;
    }
    if (!std::isfinite(span.f0Hz) || span.f0Hz < 0.0 ||
        !std::isfinite(span.confidence) || span.confidence < 0.0 ||
        span.confidence > 1.0) {
      return core::failure<AcousticAnalysis>(core::ErrorCode::InvariantViolation,
          "Acoustic analysis produced a non-finite measurement", unit.id);
    }
  }

  const auto checked = validateAcousticAnalysis(analysis, unit, verifiedAudioSha256,
                                                decodedFrames);
  if (!checked) return core::Result<AcousticAnalysis>{checked.error()};
  return core::success(std::move(analysis));
}

core::Result<void> validateAcousticAnalysis(
    const AcousticAnalysis& analysis, const Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames) {
  const auto validUnit = unit.validate();
  if (!validUnit) return validUnit;
  if (analysis.unitId != unit.id) {
    return core::failure(core::ErrorCode::Conflict,
        "Acoustic analysis belongs to a different unit", unit.id);
  }
  if (!isDigest(analysis.audioSha256) || !isDigest(verifiedAudioSha256) ||
      analysis.audioSha256 != verifiedAudioSha256) {
    return core::failure(core::ErrorCode::Conflict,
        "Acoustic analysis does not match the verified audio digest", unit.id);
  }
  if (analysis.sampleRate < 8000U || analysis.sampleRate > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
        "Acoustic analysis sample rate is unsupported", unit.id);
  }
  if (decodedFrames <= 0 || analysis.decodedFrames != decodedFrames) {
    return core::failure(core::ErrorCode::Conflict,
        "Acoustic analysis does not cover the decoded audio extent", unit.id);
  }
  if (analysis.algorithmId != kAcousticAnalysisAlgorithmId ||
      analysis.algorithmVersion != kAcousticAnalysisAlgorithmVersion) {
    return core::failure(core::ErrorCode::Conflict,
        "Acoustic analysis was produced by a different algorithm revision and must be regenerated",
        unit.id);
  }
  if (analysis.spans.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
        "Acoustic analysis must describe at least one span", unit.id);
  }
  time::SampleFrame expected = 0;
  for (const auto& span : analysis.spans) {
    if (span.start != expected || span.end <= span.start || span.end > decodedFrames) {
      return core::failure(core::ErrorCode::Conflict,
          "Acoustic analysis spans must be ordered, contiguous and in bounds",
          unit.id);
    }
    if (!std::isfinite(span.f0Hz) || span.f0Hz < 0.0 ||
        !std::isfinite(span.confidence) || span.confidence < 0.0 ||
        span.confidence > 1.0) {
      return core::failure(core::ErrorCode::InvalidArgument,
          "Acoustic analysis span values are out of range", unit.id);
    }
    // A voiced span without a fundamental, or an unvoiced span with one, means
    // the record contradicts itself. Both cases were reachable before spans were
    // validated, and both mislead a consumer that trusts either field alone.
    if (span.voiced && span.f0Hz <= 0.0) {
      return core::failure(core::ErrorCode::InvariantViolation,
          "A voiced acoustic span must report a fundamental", unit.id);
    }
    if (!span.voiced && span.f0Hz != 0.0) {
      return core::failure(core::ErrorCode::InvariantViolation,
          "An unvoiced acoustic span must not report a fundamental", unit.id);
    }
    expected = span.end;
  }
  if (expected != decodedFrames) {
    return core::failure(core::ErrorCode::Conflict,
        "Acoustic analysis coverage is incomplete", unit.id);
  }
  return core::success();
}

core::Result<std::string> encodeAcousticAnalysis(
    const AcousticAnalysis& analysis, const Unit& unit,
    std::string_view verifiedAudioSha256, time::SampleFrame decodedFrames) {
  const auto valid = validateAcousticAnalysis(analysis, unit, verifiedAudioSha256,
                                              decodedFrames);
  if (!valid) return core::Result<std::string>{valid.error()};
  Array spans;
  spans.reserve(analysis.spans.size());
  for (const auto& span : analysis.spans) {
    spans.emplace_back(Object{
        {"start", JsonValue{span.start}},
        {"end", JsonValue{span.end}},
        {"voiced", JsonValue{span.voiced}},
        {"f0Hz", JsonValue{span.f0Hz}},
        {"confidence", JsonValue{span.confidence}},
    });
  }
  auto json = formats::stringifyJson(JsonValue{Object{
      {"formatId", JsonValue{std::string{kAcousticAnalysisFormatId}}},
      {"schemaVersion", JsonValue{static_cast<std::int64_t>(AcousticAnalysis::kSchemaVersion)}},
      {"unitId", JsonValue{analysis.unitId}},
      {"audioSha256", JsonValue{analysis.audioSha256}},
      {"sampleRate", JsonValue{static_cast<std::int64_t>(analysis.sampleRate)}},
      {"decodedFrames", JsonValue{analysis.decodedFrames}},
      {"algorithmId", JsonValue{analysis.algorithmId}},
      {"algorithmVersion", JsonValue{analysis.algorithmVersion}},
      {"spans", JsonValue{std::move(spans)}},
  }});
  if (json.size() > 512U * 1024U) {
    return core::failure<std::string>(core::ErrorCode::InvalidArgument,
        "Encoded acoustic analysis exceeds the sidecar bound", unit.id);
  }
  return core::success(std::move(json));
}

core::Result<AcousticAnalysis> decodeAcousticAnalysis(
    std::string_view json, const Unit& unit, std::string_view verifiedAudioSha256,
    time::SampleFrame decodedFrames) {
  if (json.size() > 512U * 1024U) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::InvalidArgument,
        "Acoustic analysis sidecar exceeds the size bound", unit.id);
  }
  const auto parsed = formats::parseJson(json, formats::JsonParseLimits{
      .maximumInputBytes = 512U * 1024U,
      .maximumDepth = 4U,
      .maximumNodes = 8192U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 4096U,
  });
  if (!parsed) return core::Result<AcousticAnalysis>{parsed.error()};
  const auto& root = parsed.value();
  if (!root.isObject()) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::ParseError,
        "Acoustic analysis root must be an object");
  }
  const auto* formatId = root.find("formatId");
  const auto* schema = root.find("schemaVersion");
  const auto* unitId = root.find("unitId");
  const auto* audioSha256 = root.find("audioSha256");
  const auto* sampleRate = root.find("sampleRate");
  const auto* frames = root.find("decodedFrames");
  const auto* algorithmId = root.find("algorithmId");
  const auto* algorithmVersion = root.find("algorithmVersion");
  const auto* spans = root.find("spans");
  if (formatId == nullptr || schema == nullptr || unitId == nullptr ||
      audioSha256 == nullptr || sampleRate == nullptr || frames == nullptr ||
      algorithmId == nullptr || algorithmVersion == nullptr || spans == nullptr ||
      !formatId->isString() || !schema->isInteger() || !unitId->isString() ||
      !audioSha256->isString() || !sampleRate->isInteger() || !frames->isInteger() ||
      !algorithmId->isString() || !algorithmVersion->isString() || !spans->isArray()) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::ParseError,
        "Acoustic analysis fields are invalid");
  }
  if (formatId->asString() != kAcousticAnalysisFormatId) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::Unsupported,
        "Unsupported acoustic analysis format");
  }
  if (schema->asInt64() != static_cast<std::int64_t>(AcousticAnalysis::kSchemaVersion)) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::Unsupported,
        "Unsupported acoustic analysis schema");
  }
  const auto rateValue = sampleRate->asInt64();
  if (rateValue < 0 ||
      rateValue > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return core::failure<AcousticAnalysis>(core::ErrorCode::ParseError,
        "Acoustic analysis sample rate is invalid");
  }
  AcousticAnalysis analysis;
  analysis.unitId = unitId->asString();
  analysis.audioSha256 = audioSha256->asString();
  analysis.sampleRate = static_cast<std::uint32_t>(rateValue);
  analysis.decodedFrames = frames->asInt64();
  analysis.algorithmId = algorithmId->asString();
  analysis.algorithmVersion = algorithmVersion->asString();
  analysis.spans.reserve(spans->asArray().size());
  for (const auto& entry : spans->asArray()) {
    if (!entry.isObject()) {
      return core::failure<AcousticAnalysis>(core::ErrorCode::ParseError,
          "Acoustic analysis span must be an object");
    }
    const auto* start = entry.find("start");
    const auto* end = entry.find("end");
    const auto* voiced = entry.find("voiced");
    const auto* f0 = entry.find("f0Hz");
    const auto* confidence = entry.find("confidence");
    if (start == nullptr || end == nullptr || voiced == nullptr || f0 == nullptr ||
        confidence == nullptr || !start->isInteger() || !end->isInteger() ||
        !voiced->isBool() || !f0->isNumber() || !confidence->isNumber()) {
      return core::failure<AcousticAnalysis>(core::ErrorCode::ParseError,
          "Acoustic analysis span fields are invalid");
    }
    analysis.spans.push_back(AcousticVoicingSpan{
        .start = start->asInt64(),
        .end = end->asInt64(),
        .voiced = voiced->asBool(),
        .f0Hz = f0->asNumber(),
        .confidence = confidence->asNumber(),
    });
  }
  const auto valid = validateAcousticAnalysis(analysis, unit, verifiedAudioSha256,
                                              decodedFrames);
  if (!valid) return core::Result<AcousticAnalysis>{valid.error()};
  return core::success(std::move(analysis));
}

std::string acousticAnalysisSidecarPath(std::string_view unitId) {
  return "analysis/" + core::sha256Hex(unitId) + ".json";
}

std::optional<bool> acousticVoicedAt(const AcousticAnalysis& analysis,
                                     time::SampleFrame frame) noexcept {
  const auto* span = analysis.spanAt(frame);
  if (span == nullptr) return std::nullopt;
  return span->voiced;
}

core::Result<StoredAcousticAnalyses> storeBankAcousticAnalyses(
    const Manifest& manifest, const std::filesystem::path& bankRoot,
    std::stop_token stopToken) {
  using Output = StoredAcousticAnalyses;
  const auto validManifest = manifest.validate();
  if (!validManifest) return core::Result<Output>{validManifest.error()};
  if (bankRoot.empty()) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Storing acoustic analyses requires a bank root");
  }
  const auto directory = bankRoot / "analysis";
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if (error == std::errc::no_such_file_or_directory ||
      (!error && !std::filesystem::exists(status))) {
    error.clear();
    std::filesystem::create_directory(directory, error);
    if (error) {
      return core::failure<Output>(core::ErrorCode::IoError,
          "Cannot create the acoustic analysis directory", directory.string());
    }
  } else if (error || !std::filesystem::is_directory(status) ||
             std::filesystem::is_symlink(status)) {
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Voicebank acoustic analysis directory must be a real directory",
        directory.string());
  }

  // Units may share one recording; decode each file once.
  struct DecodedAudio final {
    std::string sha256;
    std::vector<float> mono;
    std::uint32_t sampleRate{0};
  };
  std::map<std::filesystem::path, DecodedAudio> decodedByPath;
  Output result;
  for (const auto& unit : manifest.units) {
    if (stopToken.stop_requested()) {
      return core::failure<Output>(core::ErrorCode::Conflict,
          "Storing acoustic analyses was cancelled", unit.id);
    }
    const auto resolved = resolveBankAsset(bankRoot, unit.audioPath);
    if (!resolved) return core::Result<Output>{resolved.error()};
    auto audio = decodedByPath.find(resolved.value());
    if (audio == decodedByPath.end()) {
      const auto bytes = core::readFileBytesLimited(resolved.value(), kMaximumSupportedWavBytes);
      if (!bytes) return core::Result<Output>{bytes.error()};
      auto decoded = readWav(bytes.value(), resolved.value().string());
      if (!decoded) return core::Result<Output>{decoded.error()};
      audio = decodedByPath.emplace(resolved.value(), DecodedAudio{
          core::sha256Hex(bytes.value()), decoded.value().monoMix(),
          decoded.value().sampleRate}).first;
    }
    const auto& value = audio->second;
    const auto frames = static_cast<time::SampleFrame>(value.mono.size());
    const auto sidecar = bankRoot / acousticAnalysisSidecarPath(unit.id);
    const auto leaveUnmeasured = [&]() -> core::Result<void> {
      // A record left from earlier audio would outlive the measurement it claims.
      error.clear();
      std::filesystem::remove(sidecar, error);
      if (error) {
        return core::failure(core::ErrorCode::IoError,
            "Cannot remove an acoustic analysis that no longer describes its audio",
            sidecar.string());
      }
      result.unmeasuredUnits.push_back(unit.id);
      return core::success();
    };
    // The producer analysis admits one bounded pass over a take. A longer take is
    // left unmeasured rather than failing the whole bank, which is what the
    // analyser's own budget refusal would otherwise do.
    const auto limits = producerPitchLimits(value.mono.size());
    const auto analysedFrames = value.mono.size() <= kProducerFrameSize
        ? std::size_t{1}
        : 1U + (value.mono.size() - kProducerFrameSize) / kProducerHopSize;
    if (analysedFrames > limits.maximumFrames) {
      const auto left = leaveUnmeasured();
      if (!left) return core::Result<Output>{left.error()};
      continue;
    }
    auto analysis = analyzeUnitAcoustics(value.mono, value.sampleRate, unit, value.sha256,
        frames, AcousticAnalysisLimits{.maximumSpans = 4096U, .pitch = limits}, stopToken);
    if (!analysis) {
      if (stopToken.stop_requested()) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Storing acoustic analyses was cancelled", unit.id);
      }
      if (analysis.error().code != core::ErrorCode::NotFound &&
          analysis.error().code != core::ErrorCode::Unsupported) {
        return core::Result<Output>{analysis.error()};
      }
      const auto left = leaveUnmeasured();
      if (!left) return core::Result<Output>{left.error()};
      continue;
    }
    const auto encoded = encodeAcousticAnalysis(analysis.value(), unit, value.sha256, frames);
    if (!encoded) return core::Result<Output>{encoded.error()};
    const auto written = core::durableAtomicWriteText(sidecar, encoded.value());
    if (!written) return core::Result<Output>{written.error()};
    ++result.written;
  }
  return core::success(std::move(result));
}

}  // namespace seam::voicebank
