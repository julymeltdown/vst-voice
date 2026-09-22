#pragma once

#include "seam/platform/recording_session.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace seam::platform {

enum class RecordingInputMode { Physical, SyntheticTest };

struct RecordingInputFactories final {
  std::function<std::unique_ptr<IAudioInputDevice>()> physical{createSystemAudioInputDevice};
  std::function<std::unique_ptr<IAudioInputDevice>()> synthetic{createThreadedSilenceInputDevice};
};

// Owner-thread lifecycle around a bounded capture buffer. Construction never opens
// a microphone. A failed physical device is never replaced with synthetic input.
// A healthy stopped capture stays pending until its caller confirms publication;
// an export error cannot silently clear it or let Record overwrite it.
class RecordingInputSession final : private IAudioInputProcessor {
public:
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;

  explicit RecordingInputSession(RecordingSession& recording,
      RecordingInputMode mode = RecordingInputMode::Physical,
      RecordingInputFactories factories = {}, AudioInputDeviceConfig config = {},
      Now now = [] { return Clock::now(); });
  ~RecordingInputSession() override;
  RecordingInputSession(const RecordingInputSession&) = delete;
  RecordingInputSession& operator=(const RecordingInputSession&) = delete;

  [[nodiscard]] core::Result<void> begin();
  [[nodiscard]] core::Result<void> poll();
  [[nodiscard]] core::Result<void> finish();
  [[nodiscard]] core::Result<void> exportPending(const std::filesystem::path& path,
      voicebank::WavSampleFormat format = voicebank::WavSampleFormat::Pcm24) const;
  [[nodiscard]] core::Result<void> acknowledgePublished();
  [[nodiscard]] bool capturing() const noexcept;
  [[nodiscard]] bool pending() const noexcept;
  [[nodiscard]] AudioInputDeviceInfo info() const;
  [[nodiscard]] AudioInputDeviceStats stats() const noexcept;

private:
  enum class State { Idle, Capturing, Pending, Failed };
  void process(AudioInputProcessContext context) noexcept override;
  [[nodiscard]] core::Result<void> validateDeviceInfo() const;
  [[nodiscard]] core::Result<void> validateCapture() const;
  [[nodiscard]] core::Result<void> reject(core::Error error);

  RecordingSession& recording_;
  RecordingInputMode mode_;
  RecordingInputFactories factories_;
  AudioInputDeviceConfig config_;
  Now now_;
  std::unique_ptr<IAudioInputDevice> device_;
  AudioInputDeviceInfo openedInfo_;
  AudioInputDeviceStats initialStats_;
  std::atomic<bool> invalidCallback_{false};
  Clock::time_point lastProgress_{};
  std::uint64_t lastFrames_{0U};
  State state_{State::Idle};
  std::optional<core::Error> error_;
};

}  // namespace seam::platform
