#include "seam/live_voice/realtime_publication.hpp"

#include <utility>

namespace seam::live_voice {

LiveResourcePublication::ReadHandle::ReadHandle(
    const LiveResourcePublication* owner, std::size_t slot,
    const LiveVoicebankResources* value) noexcept
    : owner_(owner), slot_(slot), value_(value) {}

LiveResourcePublication::ReadHandle::ReadHandle(ReadHandle&& other) noexcept
    : owner_(other.owner_), slot_(other.slot_), value_(other.value_) {
  other.owner_ = nullptr;
  other.value_ = nullptr;
}

LiveResourcePublication::ReadHandle&
LiveResourcePublication::ReadHandle::operator=(ReadHandle&& other) noexcept {
  if (this == &other) return *this;
  release();
  owner_ = other.owner_;
  slot_ = other.slot_;
  value_ = other.value_;
  other.owner_ = nullptr;
  other.value_ = nullptr;
  return *this;
}

LiveResourcePublication::ReadHandle::~ReadHandle() { release(); }

void LiveResourcePublication::ReadHandle::release() noexcept {
  if (owner_ != nullptr) {
    owner_->slots_[slot_].readers.fetch_sub(1U, std::memory_order_release);
  }
  owner_ = nullptr;
  value_ = nullptr;
}

LiveResourcePublication::ReadHandle LiveResourcePublication::acquire() const noexcept {
  for (;;) {
    const auto published = published_.load(std::memory_order_acquire);
    if (published < 0) return {};
    const auto slot = static_cast<std::size_t>(published);
    const auto& current = slots_[slot];
    current.readers.fetch_add(1U, std::memory_order_acquire);
    if (published == published_.load(std::memory_order_acquire) &&
        current.resources != nullptr) {
      return ReadHandle{this, slot, current.resources.get()};
    }
    current.readers.fetch_sub(1U, std::memory_order_release);
  }
}

bool LiveResourcePublication::publish(
    std::shared_ptr<const LiveVoicebankResources> resources) {
  if (resources == nullptr || resources->units.empty() ||
      resources->identity.contentHash.size() != 64U) {
    return false;
  }
  std::scoped_lock lock(writerMutex_);
  const auto current = published_.load(std::memory_order_acquire);
  for (std::size_t offset = 1U; offset < kSlotCount; ++offset) {
    const auto candidate =
        (static_cast<std::size_t>(current < 0 ? 0 : current) + offset) %
        kSlotCount;
    if (slots_[candidate].readers.load(std::memory_order_acquire) != 0U) {
      continue;
    }
    slots_[candidate].resources = std::move(resources);
    published_.store(static_cast<int>(candidate), std::memory_order_release);
    return true;
  }
  return false;
}

void LiveResourcePublication::clear() noexcept {
  std::scoped_lock lock(writerMutex_);
  published_.store(-1, std::memory_order_release);
  for (auto& slot : slots_) {
    if (slot.readers.load(std::memory_order_acquire) == 0U) {
      slot.resources.reset();
    }
  }
}

}
