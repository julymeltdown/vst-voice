#pragma once

#include "seam/core/result.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace seam::platform {

struct AudioInputProcessContext final {
  double sampleRate{48000.0};
  std::size_t frameCount{0U};
  std::span<const float> mono;
};

class IAudioInputProcessor {
public:
  virtual ~IAudioInputProcessor() = default;
  virtual void process(AudioInputProcessContext context) noexcept = 0;
};

struct AudioInputDeviceConfig final {
  std::uint32_t sampleRate{48000U};
  std::size_t blockFrames{256U};
  std::string applicationName{"Project SEAM"};
  std::string streamName{"SEAM Voicebank Recording"};
};

struct AudioInputDeviceInfo final {
  std::string backend{"unopened"};
  std::string deviceName{"default"};
  std::uint32_t sampleRate{0U};
  std::size_t blockFrames{0U};
  bool physical{false};
};

struct AudioInputDeviceStats final {
  std::uint64_t callbacks{0U};
  std::uint64_t frames{0U};
  std::uint64_t readFailures{0U};
};

class IAudioInputDevice {
public:
  virtual ~IAudioInputDevice() = default;
  [[nodiscard]] virtual core::Result<void> open(
      const AudioInputDeviceConfig& config, IAudioInputProcessor& processor) = 0;
  [[nodiscard]] virtual core::Result<void> start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual AudioInputDeviceInfo info() const = 0;
  [[nodiscard]] virtual AudioInputDeviceStats stats() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IAudioInputDevice> createSystemAudioInputDevice();
[[nodiscard]] std::unique_ptr<IAudioInputDevice> createThreadedSilenceInputDevice();

}  // namespace seam::platform
