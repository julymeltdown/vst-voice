#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/export_service.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <fstream>
#include <array>

namespace {

seam::domain::Project exportProject(const std::filesystem::path& root,
                                    std::uint64_t firstId) {
  const auto media = root / "backing.wav";
  if (!seam::voicebank::writeMonoPcm16Wav(
          media, 48000U,
          seam::test::support::sineWave(48000U, 220.0, 0.05))) {
    throw seam::test::Failure{"unable to create export fixture WAV"};
  }
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  if (!source) throw seam::test::Failure{"unable to open export fixture WAV"};
  seam::application::ProjectFactory factory{firstId};
  auto project = factory.createProject("Export transaction");
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
  if (!project.validate()) {
    throw seam::test::Failure{"invalid export fixture project"};
  }
  return project;
}

seam::authoring::ExportSettings exportSettings(bool stems = true) {
  return seam::authoring::ExportSettings{
      .sampleRate = 48000U,
      .channels = 2U,
      .format = seam::voicebank::WavSampleFormat::Pcm16,
      .includeMaster = true,
      .includeStems = stems,
      .replaceExisting = true,
  };
}

std::int64_t committedRevision(const std::filesystem::path& destination) {
  std::ifstream input{destination / "receipt.json", std::ios::binary};
  const std::string text{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
  const auto parsed = seam::formats::parseJson(text);
  const auto* revision = parsed && parsed.value().isObject()
                             ? parsed.value().find("projectRevision")
                             : nullptr;
  if (revision == nullptr || !revision->isInteger()) {
    throw seam::test::Failure{"export receipt has no committed revision"};
  }
  return revision->asInt64();
}

}

TEST_CASE("export service publishes a committed master and receipt atomically") {
  const auto root = seam::test::support::temporaryDirectory("export-service");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 2U,
      .interleaved = {0.0F, 0.0F, 0.25F, -0.25F, 0.5F, -0.5F},
  };
  seam::authoring::ExportService service;
  const auto result = service.commitRendered(
      rendered, 42U, destination, seam::voicebank::WavSampleFormat::Pcm24);
  CHECK(result);
  CHECK(result.value().state == seam::authoring::ExportState::Committed);
  CHECK(std::filesystem::exists(destination));
  CHECK(std::filesystem::exists(result.value().receiptPath));
  const auto decoded = seam::voicebank::readWav(destination);
  CHECK(decoded);
  CHECK(decoded.value().channels == 2U);
  CHECK(decoded.value().frameCount() == 3U);
}

TEST_CASE("single-file export removes rollback backups after a successful replacement") {
  const auto root = seam::test::support::temporaryDirectory("export-repeat");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F, 0.4F},
  };
  seam::authoring::ExportService service;
  CHECK(service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16));
  CHECK(service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm16));
  CHECK(!std::filesystem::exists(destination.string() + ".previous"));
  CHECK(!std::filesystem::exists(destination.string() + ".receipt.json.previous"));
  const auto third = service.commitRendered(
      rendered, 3U, destination, seam::voicebank::WavSampleFormat::Pcm16);
  CHECK(third);
  CHECK(third.value().projectRevision == 3U);
}

TEST_CASE("single-file export preserves an unowned backup collision") {
  const auto root = seam::test::support::temporaryDirectory("export-backup-canary");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F, 0.4F},
  };
  seam::authoring::ExportService service;
  CHECK(service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16));
  const auto unownedBackup = std::filesystem::path{destination.string() + ".previous"};
  std::ofstream{unownedBackup} << "unrelated-canary";

  const auto replacement = service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm16);

  CHECK(!replacement);
  std::ifstream input{unownedBackup, std::ios::binary};
  const std::string backupText{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
  CHECK(backupText == "unrelated-canary");
  CHECK(std::filesystem::exists(destination));
}

TEST_CASE("single-file export rolls back the master when receipt rotation fails") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-receipt-rollback");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F, 0.4F},
  };
  seam::authoring::ExportService service;
  CHECK(service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16));

  const auto receipt = destination.string() + ".receipt.json";
  const auto readBytes = [](const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
  };
  const auto previousMaster = readBytes(destination);
  const auto previousReceipt = readBytes(receipt);
  const auto receiptBackup = receipt + ".previous";
  CHECK(std::filesystem::create_directories(receiptBackup));
  std::ofstream{std::filesystem::path{receiptBackup} / "keep.txt"}
      << "keep collision";

  const auto replacement = service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm24);
  CHECK(!replacement);
  CHECK(readBytes(destination) == previousMaster);
  CHECK(readBytes(receipt) == previousReceipt);
  CHECK(!std::filesystem::exists(destination.string() + ".previous"));
  CHECK(std::filesystem::exists(std::filesystem::path{receiptBackup} / "keep.txt"));
}

TEST_CASE("export service cancellation leaves no claimed output") {
  const auto root = seam::test::support::temporaryDirectory("export-cancel");
  const auto destination = root / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.0F, 0.1F, 0.2F},
  };
  std::stop_source source;
  source.request_stop();
  seam::authoring::ExportService service;
  const auto result = service.commitRendered(
      rendered, 1U, destination, seam::voicebank::WavSampleFormat::Pcm16,
      source.get_token());
  CHECK(result);
  CHECK(result.value().state == seam::authoring::ExportState::Cancelled);
  CHECK(!std::filesystem::exists(destination));
}

TEST_CASE("export service publishes master and stems while preserving canaries") {
  const auto root = seam::test::support::temporaryDirectory("export-set");
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.05)));
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);

  seam::application::ProjectFactory factory{20000U};
  auto project = factory.createProject("Export set");
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = seam::domain::TrackId{20001U},
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
  CHECK(project.validate());

  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  const auto first = service.exportSet(
      project, {}, {}, {}, 7U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = false,
      });
  CHECK(first);
  CHECK(first.value().state == seam::authoring::ExportState::Committed);
  CHECK(first.value().files.size() == 2U);
  CHECK(std::filesystem::exists(destination / "master.wav"));
  CHECK(std::filesystem::exists(destination / "receipt.json"));

  const auto canary = destination / "keep.txt";
  std::ofstream{canary} << "keep";
  const auto second = service.exportSet(
      project, {}, {}, {}, 8U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = true,
      });
  CHECK(second);
  CHECK(second.value().state == seam::authoring::ExportState::Committed);
  CHECK(std::filesystem::exists(canary));
  CHECK(second.value().projectRevision == 8U);
}

TEST_CASE("export set failure restoring preserved files rolls back the new set") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-preserved-file-rollback");
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.05)));
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);

  seam::application::ProjectFactory factory{20000U};
  auto project = factory.createProject("Export rollback");
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
  CHECK(project.validate());

  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  CHECK(service.exportSet(
      project, {}, {}, {}, 7U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = false,
      }));

  std::ofstream{destination / "keep.txt"} << "keep this too";
  CHECK(std::filesystem::remove(destination / "receipt.json"));
  CHECK(std::filesystem::create_directories(destination / "receipt.json"));
  std::ofstream{destination / "receipt.json" / "keep.txt"} << "keep";

  const auto replacement = service.exportSet(
      project, {}, {}, {}, 8U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true,
          .includeStems = true,
          .replaceExisting = true,
      });
  CHECK(!replacement);
  CHECK(std::filesystem::exists(destination / "master.wav"));
  CHECK(std::filesystem::exists(destination / "keep.txt"));
  CHECK(std::filesystem::exists(destination / "receipt.json" / "keep.txt"));
}

TEST_CASE("export replacement removes files owned only by the previous receipt") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-removed-stem");
  const auto project = exportProject(root, 24000U);
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  auto firstSettings = exportSettings(true);
  firstSettings.replaceExisting = false;
  const auto first = service.exportSet(
      project, {}, {}, {}, 1U, destination, firstSettings);
  CHECK(first);
  CHECK(first.value().files.size() == 2U);
  const auto stem = first.value().files.back().path;
  CHECK(std::filesystem::exists(stem));
  std::ofstream{destination / "keep.txt"} << "unowned-canary";

  const auto second = service.exportSet(
      project, {}, {}, {}, 2U, destination, exportSettings(false));
  CHECK(second);
  CHECK(!std::filesystem::exists(stem));
  CHECK(std::filesystem::exists(destination / "keep.txt"));
}

TEST_CASE("export replacement never removes an unowned previous sibling") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-unowned-previous");
  const auto project = exportProject(root, 25000U);
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  auto firstSettings = exportSettings(true);
  firstSettings.replaceExisting = false;
  CHECK(service.exportSet(project, {}, {}, {}, 1U, destination, firstSettings));
  const auto unowned = std::filesystem::path{destination.string() + ".previous"};
  CHECK(std::filesystem::create_directories(unowned));
  std::ofstream{unowned / "keep.txt"} << "not export-owned";

  CHECK(service.exportSet(
      project, {}, {}, {}, 2U, destination, exportSettings(true)));
  CHECK(std::filesystem::exists(unowned / "keep.txt"));
}

TEST_CASE("export recovery reconciles every journalled publication phase") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-journal-recovery");
  const auto project = exportProject(root, 26000U);
  seam::authoring::ExportService service;
  const auto phases = std::array{
      seam::authoring::ExportPublicationPhase::JournalPrepared,
      seam::authoring::ExportPublicationPhase::PreviousMoved,
      seam::authoring::ExportPublicationPhase::DestinationPublished,
      seam::authoring::ExportPublicationPhase::ReceiptCommitted,
      seam::authoring::ExportPublicationPhase::BackupRemoved,
  };
  for (std::size_t index = 0U; index < phases.size(); ++index) {
    const auto destination = root / "exports" / ("song-" + std::to_string(index));
    auto firstSettings = exportSettings(true);
    firstSettings.replaceExisting = false;
    CHECK(service.exportSet(
        project, {}, {}, {}, 1U, destination, firstSettings));
    std::ofstream{destination / "keep.txt"} << "unowned-canary";
    auto interruptedSettings = exportSettings(false);
    interruptedSettings.publicationFaultInjector =
        [phase = phases[index]](seam::authoring::ExportPublicationPhase current) {
          return current == phase;
        };
    const auto interrupted = service.exportSet(
        project, {}, {}, {}, 2U, destination, interruptedSettings);
    CHECK(interrupted);
    CHECK(interrupted.value().state ==
          seam::authoring::ExportState::RollbackRequired);

    const auto recovered = service.recoverSet(destination);
    CHECK(recovered);
    CHECK(std::filesystem::exists(destination / "master.wav"));
    CHECK(std::filesystem::exists(destination / "receipt.json"));
    CHECK(std::filesystem::exists(destination / "keep.txt"));
    const auto expectedRevision = index < 3U ? 1 : 2;
    CHECK(committedRevision(destination) == expectedRevision);
    CHECK(std::filesystem::exists(destination / "stems") ==
          (expectedRevision == 1));
    for (const auto& entry : std::filesystem::directory_iterator{
             destination.parent_path()}) {
      const auto name = entry.path().filename().string();
      CHECK(name.find(destination.filename().string() + "-export-") ==
            std::string::npos);
    }
  }
}

TEST_CASE("export rejects symlinked destination files") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-symlink-preserved-file");
  const auto destination = root / "exports" / "master.wav";
  seam::rendering::ProjectRenderResult rendered{
      .sampleRate = 48000U,
      .channelCount = 1U,
      .interleaved = {0.1F, 0.2F, 0.3F},
  };
  seam::authoring::ExportService service;
  const auto outside = root / "outside.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      outside, 48000U, std::vector<float>{0.2F, 0.3F}));
  std::error_code error;
  std::filesystem::create_symlink(outside, destination, error);
  if (error) return;

  const auto replacement = service.commitRendered(
      rendered, 2U, destination, seam::voicebank::WavSampleFormat::Pcm24);
  CHECK(!replacement);
  CHECK(replacement.error().code == seam::core::ErrorCode::Conflict);
  CHECK(std::filesystem::is_symlink(destination));
  CHECK(std::filesystem::exists(outside));
}

TEST_CASE("export recovery rejects symlinked master files") {
  const auto root = seam::test::support::temporaryDirectory(
      "export-recovery-symlink");
  const auto destination = root / "exports" / "song";
  seam::authoring::ExportService service;
  CHECK(std::filesystem::create_directories(destination));
  const auto outside = root / "outside.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      outside, 48000U, std::vector<float>{0.2F, 0.3F}));
  const auto master = destination / "master.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      master, 48000U, std::vector<float>{0.1F, 0.2F}));
  std::error_code error;
  std::filesystem::remove(master, error);
  CHECK(!error);
  std::filesystem::create_symlink(outside, master, error);
  if (error) return;

  const auto recovered = service.recoverSet(destination);
  CHECK(!recovered);
  CHECK(recovered.error().code == seam::core::ErrorCode::Conflict);
  CHECK(std::filesystem::is_symlink(master));
}

TEST_CASE("export receipt records canonical project and render identities") {
  seam::application::ProjectFactory factory{21000U};
  auto project = factory.createProject("Receipt identity");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  auto root = seam::test::support::temporaryDirectory("export-receipt-identity");
  project.findVocalTrack(trackId)->muted = true;
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.02)));
  auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);
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
  });
  CHECK(project.validate());
  const auto destination = root / "set";
  seam::authoring::ExportService service;
  const auto result = service.exportSet(
      project, {}, {}, {}, 9U, destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Float32,
          .includeMaster = true,
          .includeStems = false,
          .replaceExisting = false,
      });
  CHECK(result);
  CHECK(result.value().state == seam::authoring::ExportState::Committed);
  std::ifstream receipt(destination / "receipt.json");
  const std::string text{std::istreambuf_iterator<char>{receipt},
                          std::istreambuf_iterator<char>{}};
  CHECK(text.find("\"projectId\": \"" + project.id().toString()) !=
        std::string::npos);
  CHECK(text.find("\"projectSchema\": 6") != std::string::npos);
  CHECK(text.find("\"renderQuality\": \"Final\"") != std::string::npos);
  CHECK(text.find("\"renderAbi\": \"") != std::string::npos);
  CHECK(text.find("\"applicationBuildSha\": \"") != std::string::npos);
  CHECK(text.find("\"executionDateUnixMs\": ") != std::string::npos);
}

TEST_CASE("export writer covers the production format matrix") {
  const auto root = seam::test::support::temporaryDirectory("export-matrix");
  seam::authoring::ExportService service;
  for (const auto sampleRate : {44100U, 48000U, 96000U}) {
    for (const auto channels : {std::uint8_t{1U}, std::uint8_t{2U},
                                std::uint8_t{4U}, std::uint8_t{8U}}) {
      for (const auto format : {seam::voicebank::WavSampleFormat::Pcm16,
                                seam::voicebank::WavSampleFormat::Pcm24,
                                seam::voicebank::WavSampleFormat::Float32}) {
        seam::rendering::ProjectRenderResult rendered{
            .sampleRate = sampleRate,
            .channelCount = channels,
            .interleaved = std::vector<float>(
                static_cast<std::size_t>(channels) * 8U, 0.125F),
        };
        const auto destination = root /
            (std::to_string(sampleRate) + "-" +
             std::to_string(static_cast<unsigned>(channels)) + "-" +
             std::to_string(static_cast<int>(format)) + ".wav");
        const auto result = service.commitRendered(
            rendered, static_cast<std::uint64_t>(sampleRate) + channels,
            destination, format);
        CHECK(result);
        CHECK(result.value().state == seam::authoring::ExportState::Committed);
        const auto decoded = seam::voicebank::readWav(destination);
        CHECK(decoded);
        CHECK(decoded.value().sampleRate == sampleRate);
        CHECK(decoded.value().channels == channels);
        CHECK(decoded.value().frameCount() == 8U);
      }
    }
  }
}
