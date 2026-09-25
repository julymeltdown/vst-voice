#pragma once

#include "seam/clap_editor/host_timeline.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace seam::clap_editor {

// Bounded SPSC transfer from the real-time audio callback to the owner thread. Keeping
// intermediate reports is required because CLAP can provide sample-offset transport events.
class HostTransportPublication final {
public:
  static constexpr std::size_t kCapacity{4096U};
  static constexpr double kBeatQuantum{0.5};
  static constexpr double kSecondsQuantum{0.25};

  // force is used for CLAP_EVENT_TRANSPORT updates inside a process block.
  [[nodiscard]] bool publish(const HostTimelineState& state,
                             bool force = false) noexcept;
  [[nodiscard]] bool requestCallbackIfNeeded() noexcept;
  [[nodiscard]] bool tryConsume(HostTimelineState& out) noexcept;
  [[nodiscard]] bool shouldNotifyOwner() const noexcept;
  void clearNotify() noexcept;

  [[nodiscard]] std::uint64_t publishedCount() const noexcept {
    return published_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t tornReadCount() const noexcept { return 0U; }
  [[nodiscard]] std::uint64_t overflowCount() const noexcept {
    return overflowCount_.load(std::memory_order_relaxed);
  }

private:
  std::array<HostTimelineState, kCapacity> entries_{};
  std::atomic<std::uint64_t> head_{0U};
  std::atomic<std::uint64_t> tail_{0U};
  std::atomic<bool> notifyPending_{false};
  std::atomic<bool> overflowPending_{false};
  std::atomic<std::uint64_t> published_{0U};
  std::atomic<std::uint64_t> overflowCount_{0U};

  // Audio-thread-only forwarding-rate bookkeeping.
  HostTimelineState forwarded_{};
  bool hasForwarded_{false};
};

}  // namespace seam::clap_editor
