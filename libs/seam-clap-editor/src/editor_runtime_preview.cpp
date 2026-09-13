#include "seam/clap_editor/editor_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace seam::clap_editor {

namespace {

native_ui::RenderStatusState renderStatusFor(authoring::RenderState state) noexcept {
  switch (state) {
    case authoring::RenderState::Idle: return native_ui::RenderStatusState::Idle;
    case authoring::RenderState::Queued: return native_ui::RenderStatusState::Queued;
    case authoring::RenderState::Rendering: return native_ui::RenderStatusState::Rendering;
    case authoring::RenderState::Ready: return native_ui::RenderStatusState::Ready;
    case authoring::RenderState::Stale: return native_ui::RenderStatusState::Stale;
    case authoring::RenderState::Cancelled: return native_ui::RenderStatusState::Cancelled;
    case authoring::RenderState::Failed: return native_ui::RenderStatusState::Failed;
  }
  return native_ui::RenderStatusState::Idle;
}

native_ui::RenderStatusState renderStatusFor(
    OfflineRenderState state) noexcept {
  switch (state) {
    case OfflineRenderState::Idle: return native_ui::RenderStatusState::Idle;
    case OfflineRenderState::Pending: return native_ui::RenderStatusState::Rendering;
    case OfflineRenderState::Ready: return native_ui::RenderStatusState::Ready;
    case OfflineRenderState::Stale: return native_ui::RenderStatusState::Stale;
    case OfflineRenderState::Failed: return native_ui::RenderStatusState::Failed;
  }
  return native_ui::RenderStatusState::Idle;
}

}  // namespace

void EditorRuntime::refreshRenderStatusView() {
  if (!controller_) return;
  const auto progress = authoring_->renderer().progress();
  const auto offline = offlineRender_.view();
  // A prepared or refused bounce is what the host asked about, so it wins over the ordinary
  // preview state; before any bounce the preview state is all there is.
  const bool bounceRelevant = offline.state != OfflineRenderState::Idle;
  const auto rendererReady = authoring_->renderer().acquireCurrent();
  controller_->setRenderStatus(native_ui::RenderStatusView{
      .state = bounceRelevant ? renderStatusFor(offline.state)
                              : renderStatusFor(progress.state),
      .requestedRevision = progress.requestedRevision,
      .audibleRevision = progress.publishedRevision,
      .requestedQuality = progress.requestedQuality,
      .audibleQuality = progress.publishedQuality,
      .completedPhrases = progress.completedPhrases,
      .totalPhrases = progress.totalPhrases,
      .fraction = progress.fraction,
      .hasAudibleAudio = offlineAudioReady_.load(std::memory_order_acquire) ||
                         static_cast<bool>(rendererReady),
      .audibleAudioStale = progress.audibleAudioStale,
      .diagnostic = bounceRelevant && !offline.diagnostic.empty()
                        ? offline.diagnostic
                        : progress.diagnostic,
      .activeVoicebankId = progress.activeVoicebankId,
      .activeVoicebankVersion = progress.activeVoicebankVersion,
      .activeRenderer = progress.activeRenderer,
  });
}

native_ui::RenderStatusView EditorRuntime::renderStatusView() const {
  std::lock_guard lock(mutex_);
  if (!controller_) return native_ui::RenderStatusView{};
  return controller_->renderStatus().view();
}

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
