#pragma once
#include <atomic>
#include <chrono>
#include <csignal>
#include <stop_token>
#include <thread>
#include <system_error>

namespace seam::voicebank_cli {
// Single process-wide CLI scope. Only lock-free recording runs in the handler;
// requesting stop (which can invoke callbacks) happens on a normal thread.
inline std::atomic<int> generationSignal{0};
static_assert(std::atomic<int>::is_always_lock_free);
inline void recordGenerationSignal(int signal) noexcept { generationSignal.store(signal, std::memory_order_relaxed); }
class SignalCancellation final {
public:
  SignalCancellation() = default;
  SignalCancellation(const SignalCancellation&) = delete;
  SignalCancellation& operator=(const SignalCancellation&) = delete;
  ~SignalCancellation() {
    watcher_.request_stop();
    if (watcher_.joinable()) watcher_.join();
    if (oldInt_ != SIG_ERR) std::signal(SIGINT, oldInt_);
    if (oldTerm_ != SIG_ERR) std::signal(SIGTERM, oldTerm_);
  }
  bool install() {
    if (oldInt_ != SIG_ERR || oldTerm_ != SIG_ERR) return false;
    generationSignal.store(0, std::memory_order_relaxed);
    oldInt_ = std::signal(SIGINT, recordGenerationSignal);
    oldTerm_ = std::signal(SIGTERM, recordGenerationSignal);
    if (oldInt_ == SIG_ERR || oldTerm_ == SIG_ERR) return false;
    try {
      watcher_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
          if (signal() != 0) { source_.request_stop(); return; }
          std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
      });
    } catch (const std::system_error&) { return false; }
    return true;
  }
  [[nodiscard]] std::stop_token token() const noexcept { return source_.get_token(); }
  [[nodiscard]] int signal() const noexcept { return generationSignal.load(std::memory_order_relaxed); }
private:
  using Handler = void (*)(int);
  Handler oldInt_{SIG_ERR}, oldTerm_{SIG_ERR};
  std::stop_source source_;
  std::jthread watcher_;
};
}
