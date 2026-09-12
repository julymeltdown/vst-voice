#pragma once
#include "seam/authoring/japanese_reading_capture.hpp"
#include <mutex>
#include <thread>

namespace seam::authoring {
// Owner-thread lifecycle. The worker owns only an immutable capture and never
// touches EditorSession, VoicebankSession, coordinator or UI state.
class JapaneseReadingJob final {
public:
  enum class State { Idle, Preparing, Ready, Applied, Cancelled, Failed };
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool preparing() const noexcept { return worker_.joinable(); }
  [[nodiscard]] std::uint64_t requestId() const noexcept { return requestId_; }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  [[nodiscard]] core::Result<void> start(const application::EditorSession& session,
      domain::RegionId region, std::span<const domain::NoteId> notes,
      StagedJapaneseReadingResource resource) {
    if (worker_.joinable()) return core::failure(core::ErrorCode::Conflict, "Retire the previous Japanese reading first");
    completed_.reset(); current_.reset(); error_.clear(); cancelled_ = false; state_ = State::Idle;
    if (requestId_ == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Japanese reading request identity exhausted");
    auto captured = JapaneseReadingCapture::prepare(session, region, notes, std::move(resource));
    if (!captured) { state_ = State::Failed; error_ = captured.error().message; return core::Result<void>{captured.error()}; }
    capture_.emplace(std::move(captured.value())); ++requestId_; state_ = State::Preparing;
    try {
      worker_ = std::jthread([this, source = *capture_](std::stop_token stop) {
        auto result = core::failure<JapaneseReadingReview>(core::ErrorCode::Internal, "Japanese reading failed");
        try { result = source.read(stop); }
        catch (const std::exception& exception) {
          result = core::failure<JapaneseReadingReview>(core::ErrorCode::Internal, "Japanese reading failed", exception.what());
        } catch (...) { result = core::failure<JapaneseReadingReview>(core::ErrorCode::Internal, "Japanese reading failed"); }
        std::lock_guard lock(mutex_); completed_.emplace(std::move(result));
      });
    } catch (const std::exception& exception) {
      capture_.reset(); state_ = State::Failed; error_ = exception.what();
      return core::failure(core::ErrorCode::Internal, "Cannot start Japanese reading worker", exception.what());
    }
    return core::success();
  }

  [[nodiscard]] core::Result<bool> poll(const application::EditorSession& session,
      domain::RegionId region, const phonemizer::JapaneseReadingIdentity& identity) {
    if (!worker_.joinable()) return true;
    std::optional<core::Result<JapaneseReadingReview>> result;
    { std::lock_guard lock(mutex_); if (!completed_) return false; result = std::move(completed_); completed_.reset(); }
    worker_.join();
    if (cancelled_) { capture_.reset(); state_ = State::Cancelled; return true; }
    if (!*result) { error_ = result->error().message; capture_.reset(); state_ = State::Failed; return core::Result<bool>{result->error()}; }
    if (!capture_ || !capture_->matches(session, region, identity)) {
      error_ = "Japanese reading source or document changed"; capture_.reset(); state_ = State::Failed;
      return core::failure<bool>(core::ErrorCode::Conflict, error_);
    }
    current_.emplace(std::move(result->value())); state_ = State::Ready; return true;
  }

  [[nodiscard]] const JapaneseReadingReview* current(const application::EditorSession& session,
      domain::RegionId region, const phonemizer::JapaneseReadingIdentity& identity) const {
    return state_ == State::Ready && current_ && capture_ && capture_->matches(session, region, identity) ? &*current_ : nullptr;
  }
  [[nodiscard]] core::Result<void> canApplyCurrent(const application::EditorSession& session,
      domain::RegionId region, const phonemizer::JapaneseReadingIdentity& identity) const {
    const auto* review = current(session, region, identity);
    if (!review || !capture_) return core::failure(core::ErrorCode::Conflict, "Japanese reading result is stale or unavailable");
    return capture_->validateApplyPlan(*review);
  }
  [[nodiscard]] core::Result<void> applyCurrent(application::EditorSession& session,
      domain::RegionId region, const phonemizer::JapaneseReadingIdentity& identity) {
    if (state_ != State::Ready || !current_ || !capture_ || !capture_->matches(session, region, identity))
      return core::failure(core::ErrorCode::Conflict, "Japanese reading result is stale or unavailable");
    const auto applied = capture_->apply(*current_, session);
    if (!applied) return applied;
    current_.reset(); capture_.reset(); state_ = State::Applied;
    return core::success();
  }
  void cancel() noexcept {
    cancelled_ = true; current_.reset(); state_ = State::Cancelled;
    if (worker_.joinable()) worker_.request_stop();
    else { capture_.reset(); state_ = State::Cancelled; }
  }
  // Retire a cancelled worker without requiring the caller to keep the
  // resource identity or an EditorSession alive. This is used when a review
  // closes before the worker reaches its completion slot.
  [[nodiscard]] core::Result<bool> pollCancelled() {
    if (!worker_.joinable()) { capture_.reset(); current_.reset(); state_ = State::Cancelled; return true; }
    std::optional<core::Result<JapaneseReadingReview>> result;
    { std::lock_guard lock(mutex_); if (!completed_) return false; result = std::move(completed_); completed_.reset(); }
    worker_.join(); capture_.reset(); current_.reset(); state_ = State::Cancelled;
    return true;
  }
private:
  State state_{State::Idle};
  std::uint64_t requestId_{0U};
  bool cancelled_{false};
  std::string error_;
  std::optional<JapaneseReadingCapture> capture_;
  std::optional<JapaneseReadingReview> current_;
  std::mutex mutex_;
  std::optional<core::Result<JapaneseReadingReview>> completed_;
  std::jthread worker_; // Must be destroyed before mutex/capture state.
};
}
