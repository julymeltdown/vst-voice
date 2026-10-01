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
  // Stops the callback, and says whether it is stopped. Success is a boundary: no callback is
  // running and none will be entered, so whoever owns what the callback reads (the ring buffer
  // whose resets it answers, the processor) may take over the consumer's part, replace it or let
  // it go. Stopping a device that is not running succeeds, and a stop that failed can be asked
  // again.
  //
  // An error means that quiescence is not established: the platform did not say that the device
  // has stopped, and a callback may be in flight. Until a later stop succeeds the owner does not
  // hand the consumer's part over: it does not answer the ring's resets in the device's place,
  // replace or free what the callback reads, or start another device on that ring. running() goes
  // on saying true, which is the device's conservative claim to still be the consumer. It is not
  // evidence that audio is being played, and nothing about what the creator hears is decided from
  // it. The destructor makes a last attempt whatever the answer. Whether that ends callbacks that
  // a failed stop left running has not been verified on any platform, so an owner that has to
  // destroy such a device destroys it before anything its callback reads.
  [[nodiscard]] virtual core::Result<void> stop() noexcept = 0;
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
