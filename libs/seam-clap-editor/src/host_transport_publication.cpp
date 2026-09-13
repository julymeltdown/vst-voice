#include "seam/clap_editor/host_transport_publication.hpp"

#include <cmath>

namespace seam::clap_editor {
namespace {

bool changed(bool first, bool second) noexcept { return first != second; }

bool changed(double first, double second) noexcept {
  // Exact comparison is deliberate: this decides whether the host's own report differs from
  // the last one forwarded, not whether two musical values are close.
  return first != second;
}

}  // namespace

void HostTransportPublication::store(OptionalDouble& target, bool present,
                                     double value) noexcept {
  target.value.store(value, std::memory_order_relaxed);
  target.present.store(present, std::memory_order_relaxed);
}

double HostTransportPublication::load(const OptionalDouble& source,
                                      bool& present) noexcept {
  const auto value = source.value.load(std::memory_order_relaxed);
  present = source.present.load(std::memory_order_relaxed);
  return value;
}

bool HostTransportPublication::publish(const HostTimelineState& state) noexcept {
  if (hasForwarded_) {
    const bool tempoChanged =
        changed(state.hasTempo, forwarded_.hasTempo) ||
        (state.hasTempo && changed(state.tempo, forwarded_.tempo));
    const bool meterChanged =
        changed(state.hasTimeSignature, forwarded_.hasTimeSignature) ||
        (state.hasTimeSignature &&
         (state.numerator != forwarded_.numerator ||
          state.denominator != forwarded_.denominator));
    const bool loopChanged =
        changed(state.loopActive, forwarded_.loopActive) ||
        (state.loopActive &&
         (changed(state.loopHasSeconds, forwarded_.loopHasSeconds) ||
          changed(state.loopHasBeats, forwarded_.loopHasBeats) ||
          changed(state.loopStartSeconds, forwarded_.loopStartSeconds) ||
          changed(state.loopEndSeconds, forwarded_.loopEndSeconds) ||
          changed(state.loopStartBeats, forwarded_.loopStartBeats) ||
          changed(state.loopEndBeats, forwarded_.loopEndBeats)));
    const bool moved = state.hasBeats && forwarded_.hasBeats &&
                       std::abs(state.beats - forwarded_.beats) >= kBeatQuantum;
    const bool elapsed = state.hasSeconds && forwarded_.hasSeconds &&
                         std::abs(state.seconds - forwarded_.seconds) >= kSecondsQuantum;
    if (!changed(state.playing, forwarded_.playing) && !tempoChanged && !meterChanged &&
        !loopChanged && !moved && !elapsed) {
      return false;
    }
  }

  sequence_.fetch_add(1U, std::memory_order_acq_rel);  // odd: a publish is in progress
  playing_.store(state.playing, std::memory_order_relaxed);
  store(seconds_, state.hasSeconds && std::isfinite(state.seconds),
        state.hasSeconds ? state.seconds : 0.0);
  store(beats_, state.hasBeats && std::isfinite(state.beats),
        state.hasBeats ? state.beats : 0.0);
  store(tempo_, state.hasTempo && std::isfinite(state.tempo), state.tempo);
  loopActive_.store(state.loopActive, std::memory_order_relaxed);
  store(loopStartSeconds_, state.loopHasSeconds, state.loopStartSeconds);
  store(loopEndSeconds_, state.loopHasSeconds, state.loopEndSeconds);
  store(loopStartBeats_, state.loopHasBeats, state.loopStartBeats);
  store(loopEndBeats_, state.loopHasBeats, state.loopEndBeats);
  meter_.numerator.store(state.numerator, std::memory_order_relaxed);
  meter_.denominator.store(state.denominator, std::memory_order_relaxed);
  meter_.present.store(state.hasTimeSignature && state.numerator > 0U &&
                           state.denominator > 0U,
                       std::memory_order_relaxed);
  sequence_.fetch_add(1U, std::memory_order_release);  // even: stable again

  forwarded_ = state;
  hasForwarded_ = true;
  published_.fetch_add(1U, std::memory_order_relaxed);
  return true;
}

bool HostTransportPublication::requestCallbackIfNeeded() noexcept {
  return !notifyPending_.exchange(true, std::memory_order_acq_rel);
}

bool HostTransportPublication::tryConsume(HostTimelineState& out) noexcept {
  const auto before = sequence_.load(std::memory_order_acquire);
  if ((before & 1U) != 0U) return false;
  if (before == consumedSequence_.load(std::memory_order_acquire)) return false;

  HostTimelineState state;
  bool present = false;
  state.playing = playing_.load(std::memory_order_relaxed);
  const auto seconds = load(seconds_, present);
  state.hasSeconds = present;
  state.seconds = present ? seconds : 0.0;
  const auto beats = load(beats_, present);
  state.hasBeats = present;
  state.beats = present ? beats : 0.0;
  const auto tempo = load(tempo_, present);
  state.hasTempo = present;
  state.tempo = present ? tempo : 120.0;
  state.loopActive = loopActive_.load(std::memory_order_relaxed);
  const auto loopStartSeconds = load(loopStartSeconds_, present);
  state.loopHasSeconds = present;
  state.loopStartSeconds = loopStartSeconds;
  const auto loopEndSeconds = load(loopEndSeconds_, present);
  state.loopHasSeconds = state.loopHasSeconds && present;
  state.loopEndSeconds = loopEndSeconds;
  const auto loopStartBeats = load(loopStartBeats_, present);
  state.loopHasBeats = present;
  state.loopStartBeats = loopStartBeats;
  const auto loopEndBeats = load(loopEndBeats_, present);
  state.loopHasBeats = state.loopHasBeats && present;
  state.loopEndBeats = loopEndBeats;
  state.hasTimeSignature = meter_.present.load(std::memory_order_relaxed);
  state.numerator = meter_.numerator.load(std::memory_order_relaxed);
  state.denominator = meter_.denominator.load(std::memory_order_relaxed);

  // The fence, not the second load, is what makes this safe: an acquire load stops later
  // operations from moving earlier, and what is needed here is the opposite -- the field
  // loads above must not be delayed past the sequence re-read, or they could take their
  // values from a publish that started after it. Observed as a mixed snapshot on arm64
  // before this fence existed (CTest caught it; a single run almost never does).
  std::atomic_thread_fence(std::memory_order_acquire);
  const auto after = sequence_.load(std::memory_order_relaxed);
  if (before != after) {
    tornReads_.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  consumedSequence_.store(after, std::memory_order_release);
  out = state;
  return true;
}

bool HostTransportPublication::shouldNotifyOwner() const noexcept {
  return notifyPending_.load(std::memory_order_acquire);
}

void HostTransportPublication::clearNotify() noexcept {
  notifyPending_.store(false, std::memory_order_release);
}

}  // namespace seam::clap_editor
