#pragma once

#include "seam/rendering/pcm_cache.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace seam::rendering {

struct PublishResult final {
  bool accepted{false};
  bool replacedExisting{false};
};

struct StaleAudioSnapshot final {
  std::uint64_t revision{0};
  std::shared_ptr<const CachedPcm> pcm;
  bool dirty{false};
};

class StaleWhileRenderStore final {
public:
  [[nodiscard]] PublishResult publish(std::string phraseId,
                                      std::uint64_t revision,
                                      std::shared_ptr<const CachedPcm> pcm);
  [[nodiscard]] std::optional<StaleAudioSnapshot> current(
      std::string_view phraseId) const;
  [[nodiscard]] bool markDirty(std::string_view phraseId);
  [[nodiscard]] bool isDirty(std::string_view phraseId) const;

private:
  struct Entry final {
    std::uint64_t revision{0};
    std::shared_ptr<const CachedPcm> pcm;
    bool dirty{false};
  };
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace seam::rendering
