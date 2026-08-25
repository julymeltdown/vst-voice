#include "seam/rendering/streaming_pcm_source.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace seam::rendering {
namespace {

std::uint16_t readU16(const std::array<std::byte, 40>& bytes,
                      std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U]))
       << 8U));
}

std::uint32_t readU32(const std::array<std::byte, 40>& bytes,
                      std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U]))
          << 8U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U]))
          << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U]))
          << 24U);
}

bool fourcc(const std::array<std::byte, 4>& bytes, std::string_view value) noexcept {
  return value.size() == 4U && std::to_integer<char>(bytes[0]) == value[0] &&
         std::to_integer<char>(bytes[1]) == value[1] &&
         std::to_integer<char>(bytes[2]) == value[2] &&
         std::to_integer<char>(bytes[3]) == value[3];
}

std::int32_t readI24(const std::array<std::byte, 4>& bytes) noexcept {
  std::uint32_t value = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
                        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1]))
                         << 8U) |
                        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2]))
                         << 16U);
  if ((value & 0x00800000U) != 0U) value |= 0xFF000000U;
  return static_cast<std::int32_t>(value);
}

core::Result<std::array<std::byte, 40>> readBytes(std::ifstream& stream,
                                                   std::size_t count,
                                                   std::string_view source) {
  std::array<std::byte, 40> result{};
  if (count > result.size()) {
    return core::failure<std::array<std::byte, 40>>(
        core::ErrorCode::Unsupported, "WAV metadata chunk is too large",
        std::string{source});
  }
  stream.read(reinterpret_cast<char*>(result.data()),
              static_cast<std::streamsize>(count));
  if (stream.gcount() != static_cast<std::streamsize>(count)) {
    return core::failure<std::array<std::byte, 40>>(
        core::ErrorCode::ParseError, "WAV metadata chunk is truncated",
        std::string{source});
  }
  return result;
}

}

core::Result<std::unique_ptr<StreamingPcmSource>> StreamingPcmSource::open(
    const std::filesystem::path& path, std::size_t chunkFrames) {
  if (path.empty() || chunkFrames == 0U) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        core::ErrorCode::InvalidArgument,
        "Streaming PCM source requires a path and positive chunk size");
  }
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes < 44U || bytes > voicebank::kMaximumSupportedWavBytes) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        error ? core::ErrorCode::IoError : core::ErrorCode::Unsupported,
        error ? "Unable to inspect streaming PCM source"
              : "Streaming PCM source size is outside supported limits",
        path.string());
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        core::ErrorCode::IoError, "Unable to open streaming PCM source",
        path.string());
  }
  std::array<std::byte, 12> header{};
  stream.read(reinterpret_cast<char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
  const auto fourcc12 = [&header](std::size_t offset, std::string_view value) {
    return value.size() == 4U && std::to_integer<char>(header[offset]) == value[0] &&
           std::to_integer<char>(header[offset + 1U]) == value[1] &&
           std::to_integer<char>(header[offset + 2U]) == value[2] &&
           std::to_integer<char>(header[offset + 3U]) == value[3];
  };
  if (stream.gcount() != static_cast<std::streamsize>(header.size()) ||
      !fourcc12(0U, "RIFF") || !fourcc12(8U, "WAVE")) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        core::ErrorCode::ParseError, "File is not a RIFF/WAVE container",
        path.string());
  }
  std::uint32_t sampleRate = 0U;
  std::uint16_t channels = 0U;
  std::uint16_t blockAlign = 0U;
  std::uint16_t bitsPerSample = 0U;
  std::uint16_t formatCode = 0U;
  std::uint64_t dataOffset = 0U;
  std::uint64_t dataBytes = 0U;
  std::uint64_t position = 12U;
  while (position + 8U <= bytes) {
    stream.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    std::array<std::byte, 8> chunkHeader{};
    stream.read(reinterpret_cast<char*>(chunkHeader.data()),
                static_cast<std::streamsize>(chunkHeader.size()));
    if (stream.gcount() != static_cast<std::streamsize>(chunkHeader.size())) {
      return core::failure<std::unique_ptr<StreamingPcmSource>>(
          core::ErrorCode::ParseError, "WAV chunk header is truncated",
          path.string());
    }
    const auto chunkSize = static_cast<std::uint64_t>(
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(chunkHeader[4])) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(chunkHeader[5])) << 8U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(chunkHeader[6])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(chunkHeader[7])) << 24U));
    const auto padded = chunkSize + (chunkSize & 1U);
    if (padded > bytes - position - 8U) {
      return core::failure<std::unique_ptr<StreamingPcmSource>>(
          core::ErrorCode::ParseError, "WAV chunk exceeds file bounds",
          path.string());
    }
    const std::array<std::byte, 4> id{chunkHeader[0], chunkHeader[1],
                                      chunkHeader[2], chunkHeader[3]};
    if (fourcc(id, "fmt ")) {
      if (chunkSize < 16U) {
        return core::failure<std::unique_ptr<StreamingPcmSource>>(
            core::ErrorCode::ParseError, "WAV fmt chunk is truncated",
            path.string());
      }
      stream.seekg(static_cast<std::streamoff>(position + 8U), std::ios::beg);
      auto fmt = readBytes(stream, static_cast<std::size_t>(std::min<std::uint64_t>(
          chunkSize, 40U)), path.string());
      if (!fmt) return core::Result<std::unique_ptr<StreamingPcmSource>>{fmt.error()};
      formatCode = readU16(fmt.value(), 0U);
      channels = readU16(fmt.value(), 2U);
      sampleRate = readU32(fmt.value(), 4U);
      blockAlign = readU16(fmt.value(), 12U);
      bitsPerSample = readU16(fmt.value(), 14U);
      if (formatCode == 0xFFFEU && chunkSize >= 40U) {
        formatCode = readU16(fmt.value(), 24U);
      }
    } else if (fourcc(id, "data")) {
      dataOffset = position + 8U;
      dataBytes = chunkSize;
    }
    position += 8U + padded;
  }
  if (dataOffset == 0U || channels == 0U || channels > 8U || sampleRate < 8000U ||
      sampleRate > 384000U || blockAlign == 0U || dataBytes < blockAlign) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        core::ErrorCode::ParseError, "WAV format or data chunk is invalid",
        path.string());
  }
  const bool integerPcm = formatCode == 1U;
  const bool floatPcm = formatCode == 3U;
  if ((!integerPcm && !floatPcm) ||
      (integerPcm && bitsPerSample != 8U && bitsPerSample != 16U &&
       bitsPerSample != 24U && bitsPerSample != 32U) ||
      (floatPcm && bitsPerSample != 32U)) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        core::ErrorCode::Unsupported, "WAV encoding is not supported",
        path.string());
  }
  const auto bytesPerSample = static_cast<std::size_t>(bitsPerSample / 8U);
  if (bytesPerSample * static_cast<std::size_t>(channels) != blockAlign ||
      dataBytes % blockAlign != 0U) {
    return core::failure<std::unique_ptr<StreamingPcmSource>>(
        core::ErrorCode::ParseError, "WAV block alignment is inconsistent",
        path.string());
  }
  auto hash = core::sha256File(path, voicebank::kMaximumSupportedWavBytes);
  if (!hash) return core::Result<std::unique_ptr<StreamingPcmSource>>{hash.error()};
  StreamingPcmSourceInfo info{
      .sampleRate = sampleRate,
      .channels = channels,
      .frameCount = dataBytes / blockAlign,
      .byteSize = bytes,
      .contentHash = hash.value(),
  };
  return std::unique_ptr<StreamingPcmSource>{new StreamingPcmSource{
      std::move(info), path, dataOffset, dataBytes, formatCode, bitsPerSample,
      blockAlign, chunkFrames}};
}

std::size_t StreamingPcmSource::chunkCount() const noexcept {
  if (info_.frameCount == 0U) return 0U;
  return static_cast<std::size_t>((info_.frameCount + chunkFrames_ - 1U) /
                                  chunkFrames_);
}

core::Result<std::vector<float>> StreamingPcmSource::readChunk(
    std::size_t chunkIndex) const {
  if (chunkIndex >= chunkCount()) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::NotFound, "Streaming PCM chunk does not exist");
  }
  const auto firstFrame = chunkIndex * chunkFrames_;
  const auto frames = std::min<std::uint64_t>(
      chunkFrames_, info_.frameCount - firstFrame);
  const auto sampleCount = frames * static_cast<std::uint64_t>(info_.channels);
  if (sampleCount > std::numeric_limits<std::size_t>::max()) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::Unsupported, "Streaming PCM chunk is too large");
  }
  const auto bytesToRead = frames * blockAlign_;
  if (bytesToRead > std::numeric_limits<std::streamsize>::max() ||
      firstFrame * blockAlign_ > dataBytes_ - bytesToRead) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::InvariantViolation, "Streaming PCM chunk exceeds data bounds");
  }
  std::ifstream stream(path_, std::ios::binary);
  if (!stream.is_open()) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::IoError, "Unable to open streaming PCM source",
        path_.string());
  }
  stream.seekg(static_cast<std::streamoff>(dataOffset_ + firstFrame * blockAlign_),
               std::ios::beg);
  const auto bytesPerSample = static_cast<std::size_t>(bitsPerSample_ / 8U);
  if (bytesPerSample == 0U || bytesPerSample > 4U) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::Unsupported, "WAV sample width is unsupported");
  }
  std::vector<std::byte> raw(static_cast<std::size_t>(bytesToRead));
  stream.read(reinterpret_cast<char*>(raw.data()),
              static_cast<std::streamsize>(raw.size()));
  if (stream.gcount() != static_cast<std::streamsize>(raw.size())) {
    return core::failure<std::vector<float>>(
        core::ErrorCode::IoError, "Unable to read streaming PCM chunk",
        path_.string());
  }
  std::vector<float> result(static_cast<std::size_t>(sampleCount), 0.0F);
  for (std::size_t index = 0U; index < result.size(); ++index) {
    const auto offset = index * bytesPerSample;
    const std::array<std::byte, 4> sample{
        raw[offset], offset + 1U < raw.size() ? raw[offset + 1U] : std::byte{0},
        offset + 2U < raw.size() ? raw[offset + 2U] : std::byte{0},
        offset + 3U < raw.size() ? raw[offset + 3U] : std::byte{0}};
    float value = 0.0F;
    if (formatCode_ == 3U) {
      const auto rawValue = static_cast<std::uint32_t>(
          std::to_integer<std::uint8_t>(sample[0]) |
          (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[1])) << 8U) |
          (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[2])) << 16U) |
          (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[3])) << 24U));
      std::memcpy(&value, &rawValue, sizeof(value));
      if (!std::isfinite(value)) value = 0.0F;
    } else if (bitsPerSample_ == 8U) {
      value = (static_cast<float>(std::to_integer<std::uint8_t>(sample[0])) - 128.0F) /
              128.0F;
    } else if (bitsPerSample_ == 16U) {
      const auto rawValue = static_cast<std::int16_t>(
          static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(sample[0])) |
          (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(sample[1])) << 8U));
      value = static_cast<float>(rawValue) / 32768.0F;
    } else if (bitsPerSample_ == 24U) {
      value = static_cast<float>(readI24(sample)) / 8388608.0F;
    } else {
      const auto rawValue = static_cast<std::int32_t>(
          static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[0])) |
          (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[1])) << 8U) |
          (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[2])) << 16U) |
          (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sample[3])) << 24U));
      value = static_cast<float>(static_cast<double>(rawValue) / 2147483648.0);
    }
    result[index] = value;
  }
  return result;
}

}
