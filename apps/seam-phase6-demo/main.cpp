#include "seam/domain/routing.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/platform/audio_callback.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 48000U;
constexpr std::size_t kFrames = 48000U;

seam::domain::RoutingMatrix routeTwoToFour(bool front) {
  seam::domain::RoutingMatrix matrix{
      .sourceChannels = 2U,
      .destinationChannels = 4U,
      .gains = std::vector<float>(8U, 0.0F),
  };
  if (front) {
    matrix.setGain(0U, 0U, 1.0F);
    matrix.setGain(1U, 1U, 1.0F);
  } else {
    matrix.setGain(2U, 0U, 1.0F);
    matrix.setGain(3U, 1U, 1.0F);
  }
  return matrix;
}

seam::domain::ProjectRouting makeRouting() {
  return seam::domain::ProjectRouting{
      .deviceOutputChannels = 4U,
      .masterBus = seam::domain::BusId{3U},
      .buses = {
          seam::domain::AudioBus{.id = seam::domain::BusId{1U},
                                 .name = "Vocal Bus",
                                 .channelCount = 2U},
          seam::domain::AudioBus{.id = seam::domain::BusId{2U},
                                 .name = "Backing Bus",
                                 .channelCount = 2U},
          seam::domain::AudioBus{.id = seam::domain::BusId{3U},
                                 .name = "4ch Master",
                                 .channelCount = 4U},
      },
      .sends = {
          seam::domain::BusSend{.sourceBus = seam::domain::BusId{1U},
                                .destinationBus = seam::domain::BusId{3U},
                                .matrix = routeTwoToFour(true)},
          seam::domain::BusSend{.sourceBus = seam::domain::BusId{2U},
                                .destinationBus = seam::domain::BusId{3U},
                                .matrix = routeTwoToFour(false)},
      },
      .deviceRoutes = {
          seam::domain::DeviceOutputRoute{
              .sourceBus = seam::domain::BusId{3U},
              .matrix = seam::domain::RoutingMatrix::identity(4U)},
      },
  };
}

std::shared_ptr<const seam::rendering::RoutedPcm> makeTone(
    double frequency, float amplitude) {
  auto pcm = std::make_shared<seam::rendering::RoutedPcm>();
  pcm->sampleRate = kSampleRate;
  pcm->startFrame = 0;
  pcm->channelCount = 1U;
  pcm->interleavedSamples.resize(kFrames);
  constexpr double twoPi = 6.28318530717958647692;
  for (std::size_t frame = 0U; frame < kFrames; ++frame) {
    const auto envelope = std::min(1.0, static_cast<double>(frame) / 512.0) *
        std::min(1.0, static_cast<double>(kFrames - frame - 1U) / 512.0);
    pcm->interleavedSamples[frame] = amplitude * static_cast<float>(
        std::sin(twoPi * frequency * static_cast<double>(frame) /
                 static_cast<double>(kSampleRate)) * envelope);
  }
  return pcm;
}

void writeSummary(const std::filesystem::path& path,
                  const seam::platform::MultichannelRingProcessorStats& stats,
                  const std::array<double, 4U>& rms) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << "{\n"
         << "  \"phase\": \"6\",\n"
         << "  \"outputChannels\": 4,\n"
         << "  \"frames\": " << kFrames << ",\n"
         << "  \"callbackDeliveredFrames\": " << stats.deliveredFrames << ",\n"
         << "  \"callbackUnderflowFrames\": " << stats.underflowFrames << ",\n"
         << "  \"channelRms\": [" << rms[0] << ", " << rms[1] << ", "
         << rms[2] << ", " << rms[3] << "]\n"
         << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path outputDirectory = "out/phase6";
  for (int index = 1; index < argc; ++index) {
    if (std::string_view{argv[index]} == "--output" && index + 1 < argc) {
      outputDirectory = argv[++index];
    }
  }
  std::error_code error;
  std::filesystem::create_directories(outputDirectory, error);
  if (error) {
    std::cerr << "Unable to create output directory\n";
    return 1;
  }

  auto routing = makeRouting();
  const auto routingValidation = routing.validate();
  if (!routingValidation) {
    std::cerr << routingValidation.error().message << '\n';
    return 2;
  }

  auto timeline = std::make_shared<seam::rendering::RoutedPlaybackTimeline>(kSampleRate);
  const auto configured = timeline->configure(
      routing,
      std::vector<seam::rendering::RoutedPlaybackClip>{
          seam::rendering::RoutedPlaybackClip{
              .id = "official-voice-01",
              .pcm = makeTone(220.0, 0.72F),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{1U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(-0.35F),
              },
          },
          seam::rendering::RoutedPlaybackClip{
              .id = "backing-track",
              .pcm = makeTone(110.0, 0.42F),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = seam::domain::BusId{2U},
                  .matrix = seam::domain::RoutingMatrix::monoToStereo(0.35F),
              },
          },
      });
  if (!configured) {
    std::cerr << configured.error().message << '\n';
    return 3;
  }

  seam::rendering::RoutingWorkspace workspace;
  if (!workspace.prepare(timeline->routing(), kFrames)) return 4;
  std::vector<float> rendered(kFrames * 4U, 0.0F);
  if (!timeline->mix(0, kFrames, rendered, workspace)) return 5;
  if (!seam::voicebank::writePcm16Wav(
          outputDirectory / "phase6-four-channel-routing.wav",
          kSampleRate, 4U, rendered)) {
    return 6;
  }

  seam::rendering::SpscInterleavedAudioRingBuffer ring{8192U, 4U};
  seam::rendering::MultichannelPlaybackFeeder feeder{ring, kSampleRate, 4U, 512U};
  if (!feeder.setTimeline(timeline) || !feeder.setPlaying(true)) return 7;
  static_cast<void>(feeder.feedToWatermark(4096U));

  seam::platform::MultichannelRingBufferAudioProcessor processor{ring, 512U};
  std::array<std::vector<float>, 4U> channels{
      std::vector<float>(512U), std::vector<float>(512U),
      std::vector<float>(512U), std::vector<float>(512U)};
  std::array<std::span<float>, 4U> views{
      channels[0], channels[1], channels[2], channels[3]};
  processor.process(seam::platform::AudioProcessContext{
      .sampleRate = static_cast<double>(kSampleRate),
      .frameCount = 512U,
      .left = channels[0],
      .right = channels[1],
      .outputs = views,
  });

  std::array<double, 4U> rms{};
  for (std::size_t channel = 0U; channel < 4U; ++channel) {
    long double square = 0.0;
    for (std::size_t frame = 0U; frame < kFrames; ++frame) {
      const auto sample = rendered[frame * 4U + channel];
      square += static_cast<long double>(sample) * sample;
    }
    rms[channel] = std::sqrt(static_cast<double>(square / kFrames));
  }
  writeSummary(outputDirectory / "phase6-summary.json", processor.stats(), rms);

  seam::domain::Project project{seam::domain::ProjectId{600U}, "Phase 6 Routing"};
  project.routing() = routing;
  project.vocalTracks().push_back(seam::domain::VocalTrack{
      .id = seam::domain::TrackId{601U},
      .name = "Official Voice 01",
      .voicebank = {},
      .character = {},
      .regions = {},
      .gainDb = 0.0F,
      .pan = -0.35F,
      .muted = false,
      .solo = false,
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(-0.35F)},
  });
  seam::formats::ProjectJsonCodec codec;
  if (!codec.save(project, outputDirectory / "phase6-routing-project.json")) return 8;

  std::cout << "Project SEAM Phase 6 multichannel routing\n"
            << "  buses: " << routing.buses.size() << '\n'
            << "  sends: " << routing.sends.size() << '\n'
            << "  output channels: 4\n"
            << "  callback underflow: " << processor.stats().underflowFrames << '\n';
  return processor.stats().underflowFrames == 0U ? 0 : 9;
}
