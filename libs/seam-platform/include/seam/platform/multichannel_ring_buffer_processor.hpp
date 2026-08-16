#pragma once

#include "seam/platform/audio_callback.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace seam::platform {

struct MultichannelRingProcessorStats final {
  std::uint64_t callbacks{0U};
  std::uint64_t requestedFrames{0U};
  std::uint64_t deliveredFrames{0U};
  std::uint64_t underflowFrames{0U};
  std::uint64_t channelMismatchCallbacks{0U};
};

class MultichannelRingBufferAudioProcessor final : public IAudioProcessor {
public:
  MultichannelRingBufferAudioProcessor(
      rendering::SpscInterleavedAudioRingBuffer& ring,
      std::size_t maximumBlockFrames);

  void process(AudioProcessContext context) noexcept override;
  void setGain(float gain) noexcept;
  [[nodiscard]] MultichannelRingProcessorStats stats() const noexcept;

private:
  rendering::SpscInterleavedAudioRingBuffer& ring_;
  std::vector<float> scratch_;
  std::atomic<float> gain_{1.0F};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> requestedFrames_{0U};
  std::atomic<std::uint64_t> deliveredFrames_{0U};
  std::atomic<std::uint64_t> underflowFrames_{0U};
  std::atomic<std::uint64_t> channelMismatchCallbacks_{0U};
};

}  // namespace seam::platform
