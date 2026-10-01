#include "test_framework.hpp"

#include "seam/authoring/transport_controller.hpp"
#include "seam/authoring/render_coordinator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace {

seam::authoring::RealtimeProjectAudioPublication::ReadHandle publishAudio(
    seam::authoring::RealtimeProjectAudioPublication& publication,
    std::uint64_t revision, std::size_t frames, float offset = 0.0F,
    std::uint32_t sampleRate = 48000U) {
  seam::authoring::PublishedProjectAudio audio;
  audio.projectRevision = revision;
  audio.state = seam::authoring::RenderState::Ready;
  audio.result.sampleRate = sampleRate;
  audio.result.channelCount = 2U;
  audio.result.interleaved.resize(frames * 2U);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const auto value = offset + static_cast<float>(frame) / 1000.0F;
    audio.result.interleaved[frame * 2U] = value;
    audio.result.interleaved[frame * 2U + 1U] = value;
  }
  CHECK(publication.publish(std::move(audio)));
  return publication.acquire();
}

bool waitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout =
                   std::chrono::milliseconds{1500}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return predicate();
}

std::vector<float> readFrames(seam::authoring::TransportController& controller,
                              std::size_t frames) {
  std::vector<float> output(frames * controller.outputChannels(), -1.0F);
  static_cast<void>(controller.ringBuffer().readFrames(output));
  return output;
}

}  // namespace

TEST_CASE("transport reports unavailable playback before a successful render") {
  seam::authoring::TransportController controller;
  const auto state = controller.state();
  CHECK(!state.available);
  CHECK(!state.availabilityDiagnostic.empty());
}

TEST_CASE("transport treats revision zero as available after a successful render") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 512U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 128U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 0U, 128U)));
  const auto state = controller.state();
  CHECK(state.publishedRevision == 0U);
  CHECK(state.timelineEnd == 128);
  CHECK(state.available);
  CHECK(state.availabilityDiagnostic.empty());
}

TEST_CASE("transport_controller_stop_resets_to_project_start") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playhead > 0; }));
  CHECK(controller.stop());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return !state.playing && state.playhead == 0;
  }));
  auto output = readFrames(controller, 32U);
  CHECK(std::all_of(output.begin(), output.end(),
                    [](float value) { return value == 0.0F; }));
}

TEST_CASE("transport_controller_seek_discards_old_buffered_frames") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 512U)));
  CHECK(controller.seek(seam::time::SampleFrame{200}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 64U; }));
  auto output = readFrames(controller, 32U);
  if (std::all_of(output.begin(), output.end(),
                  [](float value) { return value == 0.0F; })) {
    CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 64U; }));
    output = readFrames(controller, 32U);
  }
  CHECK_NEAR(output.front(), 0.2, 0.002);
}

TEST_CASE("transport_controller_loop_is_sample_accurate") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 512U,
                                       .blockFrames = 16U,
                                       .watermarkFrames = 96U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 3U, 64U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 10, .endFrame = 14}));
  CHECK(controller.seek(10));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 16U; }));
  auto output = readFrames(controller, 8U);
  if (std::all_of(output.begin(), output.end(),
                  [](float value) { return value == 0.0F; })) {
    CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 16U; }));
    output = readFrames(controller, 8U);
  }
  const std::vector<float> expected{0.010F, 0.011F, 0.012F, 0.013F,
                                    0.010F, 0.011F, 0.012F, 0.013F};
  for (std::size_t frame = 0; frame < expected.size(); ++frame) {
    CHECK_NEAR(output[frame * 2U], expected[frame], 0.002);
  }
}

TEST_CASE("transport_controller_rejects_older_publication") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 64U, 0.1F)));
  CHECK(!controller.publishAudio(publishAudio(publication, 9U, 64U, 0.2F)));
  CHECK(controller.state().publishedRevision == 10U);
}

TEST_CASE("transport_controller_replacement_clamps_playhead_to_new_timeline") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 20U, 512U)));
  CHECK(controller.seek(seam::time::SampleFrame{480}));
  CHECK(waitUntil([&] { return controller.state().playhead == 480; }));

  CHECK(controller.publishAudio(publishAudio(publication, 21U, 128U)));
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.publishedRevision == 21U &&
           state.playhead == state.timelineEnd;
  }));
  const auto state = controller.state();
  CHECK(state.playhead == state.timelineEnd);
}

TEST_CASE("transport_controller_replacement_remaps_loop_to_new_timeline") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 22U, 512U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 400}));
  CHECK(controller.seek(seam::time::SampleFrame{200}));
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.loop.enabled && state.loop.startFrame == 100 &&
           state.loop.endFrame == 400 && state.playhead == 200;
  }));

  CHECK(controller.publishAudio(publishAudio(publication, 23U, 128U)));
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.publishedRevision == 23U &&
           (!state.loop.enabled ||
            (state.loop.startFrame >= 0 &&
             state.loop.endFrame <= state.timelineEnd &&
             state.loop.endFrame > state.loop.startFrame));
  }));
  const auto state = controller.state();
  CHECK(!state.loop.enabled || state.loop.endFrame <= state.timelineEnd);
}

TEST_CASE("transport_controller_reconfigure_preserves_logical_position_for_next_render") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 24U, 512U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 400}));
  CHECK(controller.seek(seam::time::SampleFrame{200}));
  CHECK(waitUntil([&] { return controller.state().playhead == 200; }));

  CHECK(controller.reconfigure(seam::authoring::TransportConfig{
      .sampleRate = 44100U,
      .outputChannels = 1U,
      .ringCapacityFrames = 2048U,
      .blockFrames = 128U,
      .watermarkFrames = 512U,
  }));
  CHECK(!controller.state().available);
  CHECK(controller.publishAudio(
      publishAudio(publication, 25U, 512U, 0.0F, 44100U)));
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.publishedRevision == 25U && state.loop.enabled &&
           state.playhead == 184 && state.loop.startFrame == 92 &&
           state.loop.endFrame == 368;
  }));
}

TEST_CASE("transport_controller_pause_yields_silence_without_queued_zero_clip") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 512U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 128U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 2048U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] {
    return controller.state().playing &&
           controller.ringBuffer().availableReadFrames() > 0U;
  }));
  CHECK(controller.pause());
  CHECK(waitUntil([&] { return !controller.state().playing; }));
  auto output = readFrames(controller, 32U);
  CHECK(std::all_of(output.begin(), output.end(),
                    [](float value) { return value == 0.0F; }));
  CHECK(controller.ringBuffer().availableReadFrames() == 0U);
}

TEST_CASE("transport_controller_reconfigure_rebuilds_ring_and_preserves_service_state") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 12U, 512U)));
  CHECK(controller.reconfigure(seam::authoring::TransportConfig{
      .sampleRate = 44100U,
      .outputChannels = 1U,
      .ringCapacityFrames = 2048U,
      .blockFrames = 128U,
      .watermarkFrames = 512U,
  }));
  CHECK(controller.config().sampleRate == 44100U);
  CHECK(controller.config().outputChannels == 1U);
  CHECK(controller.config().blockFrames == 128U);
  CHECK(controller.ringBuffer().channelCount() == 1U);
  CHECK(!controller.state().available);
  CHECK(controller.publishAudio(
      publishAudio(publication, 13U, 512U, 0.25F, 44100U)));
  CHECK(controller.state().available);
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 2U; }));
}

TEST_CASE("transport_controller_reconfigure_serializes_with_control_calls") {
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 2048U,
                                       .blockFrames = 128U,
                                       .watermarkFrames = 512U}};
  CHECK(controller.start());
  std::atomic<bool> reconfigureSucceeded{true};
  std::jthread reconfigurer([&controller, &reconfigureSucceeded](std::stop_token) {
    for (int index = 0; index < 32; ++index) {
      const bool alternate = index % 2 != 0;
      if (!controller.reconfigure(seam::authoring::TransportConfig{
          .sampleRate = alternate ? 44100U : 48000U,
          .outputChannels = static_cast<std::uint8_t>(alternate ? 1U : 2U),
          .ringCapacityFrames = 2048U,
          .blockFrames = alternate ? 64U : 128U,
          .watermarkFrames = 512U,
      })) {
        reconfigureSucceeded.store(false, std::memory_order_release);
        return;
      }
    }
  });
  for (int index = 0; index < 64; ++index) {
    static_cast<void>(controller.pause());
    static_cast<void>(controller.stop());
    static_cast<void>(controller.state());
  }
  reconfigurer.join();
  CHECK(reconfigureSucceeded.load(std::memory_order_acquire));
  CHECK(controller.config().sampleRate == 44100U ||
        controller.config().sampleRate == 48000U);
}

TEST_CASE("clearing the transport's audio stops playback, empties the timeline and forgets the revision") {
  // A score that has been emptied has nothing to play. Without this the transport keeps the
  // last audio it was given and plays a vocal that is no longer in the project.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 5U, 4096U, 0.5F)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 2000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playhead > 0; }));
  CHECK(controller.clearAudio());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return !state.playing && state.playhead == 0;
  }));
  const auto state = controller.state();
  CHECK(!state.available);
  CHECK(state.timelineEnd == 0);
  CHECK(state.publishedRevision == 0U);
  CHECK(!state.loop.enabled);
  CHECK(!state.availabilityDiagnostic.empty());
  // Whatever the old audio had already put into the ring is discarded, not played.
  const auto output = readFrames(controller, 32U);
  CHECK(std::all_of(output.begin(), output.end(), [](float value) { return value == 0.0F; }));
  // Playing what is not there is refused, as it is before the first render.
  CHECK(!controller.seek(seam::time::SampleFrame{10}));
  // The next audio is accepted whatever its revision: an older number than the cleared one is
  // not "older audio" any more.
  const auto commandsBefore = controller.feederStats().controlCommands;
  // Long enough that playback, had it been resumed, would still be running when it is checked.
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 96000U, 0.25F)));
  const auto republished = controller.state();
  CHECK(republished.available);
  CHECK(republished.publishedRevision == 2U);
  CHECK(republished.timelineEnd == 96000);
  // The play that came before the clear is not resumed by the next audio: a score that was emptied
  // and then written again starts silent until the creator plays it.
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 3U; }));
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  CHECK(!controller.state().playing);
  CHECK(!controller.state().loop.enabled);
}

TEST_CASE("clearing the transport's audio when nothing is published changes nothing") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 512U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 128U}};
  CHECK(controller.start());
  CHECK(controller.clearAudio());
  CHECK(!controller.state().available);
  CHECK(controller.state().publishedRevision == 0U);
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 256U)));
  CHECK(controller.state().available);
}

TEST_CASE("a transport whose audio was cleared plays silence when it is asked to play") {
  // The old audio must be gone from the timeline, not only from the transport's bookkeeping: a
  // creator who presses play on an emptied score hears nothing, not the vocal that was deleted.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 4U, 4096U, 0.5F)));
  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.clearAudio());
  CHECK(controller.play());
  // clearAudio queues four control commands and play one.
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 5U; }));
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  const auto output = readFrames(controller, 64U);
  CHECK(std::all_of(output.begin(), output.end(), [](float value) { return value == 0.0F; }));
}

namespace {

seam::authoring::TransportConfig transportConfigAt(std::uint32_t sampleRate) {
  return seam::authoring::TransportConfig{.sampleRate = sampleRate,
                                          .outputChannels = 2U,
                                          .ringCapacityFrames = 1024U,
                                          .blockFrames = 64U,
                                          .watermarkFrames = 256U};
}

}  // namespace

TEST_CASE("clearing the transport's audio after a reconfigure drops the play and the loop the reconfigure was carrying") {
  // A reconfigure empties the timeline, because the old audio has the wrong sample rate, but keeps
  // what the creator was doing so the audio rendered for the new format can carry on from there.
  // If the score is emptied before that audio arrives, what was kept belongs to audio that no
  // longer exists: handing it to whatever is written next would start a vocal playing, inside a
  // loop the creator set for something else.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 2000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(!controller.state().available);
  CHECK(controller.clearAudio());
  CHECK(!controller.state().loop.enabled);

  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 88200U, 0.0F, 44100U)));
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 3U; }));
  // A play that was wrongly resumed shows within a few milliseconds; wait that long for it.
  CHECK(!waitUntil([&] { return controller.state().playing; }, std::chrono::milliseconds{100}));
  CHECK(!controller.state().loop.enabled);
  CHECK(controller.state().publishedRevision == 1U);
}

TEST_CASE("a reconfigure after the transport's audio was cleared does not bring the old play back") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 2000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  CHECK(controller.clearAudio());
  CHECK(waitUntil([&] { return !controller.state().playing; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));

  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 88200U, 0.0F, 44100U)));
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, std::chrono::milliseconds{100}));
  CHECK(!controller.state().loop.enabled);
  CHECK(controller.state().publishedRevision == 1U);
}

TEST_CASE("clearing the transport's audio leaves a play that was asked for while it held no audio") {
  // A session that starts playing still plays the first audio it is given, however many changes
  // that leave nothing to sing come before it: the play belongs to no audio, so a clear that has
  // nothing to drop must not take it away.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  CHECK(controller.clearAudio());
  // A clear that stopped the feeder would show within a few milliseconds; wait that long for it.
  CHECK(!waitUntil([&] { return !controller.state().playing; }, std::chrono::milliseconds{100}));
  CHECK(controller.publishAudio(publishAudio(publication, 3U, 96000U)));
  CHECK(waitUntil([&] { return controller.state().playhead > 0; }));
  CHECK(controller.state().playing);
  CHECK(controller.state().publishedRevision == 3U);
}

TEST_CASE("a play asked for after the transport's audio was cleared stays armed for the next audio") {
  // The armed play is not limited to a transport that never had audio: it belongs to whatever
  // audio comes next, so a clear that finds nothing held and nothing dropped leaves it alone.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  const auto base = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U)));  // timeline, loop, seek
  CHECK(controller.clearAudio());                                         // stop, timeline, loop, seek
  CHECK(controller.play());
  CHECK(controller.clearAudio());                                         // nothing held, nothing dropped: nothing
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 96000U)));
  // timeline, loop, seek and, because the play was armed, playing. A clear that had dropped it
  // would have queued four commands of its own and left the transport paused.
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= base + 3U + 4U + 1U + 4U; }));
  CHECK(controller.feederStats().controlCommands == base + 3U + 4U + 1U + 4U);
  CHECK(controller.state().playing);
  CHECK(controller.state().publishedRevision == 2U);
}

TEST_CASE("a second reconfigure before the audio is rendered again keeps the play and the loop") {
  // The audio settings can change twice before the score has been rendered for the new format.
  // The second reconfigure finds a feeder that was only just rebuilt, which is not playing;
  // what the creator was doing is what the first reconfigure recorded.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 2000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.reconfigure(transportConfigAt(48000U)));
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 96000U)));
  CHECK(waitUntil([&] { return controller.state().playing; }));
  CHECK(controller.state().loop.enabled);
}

TEST_CASE("a second reconfigure before the audio is rendered again keeps the playhead") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.reconfigure(transportConfigAt(48000U)));
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 96000U)));
  // 40000 frames at 48 kHz are 36750 at 44.1 kHz and 40000 again: nothing is lost on the way back.
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(!controller.state().playing);
}

TEST_CASE("the audio a reconfigure dropped is still remembered after a second reconfigure") {
  // What the first reconfigure carried belongs to audio that is gone, and the second one finds the
  // timeline already empty. A clear that comes after both must still drop it.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 100, .endFrame = 2000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.reconfigure(transportConfigAt(48000U)));
  CHECK(controller.clearAudio());
  CHECK(!controller.state().loop.enabled);

  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U)));
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, std::chrono::milliseconds{100}));
  CHECK(!controller.state().loop.enabled);
}

TEST_CASE("clearing the transport's audio after a reconfigure forgets the playhead the reconfigure saved") {
  // The position a reconfigure keeps is a place in the audio that was dropped. The audio that comes
  // next starts at the beginning, even when nothing was playing and no loop was set.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.clearAudio());

  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 88200U, 0.0F, 44100U)));
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playhead != 0; }, std::chrono::milliseconds{100}));
  CHECK(controller.state().publishedRevision == 1U);
}

TEST_CASE("clearing the transport's audio when it holds nothing sends the feeder nothing") {
  // A score with nothing audible asks for this on every change it sees, so a transport that is
  // already clean must not be handed four commands each time.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  const auto base = controller.feederStats().controlCommands;
  CHECK(controller.clearAudio());                                        // pristine: nothing queued
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U)));  // timeline, loop, seek
  CHECK(controller.clearAudio());                                        // stop, timeline, loop, seek
  CHECK(controller.clearAudio());                                        // already clean: nothing
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 96000U)));  // timeline, loop, seek
  CHECK(controller.play());                                              // playing
  // The feeder handles its commands in the order they were queued: once it is playing it has
  // counted every one before the play.
  CHECK(waitUntil([&] { return controller.state().playhead > 0; }));
  CHECK(controller.feederStats().controlCommands == base + 3U + 4U + 3U + 1U);
}

TEST_CASE("clearing the transport's audio twice after a reconfigure sends the feeder nothing the second time") {
  // What the first clear dropped is dropped: a second one has nothing left to forget.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  // The feeder was rebuilt, so its command count starts again from nothing.
  const auto base = controller.feederStats().controlCommands;
  CHECK(controller.clearAudio());                                                  // stop, timeline, loop, seek
  CHECK(controller.clearAudio());                                                  // nothing left: nothing
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 88200U, 0.0F, 44100U)));  // timeline, loop, seek
  CHECK(controller.play());                                                        // playing
  CHECK(waitUntil([&] { return controller.state().playhead > 0; }));
  CHECK(controller.feederStats().controlCommands == base + 4U + 3U + 1U);
}

TEST_CASE("a transport that had finished playing does not start again when the audio settings change") {
  // The request to play is remembered until the creator pauses, but the feeder stops by itself at
  // the end of the audio. A reconfigure goes by what the feeder was doing, not by what was asked.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playhead == 512; }));
  CHECK(waitUntil([&] { return !controller.state().playing; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 88200U, 0.0F, 44100U)));
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= commandsBefore + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, std::chrono::milliseconds{100}));
}

TEST_CASE("a stop after a reconfigure supersedes the playhead the reconfigure saved") {
  // A reconfigure keeps the playhead for the audio that follows it. A Stop rewinds, and it is what
  // the creator asked for last: the audio that comes next starts at the beginning.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  const auto base = controller.feederStats().controlCommands;
  CHECK(controller.stop());                                                        // paused, seek
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));  // timeline, loop, seek
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= base + 2U + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playhead != 0; }, std::chrono::milliseconds{100}));
  CHECK(!controller.state().playing);
  CHECK(controller.state().publishedRevision == 11U);
}

TEST_CASE("a stop between two reconfigures still starts the audio that follows at the beginning") {
  // The second reconfigure finds the timeline empty, so it goes by what the first one recorded. A
  // Stop in between has replaced that record.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.stop());
  CHECK(controller.reconfigure(transportConfigAt(48000U)));
  const auto base = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 96000U)));  // timeline, loop, seek
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= base + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playhead != 0; }, std::chrono::milliseconds{100}));
  CHECK(!controller.state().playing);
}

TEST_CASE("a stop after a reconfigure supersedes the play and the playhead the reconfigure carried") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing && controller.state().playhead > 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.stop());
  const auto base = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));  // timeline, loop, seek
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= base + 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playing || controller.state().playhead != 0; },
                   std::chrono::milliseconds{100}));
}

TEST_CASE("a pause after a reconfigure keeps the playhead the reconfigure saved") {
  // Only a Stop rewinds. A Pause holds the place, so the audio that comes next resumes from it.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.pause());
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));
  // 40000 frames at 48 kHz are 36750 at 44.1 kHz.
  CHECK(waitUntil([&] { return controller.state().playhead == 36750; }));
  CHECK(!controller.state().playing);
}

TEST_CASE("a play after a reconfigure keeps the playhead the reconfigure saved") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.play());
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));
  CHECK(waitUntil([&] { return controller.state().playing && controller.state().playhead >= 36750; }));
}

namespace {

// Sends the feeder `count` commands that change nothing (an empty loop). With the service not
// running nothing consumes them: they stay in the feeder's queue, which holds 64 commands.
void occupyQueue(seam::authoring::TransportController& controller, std::size_t count) {
  for (std::size_t index = 0U; index < count; ++index) CHECK(controller.setLoop({}));
}

bool hasSound(const std::vector<float>& samples) {
  return std::any_of(samples.begin(), samples.end(), [](float value) { return value != 0.0F; });
}

constexpr auto kQuiet = std::chrono::milliseconds{100};

}  // namespace

TEST_CASE("a publication that does not fit in the feeder's queue queues nothing and can be made again") {
  // The service is not running, so nothing takes commands out of the queue. 63 of its 64 slots
  // are taken and a publication needs three.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  for (unsigned index = 0U; index < 63U; ++index) CHECK(controller.pause());
  CHECK(!controller.publishAudio(publishAudio(publication, 1U, 96000U, 0.25F)));
  CHECK(!controller.state().available);
  CHECK(controller.feederStats().rejectedCommands == 3U);
  // There is nothing to clear, and nothing is sent to clear it.
  CHECK(controller.clearAudio());
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 63U; }));
  // Not even the timeline went in: the feeder saw the 63 pauses and nothing else, so when it is
  // told to play it has nothing to play.
  CHECK(!waitUntil([&] { return controller.feederStats().controlCommands > 63U; }, kQuiet));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 64U; }));
  std::this_thread::sleep_for(kQuiet);
  CHECK(!hasSound(readFrames(controller, 32U)));
  // Asking again, now that the queue has room, publishes it, and it plays.
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U, 0.25F)));
  CHECK(controller.state().available);
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 64U; }));
  CHECK(hasSound(readFrames(controller, 32U)));
}

TEST_CASE("a clear that does not fit in the feeder's queue clears nothing and can be asked again") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.publishAudio(publishAudio(publication, 5U, 4096U, 0.5F)));  // three commands
  occupyQueue(controller, 58);  // 61 of 64 taken: a clear needs four
  CHECK(!controller.clearAudio());
  // What was published is still published, as far as the controller knows...
  const auto state = controller.state();
  CHECK(state.available);
  CHECK(state.publishedRevision == 5U);
  CHECK(state.timelineEnd == 4096);
  CHECK(controller.feederStats().rejectedCommands == 4U);
  // ...and as far as the feeder does: none of the clear reached it, not even its pause.
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 61U; }));
  CHECK(!waitUntil([&] { return controller.feederStats().controlCommands > 61U; }, kQuiet));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 64U; }));
  CHECK(hasSound(readFrames(controller, 32U)));
  // Asked again with room in the queue, the clear is carried out whole.
  CHECK(controller.clearAudio());
  CHECK(!controller.state().available);
  CHECK(controller.state().publishedRevision == 0U);
}

TEST_CASE("a stop that does not fit in the feeder's queue pauses nothing and rewinds nothing") {
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  occupyQueue(controller, 63);  // one slot left: a stop needs two
  CHECK(!controller.stop());
  CHECK(controller.feederStats().rejectedCommands == 2U);
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 63U; }));
  CHECK(!waitUntil([&] { return controller.feederStats().controlCommands > 63U; }, kQuiet));
}

TEST_CASE("a pause that does not fit in the feeder's queue does not forget the play that is waiting for audio") {
  // A play asked for before there is any audio stays armed: the first publication carries it.
  // A pause that was turned away did not pause anything, so the play is still armed.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  controller.shutdown();
  occupyQueue(controller, 64);  // the queue is full
  CHECK(!controller.pause());
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 65U; }));
  const auto before = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U, 0.25F)));
  // Timeline, loop, playhead, and the play that was still armed.
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= before + 4U; }));
  CHECK(!waitUntil([&] { return controller.feederStats().controlCommands > before + 4U; }, kQuiet));
}

TEST_CASE("a stop that does not fit in the feeder's queue does not forget the play that is waiting for audio") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  controller.shutdown();
  occupyQueue(controller, 64);  // the queue is full
  CHECK(!controller.stop());
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 65U; }));
  const auto before = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U, 0.25F)));
  // Timeline, loop, playhead, and the play that is still armed.
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= before + 4U; }));
  CHECK(!waitUntil([&] { return controller.feederStats().controlCommands > before + 4U; }, kQuiet));
}

TEST_CASE("a stop that does not fit in the feeder's queue leaves the playhead a reconfigure saved") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));  // saves 36750 for the audio to come
  controller.shutdown();
  occupyQueue(controller, 64);
  CHECK(!controller.stop());
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 64U; }));
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));
  CHECK(waitUntil([&] { return controller.state().playhead == 36750; }));
  CHECK(!waitUntil([&] { return controller.state().playhead != 36750; }, kQuiet));
}

TEST_CASE("a stop the feeder has not applied yet decides where the audio that replaces it starts") {
  // The feeder applies commands on its own thread, so what it reports lags what was asked. The
  // replacement goes by what was asked: the creator pressed Stop, so it starts at the beginning.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(waitUntil([&] { return controller.state().playhead == 40000; }));
  controller.shutdown();  // nothing is applied from here on; what follows stays queued
  const auto applied = controller.feederStats().controlCommands;
  CHECK(controller.stop());
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 96000U)));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= applied + 2U + 3U; }));
  CHECK(controller.state().publishedRevision == 11U);
  CHECK(!waitUntil([&] { return controller.state().playhead != 0; }, kQuiet));
  CHECK(!controller.state().playing);
}

TEST_CASE("a stop the feeder has not applied yet is what a reconfigure carries for the audio to come") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.seek(seam::time::SampleFrame{40000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing && controller.state().playhead > 40000; }));
  controller.shutdown();
  CHECK(controller.stop());  // queued, not applied: the feeder still reports playing, past 40000
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 3U; }));
  CHECK(controller.state().publishedRevision == 11U);
  CHECK(!waitUntil([&] { return controller.state().playing || controller.state().playhead != 0; }, kQuiet));
}

TEST_CASE("a pause the feeder has not applied yet is not undone by a reconfigure") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
  controller.shutdown();
  CHECK(controller.pause());  // queued, not applied: the feeder still reports playing
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 3U; }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, kQuiet));
}

TEST_CASE("a loop the feeder has not applied yet moves the playhead a reconfigure carries, as the feeder will") {
  // The feeder sends a playhead that is past the end of a new loop back to its start. A reconfigure
  // before the feeder has done that carries the start, not the playhead it had.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 10U, 96000U)));
  CHECK(waitUntil([&] { return controller.feederStats().controlCommands >= 3U; }));
  controller.shutdown();
  CHECK(controller.seek(seam::time::SampleFrame{50000}));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{.enabled = true, .startFrame = 10000, .endFrame = 20000}));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.publishAudio(publishAudio(publication, 11U, 88200U, 0.0F, 44100U)));
  CHECK(controller.start());
  // 10000 frames at 48 kHz are 9187.5, which rounds to 9188, at 44.1 kHz.
  CHECK(waitUntil([&] { return controller.state().playhead == 9188; }));
  CHECK(!waitUntil([&] { return controller.state().playhead != 9188; }, kQuiet));
  CHECK(controller.state().loop.enabled);
}

TEST_CASE("what was asked is recorded afresh for the feeder that a reconfigure builds") {
  // The new feeder counts the commands it consumes from nothing, and so does the record of what
  // was sent to it. Otherwise the feeder could never catch up with the record, and every later
  // decision would go by what was asked, not by what the feeder reports.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 96000U)));
  CHECK(controller.reconfigure(transportConfigAt(44100U)));
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 88200U, 0.0F, 44100U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing && controller.state().playhead > 100; }));
  // The creator hears some of it first: a pause puts the playhead where they are, and nothing has
  // been heard until the device reads.
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 200U; }));
  static_cast<void>(readFrames(controller, 200U));
  CHECK(controller.pause());
  CHECK(waitUntil([&] { return !controller.state().playing; }));
  controller.shutdown();
  // The feeder has applied all of it, and the playhead moved on from where the play found it.
  const auto playhead = controller.state().playhead;
  CHECK(playhead > 100);
  CHECK(controller.reconfigure(transportConfigAt(48000U)));
  CHECK(controller.publishAudio(publishAudio(publication, 3U, 96000U)));
  CHECK(controller.start());
  const auto expected = static_cast<seam::time::SampleFrame>(
      std::llround(static_cast<long double>(playhead) * 48000.0L / 44100.0L));
  CHECK(waitUntil([&] { return controller.state().playhead == expected; }));
  CHECK(!waitUntil([&] { return controller.state().playhead != expected; }, kQuiet));
}

namespace {

// What a consumer that is still running takes: everything the ring holds, as the audio callback does.
std::vector<float> playOut(seam::authoring::TransportController& controller) {
  return readFrames(controller, controller.ringBuffer().availableReadFrames());
}

}  // namespace

TEST_CASE("transport_controller_play_at_the_end_plays_the_audio_again_from_its_start") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(controller.play());
  // 512 frames fit in the ring: the feeder hands all of them over and reports the end.
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == state.timelineEnd;
  }));
  const auto first = playOut(controller);
  CHECK(first.size() >= 512U * 2U);
  CHECK_NEAR(first[100U * 2U], 0.100, 0.002);

  // The playhead stands at the end. Played from there it would end in the same instant, and the
  // creator would press Play and hear nothing: Play starts the audio again from its beginning.
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 512U; }));
  const auto again = playOut(controller);
  CHECK(again.size() >= 512U * 2U);
  CHECK_NEAR(again[0], 0.000, 0.002);
  CHECK_NEAR(again[100U * 2U], 0.100, 0.002);
  CHECK_NEAR(again[400U * 2U], 0.400, 0.002);
}

TEST_CASE("transport_controller_play_away_from_the_end_does_not_rewind") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(controller.seek(200));
  CHECK(waitUntil([&] { return controller.state().settled && controller.state().playhead == 200; }));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() >= 64U; }));
  // Played from where it stood, not from the start.
  const auto output = playOut(controller);
  CHECK_NEAR(output[0], 0.200, 0.002);
}

TEST_CASE("transport_controller_state_is_settled_once_the_feeder_has_applied_what_was_sent") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.state().settled);
  // The service is not running, so nothing takes the commands out of the queue.
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(!controller.state().settled);
  CHECK(!waitUntil([&] { return controller.state().settled; }, kQuiet));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(controller.state().available);
}

TEST_CASE("transport_controller_await_start_buffer_answers_the_clear_that_no_consumer_reads") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 300U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == state.timelineEnd;
  }));
  // The device plays some of it and is stopped: audio from before is left in the ring, and it is
  // fewer frames than the start buffer.
  static_cast<void>(readFrames(controller, 100U));
  const auto stale = controller.ringBuffer().availableReadFrames();
  CHECK(stale > 0U);
  CHECK(stale < 256U);

  // The creator seeks to the start and presses Play. The feeder applies both, asks the consumer to
  // drop what the ring holds, and writes nothing until it has. The consumer is stopped.
  CHECK(controller.seek(0));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(waitUntil([&] { return controller.feederStats().resetWaits > 0U; }));
  CHECK(!waitUntil([&] { return controller.ringBuffer().availableReadFrames() != stale; }, kQuiet));

  // The owner of the stopped consumer answers in its place, and what arrives follows the seek.
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{2000}));
  CHECK(controller.ringBuffer().availableReadFrames() >= 256U);
  const auto heard = readFrames(controller, 64U);
  CHECK_NEAR(heard[0], 0.000, 0.002);
  CHECK_NEAR(heard[10U * 2U], 0.010, 0.002);
  CHECK_NEAR(heard[63U * 2U], 0.063, 0.002);

  // A second look finds nothing to answer and keeps what has been written since.
  const auto buffered = controller.ringBuffer().availableReadFrames();
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{2000}));
  CHECK(controller.ringBuffer().availableReadFrames() >= buffered);
}

TEST_CASE("transport_controller_await_start_buffer_waits_for_the_commands_before_it_answers") {
  // A reset is asked for when the feeder applies a command, so until it has applied every command
  // there may be one still to come, and the ring still holds audio from before.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  // The service is not running, so nothing is applied, and the wait gives up rather than start a
  // consumer on audio that may be from before.
  const auto result = controller.awaitStartBuffer(std::chrono::milliseconds{40});
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Conflict);
  CHECK(!result.error().message.empty());
}

TEST_CASE("transport_controller_await_start_buffer_refuses_to_start_without_audio") {
  seam::authoring::TransportController controller{};
  CHECK(controller.start());
  const auto result = controller.awaitStartBuffer(std::chrono::milliseconds{40});
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("transport_controller_await_start_buffer_does_not_wait_for_audio_the_feeder_is_not_going_to_write") {
  // The creator paused, or the audio is finished: nothing more will be written, so the wait for
  // the start buffer would only run out its timeout. The consumer starts on what there is.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{
      seam::authoring::TransportConfig{.sampleRate = 48000U,
                                       .outputChannels = 2U,
                                       .ringCapacityFrames = 1024U,
                                       .blockFrames = 64U,
                                       .watermarkFrames = 256U}};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(!controller.state().playing);
  const auto began = std::chrono::steady_clock::now();
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{2000}));
  CHECK(std::chrono::steady_clock::now() - began < std::chrono::milliseconds{1000});
}

TEST_CASE("transport_controller_state_never_pairs_an_applied_pause_with_the_playing_flag_from_before_it") {
  // The feeder applies the creator's commands on its own thread, so it can apply one at any moment
  // of the few reads state() makes of what it reports. A report that says every command is applied
  // has to carry the state they led to, wherever in the sample the feeder moved: a repaint that
  // went by such a report would see a pause as applied and the audio as still playing, and start
  // the audio device again after the creator paused it.
  for (std::size_t movesBeforeRead = 0U; movesBeforeRead <= 3U; ++movesBeforeRead) {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{
        seam::authoring::TransportConfig{.sampleRate = 48000U,
                                         .outputChannels = 2U,
                                         .ringCapacityFrames = 1024U,
                                         .blockFrames = 64U,
                                         .watermarkFrames = 256U}};
    CHECK(controller.publishAudio(publishAudio(publication, 1U, 4096U)));
    CHECK(controller.play());
    // 4096 frames do not fit in the ring and nothing reads it: the feeder is playing, and stays so.
    CHECK(waitUntil([&] {
      const auto state = controller.state();
      return state.settled && state.playing;
    }));
    // From here the test is the only thing that moves the feeder.
    controller.shutdown();
    CHECK(controller.pause());
    CHECK(!controller.state().settled);
    CHECK(controller.state().playing);

    std::size_t reads = 0U;
    bool moved = false;
    seam::rendering::MultichannelPlaybackFeeder* feeder = nullptr;
    controller.setStateSampleProbe([&](seam::rendering::MultichannelPlaybackFeeder& reported) {
      feeder = &reported;
      if (reads++ == movesBeforeRead) {
        static_cast<void>(reported.feedOnce());
        moved = true;
      }
    });
    const auto sampled = controller.state();
    controller.setStateSampleProbe({});
    CHECK(feeder != nullptr);
    // The sample is made of three reads. The feeder moved before the one asked for, or, for the last
    // case, after all of them.
    CHECK(moved == (movesBeforeRead < 3U));
    // Either the report says the feeder has not applied the pause, or it carries what the pause
    // led to. It never says the pause is applied and the audio is still playing.
    CHECK(!(sampled.settled && sampled.playing));
    if (sampled.settled) CHECK(!sampled.playing);

    // Whatever the sample caught, the feeder applies the pause and the next sample says so.
    if (!moved && feeder != nullptr) static_cast<void>(feeder->feedOnce());
    const auto later = controller.state();
    CHECK(later.settled);
    CHECK(!later.playing);
  }
}

namespace {

// The audio device's place in these cases is the test: nothing leaves the ring until the test reads
// it, so what the test has read is what the creator has heard, and the feeder has mixed ahead of
// that by whatever the ring holds. The ramp makes a frame's value say which frame of the song it is.
seam::authoring::TransportConfig mixedAheadConfig() {
  return seam::authoring::TransportConfig{.sampleRate = 48000U,
                                          .outputChannels = 2U,
                                          .ringCapacityFrames = 4096U,
                                          .blockFrames = 64U,
                                          .watermarkFrames = 2048U};
}

constexpr std::size_t kMixedAheadSongFrames = 20000U;

bool ringIsFull(seam::authoring::TransportController& controller) {
  return controller.ringBuffer().availableReadFrames() >= 2048U;
}

// Plays the song, lets the feeder fill the ring, hears `heard` frames and lets the feeder fill the
// ring again: the creator is at frame `heard` and the feeder is a ringful ahead of it.
void playAndHear(seam::authoring::TransportController& controller,
                 seam::authoring::RealtimeProjectAudioPublication& publication, std::size_t heard) {
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return ringIsFull(controller); }));
  const auto played = readFrames(controller, heard);
  CHECK_NEAR(played[(heard - 1U) * 2U], static_cast<double>(heard - 1U) / 1000.0, 0.002);
  CHECK(waitUntil([&] { return ringIsFull(controller); }));
}

// The first frame the creator hears once the feeder has applied everything it was sent: the reads
// that answer its request to drop audio hear nothing, and the first one that hears something says
// where in the song playback went on.
float firstFrameHeardOnceApplied(seam::authoring::TransportController& controller) {
  CHECK(waitUntil([&] { return controller.state().settled; }));
  std::vector<float> next;
  CHECK(waitUntil([&] {
    next = readFrames(controller, 8U);
    return next.front() != 0.0F;
  }));
  return next.front();
}

}  // namespace

TEST_CASE("transport_controller_publication_during_playback_goes_on_from_the_audible_position") {
  // A render that lands while the creator listens replaces the audio and puts the playhead in it.
  // The feeder is a ringful of audio ahead of the device, and the playhead has to be put where the
  // creator is, not where the feeder is: otherwise the audio they have not heard yet is skipped.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  CHECK(controller.publishAudio(publishAudio(publication, 2U, kMixedAheadSongFrames)));
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
}

TEST_CASE("transport_controller_play_after_a_pause_goes_on_from_where_the_audio_stopped") {
  // A pause drops the audio the feeder had mixed ahead of the device. Playing again from where the
  // feeder stood would skip all of it: the creator pauses at one place and the audio goes on from
  // a place further on.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  CHECK(controller.pause());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing;
  }));
  // The device answers the feeder's request to drop what it had mixed ahead.
  static_cast<void>(readFrames(controller, 8U));
  CHECK(controller.play());
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
}

TEST_CASE("transport_controller_loop_change_during_playback_goes_on_from_the_audible_position") {
  // A loop change makes the feeder drop what it had mixed ahead, as a pause does.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 0, .endFrame = static_cast<seam::time::SampleFrame>(kMixedAheadSongFrames)}));
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
}

TEST_CASE("transport_controller_reconfigure_during_playback_carries_the_audible_position") {
  // A change of audio settings carries the playhead to the audio that follows. It is the position
  // the creator is at that it carries, not the one the feeder had reached.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  auto changed = mixedAheadConfig();
  changed.blockFrames = 32U;
  CHECK(controller.reconfigure(changed));
  CHECK(controller.publishAudio(publishAudio(publication, 2U, kMixedAheadSongFrames)));
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
}

TEST_CASE("transport_controller_audible_playhead_is_what_the_device_has_played_not_what_the_feeder_has_mixed") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  // The feeder stands a ringful ahead of the creator, and the playhead that is shown is the creator's.
  CHECK(waitUntil([&] { return controller.state().audiblePlayhead == 1000; }));
  CHECK(controller.state().playhead >= 1000 + 2048);
  static_cast<void>(readFrames(controller, 500U));
  CHECK(waitUntil([&] { return controller.state().audiblePlayhead == 1500; }));
}

TEST_CASE("transport_controller_audible_playhead_after_a_pause_is_where_the_audio_stopped") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  CHECK(controller.pause());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing;
  }));
  static_cast<void>(readFrames(controller, 8U));
  const auto state = controller.state();
  CHECK(state.audiblePlayhead == 1000);
  // The feeder has nothing mixed ahead any more: its own playhead is where the audio stopped too.
  CHECK(state.playhead == 1000);
}

TEST_CASE("transport_controller_audible_playhead_goes_by_a_seek_the_device_has_not_answered") {
  // The feeder has asked the device to drop what the ring holds and has moved on. The audio in the
  // ring is not going to be heard: the creator is where the seek put them, not a ringful behind it.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  CHECK(controller.seek(5000));
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(controller.ringBuffer().resetPending());
  CHECK(controller.state().audiblePlayhead == 5000);
}

TEST_CASE("transport_controller_audible_playhead_goes_by_a_command_the_feeder_has_not_applied_yet") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  // From here nothing is applied: the service is stopped.
  controller.shutdown();
  CHECK(controller.stop());
  CHECK(!controller.state().settled);
  CHECK(controller.state().audiblePlayhead == 0);
  CHECK(controller.seek(7000));
  CHECK(controller.state().audiblePlayhead == 7000);
  // A command that follows one that put the playhead somewhere leaves it there.
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 0, .endFrame = static_cast<seam::time::SampleFrame>(kMixedAheadSongFrames)}));
  CHECK(!controller.state().settled);
  CHECK(controller.state().audiblePlayhead == 7000);
}

TEST_CASE("transport_controller_audible_playhead_follows_a_loop_that_has_wrapped") {
  // A loop of 1000 frames and a feeder 2048 frames ahead: the audio in the ring spans two wraps.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{.enabled = true, .startFrame = 0, .endFrame = 1000}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return ringIsFull(controller); }));
  CHECK(waitUntil([&] { return controller.state().audiblePlayhead == 0; }));
  static_cast<void>(readFrames(controller, 1000U));
  CHECK(waitUntil([&] { return ringIsFull(controller); }));
  static_cast<void>(readFrames(controller, 500U));
  CHECK(waitUntil([&] { return ringIsFull(controller); }));
  // 1500 frames are 500 frames into the second pass of the loop.
  CHECK(waitUntil([&] { return controller.state().audiblePlayhead == 500; }));
}

TEST_CASE("transport_controller_audible_playhead_reaches_the_end_with_the_audio_and_a_pause_leaves_the_tail") {
  // The audio is short enough for the ring: the feeder hands all of it over and reports the end,
  // while the device has played none of it, and then part of it. The playhead that is shown
  // follows the device to the end, and a pause has nothing to pause: the tail is heard.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == 3000;
  }));
  CHECK(controller.state().audiblePlayhead == 0);
  static_cast<void>(readFrames(controller, 1000U));
  CHECK(waitUntil([&] { return controller.state().audiblePlayhead == 1000; }));
  CHECK(controller.pause());
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(!controller.ringBuffer().resetPending());
  CHECK(controller.state().playhead == 3000);
  CHECK(controller.state().audiblePlayhead == 1000);
  static_cast<void>(readFrames(controller, 2000U));
  CHECK(controller.state().audiblePlayhead == 3000);
}

namespace {

// A ring of 1024 frames, so that the frames the feeder has mixed ahead wrap its slots.
seam::authoring::TransportConfig wrappingConfig() {
  auto config = mixedAheadConfig();
  config.ringCapacityFrames = 1024U;
  config.watermarkFrames = 1024U;
  return config;
}

// The feeder belongs to the controller. A sample probe hands it to the test, which can move it
// when the controller's service is stopped (shutdown()) and nothing else does.
seam::rendering::MultichannelPlaybackFeeder& feederOf(seam::authoring::TransportController& controller) {
  seam::rendering::MultichannelPlaybackFeeder* found = nullptr;
  controller.setStateSampleProbe(
      [&found](seam::rendering::MultichannelPlaybackFeeder& feeder) { found = &feeder; });
  static_cast<void>(controller.state());
  controller.setStateSampleProbe({});
  CHECK(found != nullptr);
  return *found;
}

// The feeder has mixed the first 1024 frames of the song into a ring that holds no more, and has
// stopped: the creator has heard none of them.
void prepareFullRing(seam::authoring::TransportController& controller,
                     seam::authoring::RealtimeProjectAudioPublication& publication) {
  CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() == 1024U; }));
  controller.shutdown();
  CHECK(controller.state().settled);
  CHECK(controller.state().playhead == 1024);
  CHECK(controller.state().audiblePlayhead == 0);
}

// While the audible position is worked out the device plays 64 frames and the feeder mixes 64 more,
// over the slots the device left: the indices of the ring's slots have wrapped, and the ring holds
// frames 64 to 1087 of the song. The creator is at frame 64.
void movesTheRingWhileThePositionIsTaken(seam::authoring::TransportController& controller,
                                         seam::rendering::MultichannelPlaybackFeeder& feeder,
                                         bool& moved) {
  controller.ringBuffer().setSnapshotProbe([&controller, &feeder, &moved](int point) {
    if (point != 1 || moved) return;
    moved = true;
    std::vector<float> output(64U * 2U);
    static_cast<void>(controller.ringBuffer().readFrames(output));
    static_cast<void>(feeder.feedOnce());
  });
}

}  // namespace

TEST_CASE("transport_controller_audible_playhead_is_where_the_creator_is_when_the_ring_wraps_under_the_sample") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  bool moved = false;
  movesTheRingWhileThePositionIsTaken(controller, feeder, moved);
  CHECK(controller.state().audiblePlayhead == 64);
  CHECK(moved);
}

TEST_CASE("transport_controller_pause_goes_on_from_where_the_creator_was_when_the_ring_wraps_under_the_sample") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  bool moved = false;
  movesTheRingWhileThePositionIsTaken(controller, feeder, moved);
  CHECK(controller.pause());
  CHECK(moved);
  controller.ringBuffer().setSnapshotProbe({});
  CHECK(controller.play());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 8U).front(), 0.064, 0.002);
}

TEST_CASE("transport_controller_audible_playhead_in_a_loop_that_begins_after_the_audio_is_in_the_first_pass") {
  // The loop begins at frame 512. The feeder has mixed 1024 frames of the first pass, from the
  // start of the audio, and the creator has heard none: they are at the start, not 1024 frames
  // back through a loop that has not wrapped.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
  CHECK(controller.setLoop({.enabled = true, .startFrame = 512, .endFrame = 2048}));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() == 1024U; }));
  controller.shutdown();
  const auto before = controller.state();
  CHECK(before.playhead == 1024);
  CHECK(before.audiblePlayhead == 0);
  CHECK(controller.pause());
  CHECK(controller.play());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  // Audio from the start of the song, and not silence.
  const auto next = readFrames(controller, 8U);
  CHECK_NEAR(next[0], 0.0, 0.0001);
  CHECK_NEAR(next[7U * 2U], 0.007, 0.0001);
}

TEST_CASE("transport_controller_pause_keeps_the_audio_the_feeder_mixes_after_the_position_was_taken") {
  // The device has played everything the feeder mixed, so the creator is at frame 1024 and so is
  // the feeder. The feeder mixes one more block before the pause reaches it: the pause has to put
  // the playhead at frame 1024 all the same, or that block is dropped and playback goes on after it.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  CHECK_NEAR(readFrames(controller, 1024U)[1023U * 2U], 1.023, 0.002);
  CHECK(controller.state().audiblePlayhead == 1024);
  bool moved = false;
  controller.setStateSampleProbe([&moved](seam::rendering::MultichannelPlaybackFeeder& feeder) {
    if (moved) return;
    moved = true;
    static_cast<void>(feeder.feedOnce());
  });
  CHECK(controller.pause());
  CHECK(moved);
  controller.setStateSampleProbe({});
  CHECK(controller.play());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 8U).front(), 1.024, 0.002);
}

TEST_CASE("transport_controller_loop_change_keeps_the_audio_the_feeder_mixes_after_the_position_was_taken") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  CHECK_NEAR(readFrames(controller, 1024U)[1023U * 2U], 1.023, 0.002);
  CHECK(controller.state().audiblePlayhead == 1024);
  bool moved = false;
  controller.setStateSampleProbe([&moved](seam::rendering::MultichannelPlaybackFeeder& feeder) {
    if (moved) return;
    moved = true;
    static_cast<void>(feeder.feedOnce());
  });
  CHECK(controller.setLoop({.enabled = true, .startFrame = 0, .endFrame = 20000}));
  CHECK(moved);
  controller.setStateSampleProbe({});
  CHECK(controller.start());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 8U).front(), 1.024, 0.002);
}

TEST_CASE("transport_controller_reconfigure_keeps_the_place_in_the_audio_the_device_has_reached_after_the_feeder_finished") {
  // The audio is short enough for the ring: the feeder has handed all of it over and is finished,
  // and the device has played 1000 of the 3000 frames. A change of settings drops the 2000 that
  // are left in the ring, so playback has to carry on from frame 1000 and not from the end: the
  // feeder finished, and that is not the creator's pause or play, so it does not start playback.
  seam::authoring::RealtimeProjectAudioPublication publication;
  auto config = mixedAheadConfig();
  seam::authoring::TransportController controller{config};
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == 3000;
  }));
  controller.shutdown();
  CHECK_NEAR(readFrames(controller, 1000U).back(), 0.999, 0.002);
  CHECK(controller.state().audiblePlayhead == 1000);
  CHECK(controller.ringBuffer().availableReadFrames() == 2000U);
  config.blockFrames = 32U;
  CHECK(controller.reconfigure(config));
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 3000U)));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.state().settled; }));
  const auto after = controller.state();
  CHECK(!after.playing);
  CHECK(after.audiblePlayhead == 1000);
  CHECK(after.playhead == 1000);
}

TEST_CASE("transport_controller_loop_change_after_the_feeder_finished_keeps_the_place_the_device_has_reached") {
  // The feeder has handed all 3000 frames over and is finished, and the device has played 1000. A
  // loop change drops the 2000 that are left in the ring, and the place the audio would go on from
  // is the one the device was at, not the end that the finished feeder stands at.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == 3000;
  }));
  controller.shutdown();
  CHECK_NEAR(readFrames(controller, 1000U).back(), 0.999, 0.002);
  CHECK(controller.state().audiblePlayhead == 1000);
  CHECK(controller.setLoop({}));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.state().settled; }));
  const auto after = controller.state();
  CHECK(!after.playing);
  CHECK(after.playhead == 1000);
  CHECK(after.audiblePlayhead == 1000);
}

TEST_CASE("transport_controller_audible_playhead_goes_by_where_a_loop_the_feeder_has_not_applied_will_put_it") {
  // The audio has been played to its end and the feeder is finished. A loop that ends before the
  // playhead moves it to the start of the loop when the feeder applies it, and until then that is
  // where the creator is.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == 3000;
  }));
  controller.shutdown();
  static_cast<void>(readFrames(controller, 3000U));
  CHECK(controller.state().audiblePlayhead == 3000);
  CHECK(controller.setLoop({.enabled = true, .startFrame = 0, .endFrame = 1000}));
  CHECK(!controller.state().settled);
  CHECK(controller.state().audiblePlayhead == 0);
}

namespace {

// Every time the ring is read for where the consumer is (at the point where the reader has a
// place), the device plays one frame and the feeder mixes one in its place: the ring holds as many
// frames as before, and no reading of it holds still. A reader that is bounded gives up.
struct MovingConsumer final {
  seam::authoring::TransportController& controller;
  seam::rendering::MultichannelPlaybackFeeder& feeder;
  std::size_t attempts{0U};
  std::size_t consumed{0U};
  std::size_t produced{0U};
  void begin() {
    controller.ringBuffer().setSnapshotProbe([this](int point) {
      if (point != 3) return;
      ++attempts;
      std::vector<float> frame(2U);
      consumed += controller.ringBuffer().readFrames(frame);
      produced += feeder.feedOnce();
    });
  }
  void end() { controller.ringBuffer().setSnapshotProbe({}); }
};

// The ring is full from the start of the song, the device has played 300 frames and the feeder has
// filled the ring again, and the playhead on screen has been asked for: it shows 300.
void playedThreeHundred(seam::authoring::TransportController& controller,
                        seam::rendering::MultichannelPlaybackFeeder& feeder) {
  static_cast<void>(readFrames(controller, 300U));
  while (controller.ringBuffer().availableWriteFrames() > 0U) {
    if (feeder.feedOnce() == 0U) break;
  }
  CHECK(controller.ringBuffer().availableReadFrames() == 1024U);
  CHECK(controller.state().audiblePlayhead == 300);
}

}  // namespace

TEST_CASE("transport_controller_audible_playhead_of_an_empty_ring_is_where_the_feeder_was_before_the_ring_was_asked") {
  // The device has played everything the feeder mixed: the ring is empty, and the creator is where
  // the feeder is. The feeder is asked before the ring. What it mixes in between is audio the
  // creator has not heard, and a place taken from it after the ring was found empty would skip it.
  // Here the feeder mixes 64 frames after the ring's counts were read, so the ring is no longer
  // empty when the answer is given, and the answer is still the place of the first of those frames.
  using Kind = seam::rendering::SpscInterleavedAudioRingBuffer::NextFrame::Kind;
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  static_cast<void>(readFrames(controller, 1024U));
  CHECK(controller.ringBuffer().nextFrame().kind == Kind::Empty);
  bool moved = false;
  std::size_t mixed = 0U;
  controller.ringBuffer().setSnapshotProbe([&](int point) {
    // Point 2 is after the counts that found the ring empty were read.
    if (point != 2 || moved) return;
    moved = true;
    mixed = feeder.feedOnce();
  });
  const auto sampled = controller.state();
  controller.ringBuffer().setSnapshotProbe({});
  CHECK(moved);
  CHECK(mixed == 64U);
  CHECK(feeder.playhead() == 1024 + 64);
  CHECK(controller.ringBuffer().availableReadFrames() == 64U);
  CHECK(controller.ringBuffer().nextFrame().place == 1024);
  CHECK(sampled.audiblePlayhead == 1024);
}

TEST_CASE("transport_controller_audible_playhead_stays_where_it_was_when_the_ring_cannot_be_read") {
  // The consumer moves under every attempt to read where it is, with the ring full: that is not an
  // empty ring, and the feeder, which is a ringful ahead, does not stand in for the device. What is
  // shown stays at the last place that was confirmed, which the device has reached or passed.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  playedThreeHundred(controller, feeder);
  MovingConsumer moving{controller, feeder};
  moving.begin();
  const auto sampled = controller.state();
  moving.end();
  CHECK(moving.attempts > 1U);
  CHECK(moving.consumed == moving.attempts);
  CHECK(moving.produced == moving.attempts);
  CHECK(sampled.audiblePlayhead == 300);
  CHECK(sampled.audiblePlayhead <= static_cast<seam::time::SampleFrame>(300U + moving.consumed));
  // Read at rest, the ring says where the device is.
  CHECK(controller.state().audiblePlayhead == static_cast<seam::time::SampleFrame>(300U + moving.consumed));
}

TEST_CASE("transport_controller_pause_is_refused_and_changes_nothing_when_the_ring_cannot_be_read") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  MovingConsumer moving{controller, feeder};
  moving.begin();
  const auto refused = controller.pause();
  moving.end();
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  CHECK(moving.attempts > 1U);
  // Nothing was sent: what the feeder has applied is everything, and it is still playing.
  CHECK(controller.state().settled);
  CHECK(controller.state().playing);
  // Asked again with the ring at rest, the pause goes on from where the device is, which is where
  // the consumer got to while the first one was refused.
  CHECK(controller.pause());
  CHECK(controller.play());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 8U).front(), static_cast<double>(moving.consumed) / 1000.0, 0.002);
}

TEST_CASE("transport_controller_loop_change_is_refused_and_changes_nothing_when_the_ring_cannot_be_read") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  MovingConsumer moving{controller, feeder};
  const seam::rendering::PlaybackLoop loop{.enabled = true, .startFrame = 0, .endFrame = 20000};
  moving.begin();
  const auto refused = controller.setLoop(loop);
  moving.end();
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  CHECK(moving.attempts > 1U);
  CHECK(controller.state().settled);
  CHECK(!controller.state().loop.enabled);
  CHECK(controller.setLoop(loop));
  CHECK(controller.start());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 8U).front(), static_cast<double>(moving.consumed) / 1000.0, 0.002);
}

TEST_CASE("transport_controller_publication_is_refused_and_changes_nothing_when_the_ring_cannot_be_read") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  MovingConsumer moving{controller, feeder};
  CHECK(publishAudio(publication, 2U, kMixedAheadSongFrames));
  moving.begin();
  const auto refused = controller.publishAudio(publication.acquire());
  moving.end();
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  CHECK(moving.attempts > 1U);
  CHECK(controller.state().settled);
  CHECK(controller.state().publishedRevision == 1U);
  // Asked again with the ring at rest, the replacement goes on from where the device is.
  CHECK(controller.publishAudio(publication.acquire()));
  CHECK(controller.state().publishedRevision == 2U);
  CHECK(controller.start());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 8U).front(), static_cast<double>(moving.consumed) / 1000.0, 0.002);
}

TEST_CASE("transport_controller_reconfigure_is_refused_and_leaves_the_transport_running_when_the_ring_cannot_be_read") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  const auto original = wrappingConfig();
  seam::authoring::TransportController controller{original};
  CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() == 1024U; }));
  auto& feeder = feederOf(controller);
  MovingConsumer moving{controller, feeder};
  auto changed = original;
  changed.blockFrames = 32U;
  changed.watermarkFrames = 512U;
  // The reconfigure stops the feeder's service before it asks the ring, so that the probe, which
  // moves the feeder, is the only thing that does.
  moving.begin();
  const auto refused = controller.reconfigure(changed);
  moving.end();
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  CHECK(moving.attempts > 1U);
  CHECK(controller.config().blockFrames == original.blockFrames);
  CHECK(controller.state().publishedRevision == 1U);
  // The feeder's service was started again: it fills what the device takes.
  static_cast<void>(readFrames(controller, 512U));
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() == 1024U; }));
  // Asked again, with the ring at rest, the change is made.
  CHECK(controller.reconfigure(changed));
  CHECK(controller.config().blockFrames == 32U);
}

TEST_CASE("transport_controller_a_seek_the_feeder_has_not_applied_decides_where_pause_and_play_go_on_from") {
  // A seek that waits for the feeder is where playback goes on from, however many commands follow
  // it, until the feeder has applied it; then the device is at the place the audio is.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  controller.shutdown();
  CHECK(controller.seek(2000));
  CHECK(controller.setLoop({}));
  CHECK(controller.pause());
  CHECK(!controller.state().settled);
  CHECK(controller.state().audiblePlayhead == 2000);
  CHECK(controller.play());
  CHECK(controller.awaitStartBuffer(std::chrono::milliseconds{1500}));
  CHECK_NEAR(readFrames(controller, 64U).front(), 2.0, 0.002);
  controller.shutdown();
  const auto after = controller.state();
  CHECK(after.settled);
  CHECK(after.audiblePlayhead == 2064);
}

TEST_CASE("transport_controller_suspend_pauses_where_the_creator_is_and_says_that_they_were_playing") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  playAndHear(controller, publication, 1000U);
  const auto suspended = controller.suspend(true);
  CHECK(suspended);
  CHECK(suspended.value());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing;
  }));
  // The feeder had mixed a ringful ahead of the creator. It is paused where they are.
  CHECK(controller.state().audiblePlayhead == 1000);
  CHECK(controller.play());
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
}

TEST_CASE("transport_controller_suspend_goes_by_what_the_creator_asked_for_not_by_what_the_feeder_reports") {
  // The feeder applies the creator's commands on its own thread, so what it reports can be a
  // command behind. A Pause that waits for it is not playing, a Play that waits for it is, and a
  // Pause it has applied is not.
  {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    playAndHear(controller, publication, 1000U);
    controller.shutdown();
    CHECK(controller.pause());
    CHECK(controller.state().playing);
    CHECK(!controller.state().settled);
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
  {
    // The Play reaches the feeder with the audio that it was waiting for, and the feeder has not
    // applied it yet.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    CHECK(controller.start());
    CHECK(controller.play());
    auto changed = mixedAheadConfig();
    changed.blockFrames = 32U;
    CHECK(controller.reconfigure(changed));
    controller.shutdown();
    CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
    CHECK(!controller.state().playing);
    CHECK(!controller.state().settled);
    const auto suspended = controller.suspend(false);
    CHECK(suspended);
    CHECK(suspended.value());
  }
  {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    playAndHear(controller, publication, 1000U);
    CHECK(controller.pause());
    CHECK(waitUntil([&] {
      const auto state = controller.state();
      return state.settled && !state.playing;
    }));
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
}

TEST_CASE("transport_controller_suspend_withdraws_a_play_that_waits_for_audio_until_it_is_asked_for_again") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  CHECK(controller.start());
  // Asked to play before anything was rendered, and then the settings changed: the new feeder has
  // never been told to play, and the Play waits for the audio that is rendered next.
  CHECK(controller.play());
  auto changed = mixedAheadConfig();
  changed.blockFrames = 32U;
  CHECK(controller.reconfigure(changed));
  const auto suspended = controller.suspend(false);
  CHECK(suspended);
  CHECK(suspended.value());
  // The transport is paused: the audio that arrives does not start it, until it is asked to play.
  CHECK(controller.publishAudio(publishAudio(publication, 1U, kMixedAheadSongFrames)));
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, kQuiet));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
}

namespace {

// What the owner of the consumer has told the transport by the time the creator presses Play.
enum class Consumer {
  // The device was started for the Play, as the app starts it.
  StartedAfterPlay,
  // The device was running already when the creator pressed Play.
  RunningBeforePlay,
  // The device has not been started: the owner says, as it does every frame, that it is not
  // running, and the creator's Play is one that no consumer has taken up.
  NotStartedYet,
};

// The feeder has handed over the whole of a short song and stopped, and the creator has heard
// "heard" frames of it: the ring holds the rest, which the consumer is still playing out. With a
// consumer that has not been started nobody has played any of it, and "heard" is zero.
void endOfTheSongInTheRing(seam::authoring::TransportController& controller,
                           seam::authoring::RealtimeProjectAudioPublication& publication,
                           std::size_t heard, Consumer consumer = Consumer::StartedAfterPlay) {
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
  if (consumer == Consumer::RunningBeforePlay) controller.setConsumerRunning(true);
  CHECK(controller.play());
  if (consumer == Consumer::StartedAfterPlay) controller.setConsumerRunning(true);
  if (consumer == Consumer::NotStartedYet) controller.setConsumerRunning(false);
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing && state.playhead == 3000;
  }));
  static_cast<void>(readFrames(controller, heard));
  CHECK(controller.ringBuffer().availableReadFrames() == 3000U - heard);
}

}  // namespace

TEST_CASE("transport_controller_suspend_says_that_they_are_playing_while_the_consumer_plays_the_end_out") {
  // The feeder reports that it has stopped as soon as it has handed over the last of the audio, and
  // the consumer plays what the ring holds of it for a while after that.
  {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(suspended.value());
    // It is paused where the creator is, and not at the end of the audio: a play goes on from there
    // and does not start the song again.
    CHECK(waitUntil([&] {
      const auto state = controller.state();
      return state.settled && !state.playing;
    }));
    CHECK(controller.state().audiblePlayhead == 1000);
    CHECK(controller.play());
    CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
  }
  // Nobody is playing the ring out: the consumer ran, and has stopped on its own. It took the
  // creator's Play up, whether it was started for the Play or was running when it was asked for.
  for (const auto consumer : {Consumer::StartedAfterPlay, Consumer::RunningBeforePlay}) {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U, consumer);
    controller.setConsumerRunning(false);
    const auto suspended = controller.suspend(false);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
  {
    // The consumer has played all of it.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 3000U);
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
  {
    // The creator paused during the end of the song, and the consumer goes on playing the ring out.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    CHECK(controller.pause());
    CHECK(waitUntil([&] { return controller.state().settled; }));
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
  {
    // The creator put the playhead back at the start after the song ended: the feeder has dropped
    // the ring, though the consumer has not answered yet.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    CHECK(controller.seek(0));
    CHECK(waitUntil([&] {
      const auto state = controller.state();
      return state.settled && state.playhead == 0;
    }));
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
  {
    // The same, and the feeder has not applied the seek yet.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    controller.shutdown();
    CHECK(controller.seek(0));
    CHECK(!controller.state().settled);
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
}

TEST_CASE("transport_controller_suspend_is_refused_and_changes_nothing_when_the_ring_cannot_be_read") {
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  prepareFullRing(controller, publication);
  auto& feeder = feederOf(controller);
  MovingConsumer moving{controller, feeder};
  moving.begin();
  const auto refused = controller.suspend(true);
  moving.end();
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  CHECK(moving.attempts > 1U);
  // Nothing was sent: the feeder has applied everything and is still playing.
  CHECK(controller.state().settled);
  CHECK(controller.state().playing);
  // Asked again with the ring at rest, it pauses the transport where the device is.
  const auto again = controller.suspend(true);
  CHECK(again);
  CHECK(again.value());
  CHECK(controller.start());
  CHECK(waitUntil([&] { return !controller.state().playing; }));
}

TEST_CASE("a publication after the audio has ended and been put back at the start does not play it again") {
  // The creator pressed Play once, the song ended on its own, they put the playhead back at the
  // start and did not press Play, and a render arrives for an edit they made. The Play they pressed
  // was for the audio that has ended: it does not start the audio that replaces it.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{transportConfigAt(48000U)};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 512U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playhead == 512; }));
  CHECK(waitUntil([&] { return !controller.state().playing; }));
  CHECK(controller.seek(0));
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && state.playhead == 0 && !state.playing;
  }));
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 512U)));
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, kQuiet));
  // A Play that is pressed after that plays it.
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.state().playing; }));
}

TEST_CASE("transport_controller_a_publication_that_lands_while_the_creator_hears_the_end_of_the_song_carries_the_rest_of_it") {
  // The feeder has handed over the whole song and stopped, the consumer has played 1000 frames of
  // it, and the ring holds the other 2000. A render lands and replaces the audio: the feeder drops
  // the ring and starts again from where the creator is, and has to go on filling it from there. If
  // it does not, the end of the song that the creator is still hearing is gone.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  endOfTheSongInTheRing(controller, publication, 1000U);
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 3000U)));
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 1.0, 0.002);
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.playhead == 3000 && !state.playing;
  }));
}

TEST_CASE("transport_controller_a_publication_does_not_play_the_end_of_a_song_the_creator_paused_or_has_heard") {
  {
    // The creator paused during the end of the song, and a render lands.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    CHECK(controller.pause());
    CHECK(waitUntil([&] { return controller.state().settled; }));
    const auto commandsBefore = controller.feederStats().controlCommands;
    CHECK(controller.publishAudio(publishAudio(publication, 2U, 3000U)));
    CHECK(waitUntil([&] { return controller.state().settled; }));
    CHECK(!waitUntil([&] { return controller.state().playing; }, kQuiet));
    // The feeder is given the timeline, the loop and the place, and is not told to pause what is
    // paused.
    CHECK(controller.feederStats().controlCommands == commandsBefore + 3U);
  }
  {
    // The consumer has played all of it, and a render lands that is longer than the song: the song
    // is over, and the end that the render gives it is not played.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 3000U);
    CHECK(controller.publishAudio(publishAudio(publication, 2U, 4000U)));
    CHECK(waitUntil([&] { return controller.state().settled; }));
    CHECK(!waitUntil([&] {
      static_cast<void>(readFrames(controller, 8U));
      const auto state = controller.state();
      return state.playing || state.playhead != 3000;
    }, kQuiet));
  }
  {
    // The creator clicked the very end of the song while the consumer played the end of it out, and
    // the feeder has not applied the seek, which drops what the ring holds. A render that lands
    // finds nothing left to hear, and does not play the end that it gives the song.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    controller.shutdown();
    CHECK(controller.seek(3000));
    CHECK(!controller.state().settled);
    CHECK(controller.publishAudio(publishAudio(publication, 2U, 4000U)));
    CHECK(controller.start());
    CHECK(waitUntil([&] { return controller.state().settled; }));
    CHECK(!waitUntil([&] {
      static_cast<void>(readFrames(controller, 8U));
      const auto state = controller.state();
      return state.playing || state.playhead != 3000;
    }, kQuiet));
  }
}

TEST_CASE("transport_controller_a_publication_goes_on_playing_when_the_feeder_finishes_before_it_applies_it") {
  // The creator is hearing the song and the feeder is still mixing the last of it when the render
  // lands. By the time the feeder applies the replacement it has handed over the last block and
  // stopped. The replacement has to start it again from where the creator is, as the Play the
  // feeder was in did not know.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{wrappingConfig()};
  CHECK(controller.start());
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 1088U)));
  CHECK(controller.play());
  CHECK(waitUntil([&] { return controller.ringBuffer().availableReadFrames() == 1024U; }));
  controller.shutdown();
  auto& feeder = feederOf(controller);
  static_cast<void>(readFrames(controller, 256U));
  CHECK(controller.state().playing);
  CHECK(controller.state().playhead == 1024);
  // Once the render's replacement has chosen where it starts (256), and before it is sent, the
  // consumer plays 128 more frames and the feeder hands over the last block and then stops.
  bool moved = false;
  controller.setStateSampleProbe([&](seam::rendering::MultichannelPlaybackFeeder&) {
    if (moved) return;
    moved = true;
    static_cast<void>(readFrames(controller, 128U));
    static_cast<void>(feeder.feedOnce());
    static_cast<void>(feeder.feedOnce());
  });
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 1088U)));
  controller.setStateSampleProbe({});
  CHECK(moved);
  CHECK(!feeder.playing());
  CHECK(controller.start());
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 0.256, 0.002);
}

namespace {

// The song is exactly one block long. The feeder has handed it over and has not taken its next
// turn, which is the one that says it has stopped: it reports that it is playing, at the end of
// the audio. With "playedOut" the consumer has played all of it and the ring is empty, and without
// it the ring holds the block. The feeder's service is stopped: the test is what moves it.
void oneBlockSongHandedOver(seam::authoring::TransportController& controller,
                            seam::authoring::RealtimeProjectAudioPublication& publication,
                            bool playedOut) {
  CHECK(controller.start());
  CHECK(controller.play());
  controller.shutdown();
  auto& feeder = feederOf(controller);
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 64U)));
  CHECK(feeder.feedOnce() == 64U);
  if (playedOut) static_cast<void>(readFrames(controller, 64U));
  const auto state = controller.state();
  CHECK(state.settled);
  CHECK(state.playing);
  CHECK(state.playhead == 64);
  CHECK(controller.ringBuffer().availableReadFrames() == (playedOut ? 0U : 64U));
}

}  // namespace

TEST_CASE("transport_controller_suspend_says_that_a_song_is_over_when_the_last_block_was_played_before_the_feeder_said_so") {
  // The feeder reports that it is playing, at the end of the audio, with nothing left in the ring:
  // that is a song that is over, and a Play that put it back would start it again from the
  // beginning.
  for (const bool consumerWasRunning : {true, false}) {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    oneBlockSongHandedOver(controller, publication, true);
    const auto suspended = controller.suspend(consumerWasRunning);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
}

TEST_CASE("transport_controller_suspend_says_that_they_are_playing_while_the_ring_holds_the_last_block_the_feeder_still_calls_playing") {
  // None of the song has been played yet, so the creator has still to hear it, whether or not the
  // consumer was running when the settings were changed: no consumer has taken the creator's Play
  // up (nothing has said that one runs), and that is a Play that stands.
  for (const bool consumerWasRunning : {true, false}) {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    oneBlockSongHandedOver(controller, publication, false);
    const auto suspended = controller.suspend(consumerWasRunning);
    CHECK(suspended);
    CHECK(suspended.value());
  }
}

TEST_CASE("transport_controller_a_publication_does_not_play_on_when_the_last_block_was_played_before_the_feeder_said_so") {
  // The song is over, and a render that is longer than it lands before the feeder has said so: its
  // new end is not played.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  oneBlockSongHandedOver(controller, publication, true);
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 128U)));
  CHECK(controller.start());
  CHECK(waitUntil([&] { return controller.state().settled; }));
  CHECK(!waitUntil([&] {
    static_cast<void>(readFrames(controller, 8U));
    const auto state = controller.state();
    return state.playing || state.playhead != 64;
  }, kQuiet));
}

TEST_CASE("transport_controller_suspend_says_that_a_song_is_over_when_the_creator_has_sought_to_its_end") {
  // The creator clicked the very end of the song while the consumer played the end of it out. The
  // seek drops what the ring holds, whether the feeder has applied it or not: nothing is left to hear.
  {
    // The feeder has not applied the seek.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    controller.shutdown();
    CHECK(controller.seek(3000));
    CHECK(!controller.state().settled);
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
  {
    // It has, and the consumer has not answered the request to drop the ring.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U);
    CHECK(controller.seek(3000));
    CHECK(waitUntil([&] {
      const auto state = controller.state();
      return state.settled && state.playhead == 3000 && controller.ringBuffer().resetPending();
    }));
    const auto suspended = controller.suspend(true);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
}

TEST_CASE("transport_controller_suspend_says_that_they_are_playing_a_loop_that_ends_where_the_audio_does") {
  // The loop ends with the audio: the feeder has mixed to its end, where it stays until its next
  // turn wraps it, and the consumer has played everything the feeder mixed. That is a song that
  // goes on, and not one that has ended.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  CHECK(controller.start());
  CHECK(controller.play());
  controller.shutdown();
  auto& feeder = feederOf(controller);
  CHECK(controller.publishAudio(publishAudio(publication, 1U, 192U)));
  CHECK(controller.setLoop(seam::rendering::PlaybackLoop{.enabled = true, .startFrame = 64, .endFrame = 192}));
  CHECK(feeder.feedOnce() == 64U);
  CHECK(feeder.feedOnce() == 64U);
  CHECK(feeder.feedOnce() == 64U);
  static_cast<void>(readFrames(controller, 192U));
  const auto state = controller.state();
  CHECK(state.settled);
  CHECK(state.playing);
  CHECK(state.playhead == 192);
  CHECK(controller.ringBuffer().availableReadFrames() == 0U);
  const auto suspended = controller.suspend(true);
  CHECK(suspended);
  CHECK(suspended.value());
  // The loop goes on from its start when the creator plays again.
  CHECK(controller.start());
  CHECK(controller.play());
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 0.064, 0.002);
}

TEST_CASE("transport_controller_a_publication_does_not_start_a_consumer_that_has_stopped_on_its_own") {
  // The consumer plays part of the end of the song and stops by itself (the device is unplugged),
  // and the ring still holds the rest: nobody is going to hear it, and the creator did not pause. A
  // render that lands replaces the audio. A Play carried to the replacement would start the device
  // again for a song that the creator has left.
  for (const auto consumer : {Consumer::StartedAfterPlay, Consumer::RunningBeforePlay}) {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 1000U, consumer);
    controller.setConsumerRunning(false);
    const auto commandsBefore = controller.feederStats().controlCommands;
    CHECK(controller.publishAudio(publishAudio(publication, 2U, 4000U)));
    CHECK(waitUntil([&] { return controller.state().settled; }));
    CHECK(!waitUntil([&] { return controller.state().playing; }, kQuiet));
    // The feeder is given the timeline, the loop and the place, and no Play.
    CHECK(controller.feederStats().controlCommands == commandsBefore + 3U);
  }
}

TEST_CASE("transport_controller_a_publication_stops_a_feeder_that_says_it_plays_when_no_consumer_runs") {
  // The feeder handed over the last block of the song and has not taken the turn that says it has
  // stopped, so it reports that it is playing at the end of the audio. The consumer ran and stopped
  // on its own with the block still in the ring, and a render that is longer than the song lands.
  // Nobody hears the block, so no Play is carried, and the feeder is told to stop: it would
  // otherwise be playing when the replacement reaches it.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  oneBlockSongHandedOver(controller, publication, false);
  controller.setConsumerRunning(true);
  controller.setConsumerRunning(false);
  const auto commandsBefore = controller.feederStats().controlCommands;
  CHECK(controller.publishAudio(publishAudio(publication, 2U, 128U)));
  CHECK(controller.start());
  CHECK(waitUntil([&] {
    const auto state = controller.state();
    return state.settled && !state.playing;
  }));
  CHECK(!waitUntil([&] { return controller.state().playing; }, kQuiet));
  // The timeline, the loop, the place, and a Pause for the flag that was left standing.
  CHECK(controller.feederStats().controlCommands == commandsBefore + 4U);
}

TEST_CASE("transport_controller_suspend_does_not_go_by_a_playing_flag_that_the_feeder_has_not_lowered_yet") {
  // The consumer ran and stopped on its own with the last block of the song still in the ring. The
  // feeder left its playing flag standing, and the turn that lowers it writes nothing and plays
  // nothing. The device, the audio and what the creator asked for are the same with and without
  // that turn, and so is what suspend says: nobody is going to hear the block.
  for (const bool feederHasTakenItsTurn : {false, true}) {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    oneBlockSongHandedOver(controller, publication, false);
    controller.setConsumerRunning(true);
    controller.setConsumerRunning(false);
    if (feederHasTakenItsTurn) {
      CHECK(feederOf(controller).feedOnce() == 0U);
      CHECK(!controller.state().playing);
    } else {
      CHECK(controller.state().playing);
    }
    CHECK(controller.ringBuffer().availableReadFrames() == 64U);
    const auto suspended = controller.suspend(false);
    CHECK(suspended);
    CHECK(!suspended.value());
  }
}

TEST_CASE("transport_controller_a_play_that_no_consumer_has_taken_up_goes_on_through_a_publication_and_a_suspension") {
  // The creator pressed Play, the feeder handed over the whole of a short song, and the device has
  // not been started yet: the owner says every frame that it is not running. Nobody has played any
  // of the song, and the creator has still to hear all of it. That Play stands.
  {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 0U, Consumer::NotStartedYet);
    const auto commandsBefore = controller.feederStats().controlCommands;
    CHECK(controller.publishAudio(publishAudio(publication, 2U, 4000U)));
    CHECK(waitUntil([&] { return controller.state().playing; }));
    CHECK(waitUntil([&] { return controller.state().settled; }));
    // The timeline, the loop, the place and the Play.
    CHECK(controller.feederStats().controlCommands == commandsBefore + 4U);
  }
  {
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 0U, Consumer::NotStartedYet);
    const auto suspended = controller.suspend(false);
    CHECK(suspended);
    CHECK(suspended.value());
  }
}

TEST_CASE("transport_controller_says_that_a_play_waits_for_a_consumer_until_one_runs") {
  // A feeder that has handed over all of a short song reports that it is not playing, and the ring
  // holds the song for a consumer nobody has started. Only this tells the owner of the consumer that
  // the creator's Play has not been served.
  {
    // Nothing was played, so there is no Play to wait.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    CHECK(controller.start());
    CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
    CHECK(waitUntil([&] { return controller.state().settled; }));
    CHECK(!controller.state().playAwaitsConsumer);
  }
  {
    // An owner that never says anything has a consumer that never runs: whatever Play is asked for
    // stands.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    CHECK(controller.start());
    CHECK(controller.publishAudio(publishAudio(publication, 1U, 3000U)));
    CHECK(controller.play());
    CHECK(controller.state().playAwaitsConsumer);
  }
  {
    // A Play that the owner has told no consumer took up, with every command applied, the feeder
    // finished, and all of the song in the ring.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 0U, Consumer::NotStartedYet);
    const auto state = controller.state();
    CHECK(state.settled);
    CHECK(state.available);
    CHECK(!state.playing);
    CHECK(state.playAwaitsConsumer);
    CHECK(controller.ringBuffer().availableReadFrames() == 3000U);
    // It stands through a seek, which asks for nothing else, and through a render that lands.
    CHECK(controller.seek(500));
    CHECK(controller.state().playAwaitsConsumer);
    CHECK(controller.publishAudio(publishAudio(publication, 2U, 4000U)));
    CHECK(waitUntil([&] { return controller.state().settled; }));
    CHECK(controller.state().playAwaitsConsumer);
  }
  {
    // A consumer that is running takes a Play up, whether it was started for the Play or was running
    // when it was asked for, and a consumer that has taken a Play up and stopped does not wait for it
    // again: the ring holds the end of a song that nobody is going to hear.
    for (const auto consumer : {Consumer::StartedAfterPlay, Consumer::RunningBeforePlay}) {
      seam::authoring::RealtimeProjectAudioPublication publication;
      seam::authoring::TransportController controller{mixedAheadConfig()};
      endOfTheSongInTheRing(controller, publication, 1000U, consumer);
      CHECK(!controller.state().playAwaitsConsumer);
      controller.setConsumerRunning(false);
      CHECK(!controller.state().playAwaitsConsumer);
    }
    // The same for a Play that waited, once the consumer has been started for it.
    seam::authoring::RealtimeProjectAudioPublication publication;
    seam::authoring::TransportController controller{mixedAheadConfig()};
    endOfTheSongInTheRing(controller, publication, 0U, Consumer::NotStartedYet);
    CHECK(controller.state().playAwaitsConsumer);
    controller.setConsumerRunning(true);
    CHECK(!controller.state().playAwaitsConsumer);
    controller.setConsumerRunning(false);
    CHECK(!controller.state().playAwaitsConsumer);
  }
  {
    // A Pause, a Stop, a suspension and a clear take the Play away, and a Play that is asked for
    // after that waits for the consumer as it is then: the bit that was left standing is not a Play.
    enum class Withdrawal { Pause, Stop, Suspend, Clear };
    for (const auto withdrawal :
         {Withdrawal::Pause, Withdrawal::Stop, Withdrawal::Suspend, Withdrawal::Clear}) {
      seam::authoring::RealtimeProjectAudioPublication publication;
      seam::authoring::TransportController controller{mixedAheadConfig()};
      endOfTheSongInTheRing(controller, publication, 0U, Consumer::NotStartedYet);
      CHECK(controller.state().playAwaitsConsumer);
      switch (withdrawal) {
        case Withdrawal::Pause: CHECK(controller.pause()); break;
        case Withdrawal::Stop: CHECK(controller.stop()); break;
        case Withdrawal::Suspend: CHECK(controller.suspend(false)); break;
        case Withdrawal::Clear: CHECK(controller.clearAudio()); break;
      }
      CHECK(!controller.state().playAwaitsConsumer);
      CHECK(waitUntil([&] { return controller.state().settled; }));
      CHECK(!controller.state().playAwaitsConsumer);
      CHECK(controller.play());
      CHECK(controller.state().playAwaitsConsumer);
    }
  }
}

TEST_CASE("transport_controller_a_publication_decides_what_the_creator_hears_before_it_asks_where_the_creator_is") {
  // The consumer plays the rest of the end of the song while the publication reads where it is, so
  // that by the time the place is known the ring is empty. The creator was hearing the end when the
  // publication decided what to carry, and the replacement goes on from the end of the song they
  // heard: asked the other way round, the ring is empty and the replacement waits at a place the
  // creator has already heard, with nothing to play.
  seam::authoring::RealtimeProjectAudioPublication publication;
  seam::authoring::TransportController controller{mixedAheadConfig()};
  endOfTheSongInTheRing(controller, publication, 1000U);
  controller.shutdown();
  bool drained = false;
  std::size_t consumed = 0U;
  controller.ringBuffer().setSnapshotProbe([&](int point) {
    if (point != 1 || drained) return;
    drained = true;
    std::vector<float> output(4000U);
    consumed = controller.ringBuffer().readFrames(output);
  });
  const auto published = controller.publishAudio(publishAudio(publication, 2U, 4000U));
  controller.ringBuffer().setSnapshotProbe({});
  CHECK(published);
  CHECK(drained);
  CHECK(consumed == 2000U);
  CHECK(controller.start());
  CHECK_NEAR(firstFrameHeardOnceApplied(controller), 3.0, 0.002);
}
