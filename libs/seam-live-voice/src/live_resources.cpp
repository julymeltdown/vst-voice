#include "seam/live_voice/live_resources.hpp"

#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace seam::live_voice {
namespace {

phase12c::UnitKind mapKind(voicebank::UnitKind kind) noexcept {
  switch (kind) {
    case voicebank::UnitKind::Release: return phase12c::UnitKind::Release;
    case voicebank::UnitKind::Sustain: return phase12c::UnitKind::Sustain;
    case voicebank::UnitKind::Vcv:
    case voicebank::UnitKind::Vv: return phase12c::UnitKind::Transition;
    case voicebank::UnitKind::Cv:
    case voicebank::UnitKind::Vc:
    case voicebank::UnitKind::Cc:
    case voicebank::UnitKind::Breath:
    case voicebank::UnitKind::Glottal:
    case voicebank::UnitKind::Special: return phase12c::UnitKind::Attack;
  }
  return phase12c::UnitKind::Sustain;
}

core::Result<std::filesystem::path> resolveUnitPath(
    const std::filesystem::path& bankRoot,
    const std::filesystem::path& relative) {
  if (relative.empty() || relative.is_absolute() ||
      std::find(relative.begin(), relative.end(), std::filesystem::path{".."}) !=
          relative.end()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Live voice resource path must remain relative to the bank");
  }
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(bankRoot, error);
  if (error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError, "Unable to canonicalize live voicebank root",
        bankRoot.string());
  }
  const auto candidate = std::filesystem::weakly_canonical(root / relative, error);
  if (error || !std::filesystem::is_regular_file(candidate, error)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::NotFound, "Live voice unit audio is missing",
        (root / relative).string());
  }
  const auto rootText = root.generic_string();
  const auto candidateText = candidate.generic_string();
  if (candidateText != rootText &&
      candidateText.rfind(rootText + "/", 0U) != 0U) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Live voice unit audio escapes the bank root", candidate.string());
  }
  return candidate;
}

LiveSegmentRole roleFor(voicebank::UnitKind kind) noexcept {
  switch (kind) {
    case voicebank::UnitKind::Release: return LiveSegmentRole::Release;
    case voicebank::UnitKind::Sustain: return LiveSegmentRole::Sustain;
    case voicebank::UnitKind::Vcv:
    case voicebank::UnitKind::Vc:
    case voicebank::UnitKind::Vv:
    case voicebank::UnitKind::Cc: return LiveSegmentRole::Transition;
    case voicebank::UnitKind::Breath: return LiveSegmentRole::Breath;
    case voicebank::UnitKind::Cv:
    case voicebank::UnitKind::Glottal:
    case voicebank::UnitKind::Special: return LiveSegmentRole::Attack;
  }
  return LiveSegmentRole::Attack;
}

struct DecodedMono final {
  std::uint32_t sampleRate{0U};
  std::shared_ptr<const std::vector<float>> samples;
};

core::Result<DecodedMono> decodeMono(
    const std::filesystem::path& path,
    std::size_t maximumDecodedBytes) {
  std::error_code fileError;
  const auto fileBytes = std::filesystem::file_size(path, fileError);
  const auto maximumFileBytes = std::min<std::uint64_t>(
      static_cast<std::uint64_t>(maximumDecodedBytes / 8U),
      voicebank::kMaximumSupportedWavBytes);
  if (fileError || fileBytes < 44U || fileBytes > maximumFileBytes) {
    return core::failure<DecodedMono>(
        core::ErrorCode::Unsupported,
        "Live voice unit exceeds the bounded decode contract", path.string());
  }
  auto decoded = voicebank::readWav(path);
  if (!decoded) return core::Result<DecodedMono>{decoded.error()};
  if (decoded.value().sampleRate < 8000U ||
      decoded.value().sampleRate > 192000U || decoded.value().frameCount() == 0U) {
    return core::failure<DecodedMono>(
        core::ErrorCode::Unsupported,
        "Live voice unit sample rate or frame count is unsupported",
        path.string());
  }
  auto mono = std::make_shared<std::vector<float>>(decoded.value().monoMix());
  if (mono->size() > maximumDecodedBytes / sizeof(float)) {
    return core::failure<DecodedMono>(
        core::ErrorCode::Unsupported,
        "Live voice unit decoded PCM exceeds the bounded memory contract",
        path.string());
  }
  if (!std::all_of(mono->begin(), mono->end(), [](float sample) {
        return std::isfinite(sample);
      })) {
    return core::failure<DecodedMono>(
        core::ErrorCode::InvalidArgument,
        "Live voice unit contains non-finite PCM", path.string());
  }
  return DecodedMono{
      .sampleRate = decoded.value().sampleRate,
      .samples = std::move(mono),
  };
}

}

core::Result<std::shared_ptr<const LiveVoicebankResources>>
LiveResourceBuilder::build(const voicebank::VoicebankCandidate& candidate,
                           const LiveResourceBuildOptions& options) const {
  if (candidate.trust == voicebank::VoicebankTrust::UntrustedInstalled) {
    return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
        core::ErrorCode::Conflict,
        "Live voice resources require a trusted Voicebank candidate");
  }
  if (candidate.contentHash.size() != 64U || candidate.manifest.units.empty()) {
    return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
        core::ErrorCode::InvalidArgument,
        "Live voice resources require an exact bank hash and units");
  }
  if (options.maximumDecodedBytes < 44U * 8U) {
    return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
        core::ErrorCode::InvalidArgument,
        "Live resource memory limit is too small for bounded WAV decoding");
  }
  if (options.style.empty()) {
    return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
        core::ErrorCode::InvalidArgument,
        "Live resource style must not be empty");
  }

  auto resources = std::make_shared<LiveVoicebankResources>();
  resources->identity = domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
  resources->style = options.style;
  resources->diagnosticIdentity = candidate.manifest.id + "@" +
                                  candidate.manifest.version + "#" +
                                  candidate.contentHash;

  std::vector<const voicebank::Unit*> units;
  units.reserve(candidate.manifest.units.size());
  for (const auto& unit : candidate.manifest.units) {
    if (!unit.enabled || unit.style != options.style) continue;
    units.push_back(&unit);
  }
  std::sort(units.begin(), units.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->id < rhs->id;
  });
  if (units.empty()) {
    return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
        core::ErrorCode::NotFound,
        "Live Voicebank style has no enabled units", options.style);
  }

  std::unordered_map<std::string, DecodedMono> decoded;
  for (const auto* unit : units) {
    const auto path = resolveUnitPath(candidate.bankRoot, unit->audioPath);
    if (!path) {
      return core::Result<std::shared_ptr<const LiveVoicebankResources>>{
          path.error()};
    }
    const auto key = path.value().generic_string();
    auto found = decoded.find(key);
    if (found == decoded.end()) {
      auto audio = decodeMono(path.value(), options.maximumDecodedBytes);
      if (!audio) {
        return core::Result<std::shared_ptr<const LiveVoicebankResources>>{
            audio.error()};
      }
      if (audio.value().sampleRate != candidate.manifest.expectedSampleRate) {
        return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
            core::ErrorCode::Unsupported,
            "Live unit sample rate does not match the Voicebank manifest",
            unit->id);
      }
      const auto decodedBytes = audio.value().samples->size() * sizeof(float);
      if (decodedBytes > options.maximumDecodedBytes -
                           std::min(options.maximumDecodedBytes,
                                    resources->decodedBytes)) {
        return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
            core::ErrorCode::Unsupported,
            "Live Voicebank decoded PCM exceeds the bounded memory contract",
            unit->id);
      }
      resources->decodedBytes += decodedBytes;
      found = decoded.emplace(key, std::move(audio).value()).first;
    }

    const auto& audio = found->second;
    const auto totalFrames = static_cast<time::SampleFrame>(
        audio.samples->size());
    const auto markerValidation = unit->markers.validate(totalFrames);
    if (!markerValidation) {
      return core::Result<std::shared_ptr<const LiveVoicebankResources>>{
          markerValidation.error()};
    }
    const auto frame = [](time::SampleFrame value) {
      return static_cast<std::uint32_t>(value);
    };
    const auto loopStart = unit->markers.loopStart.value_or(
        unit->markers.stableStart);
    const auto loopEnd = unit->markers.loopEnd.value_or(
        unit->markers.audioEnd);
    const auto releaseStart = unit->markers.releaseStart.value_or(
        unit->markers.audioEnd);
    if (loopStart >= loopEnd || releaseStart < loopEnd ||
        releaseStart > unit->markers.audioEnd) {
      return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
          core::ErrorCode::InvariantViolation,
          "Live articulation markers are not monotonic", unit->id);
    }
    const auto gain = std::pow(10.0F, unit->gainDb / 20.0F);
    if (!std::isfinite(gain) || gain <= 0.0F) {
      return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
          core::ErrorCode::InvariantViolation,
          "Live unit gain is not finite", unit->id);
    }
    resources->units.push_back(LiveUnitAudio{
        .unitId = unit->id,
        .role = roleFor(unit->kind),
        .kind = unit->kind,
        .phones = unit->phones,
        .rootMidi = unit->rootMidi,
        .priority = unit->priority,
        .take = unit->take,
        .gainLinear = gain,
        .sourceSampleRate = audio.sampleRate,
        .mono = audio.samples,
        .sourceStart = frame(unit->markers.audioOffset),
        .sourceEnd = frame(unit->markers.audioEnd),
        .stableStart = frame(unit->markers.stableStart),
        .loopStart = frame(loopStart),
        .loopEnd = frame(loopEnd),
        .releaseStart = frame(releaseStart),
    });
  }
  const auto hasSustain = std::any_of(
      resources->units.begin(), resources->units.end(), [](const auto& unit) {
        return unit.role == LiveSegmentRole::Sustain;
      });
  const auto hasRelease = std::any_of(
      resources->units.begin(), resources->units.end(), [](const auto& unit) {
        return unit.role == LiveSegmentRole::Release;
      });
  if (!hasSustain || (options.requireRelease && !hasRelease)) {
    return core::failure<std::shared_ptr<const LiveVoicebankResources>>(
        core::ErrorCode::Unsupported,
        "Live Voicebank is missing mandatory sustain or release units");
  }
  return std::shared_ptr<const LiveVoicebankResources>{std::move(resources)};
}

core::Result<std::shared_ptr<const phase12c::LiveVoicebankResource>>
buildTrustedResource(const voicebank::VoicebankCandidate& candidate,
                     ResourceBuildOptions options) {
  if (candidate.trust == voicebank::VoicebankTrust::UntrustedInstalled) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::Conflict,
        "Live voice resource requires a trusted voicebank candidate");
  }
  if (candidate.contentHash.size() != 64U || candidate.manifest.units.empty()) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::InvalidArgument,
        "Live voice resource requires an exact bank hash and units");
  }
  if (options.maximumUnits == 0U || options.maximumBytes == 0U) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::InvalidArgument,
        "Live voice resource limits must be positive");
  }
  if (candidate.manifest.expectedSampleRate < options.minimumSampleRate ||
      candidate.manifest.expectedSampleRate > options.maximumSampleRate) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::Unsupported,
        "Live voicebank sample rate is outside the bounded resource contract");
  }
  if (options.maximumBytes < 44U) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::InvalidArgument,
        "Live voice resource byte limit is too small for a WAV header");
  }
  auto resource = std::make_shared<phase12c::LiveVoicebankResource>();
  resource->sampleRate = candidate.manifest.expectedSampleRate;
  resource->contentHash = candidate.contentHash;
  resource->trusted = true;
  std::vector<const voicebank::Unit*> units;
  units.reserve(candidate.manifest.units.size());
  for (const auto& unit : candidate.manifest.units) {
    if (!unit.enabled) continue;
    units.push_back(&unit);
  }
  std::sort(units.begin(), units.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->id < rhs->id;
  });
  if (units.empty() || units.size() > options.maximumUnits) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::Unsupported,
        "Live voicebank unit count exceeds the bounded resource contract");
  }
  bool hasSustain = false;
  bool hasRelease = false;
  for (const auto* unit : units) {
    auto path = resolveUnitPath(candidate.bankRoot, unit->audioPath);
    if (!path) {
      return core::Result<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>{path.error()};
    }
    std::error_code fileError;
    const auto fileBytes = std::filesystem::file_size(path.value(), fileError);
    const auto maximumDecodeBytes = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(options.maximumBytes / 8U),
        voicebank::kMaximumSupportedWavBytes);
    if (fileError || fileBytes < 44U || fileBytes > maximumDecodeBytes) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::Unsupported,
          "Live voice unit exceeds the bounded decode contract",
          path.value().string());
    }
    auto audio = voicebank::readWav(path.value());
    if (!audio) {
      return core::Result<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>{audio.error()};
    }
    const auto markerValidation = unit->markers.validate(
        static_cast<time::SampleFrame>(audio.value().frameCount()));
    if (!markerValidation) {
      return core::Result<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>{
          markerValidation.error()};
    }
    const auto decodedBytes = audio.value().interleaved.size() >
                                      std::numeric_limits<std::size_t>::max() /
                                          sizeof(float)
                                  ? std::numeric_limits<std::size_t>::max()
                                  : audio.value().interleaved.size() * sizeof(float);
    if (decodedBytes > options.maximumBytes -
                           std::min(options.maximumBytes, resource->bytes())) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::Unsupported,
          "Live voice unit decoded PCM exceeds the bounded memory contract",
          path.value().string());
    }
    if (audio.value().sampleRate < options.minimumSampleRate ||
        audio.value().sampleRate > options.maximumSampleRate ||
        audio.value().sampleRate != resource->sampleRate ||
        audio.value().frameCount() == 0U) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::Unsupported,
          "Live voice unit sample rate or frame count is unsupported",
          path.value().string());
    }
    if (resource->mono.size() >
        std::numeric_limits<std::size_t>::max() - audio.value().frameCount()) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::Unsupported, "Live voice resource frame count overflows");
    }
    const auto begin = resource->mono.size();
    const auto mono = audio.value().monoMix();
    resource->mono.insert(resource->mono.end(), mono.begin(), mono.end());
    if (resource->bytes() > options.maximumBytes) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::Unsupported,
          "Live voice resource exceeds the bounded memory contract");
    }
    const auto& markers = unit->markers;
    const auto total = static_cast<std::uint64_t>(mono.size());
    const auto offset = static_cast<std::uint64_t>(markers.audioOffset);
    const auto end = static_cast<std::uint64_t>(markers.audioEnd);
    if (offset >= end || end > total || end - offset < 2U) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::InvalidArgument,
          "Live voice unit markers are outside the decoded audio",
          unit->id);
    }
    const auto relative = [offset](std::optional<time::SampleFrame> value,
                                   std::uint64_t fallback) {
      if (!value.has_value()) return fallback;
      return static_cast<std::uint64_t>(
          std::max<time::SampleFrame>(0, *value - static_cast<time::SampleFrame>(offset)));
    };
    const auto loopBegin = relative(markers.loopStart, static_cast<std::uint64_t>(markers.stableStart));
    const auto loopEnd = relative(markers.loopEnd, static_cast<std::uint64_t>(markers.audioEnd));
    if (loopBegin >= loopEnd || loopEnd > end - offset) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::InvalidArgument,
          "Live voice unit loop markers are invalid", unit->id);
    }
    resource->units.push_back(phase12c::UnitSpan{
        .kind = mapKind(unit->kind),
        .begin = static_cast<std::uint32_t>(begin + offset),
        .end = static_cast<std::uint32_t>(begin + end),
        .loopBegin = static_cast<std::uint32_t>(begin + loopBegin),
        .loopEnd = static_cast<std::uint32_t>(begin + loopEnd),
        .rootKey = static_cast<std::int16_t>(unit->rootMidi),
        .fromKey = -1,
        .toKey = -1,
    });
    hasSustain = hasSustain || unit->kind == voicebank::UnitKind::Sustain;
    hasRelease = hasRelease || unit->kind == voicebank::UnitKind::Release;
  }
  if ((options.requireSustain && !hasSustain) ||
      (options.requireRelease && !hasRelease)) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::Unsupported,
        "Live voicebank is missing mandatory sustain or release units");
  }
  if (!resource->valid()) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::InvariantViolation,
        "Built live voice resource failed validation");
  }
  return std::shared_ptr<const phase12c::LiveVoicebankResource>{
      std::move(resource)};
}

}
