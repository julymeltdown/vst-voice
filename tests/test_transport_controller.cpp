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

TEST_CASE("clearing the transport's audio leaves a play that was asked for before any audio existed") {
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
