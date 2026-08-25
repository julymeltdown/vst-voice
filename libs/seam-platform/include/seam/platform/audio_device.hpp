#pragma once

#include "seam/core/result.hpp"
#include "seam/platform/audio_callback.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace seam::platform {

struct AudioDeviceConfig final {
  std::string deviceId;
  std::uint32_t sampleRate{48000U};
  std::size_t blockFrames{256U};
  std::uint8_t outputChannels{2U};
  std::string applicationName{"Project SEAM"};
  std::string streamName{"SEAM Playback"};
};

struct AudioDeviceInfo final {
  std::string backend{"unopened"};
  std::string deviceId{"unknown"};
  std::string deviceName{"default"};
  std::uint32_t sampleRate{0U};
  std::size_t blockFrames{0U};
  std::uint8_t outputChannels{0U};
  bool physical{false};
};

struct AudioDeviceStats final {
  std::uint64_t callbacks{0U};
  std::uint64_t frames{0U};
  std::uint64_t writeFailures{0U};
  std::uint64_t xruns{0U};
};

class IAudioDevice {
public:
  virtual ~IAudioDevice() = default;

  [[nodiscard]] virtual core::Result<void> open(
      const AudioDeviceConfig& config, IAudioProcessor& processor) = 0;
  [[nodiscard]] virtual core::Result<void> start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual AudioDeviceInfo info() const = 0;
  [[nodiscard]] virtual AudioDeviceStats stats() const noexcept = 0;
};

// Creates the physical system adapter for the current platform: event-driven
// WASAPI on Windows, CoreAudio on macOS, and runtime-loaded PulseAudio Simple
// on Linux. open() reports Unsupported/IoError when the platform service or
// physical endpoint is unavailable.
[[nodiscard]] std::unique_ptr<IAudioDevice> createSystemAudioDevice();

// Deterministic callback-clock fallback. This owns a real dedicated OS thread
// and is suitable for tests and offline environments, but it does not write to
// physical speakers and reports physical=false.
[[nodiscard]] std::unique_ptr<IAudioDevice> createThreadedAudioDevice();

}  // namespace seam::platform
