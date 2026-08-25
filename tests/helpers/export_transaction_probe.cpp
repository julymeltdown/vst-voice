#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/voicebank/wav.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kInterruptedExitCode = 86;

std::optional<seam::authoring::ExportPublicationPhase> phaseFromName(
    const std::string& name) {
  using Phase = seam::authoring::ExportPublicationPhase;
  if (name == "JournalPrepared") return Phase::JournalPrepared;
  if (name == "PreviousMoved") return Phase::PreviousMoved;
  if (name == "DestinationPublished") return Phase::DestinationPublished;
  if (name == "ReceiptCommitted") return Phase::ReceiptCommitted;
  if (name == "BackupRemoved") return Phase::BackupRemoved;
  return std::nullopt;
}

std::optional<seam::domain::Project> fixtureProject(
    const std::filesystem::path& root) {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) return std::nullopt;

  std::vector<float> samples(2400U);
  for (std::size_t index = 0U; index < samples.size(); ++index) {
    samples[index] = 0.2F * static_cast<float>(std::sin(
        2.0 * 3.14159265358979323846 * 220.0 *
        static_cast<double>(index) / 48000.0));
  }
  const auto media = root / "backing.wav";
  if (!seam::voicebank::writeMonoPcm16Wav(media, 48000U, samples)) {
    return std::nullopt;
  }
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  if (!source) return std::nullopt;

  seam::application::ProjectFactory factory{42000U};
  auto project = factory.createProject("Export process recovery");
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = factory.nextTrackId(),
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  });
  if (!project.validate()) return std::nullopt;
  return project;
}

seam::authoring::ExportSettings settings(bool stems) {
  return seam::authoring::ExportSettings{
      .sampleRate = 48000U,
      .channels = 2U,
      .format = seam::voicebank::WavSampleFormat::Pcm16,
      .includeMaster = true,
      .includeStems = stems,
      .replaceExisting = true,
  };
}

bool receiptMatches(const std::filesystem::path& destination,
                    std::int64_t expectedRevision,
                    std::size_t expectedFileCount) {
  std::ifstream input{destination / "receipt.json", std::ios::binary};
  const std::string text{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
  auto parsed = seam::formats::parseJson(text);
  if (!parsed || !parsed.value().isObject()) return false;
  const auto* state = parsed.value().find("state");
  const auto* revision = parsed.value().find("projectRevision");
  const auto* files = parsed.value().find("files");
  return state != nullptr && state->isString() &&
         state->asString() == "COMMITTED" && revision != nullptr &&
         revision->isInteger() && revision->asInt64() == expectedRevision &&
         files != nullptr && files->isArray() &&
         files->asArray().size() == expectedFileCount;
}

bool transactionArtifactsAbsent(const std::filesystem::path& destination) {
  std::error_code error;
  const auto prefix = "." + destination.filename().string() + "-export-";
  for (std::filesystem::directory_iterator iterator{destination.parent_path(),
                                                     error};
       !error && iterator != std::filesystem::directory_iterator{};
       iterator.increment(error)) {
    if (iterator->path().filename().string().starts_with(prefix)) return false;
  }
  return !error;
}

int seed(const std::filesystem::path& root) {
  auto project = fixtureProject(root);
  if (!project) return 10;
  const auto destination = root / "exports" / "song";
  auto initial = settings(true);
  initial.replaceExisting = false;
  seam::authoring::ExportService service;
  auto result = service.exportSet(
      *project, {}, {}, {}, 1U, destination, initial);
  if (!result || result.value().state != seam::authoring::ExportState::Committed) {
    return 11;
  }
  std::ofstream{destination / "keep.txt"} << "unowned-canary";
  const auto previous = destination.parent_path() /
                        (destination.filename().string() + ".previous");
  std::error_code error;
  std::filesystem::create_directories(previous, error);
  if (error) return 12;
  std::ofstream{previous / "keep.txt"} << "unowned-sibling";
  return 0;
}

int interrupt(const std::filesystem::path& root,
              seam::authoring::ExportPublicationPhase phase) {
  auto project = fixtureProject(root);
  if (!project) return 20;
  auto replacement = settings(false);
  replacement.publicationFaultInjector = [phase](
      seam::authoring::ExportPublicationPhase current) {
    if (current == phase) std::_Exit(kInterruptedExitCode);
    return false;
  };
  seam::authoring::ExportService service;
  static_cast<void>(service.exportSet(
      *project, {}, {}, {}, 2U, root / "exports" / "song", replacement));
  return 21;
}

int recover(const std::filesystem::path& root, std::int64_t expectedRevision) {
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  const auto result = service.recoverSet(destination);
  const auto expectedFiles = expectedRevision == 1 ? 2U : 1U;
  const auto expectedStems = expectedRevision == 1;
  const auto previous = destination.parent_path() /
                        (destination.filename().string() + ".previous");
  if (!result || !std::filesystem::is_regular_file(destination / "master.wav") ||
      !std::filesystem::is_regular_file(destination / "keep.txt") ||
      !std::filesystem::is_regular_file(previous / "keep.txt") ||
      std::filesystem::exists(destination / "stems") != expectedStems ||
      !receiptMatches(destination, expectedRevision, expectedFiles) ||
      !transactionArtifactsAbsent(destination)) {
    return 30;
  }
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: export_transaction_probe MODE ROOT [PHASE|REVISION]\n";
    return 2;
  }
  const std::string mode{argv[1]};
  const std::filesystem::path root{argv[2]};
  if (mode == "seed" && argc == 3) return seed(root);
  if (mode == "interrupt" && argc == 4) {
    const auto phase = phaseFromName(argv[3]);
    return phase ? interrupt(root, *phase) : 3;
  }
  if (mode == "recover" && argc == 4) {
    if (std::string{argv[3]} == "1") return recover(root, 1);
    if (std::string{argv[3]} == "2") return recover(root, 2);
    return 3;
  }
  return 2;
}
