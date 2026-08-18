#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

class LatestRevisionWorker {
 public:
  LatestRevisionWorker() : worker_([this](std::stop_token token) { run(token); }) {}
  ~LatestRevisionWorker() { worker_.request_stop(); cv_.notify_all(); }

  void submit(std::uint64_t revision) {
    latestSubmitted_.store(revision, std::memory_order_release);
    {
      std::lock_guard lock(mutex_);
      pending_ = revision;
      hasPending_ = true;
    }
    cv_.notify_all();
  }

  std::uint64_t published() const noexcept {
    return published_.load(std::memory_order_acquire);
  }

 private:
  void run(std::stop_token token) {
    while (!token.stop_requested()) {
      std::uint64_t revision = 0;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, token, [this] { return hasPending_; });
        if (token.stop_requested()) {
          return;
        }
        revision = pending_;
        hasPending_ = false;
      }
      if (revision == latestSubmitted_.load(std::memory_order_acquire)) {
        published_.store(revision, std::memory_order_release);
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable_any cv_;
  bool hasPending_{false};
  std::uint64_t pending_{0};
  std::atomic<std::uint64_t> latestSubmitted_{0};
  std::atomic<std::uint64_t> published_{0};
  std::jthread worker_;
};

int main() {
  LatestRevisionWorker worker;
  for (std::uint64_t revision = 1; revision <= 10000; ++revision) {
    worker.submit(revision);
  }
  for (int attempt = 0; attempt < 1000 && worker.published() != 10000; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::cout << "submitted=10000 published=" << worker.published() << '\n';
  return worker.published() == 10000 ? 0 : 1;
}
