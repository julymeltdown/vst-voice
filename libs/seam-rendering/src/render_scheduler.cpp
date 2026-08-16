#include "seam/rendering/render_scheduler.hpp"

#include <algorithm>
#include <exception>

namespace seam::rendering {

BackgroundRenderScheduler::BackgroundRenderScheduler(PcmCache& cache,
                                                       std::size_t workerCount)
    : cache_(cache) {
  workerCount = std::clamp<std::size_t>(workerCount, 1U, 16U);
  workers_.reserve(workerCount);
  for (std::size_t index = 0; index < workerCount; ++index) {
    workers_.emplace_back([this](std::stop_token token) { workerLoop(token); });
  }
}

BackgroundRenderScheduler::~BackgroundRenderScheduler() {
  {
    std::scoped_lock lock{mutex_};
    stopping_ = true;
    for (auto& [phrase, control] : controls_) {
      static_cast<void>(phrase);
      control->stop.request_stop();
    }
  }
  for (auto& worker : workers_) worker.request_stop();
  condition_.notify_all();
}

core::Result<void> BackgroundRenderScheduler::submit(
    ScheduledRenderRequest request) {
  if (request.phraseId.empty() || request.cacheKey.empty() || request.task == nullptr ||
      request.sampleRate < 8000 || request.sampleRate > 384000) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Render scheduler request is invalid");
  }

  {
    std::scoped_lock lock{mutex_};
    if (stopping_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Render scheduler is stopping");
    }
    const auto latest = latestRevision_.find(request.phraseId);
    if (latest != latestRevision_.end() && request.revision < latest->second) {
      ++stats_.submitted;
      ++stats_.stale;
      completions_.push_back(RenderCompletion{
          .phraseId = request.phraseId,
          .cacheKey = request.cacheKey,
          .revision = request.revision,
          .status = RenderCompletionStatus::Stale,
          .pcm = nullptr,
          .error = {},
      });
      idleCondition_.notify_all();
      return core::success();
    }
  }

  auto cached = cache_.load(request.cacheKey);
  if (cached) {
    if (cached.value()->sampleRate != request.sampleRate) {
      return core::failure(
          core::ErrorCode::Conflict,
          "PCM cache entry sample rate does not match the render request",
          request.cacheKey);
    }
    std::scoped_lock lock{mutex_};
    if (stopping_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Render scheduler is stopping");
    }
    ++stats_.submitted;
    const auto latest = latestRevision_.find(request.phraseId);
    const bool stale = latest != latestRevision_.end() &&
                       request.revision < latest->second;
    if (!stale) {
      if (const auto existing = controls_.find(request.phraseId);
          existing != controls_.end()) {
        existing->second->stop.request_stop();
      }
      latestRevision_[request.phraseId] = request.revision;
      ++stats_.cacheHits;
    } else {
      ++stats_.stale;
    }
    completions_.push_back(RenderCompletion{
        .phraseId = std::move(request.phraseId),
        .cacheKey = std::move(request.cacheKey),
        .revision = request.revision,
        .status = stale ? RenderCompletionStatus::Stale
                        : RenderCompletionStatus::CacheHit,
        .pcm = stale ? nullptr : cached.value(),
        .error = {},
    });
    idleCondition_.notify_all();
    return core::success();
  }
  if (cached.error().code != core::ErrorCode::NotFound) {
    return core::Result<void>{cached.error()};
  }

  auto control = std::make_shared<JobControl>();
  {
    std::scoped_lock lock{mutex_};
    if (stopping_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Render scheduler is stopping");
    }
    const auto latest = latestRevision_.find(request.phraseId);
    if (latest != latestRevision_.end() && request.revision < latest->second) {
      ++stats_.submitted;
      ++stats_.stale;
      completions_.push_back(RenderCompletion{
          .phraseId = request.phraseId,
          .cacheKey = request.cacheKey,
          .revision = request.revision,
          .status = RenderCompletionStatus::Stale,
          .pcm = nullptr,
          .error = {},
      });
      idleCondition_.notify_all();
      return core::success();
    }
    const auto existing = controls_.find(request.phraseId);
    if (existing != controls_.end()) existing->second->stop.request_stop();
    latestRevision_[request.phraseId] = std::max(
        latestRevision_[request.phraseId], request.revision);
    controls_[request.phraseId] = control;
    jobs_.push(Job{
        .request = std::move(request),
        .sequence = nextSequence_++,
        .control = std::move(control),
    });
    ++stats_.submitted;
  }
  condition_.notify_one();
  return core::success();
}

void BackgroundRenderScheduler::cancelPhrase(std::string_view phraseId) {
  std::scoped_lock lock{mutex_};
  const auto iterator = controls_.find(std::string{phraseId});
  if (iterator != controls_.end()) iterator->second->stop.request_stop();
}

void BackgroundRenderScheduler::workerLoop(std::stop_token token) {
  while (!token.stop_requested()) {
    Job job;
    {
      std::unique_lock lock{mutex_};
      condition_.wait(lock, [this, &token] {
        return stopping_ || token.stop_requested() || !jobs_.empty();
      });
      if (stopping_ || token.stop_requested()) return;
      job = jobs_.top();
      jobs_.pop();
      ++activeWorkers_;
    }

    RenderCompletion completion{
        .phraseId = job.request.phraseId,
        .cacheKey = job.request.cacheKey,
        .revision = job.request.revision,
        .status = RenderCompletionStatus::Failed,
        .pcm = nullptr,
        .error = {},
    };
    if (job.control->stop.stop_requested()) {
      completion.status = RenderCompletionStatus::Cancelled;
    } else {
      try {
        auto rendered = job.request.task(job.control->stop.get_token());
        if (!rendered) {
          completion.error = rendered.error();
          completion.status = job.control->stop.stop_requested()
              ? RenderCompletionStatus::Cancelled
              : RenderCompletionStatus::Failed;
        } else if (job.control->stop.stop_requested()) {
          completion.status = RenderCompletionStatus::Cancelled;
        } else {
          bool stale = false;
          {
            std::scoped_lock lock{mutex_};
            const auto latest = latestRevision_.find(job.request.phraseId);
            stale = latest != latestRevision_.end() &&
                    job.request.revision < latest->second;
          }
          if (stale) {
            completion.status = RenderCompletionStatus::Stale;
          } else {
            CachedPcm pcm{
                .sampleRate = job.request.sampleRate,
                .startFrame = rendered.value().startFrame,
                .samples = std::move(rendered).value().samples,
            };
            auto stored = cache_.store(job.request.cacheKey, pcm);
            if (!stored) {
              completion.status = RenderCompletionStatus::Failed;
              completion.error = stored.error();
            } else {
              auto loaded = cache_.load(job.request.cacheKey);
              if (!loaded) {
                completion.status = RenderCompletionStatus::Failed;
                completion.error = loaded.error();
              } else {
                completion.status = RenderCompletionStatus::Completed;
                completion.pcm = loaded.value();
              }
            }
          }
        }
      } catch (const std::exception& exception) {
        completion.status = RenderCompletionStatus::Failed;
        completion.error = core::Error{core::ErrorCode::Internal,
                                       "Render task threw an exception",
                                       exception.what()};
      } catch (...) {
        completion.status = RenderCompletionStatus::Failed;
        completion.error = core::Error{core::ErrorCode::Internal,
                                       "Render task threw an unknown exception", {}};
      }
    }
    pushCompletion(std::move(completion));
    {
      std::scoped_lock lock{mutex_};
      if (activeWorkers_ > 0) --activeWorkers_;
      const auto control = controls_.find(job.request.phraseId);
      if (control != controls_.end() && control->second == job.control) {
        controls_.erase(control);
      }
      if (jobs_.empty() && activeWorkers_ == 0) idleCondition_.notify_all();
    }
  }
}

void BackgroundRenderScheduler::pushCompletion(RenderCompletion completion) {
  std::scoped_lock lock{mutex_};
  switch (completion.status) {
    case RenderCompletionStatus::Completed: ++stats_.completed; break;
    case RenderCompletionStatus::CacheHit: ++stats_.cacheHits; break;
    case RenderCompletionStatus::Cancelled: ++stats_.cancelled; break;
    case RenderCompletionStatus::Stale: ++stats_.stale; break;
    case RenderCompletionStatus::Failed: ++stats_.failed; break;
  }
  completions_.push_back(std::move(completion));
}

std::vector<RenderCompletion> BackgroundRenderScheduler::drainCompleted() {
  std::scoped_lock lock{mutex_};
  std::vector<RenderCompletion> result;
  result.swap(completions_);
  return result;
}

bool BackgroundRenderScheduler::waitIdle(std::chrono::milliseconds timeout) {
  std::unique_lock lock{mutex_};
  return idleCondition_.wait_for(lock, timeout, [this] {
    return jobs_.empty() && activeWorkers_ == 0;
  });
}

RenderSchedulerStats BackgroundRenderScheduler::stats() const {
  std::scoped_lock lock{mutex_};
  return stats_;
}

}  // namespace seam::rendering
