#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <limits>

TEST_CASE("streaming WAV writer round trips PCM16 PCM24 and Float32") {
  const auto root = seam::test::support::temporaryDirectory("wav-formats");
  const std::vector formats{seam::voicebank::WavSampleFormat::Pcm16,
                            seam::voicebank::WavSampleFormat::Pcm24,
                            seam::voicebank::WavSampleFormat::Float32};
  for (const auto format : formats) {
    const auto path = root / (std::to_string(static_cast<int>(format)) + ".wav");
    auto writer = seam::voicebank::WavStreamWriter::create(
        path, seam::voicebank::WavOutputFormat{48000U, 2U, format});
    CHECK(writer);
    const std::vector<float> frames{-1.0F, -0.25F, 0.0F, 0.5F, 1.0F, 0.25F};
    CHECK(writer.value()->writeFrames(frames));
    CHECK(writer.value()->framesWritten() == 3U);
    CHECK(writer.value()->finalize());
    const auto decoded = seam::voicebank::readWav(path);
    CHECK(decoded);
    CHECK(decoded.value().sampleRate == 48000U);
    CHECK(decoded.value().channels == 2U);
    CHECK(decoded.value().frameCount() == 3U);
  }
}

TEST_CASE("streaming WAV writer rejects non-finite Float32 and invalid formats") {
  const auto root = seam::test::support::temporaryDirectory("wav-invalid");
  auto writer = seam::voicebank::WavStreamWriter::create(
      root / "float.wav",
      seam::voicebank::WavOutputFormat{48000U, 1U,
                                      seam::voicebank::WavSampleFormat::Float32});
  CHECK(writer);
  const std::vector<float> invalid{std::numeric_limits<float>::quiet_NaN()};
  CHECK(!writer.value()->writeFrames(invalid));
  CHECK(!seam::voicebank::WavStreamWriter::create(
      root / "invalid.wav",
      seam::voicebank::WavOutputFormat{48000U, 0U,
                                      seam::voicebank::WavSampleFormat::Pcm16}));
}
