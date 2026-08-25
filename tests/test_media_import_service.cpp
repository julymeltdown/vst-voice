#include "test_framework.hpp"

#include "seam/authoring/media_import_service.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <chrono>
#include <filesystem>

namespace {

std::filesystem::path testRoot() {
  return std::filesystem::temp_directory_path() /
         ("seam-media-import-" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

}

TEST_CASE("media import records content identity for reference and project copy") {
  const auto root = testRoot();
  std::filesystem::create_directories(root);
  const auto source = root / "backing.wav";
  const std::array<float, 8> samples{0.0F, 0.25F, -0.25F, 0.5F,
                                     -0.5F, 0.1F, -0.1F, 0.0F};
  CHECK(seam::voicebank::writePcm16Wav(source, 48000U, 2U, samples));

  const seam::domain::TrackId trackId{77U};
  const auto referenced = seam::authoring::MediaImportService::import(
      seam::authoring::MediaImportRequest{
          .trackId = trackId,
          .trackName = "Backing",
          .sourcePath = source,
          .projectPath = root / "song.seam",
          .startTick = seam::time::Tick{960},
          .mode = seam::authoring::MediaImportMode::Reference,
      });
  CHECK(referenced);
  CHECK(referenced.value().track.mediaOwnership ==
        seam::domain::MediaOwnership::ExternalReference);
  CHECK(!referenced.value().track.mediaHash.empty());
  CHECK(referenced.value().track.sourceSampleRate == 48000U);
  CHECK(referenced.value().track.sourceChannels == 2U);
  CHECK(referenced.value().track.sourceFrameCount == 4U);

  const auto copied = seam::authoring::MediaImportService::import(
      seam::authoring::MediaImportRequest{
          .trackId = seam::domain::TrackId{78U},
          .trackName = "Backing Copy",
          .sourcePath = source,
          .projectPath = root / "song.seam",
          .startTick = seam::time::Tick{0},
          .mode = seam::authoring::MediaImportMode::Copy,
      });
  CHECK(copied);
  CHECK(copied.value().track.mediaOwnership ==
        seam::domain::MediaOwnership::ProjectCopy);
  CHECK(!copied.value().ownedPath.empty());
  CHECK(std::filesystem::exists(copied.value().ownedPath));
  CHECK(copied.value().track.mediaPath.find("song.seam.media") !=
        std::string::npos);
  CHECK(copied.value().track.mediaHash == referenced.value().track.mediaHash);
  seam::domain::Project project{seam::domain::ProjectId{80U}, "Media Project"};
  project.audioTracks().push_back(copied.value().track);
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value().audioTracks().front().mediaHash ==
        copied.value().track.mediaHash);
  CHECK(decoded.value().audioTracks().front().mediaOwnership ==
        seam::domain::MediaOwnership::ProjectCopy);
  std::filesystem::remove_all(root);
}

TEST_CASE("media import rejects compressed or missing sources before copying") {
  const auto root = testRoot();
  std::filesystem::create_directories(root);
  const auto request = seam::authoring::MediaImportRequest{
      .trackId = seam::domain::TrackId{79U},
      .sourcePath = root / "missing.mp3",
      .projectPath = root / "song.seam",
      .mode = seam::authoring::MediaImportMode::Copy,
  };
  const auto result = seam::authoring::MediaImportService::import(request);
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(!std::filesystem::exists(root / "song.seam.media"));
  std::filesystem::remove_all(root);
}
