#pragma once

#include "seam/native_ui/diagnostic_search.hpp"
#include "seam/application/editor_session.hpp"
#include <mutex>
#include <thread>

namespace seam::native_ui {

class DiagnosticSearchReview final {
public:
  [[nodiscard]] const DiagnosticSearchSnapshot& snapshot() const noexcept { return snapshot_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, const DiagnosticPanelModel& panel) const {
    return !closed_ && session.revision() == revision_ && snapshot_.matches(panel) &&
        static_cast<bool>(session.validatePerformanceJob(context_));
  }
  [[nodiscard]] core::Result<std::size_t> resolve(const application::EditorSession& session,
      const DiagnosticPanelModel& panel, std::size_t hitIndex) const {
    if (!matches(session, panel)) return core::failure<std::size_t>(core::ErrorCode::Conflict, "Diagnostic results changed; refresh first");
    if (hitIndex >= snapshot_.hits().size()) return core::failure<std::size_t>(core::ErrorCode::NotFound, "Diagnostic result is unavailable");
    return snapshot_.hits()[hitIndex].sourceIndex;
  }
  void close() noexcept { closed_ = true; }
private:
  friend class DiagnosticSearchJob;
  DiagnosticSearchReview(application::PerformanceJobContext context, std::uint64_t revision, DiagnosticSearchSnapshot snapshot)
      : context_(std::move(context)), revision_(revision), snapshot_(std::move(snapshot)) {}
  application::PerformanceJobContext context_;
  std::uint64_t revision_;
  DiagnosticSearchSnapshot snapshot_;
  bool closed_{false};
};

// All lifecycle calls run on the owner thread. The worker owns plain captured
// entries, never the panel, callbacks, session or a pointer to the UI controller.
class DiagnosticSearchJob final {
public:
  enum class State { Idle, Preparing, Ready, Cancelled, Failed };
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool preparing() const noexcept { return worker_.joinable(); }
  [[nodiscard]] core::Result<void> start(const application::EditorSession& session,
      const DiagnosticPanelModel& panel, std::string query) {
    if (worker_.joinable()) return core::failure(core::ErrorCode::Conflict, "Retire the previous diagnostic search first");
    ready_.reset(); context_.reset(); cancelled_ = false; state_ = State::Idle;
    if (query.empty() || query.size() > 1024U)
      return core::failure(core::ErrorCode::InvalidArgument, "Diagnostic query exceeds bounds");
    auto captured = DiagnosticSearchSnapshot::capture(panel);
    if (!captured) return core::Result<void>{captured.error()};
    auto context = session.capturePerformanceJob(); if (!context) return core::Result<void>{context.error()};
    context_.emplace(std::move(context.value())); revision_ = session.revision(); state_ = State::Preparing;
    try {
      worker_ = std::jthread([this, entries = std::move(captured.value()), query = std::move(query)](std::stop_token stop) {
        auto result = core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::Internal, "Diagnostic search failed");
        try { result = DiagnosticSearchSnapshot::prepare(std::span<const DiagnosticPanelEntry>{entries}, query, stop); }
        catch (const std::exception& error) { result = core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::Internal, "Diagnostic search failed", error.what()); }
        catch (...) { result = core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::Internal, "Diagnostic search failed"); }
        std::lock_guard lock(mutex_); completed_.emplace(std::move(result));
      });
    } catch (const std::exception& error) {
      state_ = State::Failed; context_.reset();
      return core::failure(core::ErrorCode::Internal, "Cannot start diagnostic search worker", error.what());
    }
    return core::success();
  }
  [[nodiscard]] core::Result<bool> poll(const application::EditorSession& session, const DiagnosticPanelModel& panel) {
    if (!worker_.joinable()) return true;
    std::optional<core::Result<DiagnosticSearchSnapshot>> result;
    {
      std::lock_guard lock(mutex_); if (!completed_) return false;
      result = std::move(completed_); completed_.reset();
    }
    worker_.join();
    if (cancelled_) { context_.reset(); state_ = State::Cancelled; return true; }
    if (!*result) { context_.reset(); state_ = State::Failed; return core::Result<bool>{result->error()}; }
    DiagnosticSearchReview review{std::move(*context_), revision_, std::move(result->value())}; context_.reset();
    if (!review.matches(session, panel)) {
      state_ = State::Failed; return core::failure<bool>(core::ErrorCode::Conflict, "Diagnostic search source changed; refresh first");
    }
    ready_.emplace(std::move(review)); state_ = State::Ready; return true;
  }
  [[nodiscard]] std::optional<DiagnosticSearchReview> takeReady() {
    if (state_ != State::Ready || cancelled_) return std::nullopt;
    auto result = std::move(ready_); ready_.reset(); state_ = State::Idle; return result;
  }
  void cancel() noexcept {
    cancelled_ = true; ready_.reset();
    if (worker_.joinable()) worker_.request_stop();
    else { context_.reset(); state_ = State::Cancelled; }
  }
private:
  State state_{State::Idle};
  bool cancelled_{false};
  std::uint64_t revision_{0U};
  std::optional<application::PerformanceJobContext> context_;
  std::optional<DiagnosticSearchReview> ready_;
  std::mutex mutex_;
  std::optional<core::Result<DiagnosticSearchSnapshot>> completed_;
  std::jthread worker_; // Destroy/stop/join before worker-visible state dies.
};
}
