#pragma once
#include "seam/authoring/audio_measurement_capture.hpp"
#include <mutex>
#include <thread>

namespace seam::authoring {
// Lifecycle and current() are owner-thread only. The analysis worker owns a
// capture copy and never calls the session, coordinator, controller or UI.
class AudioMeasurementJob final {
public:
  enum class State { Idle, Preparing, Ready, Cancelled, Failed };
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool preparing() const noexcept { return worker_.joinable(); }
  [[nodiscard]] core::Result<void> start(const application::EditorSession& session,
      const AuthoringRenderCoordinator& coordinator, std::size_t first, std::size_t count, std::size_t bins = 512U) {
    if (worker_.joinable()) return core::failure(core::ErrorCode::Conflict, "Retire the previous audio measurement first");
    ready_.reset(); capture_.reset(); cancelled_ = false; state_ = State::Idle;
    auto captured = AudioMeasurementCapture::prepare(session, coordinator);
    if (!captured) { state_ = State::Failed; return core::Result<void>{captured.error()}; }
    capture_.emplace(std::move(captured.value())); state_ = State::Preparing;
    try {
      worker_ = std::jthread([this, source = *capture_, first, count, bins](std::stop_token stop) {
        auto result = core::failure<rendering::MeasuredAudioEnvelope>(core::ErrorCode::Internal, "Audio measurement failed");
        try { result = source.measure(first, count, bins, stop); }
        catch (const std::exception& error) { result = core::failure<rendering::MeasuredAudioEnvelope>(core::ErrorCode::Internal, "Audio measurement failed", error.what()); }
        catch (...) { result = core::failure<rendering::MeasuredAudioEnvelope>(core::ErrorCode::Internal, "Audio measurement failed"); }
        std::lock_guard lock(mutex_); completed_.emplace(std::move(result));
      });
    } catch (const std::exception& error) {
      capture_.reset(); state_ = State::Failed;
      return core::failure(core::ErrorCode::Internal, "Cannot start audio measurement worker", error.what());
    }
    return core::success();
  }
  [[nodiscard]] core::Result<bool> poll(const application::EditorSession& session,
      const AuthoringRenderCoordinator& coordinator) {
    if (!worker_.joinable()) return true;
    std::optional<core::Result<rendering::MeasuredAudioEnvelope>> result;
    {
      std::lock_guard lock(mutex_); if (!completed_) return false;
      result = std::move(completed_); completed_.reset();
    }
    worker_.join();
    if (cancelled_) { capture_.reset(); state_ = State::Cancelled; return true; }
    if (!*result) { capture_.reset(); state_ = State::Failed; return core::Result<bool>{result->error()}; }
    if (!capture_ || !capture_->matches(session, coordinator)) {
      capture_.reset(); state_ = State::Failed;
      return core::failure<bool>(core::ErrorCode::Conflict, "Audio measurement source or document changed");
    }
    ready_.emplace(std::move(result->value())); state_ = State::Ready; return true;
  }
  [[nodiscard]] const rendering::MeasuredAudioEnvelope* current(const application::EditorSession& session,
      const AuthoringRenderCoordinator& coordinator) const {
    return state_ == State::Ready && ready_ && capture_ && capture_->matches(session, coordinator) ? &*ready_ : nullptr;
  }
  void cancel() noexcept {
    cancelled_ = true; ready_.reset();
    if (worker_.joinable()) worker_.request_stop();
    else { capture_.reset(); state_ = State::Cancelled; }
  }
private:
  State state_{State::Idle};
  bool cancelled_{false};
  std::optional<AudioMeasurementCapture> capture_;
  std::optional<rendering::MeasuredAudioEnvelope> ready_;
  std::mutex mutex_;
  std::optional<core::Result<rendering::MeasuredAudioEnvelope>> completed_;
  std::jthread worker_; // Stop/join before destroying worker-visible state.
};
}
