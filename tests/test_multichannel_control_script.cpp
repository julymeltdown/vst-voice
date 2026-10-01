#include "test_framework.hpp"

#include "seam/core/error.hpp"
#include "seam/domain/routing.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using seam::rendering::MultichannelPlaybackFeeder;
using seam::rendering::PlaybackLoop;
using seam::rendering::PlaybackPoint;
using Script = seam::rendering::MultichannelPlaybackFeeder::ControlScript;
using Timeline = std::shared_ptr<const seam::rendering::RoutedPlaybackTimeline>;
using Frame = seam::time::SampleFrame;

constexpr std::uint32_t kRate = 48000U;
constexpr std::uint8_t kChannels = 2U;

seam::domain::ProjectRouting masterRouting(std::uint8_t channels) {
  const seam::domain::BusId masterId{1U};
  return seam::domain::ProjectRouting{
      .deviceOutputChannels = channels,
      .masterBus = masterId,
      .buses = {seam::domain::AudioBus{.id = masterId, .name = "Master", .channelCount = channels}},
      .sends = {},
      .deviceRoutes = {seam::domain::DeviceOutputRoute{
          .sourceBus = masterId, .matrix = seam::domain::RoutingMatrix::identity(channels)}},
  };
}

// A timeline of `frames` frames of one constant value, straight to the master bus.
Timeline timelineOf(std::size_t frames, std::uint32_t sampleRate = kRate,
                    std::uint8_t channels = kChannels) {
  auto pcm = std::make_shared<seam::rendering::RoutedPcm>();
  pcm->sampleRate = sampleRate;
  pcm->startFrame = 0;
  pcm->channelCount = channels;
  pcm->interleavedSamples.assign(frames * channels, 0.5F);
  auto timeline = std::make_shared<seam::rendering::RoutedPlaybackTimeline>(sampleRate);
  std::vector<seam::rendering::RoutedPlaybackClip> clips;
  clips.push_back(seam::rendering::RoutedPlaybackClip{
      .id = "clip",
      .pcm = std::move(pcm),
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::identity(channels)},
  });
  CHECK(timeline->configure(masterRouting(channels), std::move(clips)));
  return timeline;
}

// A feeder with a small block, and a control queue that holds `queueCapacity` commands.
struct Rig final {
  explicit Rig(std::size_t queueCapacity = 8U)
      : ring(256U, kChannels), feeder(ring, kRate, kChannels, 16U, queueCapacity) {}
  seam::rendering::SpscInterleavedAudioRingBuffer ring;
  MultichannelPlaybackFeeder feeder;
};

void run(Rig& rig) { static_cast<void>(rig.feeder.feedOnce()); }

}  // namespace

TEST_CASE("a control script reaches the feeder whole, and every kind of command in it takes effect") {
  Rig rig;
  auto& feeder = rig.feeder;
  Script script;
  script.timeline(timelineOf(100))
      .loop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 14})
      .seek(12)
      .playing(false);
  CHECK(script.size() == 4U);
  CHECK(feeder.apply(std::move(script)));
  // Queued, not applied: the feeder applies its commands when it runs.
  CHECK(feeder.stats().controlCommands == 0U);
  CHECK(feeder.playhead() == 0);
  CHECK(feeder.feedOnce() == 0U);
  CHECK(feeder.stats().controlCommands == 4U);
  CHECK(feeder.playhead() == 12);
  CHECK(!feeder.playing());
  Script play;
  play.playing(true);
  CHECK(feeder.apply(std::move(play)));
  // A block of the looped timeline: the loop, the timeline and the play all came with the script.
  CHECK(feeder.feedOnce() == 16U);
  CHECK(feeder.playing());
  CHECK(feeder.stats().loopWraps > 0U);
  CHECK(feeder.playhead() >= 10 && feeder.playhead() < 14);
}

TEST_CASE("the commands of a control script are consumed in the order they were added") {
  Rig rig;
  Script first;
  first.seek(50).seek(7);
  CHECK(rig.feeder.apply(std::move(first)));
  run(rig);
  CHECK(rig.feeder.playhead() == 7);
  Script second;
  second.seek(7).seek(50);
  CHECK(rig.feeder.apply(std::move(second)));
  run(rig);
  CHECK(rig.feeder.playhead() == 50);
  Script off;
  off.playing(true).playing(false);
  CHECK(rig.feeder.apply(std::move(off)));
  run(rig);
  CHECK(!rig.feeder.playing());
  Script on;
  on.playing(false).playing(true);
  CHECK(rig.feeder.apply(std::move(on)));
  run(rig);
  CHECK(rig.feeder.playing());
}

TEST_CASE("a control script that does not fit in what is left of the queue queues nothing") {
  Rig rig{4U};
  auto& feeder = rig.feeder;
  for (int index = 0; index < 3; ++index) CHECK(feeder.setPlaying(false));  // 3 of 4 slots taken
  Script two;
  two.seek(5).seek(6);
  const auto refused = feeder.apply(std::move(two));
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  // Each of its commands is one the queue turned away, as a single command would be.
  CHECK(feeder.stats().rejectedCommands == 2U);
  // None of it reached the queue: the feeder finds the three commands that were there, no seek.
  run(rig);
  CHECK(feeder.stats().controlCommands == 3U);
  CHECK(feeder.acknowledgedCommands() == 3U);
  CHECK(feeder.playhead() == 0);
}

TEST_CASE("a control script that exactly fills what is left of the queue is queued") {
  Rig rig{4U};
  auto& feeder = rig.feeder;
  CHECK(feeder.setPlaying(false));  // one of four taken, three free
  Script three;
  three.seek(1).seek(2).seek(3);
  CHECK(feeder.apply(std::move(three)));
  // Now it is full: a single command and a one-command script are both turned away.
  CHECK(!feeder.setPlaying(true));
  CHECK(feeder.stats().rejectedCommands == 1U);
  Script one;
  one.seek(4);
  CHECK(!feeder.apply(std::move(one)));
  CHECK(feeder.stats().rejectedCommands == 2U);
  run(rig);
  CHECK(feeder.stats().controlCommands == 4U);
  CHECK(feeder.playhead() == 3);
}

TEST_CASE("control scripts keep their order as the queue wraps around") {
  Rig rig{4U};  // five slots: the positions wrap every five commands
  std::uint64_t consumed = 0U;
  for (int round = 0; round < 12; ++round) {
    const int size = 1 + round % 4;
    Script script;
    for (int index = 0; index < size; ++index) script.seek(100 * round + index);
    CHECK(rig.feeder.apply(std::move(script)));
    run(rig);
    consumed += static_cast<std::uint64_t>(size);
    CHECK(rig.feeder.acknowledgedCommands() == consumed);
    CHECK(rig.feeder.playhead() == 100 * round + size - 1);
  }
}

TEST_CASE("a control script with an invalid command queues none of its commands") {
  Rig rig;
  auto& feeder = rig.feeder;
  const auto refusedAsInvalid = [&feeder](Script script) {
    const auto result = feeder.apply(std::move(script));
    return !result && result.error().code == seam::core::ErrorCode::InvalidArgument;
  };
  Script emptyLoop;
  emptyLoop.seek(5).loop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 10});
  CHECK(refusedAsInvalid(std::move(emptyLoop)));
  Script backwardsLoop;
  backwardsLoop.seek(5).loop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 9});
  CHECK(refusedAsInvalid(std::move(backwardsLoop)));
  Script noTimeline;
  noTimeline.seek(5).timeline(nullptr);
  CHECK(refusedAsInvalid(std::move(noTimeline)));
  Script wrongRate;
  wrongRate.seek(5).timeline(timelineOf(100, 44100U));
  CHECK(refusedAsInvalid(std::move(wrongRate)));
  Script wrongChannels;
  wrongChannels.seek(5).timeline(timelineOf(100, kRate, 1U));
  CHECK(refusedAsInvalid(std::move(wrongChannels)));
  // The invalid command is the last one in each of these: the valid seek before it was not queued.
  CHECK(feeder.stats().rejectedCommands == 0U);
  run(rig);
  CHECK(feeder.stats().controlCommands == 0U);
  CHECK(feeder.playhead() == 0);
  // A loop that is not enabled is not checked, as with setLoop, and a loop one frame long is valid.
  Script disabled;
  disabled.loop(PlaybackLoop{.enabled = false, .startFrame = 10, .endFrame = 10});
  CHECK(feeder.apply(std::move(disabled)));
  Script shortest;
  shortest.loop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 11});
  CHECK(feeder.apply(std::move(shortest)));
}

TEST_CASE("a single control command is checked by the rules that check the commands of a script") {
  Rig rig;
  auto& feeder = rig.feeder;
  const auto invalid = [](const seam::core::Result<void>& result) {
    return !result && result.error().code == seam::core::ErrorCode::InvalidArgument;
  };
  CHECK(invalid(feeder.setTimeline(nullptr)));
  CHECK(invalid(feeder.setTimeline(timelineOf(100, 44100U))));
  CHECK(invalid(feeder.setTimeline(timelineOf(100, kRate, 1U))));
  CHECK(invalid(feeder.setLoop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 10})));
  CHECK(invalid(feeder.setLoop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 9})));
  // Nothing was queued, and none of it was a queue that was full.
  CHECK(feeder.stats().rejectedCommands == 0U);
  run(rig);
  CHECK(feeder.stats().controlCommands == 0U);
  // The commands that are valid are queued, in order.
  CHECK(feeder.setLoop(PlaybackLoop{.enabled = false, .startFrame = 10, .endFrame = 10}));
  CHECK(feeder.setLoop(PlaybackLoop{.enabled = true, .startFrame = 10, .endFrame = 11}));
  CHECK(feeder.setTimeline(timelineOf(100)));
  CHECK(feeder.seek(3));
  CHECK(feeder.setPlaying(false));
  run(rig);
  CHECK(feeder.stats().controlCommands == 5U);
  CHECK(feeder.acknowledgedCommands() == 5U);
  CHECK(feeder.playhead() == 3);
}

TEST_CASE("a control script larger than the whole queue is refused as invalid and changes nothing") {
  Rig rig{4U};
  auto& feeder = rig.feeder;
  Script five;
  for (int index = 0; index < 5; ++index) five.seek(index + 1);
  const auto refused = feeder.apply(std::move(five));
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::InvalidArgument);
  // It could never have fitted, so it is not a queue that was full: nothing is counted.
  CHECK(feeder.stats().rejectedCommands == 0U);
  run(rig);
  CHECK(feeder.stats().controlCommands == 0U);
  // One of exactly the queue's size is queued.
  Script four;
  for (int index = 0; index < 4; ++index) four.seek(index + 1);
  CHECK(feeder.apply(std::move(four)));
  run(rig);
  CHECK(feeder.stats().controlCommands == 4U);
  CHECK(feeder.playhead() == 4);
}

TEST_CASE("an empty control script is accepted and sends the feeder nothing") {
  Rig rig;
  CHECK(rig.feeder.apply(Script{}));
  run(rig);
  CHECK(rig.feeder.stats().controlCommands == 0U);
  CHECK(rig.feeder.stats().rejectedCommands == 0U);
  CHECK(rig.feeder.acknowledgedCommands() == 0U);
}

TEST_CASE("the feeder acknowledges the commands it has consumed and only those") {
  Rig rig;
  auto& feeder = rig.feeder;
  CHECK(feeder.acknowledgedCommands() == 0U);
  CHECK(feeder.setPlaying(false));
  CHECK(feeder.seek(3));
  Script script;
  script.seek(9).playing(false);
  CHECK(feeder.apply(std::move(script)));
  // Queued is not consumed.
  CHECK(feeder.acknowledgedCommands() == 0U);
  run(rig);
  CHECK(feeder.acknowledgedCommands() == 4U);
  CHECK(feeder.playhead() == 9);
  run(rig);
  CHECK(feeder.acknowledgedCommands() == 4U);
  Script more;
  more.seek(20).seek(21);
  CHECK(feeder.apply(std::move(more)));
  CHECK(feeder.acknowledgedCommands() == 4U);
  run(rig);
  CHECK(feeder.acknowledgedCommands() == 6U);
  CHECK(feeder.playhead() == 21);
}

TEST_CASE("the feeder acknowledges a command whose effect it has published while it waits for the ring to be reset") {
  Rig rig;
  auto& feeder = rig.feeder;
  Script start;
  start.timeline(timelineOf(1000)).seek(0).playing(true);
  CHECK(feeder.apply(std::move(start)));
  CHECK(feeder.feedOnce() == 16U);  // audio is in the ring now
  Script jump;
  jump.seek(500);
  CHECK(feeder.apply(std::move(jump)));
  // Nothing reads the ring, so the reset that the seek asked for is never acknowledged: the feeder
  // is still waiting for it. The seek has been applied and published all the same.
  CHECK(feeder.feedOnce() == 0U);
  CHECK(feeder.stats().resetWaits > 0U);
  CHECK(feeder.playhead() == 500);
  CHECK(feeder.acknowledgedCommands() == 4U);
}

TEST_CASE("the acknowledgement never gets ahead of the playhead it covers") {
  // One thread queues seek(k) as command number k, one feeds, one watches. Consuming command k
  // leaves the playhead at k, so a count that has reached k with the playhead still before k has
  // been acknowledged ahead of the state it promises.
  Rig rig{64U};
  constexpr std::uint64_t kCommands = 200000U;
  std::atomic<bool> finished{false};
  std::atomic<std::uint64_t> violations{0U};
  std::jthread feeding([&rig, &finished](std::stop_token stop) {
    while (!stop.stop_requested() && !finished.load(std::memory_order_acquire)) {
      static_cast<void>(rig.feeder.feedOnce());
    }
  });
  std::jthread watching([&rig, &finished, &violations](std::stop_token stop) {
    while (!stop.stop_requested() && !finished.load(std::memory_order_acquire)) {
      const auto acknowledged = rig.feeder.acknowledgedCommands();
      const auto playhead = rig.feeder.playhead();
      if (playhead < static_cast<Frame>(acknowledged)) {
        violations.fetch_add(1U, std::memory_order_relaxed);
      }
    }
  });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  for (std::uint64_t command = 1U; command <= kCommands; ++command) {
    while (!rig.feeder.seek(static_cast<Frame>(command))) {
      if (std::chrono::steady_clock::now() > deadline) break;
      std::this_thread::yield();
    }
  }
  while (rig.feeder.acknowledgedCommands() < kCommands &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  finished.store(true, std::memory_order_release);
  feeding.join();
  watching.join();
  CHECK(rig.feeder.acknowledgedCommands() == kCommands);
  CHECK(violations.load() == 0U);
}

namespace {

struct ProjectionRow final {
  const char* name;
  PlaybackPoint start;
  std::function<void(Script&)> build;
  PlaybackPoint expected;
  bool withTimeline;
};

PlaybackPoint point(bool playing, Frame playhead) {
  return PlaybackPoint{.playing = playing, .playhead = playhead};
}

PlaybackLoop loopOf(Frame start, Frame end, bool enabled = true) {
  return PlaybackLoop{.enabled = enabled, .startFrame = start, .endFrame = end};
}

std::vector<ProjectionRow> projectionRows() {
  return {
      {"a seek sets the playhead", point(false, 7),
       [](Script& s) { s.seek(40); }, point(false, 40), false},
      {"a play sets the flag and keeps the playhead", point(false, 7),
       [](Script& s) { s.playing(true); }, point(true, 7), false},
      {"a pause clears the flag and keeps the playhead", point(true, 7),
       [](Script& s) { s.playing(false); }, point(false, 7), false},
      {"the last of two seeks wins", point(false, 7),
       [](Script& s) { s.seek(3).seek(9); }, point(false, 9), false},
      {"a loop that is not enabled leaves the playhead where it is", point(false, 50),
       [](Script& s) { s.loop(loopOf(10, 20, false)); }, point(false, 50), false},
      {"a loop that ends before the playhead moves it to the start of the loop", point(false, 50),
       [](Script& s) { s.loop(loopOf(10, 20)); }, point(false, 10), false},
      {"a loop that ends at the playhead moves it to the start of the loop", point(false, 50),
       [](Script& s) { s.loop(loopOf(10, 50)); }, point(false, 10), false},
      {"a loop that ends after the playhead leaves it", point(false, 50),
       [](Script& s) { s.loop(loopOf(10, 51)); }, point(false, 50), false},
      {"a seek after a loop is not moved by it", point(false, 5),
       [](Script& s) { s.loop(loopOf(10, 14)).seek(50); }, point(false, 50), false},
      {"a seek before a loop is moved by it", point(false, 5),
       [](Script& s) { s.seek(50).loop(loopOf(10, 14)); }, point(false, 10), false},
      {"a timeline that ends before the playhead pulls it back to its end", point(false, 50),
       [](Script& s) { s.timeline(timelineOf(20)); }, point(false, 20), true},
      {"a timeline that ends at the playhead leaves it", point(false, 20),
       [](Script& s) { s.timeline(timelineOf(20)); }, point(false, 20), true},
      {"a timeline that ends after the playhead leaves it", point(false, 10),
       [](Script& s) { s.timeline(timelineOf(100)); }, point(false, 10), true},
      {"a seek before a timeline is pulled back by it", point(false, 5),
       [](Script& s) { s.seek(50).timeline(timelineOf(20)); }, point(false, 20), true},
      {"a seek after a timeline is not pulled back by it", point(false, 5),
       [](Script& s) { s.timeline(timelineOf(20)).seek(50); }, point(false, 50), true},
      {"a timeline, then a loop, then a seek leaves the seek", point(false, 60),
       [](Script& s) { s.timeline(timelineOf(20)).loop(loopOf(5, 10)).seek(30); },
       point(false, 30), true},
      {"a timeline then a loop can move the playhead twice", point(false, 60),
       [](Script& s) { s.timeline(timelineOf(20)).loop(loopOf(5, 10)); }, point(false, 5), true},
  };
}

// A feeder whose state is `start`, and that has a long timeline when `withTimeline` is set. A
// timeline is only put in for rows that are not playing, so nothing plays on past the start.
void position(Rig& rig, const PlaybackPoint& start, bool withTimeline) {
  Script setup;
  if (withTimeline) setup.timeline(timelineOf(1000));
  setup.seek(start.playhead).playing(start.playing);
  CHECK(rig.feeder.apply(std::move(setup)));
  run(rig);
  CHECK(rig.feeder.playhead() == start.playhead);
  CHECK(rig.feeder.playing() == start.playing);
}

void expectPoint(const PlaybackPoint& actual, const PlaybackPoint& expected, const std::string& what) {
  seam::test::check(actual.playing == expected.playing && actual.playhead == expected.playhead,
                    what + ": got playing=" + std::to_string(actual.playing) + " playhead=" +
                        std::to_string(actual.playhead) + ", wanted playing=" +
                        std::to_string(expected.playing) + " playhead=" +
                        std::to_string(expected.playhead),
                    __FILE__, __LINE__);
}

}  // namespace

TEST_CASE("a control script says what the feeder will be doing once it has consumed it") {
  for (const auto& row : projectionRows()) {
    Script script;
    row.build(script);
    // What the script says about itself...
    expectPoint(script.projectedFrom(row.start), row.expected, std::string{"projection: "} + row.name);
    // ...is what the feeder does with it.
    Rig rig;
    position(rig, row.start, row.withTimeline);
    CHECK(rig.feeder.apply(std::move(script)));
    run(rig);
    expectPoint(PlaybackPoint{.playing = rig.feeder.playing(), .playhead = rig.feeder.playhead()},
                row.expected, std::string{"feeder: "} + row.name);
  }
}

TEST_CASE("the projection of random control scripts is what the feeder does with them") {
  // A fixed sequence of pseudo-random scripts, so a failure can be repeated. One feeder keeps a
  // timeline and only ever pauses (so that nothing plays on between scripts); the other never has
  // a timeline, which lets it play and pause freely.
  std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
  const auto next = [&seed](std::uint64_t bound) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::int64_t>((seed >> 33U) % bound);
  };
  const std::array<Timeline, 4> timelines{timelineOf(20), timelineOf(60), timelineOf(100),
                                         timelineOf(130)};
  for (const bool withTimeline : {true, false}) {
    Rig rig{32U};
    position(rig, point(false, 0), withTimeline);
    for (int round = 0; round < 1500; ++round) {
      Script script;
      const auto commands = 1 + next(6);
      for (std::int64_t command = 0; command < commands; ++command) {
        switch (next(4)) {
          case 0:
            if (withTimeline) script.timeline(timelines[static_cast<std::size_t>(next(4))]);
            else script.seek(next(130));
            break;
          case 1: {
            const auto start = next(80);
            // One draw per statement: the order of evaluation inside a call is not specified.
            const auto length = 1 + next(40);
            const bool enabled = next(4) != 0;
            script.loop(loopOf(start, start + length, enabled));
            break;
          }
          case 2:
            script.seek(next(130));
            break;
          default:
            script.playing(!withTimeline && next(2) != 0);
            break;
        }
      }
      const auto before =
          PlaybackPoint{.playing = rig.feeder.playing(), .playhead = rig.feeder.playhead()};
      const auto projected = script.projectedFrom(before);
      CHECK(rig.feeder.apply(std::move(script)));
      run(rig);
      expectPoint(PlaybackPoint{.playing = rig.feeder.playing(), .playhead = rig.feeder.playhead()},
                  projected, "round " + std::to_string(round));
    }
  }
}

TEST_CASE("a_feeder_puts_only_the_audio_in_the_ring_and_not_the_silence_that_would_fill_its_last_block") {
  Rig rig;
  Script script;
  script.timeline(timelineOf(40)).playing(true);
  CHECK(rig.feeder.apply(std::move(script)));
  CHECK(rig.feeder.feedOnce() == 16U);
  CHECK(rig.feeder.feedOnce() == 16U);
  // 40 frames are two blocks and half of a third. The last block holds 8 frames of audio and the
  // ring gets those 8: the playhead accounts for every frame the ring holds.
  CHECK(rig.feeder.feedOnce() == 8U);
  CHECK(rig.feeder.playhead() == 40);
  CHECK(!rig.feeder.playing());
  CHECK(rig.ring.availableReadFrames() == 40U);
  CHECK(rig.feeder.feedOnce() == 0U);
  CHECK(rig.ring.availableReadFrames() == 40U);
}

TEST_CASE("a_script_says_whether_it_puts_the_playhead_somewhere_itself") {
  CHECK(!Script{}.seeks());
  CHECK(!Script{}.playing(true).loop(loopOf(0, 10, true)).seeks());
  CHECK(Script{}.seek(0).seeks());
  CHECK(Script{}.playing(false).seek(40).playing(true).seeks());
}

TEST_CASE("a_ring_says_whether_a_reset_waits_for_its_consumer") {
  seam::rendering::SpscInterleavedAudioRingBuffer ring(64U, kChannels);
  CHECK(!ring.resetPending());
  const auto epoch = ring.requestConsumerReset();
  CHECK(ring.resetPending());
  CHECK(!ring.resetAcknowledged(epoch));
  CHECK(ring.serviceResetRequest());
  CHECK(!ring.resetPending());
  CHECK(ring.resetAcknowledged(epoch));
  // A second request is pending again, whether or not the first was answered.
  static_cast<void>(ring.requestConsumerReset());
  CHECK(ring.resetPending());
  CHECK(ring.serviceResetRequest());
  CHECK(!ring.resetPending());
}

namespace {

using Ring = seam::rendering::SpscInterleavedAudioRingBuffer;

// Frames of one value each, and the place in the audio of each of them: first, first + 1, and so on.
struct Block final {
  std::vector<float> samples;
  std::vector<std::int64_t> places;
};

Block blockOf(std::int64_t first, std::size_t frames) {
  Block block;
  block.samples.assign(frames * kChannels, 0.5F);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    block.places.push_back(first + static_cast<std::int64_t>(frame));
  }
  return block;
}

std::size_t put(Ring& ring, const Block& block) {
  return ring.writeFrames(block.samples, block.places);
}

std::size_t take(Ring& ring, std::size_t frames) {
  std::vector<float> output(frames * kChannels);
  return ring.readFrames(output);
}

// The place in the audio of each frame the consumer finds in the ring, in the order it plays them.
std::vector<std::int64_t> placesInRing(Rig& rig) {
  std::vector<std::int64_t> places;
  std::vector<float> frame(kChannels);
  while (const auto place = rig.ring.nextFramePosition()) {
    places.push_back(*place);
    static_cast<void>(rig.ring.readFrames(frame));
  }
  return places;
}

void appendPlaces(std::vector<std::int64_t>& places, std::int64_t first, std::int64_t end) {
  for (auto place = first; place < end; ++place) places.push_back(place);
}

}  // namespace

TEST_CASE("a_ring_says_where_in_the_audio_the_frame_is_that_its_consumer_plays_next") {
  Ring ring(8U, kChannels);
  CHECK(!ring.nextFramePosition());
  CHECK(put(ring, blockOf(100, 5)) == 5U);
  CHECK(ring.nextFramePosition() == 100);
  CHECK(take(ring, 3U) == 3U);
  CHECK(ring.nextFramePosition() == 103);
  // Frames that follow each other in the ring need not follow each other in the audio: a loop that
  // has wrapped and a seek put the next frames anywhere.
  CHECK(put(ring, blockOf(10, 4)) == 4U);
  CHECK(take(ring, 2U) == 2U);
  CHECK(ring.nextFramePosition() == 10);
  // Nothing is next once the last frame has been played, though its slot still holds its place.
  CHECK(take(ring, 8U) == 4U);
  CHECK(!ring.nextFramePosition());
}

TEST_CASE("a_ring_goes_on_saying_where_its_next_frame_is_as_its_slots_come_round_again") {
  Ring ring(8U, kChannels);
  std::int64_t place = 1000;
  for (int round = 0; round < 60; ++round) {
    const auto frames = static_cast<std::size_t>(1 + round % 8);
    CHECK(put(ring, blockOf(place, frames)) == frames);
    CHECK(ring.nextFramePosition() == place);
    CHECK(take(ring, frames - 1U) == frames - 1U);
    CHECK(ring.nextFramePosition() == place + static_cast<std::int64_t>(frames) - 1);
    CHECK(take(ring, 1U) == 1U);
    CHECK(!ring.nextFramePosition());
    place += 17;
  }
}

TEST_CASE("frames_written_without_a_place_have_none_and_a_block_with_too_few_places_is_refused") {
  Ring ring(8U, kChannels);
  const std::vector<float> plain(3U * kChannels, 0.25F);
  CHECK(ring.writeFrames(plain) == 3U);
  CHECK(!ring.nextFramePosition());
  // A place for every frame, or nothing is written.
  auto block = blockOf(50, 4);
  block.places.pop_back();
  CHECK(ring.writeFrames(block.samples, block.places) == 0U);
  block.places.assign(5U, std::int64_t{7});
  CHECK(ring.writeFrames(block.samples, block.places) == 0U);
  CHECK(ring.availableReadFrames() == 3U);
  // Frames that come after have theirs, and they are found once those that have none are played.
  CHECK(put(ring, blockOf(50, 2)) == 2U);
  CHECK(!ring.nextFramePosition());
  CHECK(take(ring, 3U) == 3U);
  CHECK(ring.nextFramePosition() == 50);
}

TEST_CASE("a_ring_whose_consumer_answered_a_reset_has_no_next_frame_until_something_is_written") {
  Ring ring(8U, kChannels);
  CHECK(put(ring, blockOf(20, 6)) == 6U);
  CHECK(take(ring, 1U) == 1U);
  const auto epoch = ring.requestConsumerReset();
  // Until the consumer answers, the ring holds the frames it was asked to drop.
  CHECK(ring.nextFramePosition() == 21);
  CHECK(ring.serviceResetRequest());
  CHECK(ring.resetAcknowledged(epoch));
  CHECK(ring.availableReadFrames() == 0U);
  CHECK(!ring.nextFramePosition());
  CHECK(put(ring, blockOf(300, 2)) == 2U);
  CHECK(ring.nextFramePosition() == 300);
}

TEST_CASE("a_reader_that_the_consumer_and_the_producer_move_under_gets_the_place_of_the_next_frame") {
  // The ring holds eight frames, places 0 to 7. At the point the reader has reached (1: it has the
  // count of frames read, 2: it has the count of frames written too, 3: it has a place) the consumer
  // plays some frames and the producer writes as many more, over the slots they left. The slot the
  // reader is at has then been written again, with a place that belongs to a later frame, and the
  // reader must not answer with it: it starts again and finds the frame that is next now. When the
  // consumer has played every frame it had, the frames that the producer has written since are what
  // the ring holds, and the reader must not take the ring for empty.
  for (const std::size_t frames : {std::size_t{3}, std::size_t{8}}) {
    for (const int point : {1, 2, 3}) {
      Ring ring(8U, kChannels);
      CHECK(put(ring, blockOf(0, 8)) == 8U);
      bool moved = false;
      std::size_t played = 0U;
      std::size_t written = 0U;
      ring.setSnapshotProbe([&](int at) {
        if (at != point || moved) return;
        moved = true;
        played = take(ring, frames);
        written = put(ring, blockOf(8, frames));
      });
      const auto next = ring.nextFramePosition();
      CHECK(moved);
      CHECK(played == frames);
      CHECK(written == frames);
      CHECK(next == static_cast<std::int64_t>(frames));
    }
  }
}

TEST_CASE("a_reader_that_a_reset_moves_under_does_not_answer_with_a_frame_that_was_dropped") {
  for (const int point : {1, 2, 3}) {
    Ring ring(8U, kChannels);
    CHECK(put(ring, blockOf(40, 5)) == 5U);
    bool moved = false;
    ring.setSnapshotProbe([&](int at) {
      if (at != point || moved) return;
      moved = true;
      static_cast<void>(ring.requestConsumerReset());
      static_cast<void>(ring.serviceResetRequest());
    });
    const auto next = ring.nextFramePosition();
    CHECK(moved);
    CHECK(!next);
  }
}

TEST_CASE("a_reader_that_the_consumer_never_leaves_alone_gives_up_without_an_answer") {
  Ring ring(8U, kChannels);
  CHECK(put(ring, blockOf(0, 8)) == 8U);
  std::int64_t place = 8;
  int attempts = 0;
  // Each time the reader has a place the consumer has played a frame and the producer written one.
  ring.setSnapshotProbe([&](int at) {
    if (at != 3) return;
    ++attempts;
    static_cast<void>(take(ring, 1U));
    static_cast<void>(put(ring, blockOf(place++, 1U)));
  });
  CHECK(!ring.nextFramePosition());
  CHECK(attempts > 1);
  CHECK(attempts <= 100000);
}

TEST_CASE("a_feeder_writes_each_frame_to_the_ring_with_its_place_in_the_audio") {
  Rig rig;
  Script script;
  // The loop begins after the audio does: the first pass plays up to the end of the loop, and each
  // pass after that plays the whole of it.
  script.timeline(timelineOf(100)).loop(loopOf(10, 20)).playing(true);
  CHECK(rig.feeder.apply(std::move(script)));
  CHECK(rig.feeder.feedOnce() == 16U);
  CHECK(rig.feeder.feedOnce() == 16U);
  std::vector<std::int64_t> expected;
  appendPlaces(expected, 0, 16);   // The first block.
  appendPlaces(expected, 16, 20);  // The rest of the first pass.
  appendPlaces(expected, 10, 20);  // A whole pass.
  appendPlaces(expected, 10, 12);  // The start of the next.
  CHECK(placesInRing(rig) == expected);
}

TEST_CASE("a_feeder_that_reaches_the_end_of_the_audio_has_written_the_place_of_every_frame_up_to_it") {
  for (const Frame start : {Frame{0}, Frame{30}}) {
    Rig rig;
    Script script;
    script.timeline(timelineOf(40)).seek(start).playing(true);
    CHECK(rig.feeder.apply(std::move(script)));
    while (rig.feeder.feedOnce() > 0U) {
    }
    std::vector<std::int64_t> expected;
    appendPlaces(expected, start, 40);
    CHECK(placesInRing(rig) == expected);
  }
}
