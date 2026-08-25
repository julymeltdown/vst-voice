#pragma once

#include <atomic>
#include <cstdint>

namespace seam::core {

struct RealtimeAuditCounters final {
  std::atomic<std::uint64_t> fileIoCalls{0U};
  std::atomic<std::uint64_t> loggerCalls{0U};
  std::atomic<std::uint64_t> lockAttempts{0U};
};

namespace detail {

inline thread_local RealtimeAuditCounters* activeRealtimeAudit = nullptr;

}

class RealtimeAuditScope final {
public:
  explicit RealtimeAuditScope(RealtimeAuditCounters& counters) noexcept
      : previous_(detail::activeRealtimeAudit) {
    detail::activeRealtimeAudit = &counters;
  }

  ~RealtimeAuditScope() { detail::activeRealtimeAudit = previous_; }

  RealtimeAuditScope(const RealtimeAuditScope&) = delete;
  RealtimeAuditScope& operator=(const RealtimeAuditScope&) = delete;

private:
  RealtimeAuditCounters* previous_;
};

inline void recordRealtimeFileIo() noexcept {
  if (auto* audit = detail::activeRealtimeAudit; audit != nullptr) {
    audit->fileIoCalls.fetch_add(1U, std::memory_order_relaxed);
  }
}

inline void recordRealtimeLogger() noexcept {
  if (auto* audit = detail::activeRealtimeAudit; audit != nullptr) {
    audit->loggerCalls.fetch_add(1U, std::memory_order_relaxed);
  }
}

inline void recordRealtimeLockAttempt() noexcept {
  if (auto* audit = detail::activeRealtimeAudit; audit != nullptr) {
    audit->lockAttempts.fetch_add(1U, std::memory_order_relaxed);
  }
}

}
