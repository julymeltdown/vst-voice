#include "seam/rendering/stale_audio_store.hpp"

namespace seam::rendering {

PublishResult StaleWhileRenderStore::publish(
    std::string phraseId,
    std::uint64_t revision,
    std::shared_ptr<const CachedPcm> pcm) {
  if (phraseId.empty() || pcm == nullptr) return {};
  std::scoped_lock lock{mutex_};
  auto& entry = entries_[std::move(phraseId)];
  if (entry.pcm != nullptr && revision < entry.revision) return {};
  const bool replaced = entry.pcm != nullptr;
  entry.revision = revision;
  entry.pcm = std::move(pcm);
  entry.dirty = false;
  return PublishResult{.accepted = true, .replacedExisting = replaced};
}

std::optional<StaleAudioSnapshot> StaleWhileRenderStore::current(
    std::string_view phraseId) const {
  std::scoped_lock lock{mutex_};
  const auto iterator = entries_.find(std::string{phraseId});
  if (iterator == entries_.end() || iterator->second.pcm == nullptr) {
    return std::nullopt;
  }
  return StaleAudioSnapshot{
      .revision = iterator->second.revision,
      .pcm = iterator->second.pcm,
      .dirty = iterator->second.dirty,
  };
}

bool StaleWhileRenderStore::markDirty(std::string_view phraseId) {
  std::scoped_lock lock{mutex_};
  const auto iterator = entries_.find(std::string{phraseId});
  if (iterator == entries_.end()) return false;
  iterator->second.dirty = true;
  return true;
}

bool StaleWhileRenderStore::isDirty(std::string_view phraseId) const {
  std::scoped_lock lock{mutex_};
  const auto iterator = entries_.find(std::string{phraseId});
  return iterator != entries_.end() && iterator->second.dirty;
}

}  // namespace seam::rendering
