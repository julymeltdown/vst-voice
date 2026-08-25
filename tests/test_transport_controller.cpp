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
