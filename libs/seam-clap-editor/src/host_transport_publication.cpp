#include "seam/clap_editor/host_transport_publication.hpp"

#include <cmath>

namespace seam::clap_editor {
namespace {

bool sameLoop(const HostTimelineState& first,
              const HostTimelineState& second) noexcept {
  return first.loopActive == second.loopActive &&
         first.loopHasSeconds == second.loopHasSeconds &&
         first.loopHasBeats == second.loopHasBeats &&
         first.loopStartSeconds == second.loopStartSeconds &&
         first.loopEndSeconds == second.loopEndSeconds &&
         first.loopStartBeats == second.loopStartBeats &&
         first.loopEndBeats == second.loopEndBeats;
}

bool worthForwarding(const HostTimelineState& state,
                     const HostTimelineState& previous) noexcept {
  const bool tempoChanged = state.hasTempo != previous.hasTempo ||
      (state.hasTempo && state.tempo != previous.tempo);
  const bool meterChanged = state.hasTimeSignature != previous.hasTimeSignature ||
      (state.hasTimeSignature && (state.numerator != previous.numerator ||
                                  state.denominator != previous.denominator));
  const bool moved = state.hasBeats && previous.hasBeats &&
      std::abs(state.beats - previous.beats) >= HostTransportPublication::kBeatQuantum;
  const bool elapsed = state.hasSeconds && previous.hasSeconds &&
      std::abs(state.seconds - previous.seconds) >= HostTransportPublication::kSecondsQuantum;
  return state.playing != previous.playing || tempoChanged || meterChanged ||
      state.hasTempoRamp != previous.hasTempoRamp || !sameLoop(state, previous) ||
      moved || elapsed;
}

}  // namespace

bool HostTransportPublication::publish(const HostTimelineState& state,
                                       bool force) noexcept {
  if (hasForwarded_ && !force && !state.hasTempoRamp &&
      !worthForwarding(state, forwarded_)) {
    return false;
  }

  const auto head = head_.load(std::memory_order_relaxed);
  const auto tail = tail_.load(std::memory_order_acquire);
  if (head - tail >= kCapacity) {
    // Do not silently lose history. The owner receives an incomplete-capture marker after
    // it drains the queued prefix, and Follow Host freeze will fail closed.
    overflowPending_.store(true, std::memory_order_release);
    overflowCount_.fetch_add(1U, std::memory_order_relaxed);
    return true;
  }
  entries_[head % kCapacity] = state;
  head_.store(head + 1U, std::memory_order_release);
  forwarded_ = state;
  hasForwarded_ = true;
  published_.fetch_add(1U, std::memory_order_relaxed);
  return true;
}

bool HostTransportPublication::requestCallbackIfNeeded() noexcept {
  return !notifyPending_.exchange(true, std::memory_order_acq_rel);
}

bool HostTransportPublication::tryConsume(HostTimelineState& out) noexcept {
  const auto tail = tail_.load(std::memory_order_relaxed);
  const auto head = head_.load(std::memory_order_acquire);
  if (tail != head) {
    out = entries_[tail % kCapacity];
    tail_.store(tail + 1U, std::memory_order_release);
    return true;
  }
  if (overflowPending_.exchange(false, std::memory_order_acq_rel)) {
    out = {};
    out.captureIncomplete = true;
    return true;
  }
  return false;
}

bool HostTransportPublication::shouldNotifyOwner() const noexcept {
  return notifyPending_.load(std::memory_order_acquire);
}

void HostTransportPublication::clearNotify() noexcept {
  notifyPending_.store(false, std::memory_order_release);
}

}  // namespace seam::clap_editor
