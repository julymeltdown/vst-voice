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
