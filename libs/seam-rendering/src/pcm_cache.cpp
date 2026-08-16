#include "seam/rendering/pcm_cache.hpp"

#include "seam/core/stable_hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>

namespace seam::rendering {
namespace {

constexpr std::array<char, 8> kMagic{'S', 'E', 'A', 'M', 'P', 'C', 'M', '3'};
constexpr std::uint32_t kVersion = 2;
constexpr std::uint64_t kMaximumFrames = 200'000'000ULL;

template <typename T>
void writeLittle(std::ostream& stream, T value) {
  using Unsigned = std::make_unsigned_t<T>;
  auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    stream.put(static_cast<char>((bits >> (index * 8U)) & static_cast<Unsigned>(0xffU)));
  }
}

template <typename T>
bool readLittle(std::istream& stream, T& value) {
  using Unsigned = std::make_unsigned_t<T>;
  Unsigned bits = 0;
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    const auto character = stream.get();
    if (character == std::char_traits<char>::eof()) return false;
    bits |= static_cast<Unsigned>(static_cast<unsigned char>(character)) << (index * 8U);
  }
  value = static_cast<T>(bits);
  return true;
}

bool validKey(std::string_view key) {
  if (key.empty() || key.size() > 128U) return false;
  return std::all_of(key.begin(), key.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '-' || value == '_';
  });
}

}  // namespace

std::uint64_t pcmChecksum(std::span<const float> samples) noexcept {
  core::StableHash64 hash;
  for (const auto sample : samples) {
    hash.add(sample);
  }
  return hash.value();
}

PcmCache::PcmCache(std::filesystem::path root) : root_(std::move(root)) {}

core::Result<std::filesystem::path> PcmCache::pathFor(std::string_view key) const {
  if (!validKey(key)) {
    return core::failure<std::filesystem::path>(core::ErrorCode::InvalidArgument,
                                                 "PCM cache key is invalid");
  }
  return root_ / (std::string{key} + ".spcm");
}

core::Result<std::shared_ptr<const CachedPcm>> PcmCache::load(std::string_view key) {
  const auto pathResult = pathFor(key);
  if (!pathResult) return core::Result<std::shared_ptr<const CachedPcm>>{pathResult.error()};
  {
    std::scoped_lock lock{mutex_};
    const auto iterator = memory_.find(std::string{key});
    if (iterator != memory_.end()) {
      ++stats_.memoryHits;
      return iterator->second;
    }
  }

  std::scoped_lock ioLock{ioMutex_};
  std::ifstream stream(pathResult.value(), std::ios::binary);
  if (!stream) {
    std::scoped_lock lock{mutex_};
    ++stats_.misses;
    return core::failure<std::shared_ptr<const CachedPcm>>(
        core::ErrorCode::NotFound, "PCM cache entry was not found", std::string{key});
  }
  std::array<char, 8> magic{};
  stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  std::uint32_t version = 0;
  std::uint32_t sampleRate = 0;
  std::int64_t startFrame = 0;
  std::uint64_t frameCount = 0;
  std::uint64_t expectedChecksum = 0;
  if (!stream || magic != kMagic || !readLittle(stream, version) ||
      !readLittle(stream, sampleRate) || !readLittle(stream, startFrame) ||
      !readLittle(stream, frameCount) ||
      !readLittle(stream, expectedChecksum) || version != kVersion ||
      sampleRate < 8000 || sampleRate > 384000 || frameCount == 0 ||
      frameCount > kMaximumFrames) {
    std::scoped_lock lock{mutex_};
    ++stats_.corruptEntries;
    return core::failure<std::shared_ptr<const CachedPcm>>(
        core::ErrorCode::ParseError, "PCM cache header is invalid", std::string{key});
  }
  auto pcm = std::make_shared<CachedPcm>();
  pcm->sampleRate = sampleRate;
  pcm->startFrame = startFrame;
  pcm->samples.resize(static_cast<std::size_t>(frameCount));
  for (auto& sample : pcm->samples) {
    std::uint32_t bits = 0;
    if (!readLittle(stream, bits)) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError, "PCM cache payload is truncated", std::string{key});
    }
    sample = std::bit_cast<float>(bits);
    if (!std::isfinite(sample)) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError,
          "PCM cache contains a non-finite sample", std::string{key});
    }
  }
  if (pcmChecksum(pcm->samples) != expectedChecksum) {
    std::scoped_lock lock{mutex_};
    ++stats_.corruptEntries;
    return core::failure<std::shared_ptr<const CachedPcm>>(
        core::ErrorCode::ParseError, "PCM cache checksum does not match", std::string{key});
  }
  std::shared_ptr<const CachedPcm> immutable = pcm;
  {
    std::scoped_lock lock{mutex_};
    memory_[std::string{key}] = immutable;
    ++stats_.diskHits;
  }
  return immutable;
}

core::Result<void> PcmCache::store(std::string_view key, const CachedPcm& pcm) {
  const auto pathResult = pathFor(key);
  if (!pathResult) return core::Result<void>{pathResult.error()};
  if (pcm.sampleRate < 8000 || pcm.sampleRate > 384000 || pcm.samples.empty() ||
      pcm.samples.size() > kMaximumFrames ||
      std::any_of(pcm.samples.begin(), pcm.samples.end(),
                  [](float sample) { return !std::isfinite(sample); })) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "PCM cache payload is invalid", std::string{key});
  }
  std::scoped_lock ioLock{ioMutex_};
  std::error_code error;
  std::filesystem::create_directories(root_, error);
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to create PCM cache directory", error.message());
  }
  std::filesystem::path temporary;
  {
    std::scoped_lock lock{mutex_};
    temporary = pathResult.value().string() + ".tmp." +
                std::to_string(++temporaryCounter_);
  }
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create PCM cache entry", temporary.string());
    }
    stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    writeLittle(stream, kVersion);
    writeLittle(stream, pcm.sampleRate);
    writeLittle(stream, pcm.startFrame);
    writeLittle(stream, static_cast<std::uint64_t>(pcm.samples.size()));
    writeLittle(stream, pcmChecksum(pcm.samples));
    for (const auto sample : pcm.samples) {
      writeLittle(stream, std::bit_cast<std::uint32_t>(sample));
    }
    stream.flush();
    if (!stream) {
      std::filesystem::remove(temporary, error);
      return core::failure(core::ErrorCode::IoError,
                           "Unable to write PCM cache entry", temporary.string());
    }
  }
  std::filesystem::remove(pathResult.value(), error);
  error.clear();
  std::filesystem::rename(temporary, pathResult.value(), error);
  if (error) {
    std::filesystem::remove(temporary);
    return core::failure(core::ErrorCode::IoError,
                         "Unable to atomically replace PCM cache entry", error.message());
  }
  auto immutable = std::make_shared<const CachedPcm>(pcm);
  {
    std::scoped_lock lock{mutex_};
    memory_[std::string{key}] = std::move(immutable);
    ++stats_.writes;
  }
  return core::success();
}

core::Result<void> PcmCache::erase(std::string_view key) {
  const auto pathResult = pathFor(key);
  if (!pathResult) return core::Result<void>{pathResult.error()};
  {
    std::scoped_lock lock{mutex_};
    memory_.erase(std::string{key});
  }
  std::scoped_lock ioLock{ioMutex_};
  std::error_code error;
  std::filesystem::remove(pathResult.value(), error);
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to erase PCM cache entry", error.message());
  }
  return core::success();
}

void PcmCache::clearMemory() {
  std::scoped_lock lock{mutex_};
  memory_.clear();
}

PcmCacheStats PcmCache::stats() const {
  std::scoped_lock lock{mutex_};
  return stats_;
}

}  // namespace seam::rendering
