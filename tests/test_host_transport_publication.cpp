#include "test_framework.hpp"

#include "seam/clap_editor/host_transport_publication.hpp"

#include <atomic>
#include <thread>

namespace {

using seam::clap_editor::HostTimelineState;
using seam::clap_editor::HostTransportPublication;

HostTimelineState transport(double beats, double tempo) {
  HostTimelineState state;
  state.playing = true;
  state.hasBeats = true;
  state.beats = beats;
  state.hasTempo = true;
  state.tempo = tempo;
  return state;
}

}  // namespace

TEST_CASE("a transport publication forwards the first report and milestones only") {
  HostTransportPublication publication;
  HostTimelineState consumed;
  CHECK(!publication.tryConsume(consumed));

  // The first report is always forwarded: without it the host has said nothing.
  CHECK(publication.publish(transport(0.0, 120.0)));
  CHECK(publication.requestCallbackIfNeeded());
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.hasBeats);
  CHECK(consumed.beats == 0.0);
  CHECK(consumed.hasTempo);
  CHECK(consumed.tempo == 120.0);
  CHECK(consumed.playing);

  // An advancing playhead below the declared quantum is the same musical statement.
  CHECK(!publication.publish(transport(0.1, 120.0)));
  CHECK(!publication.tryConsume(consumed));
  CHECK(publication.publish(transport(0.5, 120.0)));
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.beats == 0.5);
  // 0.6 is only 0.1 past the last forwarded position, so it is not forwarded even though
  // it is 0.5 past the first report.
  CHECK(!publication.publish(transport(0.6, 120.0)));
  CHECK(!publication.tryConsume(consumed));

  // A tempo change is forwarded immediately, wherever the playhead is.
  CHECK(publication.publish(transport(0.6, 90.0)));
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.tempo == 90.0);

  // So is a meter change, a loop change and a transport stop.
  auto metered = transport(0.7, 90.0);
  metered.hasTimeSignature = true;
  metered.numerator = 3U;
  metered.denominator = 4U;
  CHECK(publication.publish(metered));
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.hasTimeSignature);
  CHECK(consumed.numerator == 3U);

  auto looping = metered;
  looping.loopActive = true;
  looping.loopHasBeats = true;
  looping.loopStartBeats = 0.0;
  looping.loopEndBeats = 4.0;
  CHECK(publication.publish(looping));
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.loopActive);
  CHECK(consumed.loopHasBeats);
  CHECK(consumed.loopStartBeats == 0.0);
  CHECK(consumed.loopEndBeats == 4.0);

  auto stopped = looping;
  stopped.playing = false;
  CHECK(publication.publish(stopped));
  CHECK(publication.tryConsume(consumed));
  CHECK(!consumed.playing);

  // A host that reports only seconds still advances at its own quantum.
  HostTimelineState timed;
  timed.hasSeconds = true;
  timed.seconds = 0.0;
  timed.loopActive = true;
  timed.loopHasSeconds = true;
  CHECK(publication.publish(timed));
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.hasSeconds);
  CHECK(!consumed.hasBeats);
  timed.seconds = 0.1;
  CHECK(!publication.publish(timed));
  timed.seconds = 0.25;
  CHECK(publication.publish(timed));
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.seconds == 0.25);
}

TEST_CASE("a consumed snapshot is not delivered twice") {
  HostTransportPublication publication;
  HostTimelineState consumed;
  CHECK(publication.publish(transport(1.0, 120.0)));
  CHECK(publication.tryConsume(consumed));
  CHECK(!publication.tryConsume(consumed));
  CHECK(publication.publishedCount() == 1U);
  CHECK(publication.tornReadCount() == 0U);
}

TEST_CASE("the owner is notified once per undrained report") {
  HostTransportPublication publication;
  CHECK(publication.publish(transport(0.0, 120.0)));
  CHECK(publication.requestCallbackIfNeeded());
  CHECK(publication.shouldNotifyOwner());
  // A second report before the owner has drained does not ask again; the pending request
  // already covers both, and the drain preserves both observations in order.
  CHECK(publication.publish(transport(1.0, 120.0)));
  CHECK(!publication.requestCallbackIfNeeded());
  CHECK(publication.shouldNotifyOwner());
  publication.clearNotify();
  CHECK(!publication.shouldNotifyOwner());
  HostTimelineState consumed;
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.beats == 0.0);
  CHECK(publication.tryConsume(consumed));
  CHECK(consumed.beats == 1.0);
  CHECK(!publication.tryConsume(consumed));
  CHECK(publication.publish(transport(2.0, 120.0)));
  CHECK(publication.requestCallbackIfNeeded());
  CHECK(publication.shouldNotifyOwner());
}

TEST_CASE("a transport queue preserves ordered reports across threads") {
  // Several rounds exercise the SPSC release/acquire hand-off on arm64 as well as x86.
  for (int round = 0; round < 3; ++round) {
  HostTransportPublication publication;
  constexpr int kReports = 2000;
  std::atomic<bool> writerDone{false};
  std::thread writer([&publication, &writerDone] {
    for (int index = 0; index < kReports; ++index) {
      const auto beats = static_cast<double>(index);
      // Tempo encodes the position, so a snapshot that mixed two reports is detectable.
      const auto forwarded = publication.publish(transport(beats, 120.0 + beats));
      static_cast<void>(forwarded);
      if (forwarded && publication.requestCallbackIfNeeded()) {
        // A real host would be asked for a callback here; the test only needs the drain to be
        // reachable, and the owner in this test drains directly.
        publication.clearNotify();
      }
    }
    writerDone.store(true, std::memory_order_release);
  });

  int consumedCount = 0;
  int inconsistent = 0;
  int expected = 0;
  HostTimelineState last;
  const auto record = [&consumedCount, &inconsistent, &last, &expected](const HostTimelineState& value) {
    ++consumedCount;
    last = value;
    if (!value.hasBeats || !value.hasTempo || value.tempo != 120.0 + value.beats ||
        value.beats != static_cast<double>(expected)) {
      ++inconsistent;
    }
    ++expected;
  };
  while (!writerDone.load(std::memory_order_acquire)) {
    HostTimelineState consumed;
    if (publication.tryConsume(consumed)) {
      record(consumed);
    } else {
      std::this_thread::yield();
    }
  }
  writer.join();
  HostTimelineState consumed;
  while (publication.tryConsume(consumed)) record(consumed);
  CHECK(inconsistent == 0);
  CHECK(consumedCount == kReports);
  CHECK(last.hasBeats);
  CHECK(last.beats == static_cast<double>(kReports - 1));
  CHECK(last.tempo == 120.0 + static_cast<double>(kReports - 1));
  CHECK(publication.publishedCount() == static_cast<std::uint64_t>(kReports));
  }
}

TEST_CASE("transport queue reports overflow as incomplete history") {
  HostTransportPublication publication;
  for (std::size_t index = 0; index <= HostTransportPublication::kCapacity; ++index) {
    CHECK(publication.publish(transport(static_cast<double>(index), 120.0), true));
  }
  CHECK(publication.overflowCount() == 1U);
  HostTimelineState state;
  for (std::size_t index = 0; index < HostTransportPublication::kCapacity; ++index) {
    CHECK(publication.tryConsume(state));
    CHECK(state.hasBeats);
    CHECK(state.beats == static_cast<double>(index));
  }
  CHECK(publication.tryConsume(state));
  CHECK(state.captureIncomplete);
  CHECK(!publication.tryConsume(state));
}
