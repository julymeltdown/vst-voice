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

struct PcmCacheStats final {
  std::uint64_t memoryHits{0};
  std::uint64_t diskHits{0};
  std::uint64_t misses{0};
  std::uint64_t writes{0};
  std::uint64_t corruptEntries{0};
};

class PcmCache final {
public:
  explicit PcmCache(std::filesystem::path root);

  [[nodiscard]] core::Result<std::shared_ptr<const CachedPcm>> load(
      std::string_view key);
  [[nodiscard]] core::Result<void> store(std::string_view key,
                                         const CachedPcm& pcm);
  [[nodiscard]] core::Result<void> erase(std::string_view key);
  void clearMemory();
  [[nodiscard]] PcmCacheStats stats() const;
  [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
  [[nodiscard]] core::Result<std::filesystem::path> pathFor(
      std::string_view key) const;

  std::filesystem::path root_;
  mutable std::mutex ioMutex_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<const CachedPcm>> memory_;
  PcmCacheStats stats_;
  std::uint64_t temporaryCounter_{0};
};

[[nodiscard]] std::uint64_t pcmChecksum(std::span<const float> samples) noexcept;

}  // namespace seam::rendering
