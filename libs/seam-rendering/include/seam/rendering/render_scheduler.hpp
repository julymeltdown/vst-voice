#pragma once

#include "seam/core/error.hpp"
#include "seam/core/result.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/synthesis/seam_composer.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace seam::rendering {

enum class RenderPriority : std::uint8_t {
  Background = 0,
  Viewport = 1,
  Selected = 2,
  Next = 3,
  Playhead = 4,
};

enum class RenderCompletionStatus {
  Completed,
  CacheHit,
  Cancelled,
  Stale,
  Failed,
};

using RenderTask = std::function<core::Result<synthesis::PhraseAudio>(std::stop_token)>;

struct ScheduledRenderRequest final {
  std::string phraseId;
  std::string cacheKey;
  std::uint64_t revision{0};
  std::uint32_t sampleRate{48000};
  RenderPriority priority{RenderPriority::Background};
  RenderTask task;
};

struct RenderCompletion final {
  std::string phraseId;
  std::string cacheKey;
  std::uint64_t revision{0};
  RenderCompletionStatus status{RenderCompletionStatus::Failed};
  std::shared_ptr<const CachedPcm> pcm;
  core::Error error;
};

struct RenderSchedulerHooks final {
  // Test/diagnostic hook executed after cache publication but before the final
  // revision gate. It is intentionally outside the scheduler mutex.
  std::function<void()> beforeFinalPublish;
};

struct RenderSchedulerStats final {
  std::uint64_t submitted{0};
  std::uint64_t completed{0};
  std::uint64_t cacheHits{0};
  std::uint64_t cancelled{0};
  std::uint64_t stale{0};
  std::uint64_t failed{0};
};

class BackgroundRenderScheduler final {
public:
  explicit BackgroundRenderScheduler(PcmCache& cache,
                                     std::size_t workerCount = 2U,
                                     RenderSchedulerHooks hooks = {});
  ~BackgroundRenderScheduler();

  BackgroundRenderScheduler(const BackgroundRenderScheduler&) = delete;
  BackgroundRenderScheduler& operator=(const BackgroundRenderScheduler&) = delete;

  [[nodiscard]] core::Result<void> submit(ScheduledRenderRequest request);
  [[nodiscard]] std::vector<RenderCompletion> drainCompleted();
  [[nodiscard]] bool waitIdle(std::chrono::milliseconds timeout);
  [[nodiscard]] RenderSchedulerStats stats() const;
  void cancelPhrase(std::string_view phraseId);

private:
  struct JobControl final { std::stop_source stop; };
  struct Job final {
    ScheduledRenderRequest request;
    std::uint64_t sequence{0};
    std::shared_ptr<JobControl> control;
  };
  struct Compare final {
    bool operator()(const Job& lhs, const Job& rhs) const noexcept {
      if (lhs.request.priority != rhs.request.priority) {
        return static_cast<unsigned>(lhs.request.priority) <
               static_cast<unsigned>(rhs.request.priority);
      }
      return lhs.sequence > rhs.sequence;
    }
  };

  void workerLoop(std::stop_token token);
  void pushCompletion(RenderCompletion completion);

  PcmCache& cache_;
  RenderSchedulerHooks hooks_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::condition_variable idleCondition_;
  std::priority_queue<Job, std::vector<Job>, Compare> jobs_;
  std::vector<RenderCompletion> completions_;
  std::unordered_map<std::string, std::shared_ptr<JobControl>> controls_;
  std::unordered_map<std::string, std::uint64_t> latestRevision_;
  std::vector<std::jthread> workers_;
  std::size_t activeWorkers_{0};
  std::uint64_t nextSequence_{0};
  bool stopping_{false};
  RenderSchedulerStats stats_;
};

}  // namespace seam::rendering
