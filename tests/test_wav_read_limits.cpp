#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/voicebank/wav.hpp"

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>

namespace {
namespace voicebank = seam::voicebank;
void u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
  for (unsigned shift = 0U; shift < 2U; ++shift)
    bytes[offset + shift] = static_cast<std::byte>((static_cast<std::uint32_t>(value) >> (shift * 8U)) & 0xffU);
}
void u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 4U; ++shift)
    bytes[offset + shift] = static_cast<std::byte>((value >> (shift * 8U)) & 0xffU);
}
void tag(std::vector<std::byte>& bytes, std::size_t offset, std::string_view value) {
  for (std::size_t index = 0U; index < value.size(); ++index)
    bytes[offset + index] = static_cast<std::byte>(static_cast<unsigned char>(value[index]));
}
std::vector<std::byte> pcm8(std::size_t frames, std::uint16_t channels = 1U) {
  const auto count = frames * channels;
  std::vector<std::byte> bytes(44U + count + (count & 1U), std::byte{128U});
  tag(bytes, 0U, "RIFF"); u32(bytes, 4U, static_cast<std::uint32_t>(bytes.size() - 8U));
  tag(bytes, 8U, "WAVE"); tag(bytes, 12U, "fmt "); u32(bytes, 16U, 16U);
  u16(bytes, 20U, 1U); u16(bytes, 22U, channels); u32(bytes, 24U, 48000U);
  u32(bytes, 28U, 48000U * channels); u16(bytes, 32U, channels); u16(bytes, 34U, 8U);
  tag(bytes, 36U, "data"); u32(bytes, 40U, static_cast<std::uint32_t>(count));
  return bytes;
}
void callerLimit(const seam::core::Result<voicebank::AudioBuffer>& result) {
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(result.error().message.find("before PCM allocation") != std::string::npos);
}
}  // namespace

TEST_CASE("bounded WAV decoder checks PCM8 expansion frames channels and samples before allocation") {
  const auto largePcm8 = pcm8(1024U * 1024U + 1U);
  callerLimit(voicebank::readWav(largePcm8, "oversized PCM8",
      {.maximumFrames = 1024U * 1024U, .maximumChannels = 1U, .maximumDecodedSamples = 1024U * 1024U}));
  const auto stereo = pcm8(16U, 2U);
  callerLimit(voicebank::readWav(stereo, "channels", {.maximumFrames = 16U, .maximumChannels = 1U, .maximumDecodedSamples = 32U}));
  callerLimit(voicebank::readWav(stereo, "frames", {.maximumFrames = 15U, .maximumChannels = 2U, .maximumDecodedSamples = 32U}));
  callerLimit(voicebank::readWav(stereo, "samples", {.maximumFrames = 16U, .maximumChannels = 2U, .maximumDecodedSamples = 31U}));
  const auto exact = voicebank::readWav(stereo, "exact bounds", {.maximumFrames = 16U, .maximumChannels = 2U, .maximumDecodedSamples = 32U});
  CHECK(exact); CHECK(exact.value().channels == 2U); CHECK(exact.value().frameCount() == 16U);
  CHECK(exact.value().interleaved.size() == 32U);
  CHECK(std::all_of(exact.value().interleaved.begin(), exact.value().interleaved.end(), [](float value) { return value == 0.0F; }));
}

TEST_CASE("bounded WAV decoder rejects hostile data lengths alignment and zero budgets") {
  for (unsigned scenario = 0U; scenario < 5U; ++scenario) {
    auto bytes = pcm8(16U, 2U);
    if (scenario == 0U) u32(bytes, 40U, std::numeric_limits<std::uint32_t>::max());
    if (scenario == 1U) u32(bytes, 16U, std::numeric_limits<std::uint32_t>::max());
    if (scenario == 2U) bytes.pop_back();
    if (scenario == 3U) u16(bytes, 32U, 0U);
    if (scenario == 4U) u32(bytes, 40U, 31U);
    const auto result = voicebank::readWav(bytes, "hostile metadata",
        {.maximumFrames = std::numeric_limits<std::uint64_t>::max(), .maximumChannels = 8U,
         .maximumDecodedSamples = std::numeric_limits<std::uint64_t>::max()});
    CHECK(!result); CHECK(result.error().code == seam::core::ErrorCode::ParseError);
  }
  const auto bytes = pcm8(16U);
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    voicebank::WavReadLimits limits;
    if (scenario == 0U) limits.maximumFrames = 0U;
    if (scenario == 1U) limits.maximumChannels = 0U;
    if (scenario == 2U) limits.maximumDecodedSamples = 0U;
    const auto result = voicebank::readWav(bytes, "invalid limits", limits);
    CHECK(!result); CHECK(result.error().code == seam::core::ErrorCode::InvalidArgument);
  }
}

TEST_CASE("bounded and legacy WAV overloads produce identical legitimate PCM and path results") {
  const auto root = seam::test::support::temporaryDirectory("wav-read-limits");
  const auto samples = seam::test::support::sineWave(48000U, 440.0, 0.02, 0.25F);
  for (const auto format : {voicebank::WavSampleFormat::Pcm16, voicebank::WavSampleFormat::Pcm24, voicebank::WavSampleFormat::Float32}) {
    const auto path = root / (std::to_string(static_cast<int>(format)) + ".wav");
    CHECK(voicebank::writeWav(path, {.sampleRate = 48000U, .channels = 1U, .sampleFormat = format}, samples));
    const auto bytes = seam::core::readFileBytesLimited(path, 1024U * 1024U); CHECK(bytes);
    const voicebank::WavReadLimits limits{.maximumFrames = samples.size(), .maximumChannels = 1U, .maximumDecodedSamples = samples.size()};
    const auto oldPath = voicebank::readWav(path), oldMemory = voicebank::readWav(bytes.value(), "legacy");
    const auto boundedPath = voicebank::readWav(path, limits), boundedMemory = voicebank::readWav(bytes.value(), "bounded", limits);
    CHECK(oldPath); CHECK(oldMemory); CHECK(boundedPath); CHECK(boundedMemory);
    CHECK(boundedPath.value().interleaved == oldPath.value().interleaved);
    CHECK(boundedMemory.value().interleaved == oldMemory.value().interleaved);
    CHECK(oldPath.value().interleaved == oldMemory.value().interleaved);
    CHECK(boundedMemory.value().sampleRate == 48000U);
    CHECK(boundedMemory.value().bitsPerSample == oldMemory.value().bitsPerSample);
  }
}

TEST_CASE("WAV cancellation takes precedence before parsing or file access") {
  std::stop_source cancellation;
  cancellation.request_stop();
  const auto memory = voicebank::readWav(std::span<const std::byte>{}, "cancelled before parse", {}, cancellation.get_token());
  CHECK(!memory); CHECK(memory.error().code == seam::core::ErrorCode::Conflict);
  const auto path = voicebank::readWav(std::filesystem::path{}, voicebank::WavReadLimits{}, cancellation.get_token());
  CHECK(!path); CHECK(path.error().code == seam::core::ErrorCode::Conflict);
  CHECK(memory.error().message.find("cancelled") != std::string::npos);
}

TEST_CASE("concurrent cancellation of active WAV decoding returns no partial PCM") {
  // The encoded input is prepared before starting the operation. Stop is
  // requested during an active call; no timing-derived performance PASS is
  // asserted. Chunk/sample loops also explicitly poll the same stop token.
  const auto bytes = pcm8(32U * 1024U * 1024U);
  std::stop_source cancellation;
  std::atomic<bool> entered{false};
  std::jthread canceller{[&] {
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
    cancellation.request_stop();
  }};
  entered.store(true, std::memory_order_release);
  const auto result = voicebank::readWav(bytes, "active cancellation", {}, cancellation.get_token());
  canceller.join();
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Conflict);
  CHECK(result.error().message.find("cancelled") != std::string::npos);
}
