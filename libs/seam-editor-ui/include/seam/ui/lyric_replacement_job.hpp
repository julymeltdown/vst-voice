#pragma once

#include "seam/ui/lyric_replacement_review.hpp"
#include <mutex>
#include <thread>

namespace seam::ui {

// All public operations run on the editor owner thread. Only preparation reads
// the captured immutable source on the worker. No callbacks target a destroyed UI.
class LyricReplacementJob final {
public:
  enum class State { Idle, Preparing, Ready, Applied, Cancelled, Failed };
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] const LyricReplacementReview* review() const noexcept {
    return state_ == State::Ready && review_ ? &*review_ : nullptr;
  }
  [[nodiscard]] const std::optional<core::Error>& error() const noexcept { return error_; }

  [[nodiscard]] core::Result<void> start(const application::EditorSession& session,
      domain::RegionId regionId, std::string query, std::string replacement,
      std::optional<std::vector<domain::NoteId>> distributionTargets = std::nullopt) {
    if (worker_.joinable())
      return core::failure(core::ErrorCode::Conflict, "Retire the previous replacement preparation first");
    // A new request supersedes a displayed review even if its input is invalid.
    review_.reset(); context_.reset(); error_.reset(); cancelled_ = false; state_ = State::Idle;
    const bool invalid = distributionTargets ?
        (distributionTargets->empty() || distributionTargets->size() > NoteSearchModel::maximumNotes || replacement.size() > NoteSearchModel::maximumTextScalars * 4U) :
        (query.empty() || query.size() > NoteSearchModel::maximumQueryScalars * 4U || replacement.size() > NoteSearchModel::maximumQueryScalars * 4U);
    if (invalid)
      return core::failure(core::ErrorCode::InvalidArgument, "Replacement input exceeds supported bounds");
    const auto* region = session.project().findRegion(regionId);
    if (!region || region->notes.size() > NoteSearchModel::maximumNotes ||
        region->lyrics.size() > NoteSearchModel::maximumNotes)
      return core::failure(core::ErrorCode::InvalidArgument, "Replacement region is missing or exceeds note limits");
    auto context = session.capturePerformanceJob();
    if (!context) return core::Result<void>{context.error()};
    context_ = std::move(context.value()); regionId_ = regionId; revision_ = session.revision();
    state_ = State::Preparing;
    try {
      worker_ = std::jthread([this, source = *context_, regionId, revision = revision_,
                             query = std::move(query), replacement = std::move(replacement), targets = std::move(distributionTargets)](std::stop_token stop) {
        auto result = core::failure<LyricReplacementReview>(core::ErrorCode::Internal, "Replacement preparation failed");
        try {
          result = LyricReplacementReview::prepare(source.sourceProject(), regionId, revision, query, replacement, stop, targets);
        } catch (const std::exception& exception) {
          result = core::failure<LyricReplacementReview>(core::ErrorCode::Internal, "Replacement preparation failed", exception.what());
        } catch (...) {
          result = core::failure<LyricReplacementReview>(core::ErrorCode::Internal, "Replacement preparation failed");
        }
        std::lock_guard lock(mutex_); completed_.emplace(std::move(result));
      });
    } catch (const std::exception& exception) {
      state_ = State::Failed; context_.reset();
      error_ = core::Error{core::ErrorCode::Internal, "Cannot start replacement worker", exception.what()};
      return core::Result<void>{*error_};
    }
    return core::success();
  }

  // Returns false while the worker is live; true when a result is ready or the
  // job has retired. Cancellation never joins a still-running worker here.
  [[nodiscard]] core::Result<bool> poll(const application::EditorSession& session,
      domain::RegionId selectedRegion) {
    if (!worker_.joinable()) return core::success(true);
    std::optional<core::Result<LyricReplacementReview>> completed;
    {
      std::lock_guard lock(mutex_);
      if (!completed_) return core::success(false);
      completed = std::move(completed_); completed_.reset();
    }
    worker_.join(); // The worker has published and only has stack unwinding left.
    if (cancelled_) { context_.reset(); state_ = State::Cancelled; return core::success(true); }
    const auto valid = session.validatePerformanceJob(*context_);
    if (!valid) return fail(valid.error());
    if (session.revision() != revision_ || selectedRegion != regionId_)
      return fail(core::Error{core::ErrorCode::Conflict, "Replacement result is stale", {}});
    if (!*completed) return fail(completed->error());
    if (!completed->value().matches(session, selectedRegion))
      return fail(core::Error{core::ErrorCode::Conflict, "Replacement source changed", {}});
    review_.emplace(std::move(completed->value())); state_ = State::Ready;
    return core::success(true);
  }

  void cancel() noexcept {
    cancelled_ = true;
    if (worker_.joinable()) worker_.request_stop();
    else { if (review_) review_->cancel(); review_.reset(); context_.reset(); state_ = State::Cancelled; }
  }
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session, domain::RegionId selectedRegion) {
    if (state_ != State::Ready || !review_ || !context_ || cancelled_)
      return core::failure(core::ErrorCode::Conflict, "No replacement review is ready");
    const auto valid = session.validatePerformanceJob(*context_);
    if (!valid) return valid;
    const auto result = review_->apply(session, selectedRegion);
    if (result) { state_ = State::Applied; context_.reset(); }
    return result;
  }

private:
  core::Result<bool> fail(core::Error error) {
    error_ = std::move(error); state_ = State::Failed; context_.reset(); review_.reset();
    return core::Result<bool>{*error_};
  }
  State state_{State::Idle};
  bool cancelled_{false};
  domain::RegionId regionId_;
  std::uint64_t revision_{0U};
  std::optional<application::PerformanceJobContext> context_;
  std::optional<LyricReplacementReview> review_;
  std::optional<core::Error> error_;
  std::mutex mutex_;
  std::optional<core::Result<LyricReplacementReview>> completed_;
  // Destroy first: requests stop and joins before worker-visible members die.
  std::jthread worker_;
};
}
