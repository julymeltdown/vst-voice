#include "seam/voicebank/wav.hpp"

#include "seam/core/file_io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace seam::voicebank {
namespace {

std::uint16_t readU16(const std::byte* data) noexcept {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(data[0]) |
      (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[1])) << 8U));
}

std::uint32_t readU32(const std::byte* data) noexcept {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 8U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[3])) << 24U);
}

std::int32_t readI24(const std::byte* data) noexcept {
  std::uint32_t value = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) |
                        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 8U) |
                        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 16U);
  if ((value & 0x00800000U) != 0U) {
    value |= 0xFF000000U;
  }
  return static_cast<std::int32_t>(value);
}

void writeU16(std::ostream& stream, std::uint16_t value) {
  const std::array<char, 2> bytes{
      static_cast<char>(value & 0xFFU),
      static_cast<char>((value >> 8U) & 0xFFU),
  };
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ostream& stream, std::uint32_t value) {
  const std::array<char, 4> bytes{
      static_cast<char>(value & 0xFFU),
      static_cast<char>((value >> 8U) & 0xFFU),
      static_cast<char>((value >> 16U) & 0xFFU),
      static_cast<char>((value >> 24U) & 0xFFU),
  };
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool fourcc(const std::byte* data, std::string_view value) noexcept {
  return value.size() == 4 &&
         std::to_integer<char>(data[0]) == value[0] &&
         std::to_integer<char>(data[1]) == value[1] &&
         std::to_integer<char>(data[2]) == value[2] &&
         std::to_integer<char>(data[3]) == value[3];
}

}  // namespace

std::vector<float> AudioBuffer::monoMix() const {
  if (channels == 0 || interleaved.empty()) {
    return {};
  }
  std::vector<float> result(frameCount(), 0.0F);
  const auto channelCount = static_cast<std::size_t>(channels);
  for (std::size_t frame = 0; frame < result.size(); ++frame) {
    double sum = 0.0;
    for (std::size_t channel = 0; channel < channelCount; ++channel) {
      sum += interleaved[frame * channelCount + channel];
    }
    result[frame] = static_cast<float>(sum / static_cast<double>(channelCount));
  }
  return result;
}

core::Result<AudioBuffer> readWav(std::span<const std::byte> bytes,
                                  std::string_view sourceLabel) {
  const auto source = sourceLabel.empty() ? std::string{"<memory>"}
                                          : std::string{sourceLabel};
  if (bytes.size() < 44U || bytes.size() > kMaximumSupportedWavBytes) {
    return core::failure<AudioBuffer>(core::ErrorCode::Unsupported,
                                      "WAV payload size is outside supported limits",
                                      source);
  }
  if (!fourcc(bytes.data(), "RIFF") || !fourcc(bytes.data() + 8, "WAVE")) {
    return core::failure<AudioBuffer>(core::ErrorCode::ParseError,
                                      "File is not a RIFF/WAVE container",
                                      source);
  }

  std::uint16_t formatCode = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t blockAlign = 0;
  std::uint16_t bitsPerSample = 0;
  const std::byte* audioData = nullptr;
  std::size_t audioBytes = 0;

  std::size_t position = 12U;
  while (position + 8U <= bytes.size()) {
    const auto* chunk = bytes.data() + position;
    const auto chunkSize = static_cast<std::size_t>(readU32(chunk + 4));
    position += 8U;
    if (chunkSize > bytes.size() - position) {
      return core::failure<AudioBuffer>(core::ErrorCode::ParseError,
                                        "WAV chunk exceeds file bounds",
                                        source);
    }
    if (fourcc(chunk, "fmt ")) {
      if (chunkSize < 16U) {
        return core::failure<AudioBuffer>(core::ErrorCode::ParseError,
                                          "WAV fmt chunk is truncated",
                                          source);
      }
      const auto* fmt = bytes.data() + position;
      formatCode = readU16(fmt);
      channels = readU16(fmt + 2);
      sampleRate = readU32(fmt + 4);
      blockAlign = readU16(fmt + 12);
      bitsPerSample = readU16(fmt + 14);
      if (formatCode == 0xFFFEU && chunkSize >= 40U) {
        formatCode = readU16(fmt + 24);
      }
    } else if (fourcc(chunk, "data")) {
      audioData = bytes.data() + position;
      audioBytes = chunkSize;
    }
    const auto padded = chunkSize + (chunkSize & 1U);
    if (padded > bytes.size() - position) {
      return core::failure<AudioBuffer>(core::ErrorCode::ParseError,
                                        "WAV padded chunk exceeds file bounds",
                                        source);
    }
    position += padded;
  }

  if (audioData == nullptr || channels == 0U || channels > 8U ||
      sampleRate < 8000U || sampleRate > 384000U || blockAlign == 0U ||
      audioBytes < blockAlign) {
    return core::failure<AudioBuffer>(core::ErrorCode::ParseError,
                                      "WAV format or data chunk is invalid",
                                      source);
  }
  const bool integerPcm = formatCode == 1U;
  const bool floatPcm = formatCode == 3U;
  if ((!integerPcm && !floatPcm) ||
      (integerPcm && bitsPerSample != 8U && bitsPerSample != 16U &&
       bitsPerSample != 24U && bitsPerSample != 32U) ||
      (floatPcm && bitsPerSample != 32U)) {
    return core::failure<AudioBuffer>(core::ErrorCode::Unsupported,
                                      "WAV encoding is not supported",
                                      source);
  }
  const auto bytesPerSample = static_cast<std::size_t>(bitsPerSample / 8U);
  const auto expectedAlign = bytesPerSample * static_cast<std::size_t>(channels);
  if (expectedAlign != blockAlign || audioBytes % blockAlign != 0U) {
    return core::failure<AudioBuffer>(core::ErrorCode::ParseError,
                                      "WAV block alignment is inconsistent",
                                      source);
  }

  const auto frameCount = audioBytes / blockAlign;
  if (frameCount > std::numeric_limits<std::size_t>::max() /
                       static_cast<std::size_t>(channels)) {
    return core::failure<AudioBuffer>(core::ErrorCode::Unsupported,
                                      "WAV sample count is too large",
                                      source);
  }
  AudioBuffer result;
  result.sampleRate = sampleRate;
  result.channels = channels;
  result.interleaved.resize(frameCount * static_cast<std::size_t>(channels));

  const auto sampleCount = result.interleaved.size();
  for (std::size_t index = 0U; index < sampleCount; ++index) {
    const auto* sample = audioData + index * bytesPerSample;
    float value = 0.0F;
    if (floatPcm) {
      const std::uint32_t raw = readU32(sample);
      std::memcpy(&value, &raw, sizeof(value));
      if (!std::isfinite(value)) value = 0.0F;
    } else if (bitsPerSample == 8U) {
      value = (static_cast<float>(std::to_integer<std::uint8_t>(*sample)) - 128.0F) /
              128.0F;
    } else if (bitsPerSample == 16U) {
      const auto raw = static_cast<std::int16_t>(readU16(sample));
      value = static_cast<float>(raw) / 32768.0F;
    } else if (bitsPerSample == 24U) {
      value = static_cast<float>(readI24(sample)) / 8388608.0F;
    } else {
      const auto raw = static_cast<std::int32_t>(readU32(sample));
      value = static_cast<float>(static_cast<double>(raw) / 2147483648.0);
    }
    result.interleaved[index] = value;
  }
  return result;
}

core::Result<AudioBuffer> readWav(const std::filesystem::path& path) {
  auto bytes = core::readFileBytesLimited(path, kMaximumSupportedWavBytes);
  if (!bytes) return core::Result<AudioBuffer>{bytes.error()};
  return readWav(bytes.value(), path.string());
}

core::Result<void> writePcm16Wav(const std::filesystem::path& path,
                                 std::uint32_t sampleRate,
                                 std::uint16_t channels,
                                 std::span<const float> interleaved) {
  if (sampleRate < 8000 || sampleRate > 384000 || channels == 0 || channels > 8 ||
      interleaved.empty() || interleaved.size() % channels != 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "WAV output format is invalid");
  }
  const auto channelCount = static_cast<std::uint32_t>(channels);
  if (sampleRate > std::numeric_limits<std::uint32_t>::max() /
                       (channelCount * 2U) ||
      interleaved.size() > (std::numeric_limits<std::uint32_t>::max() - 36U) / 2U) {
    return core::failure(core::ErrorCode::Unsupported,
                         "WAV output is too large for RIFF");
  }
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create WAV output directory",
                           error.message());
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to create WAV output",
                         path.string());
  }

  const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * 2U);
  const auto blockAlign = static_cast<std::uint16_t>(channels * 2U);
  stream.write("RIFF", 4);
  writeU32(stream, 36U + dataBytes);
  stream.write("WAVE", 4);
  stream.write("fmt ", 4);
  writeU32(stream, 16);
  writeU16(stream, 1);
  writeU16(stream, channels);
  writeU32(stream, sampleRate);
  writeU32(stream, sampleRate * channelCount * 2U);
  writeU16(stream, blockAlign);
  writeU16(stream, 16);
  stream.write("data", 4);
  writeU32(stream, dataBytes);
  for (const auto sample : interleaved) {
    const auto finite = std::isfinite(sample) ? sample : 0.0F;
    const auto clamped = std::clamp(finite, -1.0F, 1.0F);
    const auto scaled = static_cast<std::int32_t>(std::lround(
        static_cast<double>(clamped) * (clamped < 0.0F ? 32768.0 : 32767.0)));
    writeU16(stream, static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
  }
  stream.flush();
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to write WAV output",
                         path.string());
  }
  return core::success();
}

core::Result<void> writeMonoPcm16Wav(const std::filesystem::path& path,
                                     std::uint32_t sampleRate,
                                     std::span<const float> samples) {
  if (samples.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Mono WAV output cannot be empty");
  }
  return writePcm16Wav(path, sampleRate, 1U, samples);
}

AudioStatistics analyzeAudio(std::span<const float> samples) noexcept {
  AudioStatistics result;
  if (samples.empty()) return result;
  long double squareSum = 0.0L;
  long double sum = 0.0L;
  for (const auto sample : samples) {
    const auto finite = std::isfinite(sample) ? sample : 0.0F;
    result.peak = std::max(result.peak, std::abs(finite));
    squareSum += static_cast<long double>(finite) * static_cast<long double>(finite);
    sum += finite;
    if (std::abs(finite) >= 0.9999F) ++result.clippedSamples;
  }
  result.rms = std::sqrt(static_cast<double>(squareSum / samples.size()));
  result.dcOffset = static_cast<double>(sum / samples.size());
  return result;
}

}  // namespace seam::voicebank
