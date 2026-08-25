#include "seam/core/sha256.hpp"
#include "seam/core/realtime_audit.hpp"
#include "seam/domain/routing.hpp"
#include "seam/platform/audio_callback.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <span>
#include <string>
#include <vector>

namespace {

std::atomic<std::uint64_t> allocations{0U};
std::atomic<std::uint64_t> deallocations{0U};
thread_local bool probeCallback{false};

std::shared_ptr<const seam::rendering::RoutedPlaybackTimeline> makeTimeline(
    std::uint8_t channels, std::size_t frames) {
  auto timeline = std::make_shared<seam::rendering::RoutedPlaybackTimeline>(
      48000U);
  const auto master = seam::domain::BusId{1U};
  seam::domain::ProjectRouting routing{
      .deviceOutputChannels = channels,
      .masterBus = master,
      .buses = {seam::domain::AudioBus{.id = master,
                                       .name = "Probe Master",
                                       .channelCount = channels}},
      .sends = {},
      .deviceRoutes = {seam::domain::DeviceOutputRoute{
          .sourceBus = master,
          .matrix = seam::domain::RoutingMatrix::identity(channels)}},
  };
  auto pcm = std::make_shared<seam::rendering::RoutedPcm>();
  pcm->sampleRate = 48000U;
  pcm->channelCount = channels;
  pcm->interleavedSamples.assign(frames * channels, 0.0F);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    for (std::uint8_t channel = 0U; channel < channels; ++channel) {
      pcm->interleavedSamples[frame * channels + channel] =
          static_cast<float>((frame + channel + 1U) % 31U) / 31.0F;
    }
  }
  auto configured = timeline->configure(
      std::move(routing),
      std::vector<seam::rendering::RoutedPlaybackClip>{
          seam::rendering::RoutedPlaybackClip{
              .id = "probe",
              .pcm = std::move(pcm),
              .outputRoute = seam::domain::TrackOutputRoute{
                  .bus = master,
                  .matrix = seam::domain::RoutingMatrix::identity(channels)},
          }});
  return configured ? std::shared_ptr<const seam::rendering::RoutedPlaybackTimeline>{
                          std::move(timeline)}
                    : nullptr;
}

}

void* operator new(std::size_t size) {
  if (probeCallback) allocations.fetch_add(1U, std::memory_order_relaxed);
  if (auto* pointer = std::malloc(size == 0U ? 1U : size)) return pointer;
  throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
  if (probeCallback) allocations.fetch_add(1U, std::memory_order_relaxed);
  if (auto* pointer = std::malloc(size == 0U ? 1U : size)) return pointer;
  throw std::bad_alloc{};
}

void operator delete(void* pointer) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
  if (probeCallback) deallocations.fetch_add(1U, std::memory_order_relaxed);
  std::free(pointer);
}

int main(int argc, char** argv) {
  const auto outputPath = argc > 1
                              ? std::filesystem::path{argv[1]}
                              : std::filesystem::path{"realtime-allocation-probe.json"};
  constexpr std::array<std::size_t, 4> blockSizes{64U, 128U, 256U, 512U};
  constexpr std::array<std::uint8_t, 4> channels{1U, 2U, 4U, 8U};
  constexpr std::size_t blocksPerConfiguration = 6250U;

  std::uint64_t callbacks = 0U;
  std::uint64_t requestedFrames = 0U;
  std::uint64_t deliveredFrames = 0U;
  std::uint64_t underflowFrames = 0U;
  std::uint64_t unexpectedUnderflowFrames = 0U;
  std::uint64_t intentionalResetZeroFillFrames = 0U;
  std::uint64_t nonFiniteSamples = 0U;
  bool finite = true;
  seam::core::RealtimeAuditCounters audit;

  for (const auto blockFrames : blockSizes) {
    for (const auto outputChannels : channels) {
      seam::rendering::SpscInterleavedAudioRingBuffer ring{
          blockFrames * 4U, outputChannels};
      const auto timeline = makeTimeline(outputChannels, blockFrames * 32U);
      if (timeline == nullptr) return 1;
      seam::rendering::MultichannelPlaybackFeeder feeder{
          ring, 48000U, outputChannels, blockFrames};
      if (!feeder.setTimeline(timeline) || !feeder.setPlaying(true)) return 1;
      seam::platform::MultichannelRingBufferAudioProcessor processor{
          ring, blockFrames};
      std::array<std::vector<float>, 8U> buffers;
      std::array<std::span<float>, 8U> views;
      for (std::uint8_t channel = 0U; channel < outputChannels; ++channel) {
        buffers[channel].assign(blockFrames, 0.0F);
        views[channel] = buffers[channel];
      }
      for (std::size_t block = 0U; block < blocksPerConfiguration; ++block) {
        bool intentionalSilence = false;
        if (block % 997U == 0U) {
          static_cast<void>(feeder.seek(static_cast<seam::time::SampleFrame>(
              blockFrames * 2U)));
          static_cast<void>(feeder.setLoop(seam::rendering::PlaybackLoop{
              .enabled = true,
              .startFrame = static_cast<seam::time::SampleFrame>(blockFrames),
              .endFrame = static_cast<seam::time::SampleFrame>(blockFrames * 8U)}));
        }
        if (block % 1231U == 0U) {
          static_cast<void>(feeder.setTimeline(timeline));
        }
        if (block % 1741U == 0U) {
          static_cast<void>(feeder.setPlaying(false));
          intentionalSilence = true;
        } else if (block % 1741U == 1U) {
          static_cast<void>(feeder.setPlaying(true));
        }
        static_cast<void>(feeder.feedToWatermark(blockFrames * 2U));
        const auto beforeUnderflow = processor.stats().underflowFrames;
        probeCallback = true;
        {
          seam::core::RealtimeAuditScope auditScope{audit};
          processor.process(seam::platform::AudioProcessContext{
              .sampleRate = 48000.0,
              .frameCount = blockFrames,
              .left = views[0],
              .right = outputChannels > 1U ? views[1] : std::span<float>{},
              .outputs = std::span<std::span<float>>{views}.first(outputChannels),
          });
        }
        probeCallback = false;
        const auto callbackUnderflow =
            processor.stats().underflowFrames - beforeUnderflow;
        if (intentionalSilence || block % 997U == 0U || block % 1231U == 0U) {
          intentionalResetZeroFillFrames += callbackUnderflow;
        } else {
          unexpectedUnderflowFrames += callbackUnderflow;
        }
        for (std::uint8_t channel = 0U; channel < outputChannels; ++channel) {
          for (const auto sample : buffers[channel]) {
            if (!std::isfinite(sample)) {
              finite = false;
              ++nonFiniteSamples;
            }
          }
        }
        ++callbacks;
      }
      const auto stats = processor.stats();
      requestedFrames += stats.requestedFrames;
      deliveredFrames += stats.deliveredFrames;
      underflowFrames += stats.underflowFrames;
    }
  }

  const auto executableHash = seam::core::sha256File(
      std::filesystem::path{argv[0]}, 64ULL * 1024ULL * 1024ULL);
  const auto allocationCount = allocations.load(std::memory_order_relaxed);
  const auto deallocationCount =
      deallocations.load(std::memory_order_relaxed);
  const auto lockAttempts = audit.lockAttempts.load(std::memory_order_relaxed);
  const auto fileIoCalls = audit.fileIoCalls.load(std::memory_order_relaxed);
  const auto loggerCalls = audit.loggerCalls.load(std::memory_order_relaxed);
  const bool pass = finite && allocationCount == 0U &&
                    deallocationCount == 0U && callbacks == 100000U &&
                    unexpectedUnderflowFrames == 0U && lockAttempts == 0U &&
                    fileIoCalls == 0U && loggerCalls == 0U;
  std::ofstream output(outputPath, std::ios::trunc);
  output << "{\n"
         << "  \"schemaVersion\": 1,\n"
         << "  \"callbacks\": " << callbacks << ",\n"
         << "  \"requestedFrames\": " << requestedFrames << ",\n"
         << "  \"deliveredFrames\": " << deliveredFrames << ",\n"
         << "  \"underflowFrames\": " << underflowFrames << ",\n"
         << "  \"unexpectedUnderflowFrames\": " << unexpectedUnderflowFrames << ",\n"
         << "  \"intentionalResetZeroFillFrames\": "
         << intentionalResetZeroFillFrames << ",\n"
         << "  \"callbackAllocations\": " << allocationCount << ",\n"
         << "  \"callbackDeallocations\": " << deallocationCount << ",\n"
         << "  \"callbackLocks\": " << lockAttempts << ",\n"
         << "  \"callbackFileIo\": " << fileIoCalls << ",\n"
         << "  \"callbackLogging\": " << loggerCalls << ",\n"
         << "  \"nonFiniteSamples\": " << nonFiniteSamples << ",\n"
         << "  \"executableSha256\": \""
         << (executableHash ? executableHash.value() : std::string{}) << "\",\n"
         << "  \"result\": \"" << (pass ? "PASS" : "FAIL") << "\"\n"
         << "}\n";
  output.close();
  std::cout << "callbacks=" << callbacks
            << " allocations=" << allocationCount
            << " deallocations=" << deallocationCount
            << " locks=" << lockAttempts
            << " file_io=" << fileIoCalls
            << " logging=" << loggerCalls
            << " underflow=" << underflowFrames
            << " unexpected_underflow=" << unexpectedUnderflowFrames
            << " result=" << (pass ? "PASS" : "FAIL") << '\n';
  return pass ? 0 : 1;
}
