#include "seam/clap_editor/editor_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace seam::clap_editor {

std::string_view previewStatusName(PreviewStatus status) noexcept {
  switch (status) {
    case PreviewStatus::Empty: return "empty";
    case PreviewStatus::Ready: return "ready";
    case PreviewStatus::VoicebankMissing: return "voicebank-missing";
    case PreviewStatus::VoicebankVersionMismatch: return "voicebank-version-mismatch";
    case PreviewStatus::VoicebankContentHashMissing: return "voicebank-content-hash-missing";
    case PreviewStatus::VoicebankContentMismatch: return "voicebank-content-mismatch";
    case PreviewStatus::VoicebankUntrusted: return "voicebank-untrusted";
    case PreviewStatus::Failed: return "failed";
  }
  return "unknown";
}


RealtimePreviewPublication::ReadHandle::ReadHandle(
    const RealtimePreviewPublication* owner, std::size_t slot,
    const RenderedPreview* value) noexcept
    : owner_(owner), slot_(slot), value_(value) {}

RealtimePreviewPublication::ReadHandle::ReadHandle(ReadHandle&& other) noexcept
    : owner_(other.owner_), slot_(other.slot_), value_(other.value_) {
  other.owner_ = nullptr;
  other.value_ = nullptr;
}

RealtimePreviewPublication::ReadHandle&
RealtimePreviewPublication::ReadHandle::operator=(ReadHandle&& other) noexcept {
  if (this == &other) return *this;
  release();
  owner_ = other.owner_;
  slot_ = other.slot_;
  value_ = other.value_;
  other.owner_ = nullptr;
  other.value_ = nullptr;
  return *this;
}

RealtimePreviewPublication::ReadHandle::~ReadHandle() { release(); }

void RealtimePreviewPublication::ReadHandle::release() noexcept {
  if (owner_ != nullptr) {
    owner_->slots_[slot_].readers.fetch_sub(1U, std::memory_order_release);
  }
  owner_ = nullptr;
  value_ = nullptr;
}

RealtimePreviewPublication::RealtimePreviewPublication() {
  slots_[0].preview = RenderedPreview{};
}

RealtimePreviewPublication::ReadHandle
RealtimePreviewPublication::acquire() const noexcept {
  for (;;) {
    const auto slot = published_.load(std::memory_order_acquire);
    slots_[slot].readers.fetch_add(1U, std::memory_order_acquire);
    if (slot == published_.load(std::memory_order_acquire)) {
      return ReadHandle{this, slot, &slots_[slot].preview};
    }
    slots_[slot].readers.fetch_sub(1U, std::memory_order_release);
  }
}

bool RealtimePreviewPublication::publish(RenderedPreview preview) {
  std::scoped_lock lock(writerMutex_);
  const auto current = published_.load(std::memory_order_acquire);
  for (std::size_t offset = 1U; offset < kSlotCount; ++offset) {
    const auto candidate = (current + offset) % kSlotCount;
    if (slots_[candidate].readers.load(std::memory_order_acquire) != 0U) {
      continue;
    }
    slots_[candidate].preview = std::move(preview);
    published_.store(candidate, std::memory_order_release);
    return true;
  }
  return false;
}

}  // namespace seam::clap_editor
