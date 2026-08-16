#pragma once

#include "seam/platform/audio_callback.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"

#include <atomic>
#include <cstddef>

namespace seam::platform {

struct RingBufferProcessorStats final {
  std::size_t callbacks{0};
  std::size_t requestedFrames{0};
  std::size_t deliveredFrames{0};
  std::size_t underflowFrames{0};
};

class RingBufferAudioProcessor final : public IAudioProcessor {
public:
  explicit RingBufferAudioProcessor(rendering::SpscAudioRingBuffer& ring)
      : ring_(ring) {}

  void process(AudioProcessContext context) noexcept override;
  void setGain(float gain) noexcept;
  [[nodiscard]] RingBufferProcessorStats stats() const noexcept;

private:
  rendering::SpscAudioRingBuffer& ring_;
  std::atomic<float> gain_{1.0F};
  std::atomic<std::size_t> callbacks_{0};
  std::atomic<std::size_t> requestedFrames_{0};
  std::atomic<std::size_t> deliveredFrames_{0};
  std::atomic<std::size_t> underflowFrames_{0};
};

}  // namespace seam::platform
