#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/export_service.hpp"

#include <filesystem>

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
