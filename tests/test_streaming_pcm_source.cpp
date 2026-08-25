#include "test_framework.hpp"

#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <filesystem>

TEST_CASE("streaming PCM source exposes hashed bounded chunks") {
  const auto root = std::filesystem::temp_directory_path() / "seam-streaming-pcm-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);
  const auto path = root / "source.wav";
  const std::array<float, 12> samples{0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F,
                                      0.6F, 0.7F, 0.8F, 0.9F, 1.0F, 0.0F};
  CHECK(seam::voicebank::writePcm16Wav(path, 48000U, 2U, samples));
  const auto source = seam::rendering::StreamingPcmSource::open(path, 2U);
  CHECK(source);
  CHECK(source.value()->info().contentHash.size() == 64U);
  CHECK(source.value()->chunkCount() == 3U);
  CHECK(source.value()->readChunk(0U));
  CHECK(source.value()->readChunk(0U).value().size() == 4U);
  CHECK_NEAR(source.value()->readChunk(0U).value()[0], 0.0F, 0.001F);
  CHECK_NEAR(source.value()->readChunk(0U).value()[3], 0.3F, 0.01F);
  CHECK_NEAR(source.value()->readChunk(2U).value()[0], 0.8F, 0.01F);
  CHECK(!source.value()->readChunk(3U));
  std::filesystem::remove_all(root, error);
}
