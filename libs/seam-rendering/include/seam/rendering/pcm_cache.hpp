#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace seam::rendering {

struct CachedPcm final {
  std::uint32_t sampleRate{48000};
  time::SampleFrame startFrame{0};
  std::vector<float> samples;

  friend bool operator==(const CachedPcm&, const CachedPcm&) = default;
};

struct PcmCacheLimits final {
  std::uint64_t maximumMemoryBytes{128ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumDiskBytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumEntryBytes{256ULL * 1024ULL * 1024ULL};
  std::size_t maximumDiskEntries{4096U};
};

struct PcmCacheUsage final {
  std::uint64_t memoryBytes{0};
  std::size_t memoryEntries{0};
  std::uint64_t diskBytes{0};
  std::size_t diskEntries{0};
};

struct PcmCacheStats final {
  std::uint64_t memoryHits{0};
  std::uint64_t diskHits{0};
  std::uint64_t misses{0};
  std::uint64_t writes{0};
  std::uint64_t corruptEntries{0};
  std::uint64_t memoryEvictions{0};
  std::uint64_t diskEvictions{0};
  std::uint64_t evictedBytes{0};
};

class PcmCache final {
public:
  explicit PcmCache(std::filesystem::path root,
                    PcmCacheLimits limits = {});

  [[nodiscard]] core::Result<std::shared_ptr<const CachedPcm>> load(
      std::string_view key);
  [[nodiscard]] core::Result<void> store(std::string_view key,
                                         const CachedPcm& pcm);
  [[nodiscard]] core::Result<void> erase(std::string_view key);
  [[nodiscard]] core::Result<PcmCacheUsage> usage() const;
  [[nodiscard]] core::Result<PcmCacheUsage> pruneDisk();
  void clearMemory();
  [[nodiscard]] PcmCacheStats stats() const;
  [[nodiscard]] const PcmCacheLimits& limits() const noexcept { return limits_; }
  [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
  struct MemoryEntry final {
    std::shared_ptr<const CachedPcm> pcm;
    std::uint64_t bytes{0};
    std::uint64_t access{0};
  };

  [[nodiscard]] core::Result<std::filesystem::path> pathFor(
      std::string_view key) const;
  void insertMemoryLocked(std::string key,
                          std::shared_ptr<const CachedPcm> pcm);
  void pruneMemoryLocked();

  std::filesystem::path root_;
  PcmCacheLimits limits_;
  mutable std::mutex ioMutex_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, MemoryEntry> memory_;
  std::uint64_t memoryBytes_{0};
  std::uint64_t accessCounter_{0};
  PcmCacheStats stats_;
};

[[nodiscard]] std::uint64_t pcmChecksum(std::span<const float> samples) noexcept;

}  // namespace seam::rendering
