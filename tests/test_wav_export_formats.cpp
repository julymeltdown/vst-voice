#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/voicebank/wav.hpp"

#include <array>
#include <filesystem>
#include <fstream>
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

TEST_CASE("WAV reader rejects non-finite Float32 payloads") {
  const auto root = seam::test::support::temporaryDirectory("wav-nonfinite");
  const auto path = root / "nonfinite.wav";
  auto writer = seam::voicebank::WavStreamWriter::create(
      path, seam::voicebank::WavOutputFormat{48000U, 1U,
                                             seam::voicebank::WavSampleFormat::Float32});
  CHECK(writer);
  CHECK(writer.value()->writeFrames(std::vector<float>{0.25F}));
  CHECK(writer.value()->finalize());

  std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
  CHECK(stream);
  stream.seekp(44, std::ios::beg);
  const std::array<char, 4> nanBytes{
      static_cast<char>(0x00), static_cast<char>(0x00),
      static_cast<char>(0xc0), static_cast<char>(0x7f)};
  stream.write(nanBytes.data(), static_cast<std::streamsize>(nanBytes.size()));
  stream.close();
  CHECK(!seam::voicebank::readWav(path));
}

TEST_CASE("WAV path I/O rejects symlink and non-file targets") {
  const auto root = seam::test::support::temporaryDirectory("wav-paths");
  const auto outside = root / "outside.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      outside, 48000U, std::vector<float>{0.25F, -0.25F}));
  const auto inputLink = root / "input-link.wav";
  std::error_code error;
  std::filesystem::create_symlink(outside, inputLink, error);
  CHECK(!error);
  CHECK(!seam::voicebank::readWav(inputLink));
  CHECK(seam::voicebank::readWav(outside));

  const auto outputLink = root / "output-link.wav";
  std::filesystem::create_symlink(outside, outputLink, error);
  CHECK(!error);
  CHECK(!seam::voicebank::WavStreamWriter::create(
      outputLink, seam::voicebank::WavOutputFormat{48000U, 1U,
                                                   seam::voicebank::WavSampleFormat::Pcm16}));
  CHECK(seam::voicebank::readWav(outside));

  const auto directory = root / "wav-directory";
  std::filesystem::create_directories(directory);
  CHECK(!seam::voicebank::readWav(directory));
  CHECK(!seam::voicebank::WavStreamWriter::create(
      directory, seam::voicebank::WavOutputFormat{48000U, 1U,
                                                  seam::voicebank::WavSampleFormat::Pcm16}));
}
