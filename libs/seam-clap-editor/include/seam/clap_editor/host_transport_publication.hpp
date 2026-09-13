#pragma once

#include "seam/clap_editor/host_timeline.hpp"

#include <atomic>
#include <cstdint>

namespace seam::clap_editor {

// Carries what the host reported about its transport from the audio callback to the owner
// thread without allocating, locking or blocking either of them. The audio thread is the
// only writer and only stores atomics; the owner thread drains a snapshot that belongs to
// one publish() call and is the only thread allowed to lock, invalidate or render.
//
// The audio thread also decides here whether a report is worth forwarding, because that is
// the only place the previously forwarded report is known: a host reports on every block,
// and forwarding every block would grow the acquired map without adding information.
class HostTransportPublication final {
public:
  // How far the host's position must move before a repeat report of the same tempo, meter
  // and loop state is forwarded. Audio callbacks are far denser than this, so it bounds the
  // forwarding rate without leaving the acquired map with gaps it cannot cover.
  static constexpr double kBeatQuantum{0.5};
  static constexpr double kSecondsQuantum{0.25};

  // Audio thread. Stores a snapshot and returns whether it was worth forwarding: the first
  // report, any tempo, meter, loop or transport change, and any position movement past the
  // declared quantum. A report that says the same thing is not stored at all.
  [[nodiscard]] bool publish(const HostTimelineState& state) noexcept;

  // Audio thread. True once per undrained report, so a host that ignores the request is not
  // asked again for the same one. The owner thread clears it when it drains.
  [[nodiscard]] bool requestCallbackIfNeeded() noexcept;

  // Owner thread. False when nothing new has been published since the last successful
  // consume. A torn read (the audio thread published while this ran) also returns false so
  // the caller can try again on its next turn.
  [[nodiscard]] bool tryConsume(HostTimelineState& out) noexcept;

  // Owner thread: clears the waiting flag before draining, so a report published during the
  // drain raises a fresh notification instead of being lost.
  [[nodiscard]] bool shouldNotifyOwner() const noexcept;
  void clearNotify() noexcept;

  [[nodiscard]] std::uint64_t publishedCount() const noexcept {
    return published_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t tornReadCount() const noexcept {
    return tornReads_.load(std::memory_order_relaxed);
  }

private:
  struct OptionalDouble final {
    std::atomic<double> value{0.0};
    std::atomic<bool> present{false};
  };
  struct OptionalMeter final {
    std::atomic<std::uint16_t> numerator{0U};
    std::atomic<std::uint16_t> denominator{0U};
    std::atomic<bool> present{false};
  };

  void store(OptionalDouble& target, bool present, double value) noexcept;
  [[nodiscard]] static double load(const OptionalDouble& source,
                                   bool& present) noexcept;

  // Even means stable, odd means a publish is in progress. The owner retries a read that
  // straddles a publish instead of mixing two reports into one snapshot.
  std::atomic<std::uint64_t> sequence_{0U};
  std::atomic<bool> notifyPending_{false};
  std::atomic<bool> playing_{true};
  OptionalDouble seconds_;
  OptionalDouble beats_;
  OptionalDouble tempo_;
  std::atomic<bool> loopActive_{false};
  OptionalDouble loopStartSeconds_;
  OptionalDouble loopEndSeconds_;
  OptionalDouble loopStartBeats_;
  OptionalDouble loopEndBeats_;
  OptionalMeter meter_;

  std::atomic<std::uint64_t> consumedSequence_{0U};
  std::atomic<std::uint64_t> published_{0U};
  std::atomic<std::uint64_t> tornReads_{0U};

  // Audio-thread-only bookkeeping. Never read or written by the owner thread.
  HostTimelineState forwarded_{};
  bool hasForwarded_{false};
};

}  // namespace seam::clap_editor
