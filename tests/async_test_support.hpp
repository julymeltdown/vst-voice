#pragma once

#include "test_framework.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace seam::test {

// A harness timeout is an exception, never an application Result error: a
// CHECK(!result) refusal assertion must not accept an unfinished operation.
// Injectable time/pause functions keep deadline known-answer tests deterministic.
template<class Poll, class Busy, class Now, class Pause>
auto drainAsyncOperation(Poll&& poll, Busy&& busy, std::chrono::milliseconds budget,
                         std::string_view label, Now&& now, Pause&& pause) -> decltype(poll()) {
  if (budget <= std::chrono::milliseconds::zero()) throw Failure("Async test wait requires a positive budget");
  const auto deadline = now() + budget;
  const auto checkDeadline = [&] {
    if (now() >= deadline) throw Failure(std::string{label} + " exceeded its functional test wait budget");
  };
  for (;;) {
    checkDeadline();
    auto result = poll();
    // A blocking poll cannot turn a late success or refusal into a passing test.
    checkDeadline();
    const bool pending = busy();
    checkDeadline();
    if (!result) {
      if (pending) throw Failure(std::string{label} + " reported an error while still busy");
      return result;
    }
    if (!pending) return result;
    pause();
  }
}

template<class Poll, class Busy>
auto drainAsyncOperation(Poll&& poll, Busy&& busy, std::chrono::milliseconds budget,
                         std::string_view label) -> decltype(poll()) {
  return drainAsyncOperation(std::forward<Poll>(poll), std::forward<Busy>(busy), budget, label,
      [] { return std::chrono::steady_clock::now(); },
      [] { std::this_thread::sleep_for(std::chrono::milliseconds{1}); });
}

}  // namespace seam::test
