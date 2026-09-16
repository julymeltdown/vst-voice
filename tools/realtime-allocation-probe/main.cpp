#include "seam/core/sha256.hpp"
#include "seam/core/realtime_audit.hpp"
#include "seam/domain/routing.hpp"
#include "seam/platform/audio_callback.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"
#if defined(SEAM_CANDIDATE_AUDITION_PROBE)
#include "seam/native_ui/candidate_audition.hpp"
#endif

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

#if !defined(SEAM_CANDIDATE_AUDITION_PROBE)
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
#endif

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
#if defined(SEAM_CANDIDATE_AUDITION_PROBE)
  // Confirm that the interception is live before trusting a zero count.
  probeCallback = true;
  void* volatile control = ::operator new(8U);
  ::operator delete(control);
  probeCallback = false;
  const bool interceptionWorks = allocations.load() == 1U && deallocations.load() == 1U;
  allocations.store(0U);
  deallocations.store(0U);
  seam::core::RealtimeAuditCounters candidateAudit;
  std::uint64_t candidateCallbacks = 0U, candidateConfigurations = 0U, outputMismatches = 0U;
  for (const auto rate : {8000U, 48000U, 192000U}) {
    auto audio = std::make_shared<seam::voicebank::AudioBuffer>();
    audio->sampleRate = rate;
    audio->channels = 1U;
    audio->bitsPerSample = 32U;
    audio->interleaved.resize(2048U);
    for (std::size_t index = 0U; index < audio->interleaved.size(); ++index)
      audio->interleaved[index] = static_cast<float>(index % 97U) / 96.0F - 0.5F;
    for (const auto blockSize : {1U, 64U, 257U, 1024U}) {
      for (const auto channelCount : {1U, 2U}) {
        ++candidateConfigurations;
        auto prepared = seam::native_ui::CandidateAuditionProcessor::create(audio, 7U, 2035U, 0.25F);
        if (!prepared) return 1;
        std::array<std::vector<float>, 2> samples{std::vector<float>(blockSize), std::vector<float>(blockSize)};
        std::array<std::span<float>, 2> views{samples[0], samples[1]};
        const auto blocks = 2028U / blockSize + 3U;
        for (std::size_t block = 0U; block < blocks; ++block) {
          for (auto& channel : samples) std::fill(channel.begin(), channel.end(), 9.0F);
          const seam::platform::AudioProcessContext context{
              .sampleRate = static_cast<double>(rate), .frameCount = blockSize,
              .left = views[0], .right = channelCount == 2U ? views[1] : std::span<float>{},
              .outputs = block % 2U == 0U ? std::span<std::span<float>>{views}.first(channelCount)
                                        : std::span<std::span<float>>{}};
          probeCallback = true;
          {
            seam::core::RealtimeAuditScope scope{candidateAudit};
            prepared.value()->process(context);
          }
          probeCallback = false;
          ++candidateCallbacks;
          for (std::size_t index = 0U; index < blockSize; ++index) {
            const auto frame = block * blockSize + index;
            float expected = 0.0F;
            if (frame < 2028U) {
              const auto fade = std::min<std::size_t>(rate / 200U, 1014U);
              const auto envelope = std::min({1.0, static_cast<double>(frame) / static_cast<double>(fade),
                  static_cast<double>(2027U - frame) / static_cast<double>(fade)});
              expected = static_cast<float>(static_cast<double>(audio->interleaved[frame + 7U]) * 0.25 * envelope);
            }
            for (std::size_t channel = 0U; channel < channelCount; ++channel)
              if (samples[channel][index] != expected) ++outputMismatches;
          }
        }
        if (!prepared.value()->finished() || prepared.value()->failed()) ++outputMismatches;
      }
    }
  }
  const bool candidatePass = interceptionWorks && outputMismatches == 0U && candidateCallbacks == 12474U &&
      candidateConfigurations == 24U &&
      allocations.load() == 0U && deallocations.load() == 0U && candidateAudit.lockAttempts.load() == 0U &&
      candidateAudit.fileIoCalls.load() == 0U && candidateAudit.loggerCalls.load() == 0U;
  const auto hash = seam::core::sha256File(argv[0]);
  std::ofstream report(outputPath, std::ios::trunc);
  report << "{\n  \"schemaVersion\": 1,\n  \"processor\": \"candidate-audition\",\n"
         << "  \"interceptionSelfTest\": " << (interceptionWorks ? "true" : "false") << ",\n"
         << "  \"callbacks\": " << candidateCallbacks << ",\n"
         << "  \"configurations\": " << candidateConfigurations << ",\n"
         << "  \"callbackAllocations\": " << allocations.load() << ",\n"
         << "  \"callbackDeallocations\": " << deallocations.load() << ",\n"
         << "  \"callbackLocks\": " << candidateAudit.lockAttempts.load() << ",\n"
         << "  \"callbackFileIo\": " << candidateAudit.fileIoCalls.load() << ",\n"
         << "  \"callbackLogging\": " << candidateAudit.loggerCalls.load() << ",\n"
         << "  \"outputMismatches\": " << outputMismatches << ",\n"
         << "  \"executableSha256\": \"" << (hash ? hash.value() : std::string{}) << "\",\n"
         << "  \"result\": \"" << (candidatePass ? "PASS" : "FAIL") << "\"\n}\n";
  report.close();
  std::cout << "candidate_callbacks=" << candidateCallbacks << " allocations=" << allocations.load()
            << " deallocations=" << deallocations.load() << " mismatches=" << outputMismatches << '\n';
  return candidatePass && hash && report ? 0 : 1;
#else
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
#endif
}
