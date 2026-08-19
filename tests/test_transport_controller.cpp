#include "test_framework.hpp"

#include "seam/authoring/transport_controller.hpp"
#include "seam/authoring/render_coordinator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace {

seam::authoring::RealtimeProjectAudioPublication::ReadHandle publishAudio(
    seam::authoring::RealtimeProjectAudioPublication& publication,
    std::uint64_t revision, std::size_t frames, float offset = 0.0F) {
  seam::authoring::PublishedProjectAudio audio;
  audio.projectRevision = revision;
  audio.state = seam::authoring::RenderState::Ready;
  audio.result.sampleRate = 48000U;
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
