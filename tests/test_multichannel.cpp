#include "test_framework.hpp"

#include "seam/domain/routing.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/platform/audio_callback.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

namespace {

seam::domain::ProjectRouting fourChannelRouting() {
  using seam::domain::AudioBus;
  using seam::domain::BusId;
  using seam::domain::BusSend;
  using seam::domain::DeviceOutputRoute;
  using seam::domain::ProjectRouting;
  using seam::domain::RoutingMatrix;

  RoutingMatrix vocalToMaster{
      .sourceChannels = 2U,
      .destinationChannels = 4U,
      .gains = {
          1.0F, 0.0F,
          0.0F, 1.0F,
          0.0F, 0.0F,
          0.0F, 0.0F,
      },
  };
  RoutingMatrix musicToMaster{
      .sourceChannels = 2U,
      .destinationChannels = 4U,
      .gains = {
          0.0F, 0.0F,
          0.0F, 0.0F,
          1.0F, 0.0F,
          0.0F, 1.0F,
      },
  };

  return ProjectRouting{
      .deviceOutputChannels = 4U,
      .masterBus = BusId{3U},
      .buses = {
          AudioBus{.id = BusId{1U}, .name = "Vocals", .channelCount = 2U},
          AudioBus{.id = BusId{2U}, .name = "Music", .channelCount = 2U},
          AudioBus{.id = BusId{3U}, .name = "Master 4ch", .channelCount = 4U},
      },
      .sends = {
          BusSend{.sourceBus = BusId{1U},
                  .destinationBus = BusId{3U},
                  .matrix = vocalToMaster},
          BusSend{.sourceBus = BusId{2U},
                  .destinationBus = BusId{3U},
                  .matrix = musicToMaster},
      },
      .deviceRoutes = {
          DeviceOutputRoute{.sourceBus = BusId{3U},
                            .matrix = RoutingMatrix::identity(4U)},
      },
  };
}

std::shared_ptr<const seam::rendering::RoutedPcm> monoPcm(float value,
                                                          std::size_t frames) {
  auto pcm = std::make_shared<seam::rendering::RoutedPcm>();
  pcm->sampleRate = 48000U;
  pcm->startFrame = 0;
  pcm->channelCount = 1U;
  pcm->interleavedSamples.assign(frames, value);
  return pcm;
}

}  // namespace

TEST_CASE("routing graph rejects cycles and preserves deterministic order") {
  auto routing = fourChannelRouting();
  CHECK(routing.validate());
  const auto order = routing.topologicalOrder();
  CHECK(order);
  CHECK(order.value().size() == 3U);
  CHECK(order.value().back() == seam::domain::BusId{3U});

  routing.sends.push_back(seam::domain::BusSend{
      .sourceBus = seam::domain::BusId{3U},
      .destinationBus = seam::domain::BusId{1U},
      .matrix = seam::domain::RoutingMatrix{
          .sourceChannels = 4U,
          .destinationChannels = 2U,
          .gains = {1.0F, 0.0F, 0.0F, 0.0F,
                    0.0F, 1.0F, 0.0F, 0.0F},
      },
  });
  CHECK(!routing.validate());
}

TEST_CASE("multichannel timeline routes independent buses to four outputs") {
  auto routing = fourChannelRouting();
  seam::rendering::RoutedPlaybackTimeline timeline{48000U};
  std::vector<seam::rendering::RoutedPlaybackClip> clips;
  clips.push_back(seam::rendering::RoutedPlaybackClip{
      .id = "voice",
      .pcm = monoPcm(1.0F, 64U),
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(0.0F),
      },
  });
  clips.push_back(seam::rendering::RoutedPlaybackClip{
      .id = "music",
      .pcm = monoPcm(0.5F, 64U),
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{2U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(0.0F),
      },
  });
  CHECK(timeline.configure(std::move(routing), std::move(clips)));

  seam::rendering::RoutingWorkspace workspace;
  CHECK(workspace.prepare(timeline.routing(), 64U));
  std::vector<float> output(64U * 4U, 0.0F);
  CHECK(timeline.mix(0, 64U, output, workspace));
  for (std::size_t frame = 0U; frame < 64U; ++frame) {
    CHECK(output[frame * 4U] > 0.70F);
    CHECK(output[frame * 4U + 1U] > 0.70F);
    CHECK(output[frame * 4U + 2U] > 0.35F);
    CHECK(output[frame * 4U + 3U] > 0.35F);
    CHECK(output[frame * 4U] > output[frame * 4U + 2U]);
  }
}

TEST_CASE("multichannel feeder ring and callback preserve channel order") {
  auto timeline = std::make_shared<seam::rendering::RoutedPlaybackTimeline>(48000U);
  CHECK(timeline->configure(
      fourChannelRouting(),
      std::vector<seam::rendering::RoutedPlaybackClip>{
          seam::rendering::RoutedPlaybackClip{
              .id = "voice",
              .pcm = monoPcm(0.8F, 1024U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{1U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(-1.0F),
              },
          },
          seam::rendering::RoutedPlaybackClip{
              .id = "music",
              .pcm = monoPcm(0.3F, 1024U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{2U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(1.0F),
              },
          },
      }));

  seam::rendering::SpscInterleavedAudioRingBuffer ring{2048U, 4U};
  seam::rendering::MultichannelPlaybackFeeder feeder{ring, 48000U, 4U, 256U};
  CHECK(feeder.setTimeline(timeline));
  CHECK(feeder.setPlaying(true));
  CHECK(feeder.feedToWatermark(1024U) > 0U);

  seam::platform::MultichannelRingBufferAudioProcessor processor{ring, 256U};
  std::array<std::vector<float>, 4U> channels{
      std::vector<float>(256U), std::vector<float>(256U),
      std::vector<float>(256U), std::vector<float>(256U)};
  std::array<std::span<float>, 4U> views{
      channels[0], channels[1], channels[2], channels[3]};
  processor.process(seam::platform::AudioProcessContext{
      .sampleRate = 48000.0,
      .frameCount = 256U,
      .left = channels[0],
      .right = channels[1],
      .outputs = views,
  });
  const auto stats = processor.stats();
  CHECK(stats.deliveredFrames == 256U);
  CHECK(stats.underflowFrames == 0U);
  CHECK(channels[0][100] > 0.7F);
  CHECK(std::abs(channels[1][100]) < 0.01F);
  CHECK(std::abs(channels[2][100]) < 0.01F);
  CHECK(channels[3][100] > 0.29F);
}

TEST_CASE("project schema four persists buses sends and track output matrix") {
  seam::domain::Project project{seam::domain::ProjectId{44U}, "Routing project"};
  project.routing() = fourChannelRouting();
  project.vocalTracks().push_back(seam::domain::VocalTrack{
      .id = seam::domain::TrackId{45U},
      .name = "Voice",
      .voicebank = {},
      .character = {},
      .regions = {},
      .gainDb = -1.0F,
      .pan = 0.0F,
      .muted = false,
      .solo = false,
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(-0.25F),
      },
  });
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  CHECK(encoded.value().find("\"schemaVersion\": 4") != std::string::npos);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
}

TEST_CASE("bus solo includes dependencies and output path but excludes siblings") {
  auto routing = fourChannelRouting();
  routing.findBus(seam::domain::BusId{1U})->solo = true;
  seam::rendering::RoutedPlaybackTimeline timeline{48000U};
  CHECK(timeline.configure(
      std::move(routing),
      std::vector<seam::rendering::RoutedPlaybackClip>{
          seam::rendering::RoutedPlaybackClip{
              .id = "voice",
              .pcm = monoPcm(1.0F, 32U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{1U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(0.0F),
              },
          },
          seam::rendering::RoutedPlaybackClip{
              .id = "music",
              .pcm = monoPcm(0.5F, 32U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{2U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(0.0F),
              },
          },
      }));
  seam::rendering::RoutingWorkspace workspace;
  CHECK(workspace.prepare(timeline.routing(), 32U));
  std::vector<float> output(32U * 4U, 0.0F);
  CHECK(timeline.mix(0, 32U, output, workspace));
  for (std::size_t frame = 0U; frame < 32U; ++frame) {
    CHECK(output[frame * 4U] > 0.70F);
    CHECK(output[frame * 4U + 1U] > 0.70F);
    CHECK(std::abs(output[frame * 4U + 2U]) < 0.001F);
    CHECK(std::abs(output[frame * 4U + 3U]) < 0.001F);
  }
}

TEST_CASE("clip solo mutes other clips before bus routing") {
  auto routing = fourChannelRouting();
  seam::rendering::RoutedPlaybackTimeline timeline{48000U};
  CHECK(timeline.configure(
      std::move(routing),
      std::vector<seam::rendering::RoutedPlaybackClip>{
          seam::rendering::RoutedPlaybackClip{
              .id = "voice",
              .pcm = monoPcm(1.0F, 16U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{1U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(-1.0F),
              },
              .solo = true,
          },
          seam::rendering::RoutedPlaybackClip{
              .id = "music",
              .pcm = monoPcm(0.5F, 16U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{2U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(1.0F),
              },
          },
      }));
  seam::rendering::RoutingWorkspace workspace;
  CHECK(workspace.prepare(timeline.routing(), 16U));
  std::vector<float> output(16U * 4U, 0.0F);
  CHECK(timeline.mix(0, 16U, output, workspace));
  CHECK(output[0] > 0.99F);
  CHECK(std::abs(output[1]) < 0.001F);
  CHECK(std::abs(output[2]) < 0.001F);
  CHECK(std::abs(output[3]) < 0.001F);
}

TEST_CASE("multichannel feeder stops after non-looping timeline end") {
  auto timeline = std::make_shared<seam::rendering::RoutedPlaybackTimeline>(48000U);
  auto routing = fourChannelRouting();
  CHECK(timeline->configure(
      std::move(routing),
      std::vector<seam::rendering::RoutedPlaybackClip>{
          seam::rendering::RoutedPlaybackClip{
              .id = "short",
              .pcm = monoPcm(0.25F, 20U),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{1U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(0.0F),
              },
          },
      }));
  seam::rendering::SpscInterleavedAudioRingBuffer ring{64U, 4U};
  seam::rendering::MultichannelPlaybackFeeder feeder{ring, 48000U, 4U, 32U};
  CHECK(feeder.setTimeline(timeline));
  CHECK(feeder.setPlaying(true));
  CHECK(feeder.feedOnce() == 32U);
  CHECK(!feeder.playing());
  CHECK(feeder.playhead() == 20);
  CHECK(feeder.feedOnce() == 0U);
}

TEST_CASE("interleaved ring rejects partial frames") {
  seam::rendering::SpscInterleavedAudioRingBuffer ring{8U, 4U};
  const std::array<float, 5U> invalidInput{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
  CHECK(ring.writeFrames(invalidInput) == 0U);
  std::array<float, 5U> invalidOutput{1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
  CHECK(ring.readFrames(invalidOutput) == 0U);
  for (const auto sample : invalidOutput) CHECK(sample == 0.0F);
}
