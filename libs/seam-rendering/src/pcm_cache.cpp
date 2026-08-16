#include "seam/rendering/pcm_cache.hpp"

#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/stable_hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

namespace seam::rendering {
namespace {

constexpr std::array<char, 8> kMagic{'S', 'E', 'A', 'M', 'P', 'C', 'M', '4'};
constexpr std::uint32_t kVersion = build::kPcmCacheFormatRevision;
constexpr std::uint64_t kMaximumFrames = 200'000'000ULL;
constexpr std::uint64_t kHeaderBytes = 8U + 4U + 4U + 8U + 8U + 8U;

template <typename T>
void writeLittleAt(std::span<std::byte> output, std::size_t& offset, T value) {
  using Unsigned = std::make_unsigned_t<T>;
  const auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    output[offset] = static_cast<std::byte>(
        (bits >> (index * 8U)) & static_cast<Unsigned>(0xffU));
    ++offset;
  }
}

template <typename T>
bool readLittle(std::istream& stream, T& value) {
  using Unsigned = std::make_unsigned_t<T>;
  Unsigned bits = 0;
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    const auto character = stream.get();
    if (character == std::char_traits<char>::eof()) return false;
    bits |= static_cast<Unsigned>(static_cast<unsigned char>(character))
            << (index * 8U);
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

std::uint64_t payloadBytes(const CachedPcm& pcm) noexcept {
  return static_cast<std::uint64_t>(pcm.samples.size()) *
         static_cast<std::uint64_t>(sizeof(float));
}

struct DiskEntry final {
  std::filesystem::path path;
  std::filesystem::file_time_type modified;
  std::uint64_t bytes{0};
};

}  // namespace

std::uint64_t pcmChecksum(std::span<const float> samples) noexcept {
  core::StableHash64 hash;
  for (const auto sample : samples) hash.add(sample);
  return hash.value();
}

PcmCache::PcmCache(std::filesystem::path root, PcmCacheLimits limits)
    : root_(std::move(root)), limits_(limits) {
  limits_.maximumMemoryBytes = std::min<std::uint64_t>(
      limits_.maximumMemoryBytes, 16ULL * 1024ULL * 1024ULL * 1024ULL);
  limits_.maximumDiskBytes = std::min<std::uint64_t>(
      limits_.maximumDiskBytes, 64ULL * 1024ULL * 1024ULL * 1024ULL);
  limits_.maximumEntryBytes = std::min<std::uint64_t>(
      limits_.maximumEntryBytes, 4ULL * 1024ULL * 1024ULL * 1024ULL);
  limits_.maximumDiskEntries = std::min<std::size_t>(
      limits_.maximumDiskEntries, 1'000'000U);
}

core::Result<std::filesystem::path> PcmCache::pathFor(std::string_view key) const {
  if (!validKey(key)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument, "PCM cache key is invalid");
  }
  return root_ / (std::string{key} + ".spcm");
}

void PcmCache::insertMemoryLocked(std::string key,
                                  std::shared_ptr<const CachedPcm> pcm) {
  if (pcm == nullptr || limits_.maximumMemoryBytes == 0U) return;
  const auto bytes = payloadBytes(*pcm);
  const auto existing = memory_.find(key);
  if (existing != memory_.end()) {
    memoryBytes_ -= existing->second.bytes;
    memory_.erase(existing);
  }
  memoryBytes_ += bytes;
  memory_.emplace(std::move(key), MemoryEntry{
      .pcm = std::move(pcm),
      .bytes = bytes,
      .access = ++accessCounter_,
  });
  pruneMemoryLocked();
}

void PcmCache::pruneMemoryLocked() {
  while (!memory_.empty() && memoryBytes_ > limits_.maximumMemoryBytes) {
    const auto oldest = std::min_element(
        memory_.begin(), memory_.end(), [](const auto& lhs, const auto& rhs) {
          if (lhs.second.access == rhs.second.access) return lhs.first < rhs.first;
          return lhs.second.access < rhs.second.access;
        });
    if (oldest == memory_.end()) break;
    memoryBytes_ -= oldest->second.bytes;
    ++stats_.memoryEvictions;
    memory_.erase(oldest);
  }
}

core::Result<std::shared_ptr<const CachedPcm>> PcmCache::load(
    std::string_view key) {
  const auto pathResult = pathFor(key);
  if (!pathResult) {
    return core::Result<std::shared_ptr<const CachedPcm>>{pathResult.error()};
  }
  {
    std::scoped_lock lock{mutex_};
    const auto iterator = memory_.find(std::string{key});
    if (iterator != memory_.end()) {
      iterator->second.access = ++accessCounter_;
      ++stats_.memoryHits;
      return iterator->second.pcm;
    }
  }

  std::shared_ptr<CachedPcm> pcm;
  {
    std::scoped_lock ioLock{ioMutex_};
    std::error_code sizeError;
    const bool exists = std::filesystem::exists(pathResult.value(), sizeError);
    if (sizeError) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError, "Unable to inspect PCM cache entry",
          sizeError.message());
    }
    if (!exists) {
      std::scoped_lock lock{mutex_};
      ++stats_.misses;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::NotFound, "PCM cache entry was not found",
          std::string{key});
    }
    const auto fileBytes = std::filesystem::file_size(pathResult.value(), sizeError);
    if (sizeError) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError, "Unable to inspect PCM cache entry",
          sizeError.message());
    }
    if (fileBytes < kHeaderBytes || fileBytes > kHeaderBytes + limits_.maximumEntryBytes) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError, "PCM cache file size is outside configured limits",
          std::string{key});
    }
    std::ifstream stream(pathResult.value(), std::ios::binary);
    if (!stream) {
      std::scoped_lock lock{mutex_};
      ++stats_.misses;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::NotFound, "PCM cache entry was not found",
          std::string{key});
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
        sampleRate < 8000U || sampleRate > 384000U || frameCount == 0U ||
        frameCount > kMaximumFrames) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError, "PCM cache header is invalid",
          std::string{key});
    }
    const auto payload = frameCount * static_cast<std::uint64_t>(sizeof(float));
    if (payload > limits_.maximumEntryBytes || fileBytes != kHeaderBytes + payload) {
      std::scoped_lock lock{mutex_};
      ++stats_.corruptEntries;
      return core::failure<std::shared_ptr<const CachedPcm>>(
          core::ErrorCode::ParseError,
          "PCM cache payload size does not match its header",
          std::string{key});
    }
    pcm = std::make_shared<CachedPcm>();
    pcm->sampleRate = sampleRate;
    pcm->startFrame = startFrame;
    pcm->samples.resize(static_cast<std::size_t>(frameCount));
    for (auto& sample : pcm->samples) {
      std::uint32_t bits = 0;
      if (!readLittle(stream, bits)) {
        std::scoped_lock lock{mutex_};
        ++stats_.corruptEntries;
        return core::failure<std::shared_ptr<const CachedPcm>>(
            core::ErrorCode::ParseError, "PCM cache payload is truncated",
            std::string{key});
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
          core::ErrorCode::ParseError, "PCM cache checksum does not match",
          std::string{key});
    }
    std::error_code timestampError;
    std::filesystem::last_write_time(
        pathResult.value(), std::filesystem::file_time_type::clock::now(),
        timestampError);
  }

  std::shared_ptr<const CachedPcm> immutable = pcm;
  {
    std::scoped_lock lock{mutex_};
    insertMemoryLocked(std::string{key}, immutable);
    ++stats_.diskHits;
  }
  return immutable;
}

core::Result<void> PcmCache::store(std::string_view key, const CachedPcm& pcm) {
  const auto pathResult = pathFor(key);
  if (!pathResult) return core::Result<void>{pathResult.error()};
  if (pcm.sampleRate < 8000U || pcm.sampleRate > 384000U ||
      pcm.samples.empty() || pcm.samples.size() > kMaximumFrames ||
      payloadBytes(pcm) > limits_.maximumEntryBytes ||
      (limits_.maximumDiskBytes > 0U &&
       kHeaderBytes + payloadBytes(pcm) > limits_.maximumDiskBytes) ||
      std::any_of(pcm.samples.begin(), pcm.samples.end(),
                  [](float sample) { return !std::isfinite(sample); })) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "PCM cache payload is invalid", std::string{key});
  }
  {
    std::scoped_lock ioLock{ioMutex_};
    const auto encodedSize = static_cast<std::size_t>(
        kHeaderBytes + payloadBytes(pcm));
    std::vector<std::byte> encoded(encodedSize);
    std::size_t offset = 0U;
    for (const auto value : kMagic) {
      encoded[offset] = static_cast<std::byte>(value);
      ++offset;
    }
    writeLittleAt(encoded, offset, kVersion);
    writeLittleAt(encoded, offset, pcm.sampleRate);
    writeLittleAt(encoded, offset, pcm.startFrame);
    writeLittleAt(encoded, offset,
                  static_cast<std::uint64_t>(pcm.samples.size()));
    writeLittleAt(encoded, offset, pcmChecksum(pcm.samples));
    for (const auto sample : pcm.samples) {
      writeLittleAt(encoded, offset, std::bit_cast<std::uint32_t>(sample));
    }
    if (offset != encoded.size()) {
      return core::failure(core::ErrorCode::Internal,
                           "PCM cache encoder size mismatch",
                           std::string{key});
    }
    const auto written = core::durableAtomicWrite(pathResult.value(), encoded);
    if (!written) return written;
  }

  auto immutable = std::make_shared<const CachedPcm>(pcm);
  {
    std::scoped_lock lock{mutex_};
    insertMemoryLocked(std::string{key}, std::move(immutable));
    ++stats_.writes;
  }
  const auto pruned = pruneDisk();
  if (!pruned) return core::Result<void>{pruned.error()};
  return core::success();
}

core::Result<void> PcmCache::erase(std::string_view key) {
  const auto pathResult = pathFor(key);
  if (!pathResult) return core::Result<void>{pathResult.error()};
  {
    std::scoped_lock lock{mutex_};
    const auto iterator = memory_.find(std::string{key});
    if (iterator != memory_.end()) {
      memoryBytes_ -= iterator->second.bytes;
      memory_.erase(iterator);
    }
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

core::Result<PcmCacheUsage> PcmCache::usage() const {
  PcmCacheUsage result;
  {
    std::scoped_lock lock{mutex_};
    result.memoryBytes = memoryBytes_;
    result.memoryEntries = memory_.size();
  }
  std::scoped_lock ioLock{ioMutex_};
  std::error_code error;
  if (!std::filesystem::exists(root_, error)) return result;
  if (error) {
    return core::failure<PcmCacheUsage>(core::ErrorCode::IoError,
                                        "Unable to inspect PCM cache", error.message());
  }
  for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
    if (error) break;
    if (!entry.is_regular_file(error) || entry.path().extension() != ".spcm") continue;
    const auto size = entry.file_size(error);
    if (error) break;
    result.diskBytes += size;
    ++result.diskEntries;
  }
  if (error) {
    return core::failure<PcmCacheUsage>(core::ErrorCode::IoError,
                                        "Unable to enumerate PCM cache", error.message());
  }
  return result;
}

core::Result<PcmCacheUsage> PcmCache::pruneDisk() {
  std::vector<DiskEntry> entries;
  std::uint64_t totalBytes = 0U;
  {
    std::scoped_lock ioLock{ioMutex_};
    std::error_code error;
    if (!std::filesystem::exists(root_, error)) {
      PcmCacheUsage empty;
      {
        std::scoped_lock lock{mutex_};
        empty.memoryBytes = memoryBytes_;
        empty.memoryEntries = memory_.size();
      }
      return empty;
    }
    if (error) {
      return core::failure<PcmCacheUsage>(core::ErrorCode::IoError,
                                          "Unable to inspect PCM cache", error.message());
    }
    for (const auto& item : std::filesystem::directory_iterator(root_, error)) {
      if (error) break;
      if (!item.is_regular_file(error) || item.path().extension() != ".spcm") continue;
      const auto bytes = item.file_size(error);
      if (error) break;
      const auto modified = item.last_write_time(error);
      if (error) break;
      entries.push_back(DiskEntry{.path = item.path(),
                                  .modified = modified,
                                  .bytes = bytes});
      totalBytes += bytes;
    }
    if (error) {
      return core::failure<PcmCacheUsage>(core::ErrorCode::IoError,
                                          "Unable to enumerate PCM cache", error.message());
    }
    std::stable_sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
      if (lhs.modified == rhs.modified) return lhs.path.string() < rhs.path.string();
      return lhs.modified < rhs.modified;
    });
    std::uint64_t evictedBytes = 0U;
    std::uint64_t evictedEntries = 0U;
    std::size_t cursor = 0U;
    while (cursor < entries.size() &&
           (totalBytes > limits_.maximumDiskBytes ||
            entries.size() - static_cast<std::size_t>(evictedEntries) >
                limits_.maximumDiskEntries)) {
      std::filesystem::remove(entries[cursor].path, error);
      if (error) {
        return core::failure<PcmCacheUsage>(core::ErrorCode::IoError,
                                            "Unable to prune PCM cache",
                                            error.message());
      }
      totalBytes -= entries[cursor].bytes;
      evictedBytes += entries[cursor].bytes;
      ++evictedEntries;
      ++cursor;
    }
    if (evictedEntries > 0U) {
      std::scoped_lock lock{mutex_};
      stats_.diskEvictions += evictedEntries;
      stats_.evictedBytes += evictedBytes;
    }
  }
  PcmCacheUsage result;
  {
    std::scoped_lock lock{mutex_};
    result.memoryBytes = memoryBytes_;
    result.memoryEntries = memory_.size();
  }
  result.diskBytes = totalBytes;
  result.diskEntries = 0U;
  std::error_code countError;
  if (std::filesystem::exists(root_, countError)) {
    for (const auto& item : std::filesystem::directory_iterator(root_, countError)) {
      if (countError) break;
      if (item.is_regular_file(countError) && item.path().extension() == ".spcm") {
        ++result.diskEntries;
      }
    }
  }
  if (countError) {
    return core::failure<PcmCacheUsage>(core::ErrorCode::IoError,
                                        "Unable to count PCM cache entries",
                                        countError.message());
  }
  return result;
}

void PcmCache::clearMemory() {
  std::scoped_lock lock{mutex_};
  memory_.clear();
  memoryBytes_ = 0U;
}

PcmCacheStats PcmCache::stats() const {
  std::scoped_lock lock{mutex_};
  return stats_;
}

}  // namespace seam::rendering
