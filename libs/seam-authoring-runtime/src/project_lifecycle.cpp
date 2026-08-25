#include "seam/authoring/project_lifecycle.hpp"

#include "seam/core/sha256.hpp"
#include "seam/domain/note.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seam::authoring {
namespace {

std::string trimAsciiWhitespace(std::string value) {
  const auto whitespace = [](unsigned char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n' || character == '\f' || character == '\v';
  };
  const auto first = std::find_if_not(value.begin(), value.end(),
      [&whitespace](char character) {
        return whitespace(static_cast<unsigned char>(character));
      });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
      [&whitespace](char character) {
        return whitespace(static_cast<unsigned char>(character));
      }).base();
  return first >= last ? std::string{} : std::string{first, last};
}

bool supportedSampleRate(std::uint32_t sampleRate) noexcept {
  return sampleRate == 44100U || sampleRate == 48000U ||
         sampleRate == 96000U;
}

bool supportedChannelCount(std::uint8_t channels) noexcept {
  return channels == 1U || channels == 2U || channels == 4U ||
         channels == 8U;
}

domain::RoutingMatrix monoToChannels(std::uint8_t channels) {
  domain::RoutingMatrix result{
      .sourceChannels = 1U,
      .destinationChannels = channels,
      .gains = {},
  };
  const auto gain = static_cast<float>(1.0 / std::sqrt(
      static_cast<double>(channels)));
  result.gains.assign(channels, gain);
  return result;
}

void configureRouting(domain::Project& project, std::uint8_t channels) {
  project.routing() = domain::ProjectRouting{
      .deviceOutputChannels = channels,
      .masterBus = domain::BusId{1U},
      .buses = {domain::AudioBus{
          .id = domain::BusId{1U},
          .name = "Master",
          .channelCount = channels,
          .gainDb = 0.0F,
          .muted = false,
          .solo = false,
      }},
      .sends = {},
      .deviceRoutes = {domain::DeviceOutputRoute{
          .sourceBus = domain::BusId{1U},
          .matrix = domain::RoutingMatrix::identity(channels),
          .gainDb = 0.0F,
          .enabled = true,
      }},
  };
  for (auto& track : project.vocalTracks()) {
    track.outputRoute = domain::TrackOutputRoute{
        .bus = domain::BusId{1U},
        .matrix = monoToChannels(channels),
    };
  }
  for (auto& track : project.audioTracks()) {
    track.outputRoute = domain::TrackOutputRoute{
        .bus = domain::BusId{1U},
        .matrix = monoToChannels(channels),
    };
  }
}

domain::VoicebankReference referenceFor(
    const voicebank::VoicebankCandidate& candidate) {
  return domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
}

core::Result<std::filesystem::path> normalizedFilePath(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Project file path cannot be empty");
  }
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to resolve project file path", error.message());
  }
  return absolute.lexically_normal();
}

}  // namespace

core::Result<void> ProjectLifecycleService::createNew(
    ProjectDocument& document, const NewProjectRequest& request) const {
  auto name = trimAsciiWhitespace(request.name);
  if (name.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project name cannot be empty");
  }
  if (name.size() > 1024U || !domain::fromUtf8(name)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project name must be valid bounded UTF-8");
  }
  if (!std::isfinite(request.tempoBpm) || request.tempoBpm < 20.0 ||
      request.tempoBpm > 400.0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project tempo must be between 20 and 400 BPM");
  }
  if (!supportedSampleRate(request.sampleRate)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project sample rate must be 44100, 48000, or 96000 Hz");
  }
  if (!supportedChannelCount(request.outputChannels)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project output channels must be 1, 2, 4, or 8");
  }
  if (request.initialVoicebank.has_value() &&
      (request.initialVoicebank->manifest.id.empty() ||
       request.initialVoicebank->manifest.version.empty() ||
       request.initialVoicebank->contentHash.empty())) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Initial Voicebank requires ID, version, and content hash");
  }

  application::ProjectFactory factory{document.factory().nextIdValue()};
  auto project = factory.createProject(std::move(name));
  project.settings().sampleRate = static_cast<double>(request.sampleRate);
  project.settings().characterDisplay = domain::CharacterDisplayMode::Minimal;
  auto tempo = project.tempoMap().addOrReplace(time::Tick{0}, request.tempoBpm);
  if (!tempo) return tempo;
  auto meter = project.meterMap().addOrReplace(
      time::Tick{0}, request.meterNumerator, request.meterDenominator);
  if (!meter) return meter;

  if (request.createInitialVocalTrack) {
    const auto trackId = factory.addVocalTrack(project, "Voice 1");
    const auto sixteenBars = time::Tick{
        static_cast<std::int64_t>(16 * 4 * seam::time::kDefaultPpq)};
    static_cast<void>(factory.addRegion(project, trackId, "Region 1",
                                        time::Tick{0}, sixteenBars));
    if (request.initialVoicebank.has_value()) {
      project.vocalTracks().front().voicebank =
          referenceFor(*request.initialVoicebank);
    }
  } else if (request.initialVoicebank.has_value()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "An initial voicebank requires an initial vocal track");
  }
  configureRouting(project, request.outputChannels);
  const auto validation = project.validate();
  if (!validation) return validation;
  if (request.projectPath.has_value()) {
    auto normalized = normalizedFilePath(*request.projectPath);
    if (!normalized) return core::Result<void>{normalized.error()};
    const auto written = write(project, normalized.value(), {});
    if (!written) return core::Result<void>{written.error()};
    const auto replaced = document.replaceProject(std::move(project));
    if (!replaced) return replaced;
    document.markSaved(normalized.value(), written.value());
    return core::success();
  }
  return document.replaceProject(std::move(project));
}

core::Result<OpenProjectResult> ProjectLifecycleService::open(
    ProjectDocument& document, const std::filesystem::path& path) const {
  auto normalized = normalizedFilePath(path);
  if (!normalized) {
    return core::Result<OpenProjectResult>{normalized.error()};
  }
  auto source = core::readTextFileLimited(normalized.value(),
                                          64ULL * 1024ULL * 1024ULL);
  if (!source) return core::Result<OpenProjectResult>{source.error()};
  auto loaded = codec_.decode(source.value());
  if (!loaded) return core::Result<OpenProjectResult>{loaded.error()};

  OpenProjectResult result;
  if (voicebanks_ != nullptr) {
    result.voicebanks = voicebanks_->resolveAll(loaded.value());
  }
  auto replaced = document.replaceProject(std::move(loaded).value());
  if (!replaced) return core::Result<OpenProjectResult>{replaced.error()};
  document.markSaved(normalized.value(), core::sha256Hex(source.value()));
  return result;
}

core::Result<void> ProjectLifecycleService::save(
    ProjectDocument& document, const ProjectSaveOptions& options) const {
  if (!document.identity().projectPath.has_value()) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Project has no assigned file path; use Save As");
  }
  const auto written = write(document.session().project(),
                             *document.identity().projectPath, options);
  if (!written) return core::Result<void>{written.error()};
  document.markSaved(*document.identity().projectPath, written.value());
  return core::success();
}

core::Result<void> ProjectLifecycleService::saveAs(
    ProjectDocument& document, const std::filesystem::path& path,
    const ProjectSaveOptions& options) const {
  auto normalized = normalizedFilePath(path);
  if (!normalized) return core::Result<void>{normalized.error()};
  const auto written = write(document.session().project(), normalized.value(),
                             options);
  if (!written) return core::Result<void>{written.error()};
  document.markSaved(normalized.value(), written.value());
  return core::success();
}

core::Result<std::string> ProjectLifecycleService::write(
    const domain::Project& project, const std::filesystem::path& path,
    const ProjectSaveOptions& options) const {
  auto encoded = codec_.encode(project);
  if (!encoded) return core::Result<std::string>{encoded.error()};
  auto backup = path;
  backup += ".bak";
  const auto written = core::durableAtomicWriteText(
      path, encoded.value(), core::AtomicWriteOptions{
          .backupPath = std::move(backup),
          .maximumBackupBytes = 64ULL * 1024ULL * 1024ULL,
          .faultInjector = options.faultInjector,
      });
  if (!written) return core::Result<std::string>{written.error()};
  return core::sha256Hex(encoded.value());
}

}  // namespace seam::authoring
