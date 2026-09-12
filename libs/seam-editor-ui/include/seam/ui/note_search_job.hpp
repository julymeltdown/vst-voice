#pragma once

#include "seam/ui/note_search_navigation.hpp"
#include <mutex>
#include <thread>

namespace seam::ui {

// Owner-thread lifecycle; the worker sees only an immutable captured project.
class NoteSearchJob final {
public:
  enum class State { Idle, Preparing, Ready, Cancelled, Failed };
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool preparing() const noexcept { return worker_.joinable(); }
  [[nodiscard]] core::Result<void> start(const application::EditorSession& session,
      domain::RegionId region, std::string query, NoteSearchField field) {
    if (worker_.joinable()) return core::failure(core::ErrorCode::Conflict, "Retire the previous Find worker first");
    ready_.reset(); cancelled_ = false; state_ = State::Idle;
    if (query.empty() || query.size() > NoteSearchModel::maximumQueryScalars * 4U ||
        (field != NoteSearchField::Lyric && field != NoteSearchField::PronunciationHint &&
         field != NoteSearchField::NoteId && field != NoteSearchField::GeneratedPhoneme && field != NoteSearchField::PronunciationDiagnostic))
      return core::failure(core::ErrorCode::InvalidArgument, "Invalid Find query or field");
    const auto* sourceRegion = session.project().findRegion(region);
    if (!sourceRegion || sourceRegion->notes.size() > NoteSearchModel::maximumNotes || sourceRegion->lyrics.size() > NoteSearchModel::maximumNotes)
      return core::failure(core::ErrorCode::InvalidArgument, "Find region is missing or exceeds note limits");
    auto context = session.capturePerformanceJob(); if (!context) return core::Result<void>{context.error()};
    state_ = State::Preparing;
    try {
      worker_ = std::jthread([this, source = std::move(context.value()), region, revision = session.revision(),
          query = std::move(query), field](std::stop_token stop) mutable {
        auto output = core::failure<NoteSearchNavigation>(core::ErrorCode::Internal, "Find preparation failed");
        try {
          auto found = NoteSearchModel::search(source.sourceProject(), region, revision, query, field, stop);
          if (found) output = NoteSearchNavigation{std::move(source), std::move(found.value())};
          else output = core::Result<NoteSearchNavigation>{found.error()};
        } catch (const std::exception& error) {
          output = core::failure<NoteSearchNavigation>(core::ErrorCode::Internal, "Find preparation failed", error.what());
        } catch (...) {
          output = core::failure<NoteSearchNavigation>(core::ErrorCode::Internal, "Find preparation failed");
        }
        std::lock_guard lock(mutex_); completed_.emplace(std::move(output));
      });
    } catch (const std::exception& error) {
      state_ = State::Failed;
      return core::failure(core::ErrorCode::Internal, "Cannot start Find worker", error.what());
    }
    return core::success();
  }
  [[nodiscard]] core::Result<bool> poll(const application::EditorSession& session, domain::RegionId region) {
    if (!worker_.joinable()) return true;
    std::optional<core::Result<NoteSearchNavigation>> output;
    {
      std::lock_guard lock(mutex_);
      if (!completed_) return false;
      output = std::move(completed_); completed_.reset();
    }
    worker_.join(); // Published result: no further search work remains.
    if (cancelled_) { state_ = State::Cancelled; return true; }
    if (!*output) { state_ = State::Failed; return core::Result<bool>{output->error()}; }
    if (!output->value().matches(session, region)) {
      state_ = State::Failed; return core::failure<bool>(core::ErrorCode::Conflict, "Find source changed; refresh results");
    }
    ready_.emplace(std::move(output->value())); state_ = State::Ready; return true;
  }
  [[nodiscard]] std::optional<NoteSearchNavigation> takeReady() {
    if (state_ != State::Ready || cancelled_) return std::nullopt;
    auto result = std::move(ready_); ready_.reset(); state_ = State::Idle; return result;
  }
  void cancel() noexcept {
    cancelled_ = true; ready_.reset();
    if (worker_.joinable()) worker_.request_stop();
    else state_ = State::Cancelled;
  }
private:
  State state_{State::Idle};
  bool cancelled_{false};
  std::optional<NoteSearchNavigation> ready_;
  std::mutex mutex_;
  std::optional<core::Result<NoteSearchNavigation>> completed_;
  // Last member destroys first: stop/join before any worker-visible state dies.
  std::jthread worker_;
};
}
