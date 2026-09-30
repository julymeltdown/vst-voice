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
  // controller_ is a unique_ptr the owner thread reassigns inside
  // configureControllerCallbacks(), so reading it here without the lock raced that
  // write. The render worker calls this through publishPreviewFromAuthoring, and
  // ThreadSanitizer reported the pair directly: a write in unique_ptr::reset from
  // EditorRuntime::configureControllerCallbacks against a read in
  // refreshRenderStatusView from the worker, on the same address. That is a real
  // use-after-free window, not a benign race: the worker can dereference the
  // controller while the owner thread is destroying it, which is what produced an
  // intermittent segfault in seam_phase11_tests on the macOS CI leg.
  //
  // The lock is recursive, so this is safe whether or not a caller already holds
  // it, and refreshRenderStatusView has no call path that re-enters it.
  std::lock_guard lock{mutex_};
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

void EditorRuntime::publishPreviewFromAuthoring() {
  // A coordinator that is idle has nothing current: it was reset because the score has nothing to
  // sound, or it has never rendered. What it retains is history, not a preview, and handing it to
  // the host would keep a vocal that is no longer in the project playable, so the preview is
  // revoked instead of republished. Revoked, not published over: an empty publication would need a
  // free slot, and readers can hold them all. In every other state the last audio stays on offer,
  // which is what lets a failed or cancelled render leave the previous one playing.
  if (authoring_->renderer().progress().state == authoring::RenderState::Idle) {
    previewPublication_.revoke();
  } else {
    const auto shared = authoring_->renderer().latest();
    if (shared == nullptr || shared->state == authoring::RenderState::Idle) return;
    // Readers that hold every slot do not lose the render: the publication keeps it and offers
    // it again until a slot is free.
    static_cast<void>(previewPublication_.publishWhenFree(makeRenderedPreview(*shared)));
  }
  refreshRenderStatusView();
  std::function<void()> callback;
  {
    std::lock_guard lock(mutex_);
    callback = renderReadyCallback_;
  }
  if (callback) callback();
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
    if (slot == kNothing) return ReadHandle{nullptr, 0U, &empty_};
    slots_[slot].readers.fetch_add(1U, std::memory_order_acquire);
    if (slot == published_.load(std::memory_order_acquire)) {
      return ReadHandle{this, slot, &slots_[slot].preview};
    }
    slots_[slot].readers.fetch_sub(1U, std::memory_order_release);
  }
}

bool RealtimePreviewPublication::install(RenderedPreview& preview) {
  const auto current = published_.load(std::memory_order_acquire);
  // With nothing published every slot is a candidate; otherwise the published one is not.
  const bool nothing = current == kNothing;
  for (std::size_t offset = nothing ? 0U : 1U; offset < kSlotCount; ++offset) {
    const auto candidate = nothing ? offset : (current + offset) % kSlotCount;
    if (slots_[candidate].readers.load(std::memory_order_acquire) != 0U) {
      continue;
    }
    slots_[candidate].preview = std::move(preview);
    published_.store(candidate, std::memory_order_release);
    return true;
  }
  return false;
}

bool RealtimePreviewPublication::publish(RenderedPreview preview) {
  std::scoped_lock lock(writerMutex_);
  if (!install(preview)) return false;
  pending_.reset();
  return true;
}

bool RealtimePreviewPublication::publishWhenFree(RenderedPreview preview) {
  std::scoped_lock lock(writerMutex_);
  if (install(preview)) {
    pending_.reset();
    return true;
  }
  pending_ = std::move(preview);
  if (!offering_) {
    offering_ = true;
    // Any earlier helper has finished with the lock: it cleared offering_ under it.
    try {
      offerer_ = std::jthread([this](std::stop_token stop) { offerPending(stop); });
    } catch (...) {
      // No thread to make the offers: the preview keeps waiting, and the next publication or
      // revoke supersedes it, as it would have.
      offering_ = false;
    }
  }
  return false;
}

void RealtimePreviewPublication::revoke() {
  std::scoped_lock lock(writerMutex_);
  pending_.reset();
  published_.store(kNothing, std::memory_order_release);
}

void RealtimePreviewPublication::offerPending(std::stop_token stop) {
  for (unsigned attempt = 0U;; ++attempt) {
    {
      std::scoped_lock lock(writerMutex_);
      if (stop.stop_requested() || !pending_.has_value() || install(*pending_)) {
        pending_.reset();
        offering_ = false;
        return;
      }
    }
    // A reader holds a slot for one audio block or one paint, so one is free again in moments. A
    // reader that holds one for good only costs this thread an occasional look.
    std::this_thread::sleep_for(attempt < 50U ? std::chrono::milliseconds{2}
                                              : std::chrono::milliseconds{20});
  }
}

}  // namespace seam::clap_editor
