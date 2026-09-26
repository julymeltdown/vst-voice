#pragma once
#include "seam/native_ui/candidate_audition.hpp"
#include "seam/platform/audio_device.hpp"
#include <chrono>
#include <optional>

namespace seam::native_ui {
// Owner-thread device lifecycle. The device must stop before callback state dies.
class CandidateAuditionSession final {
public:
  using Clock = std::chrono::steady_clock;
  ~CandidateAuditionSession() { stop(); }
  [[nodiscard]] bool active() const noexcept { return device_ != nullptr; }
  // The measured peak of the block the device last played, or nothing when no audition plays.
  [[nodiscard]] std::optional<float> level() const noexcept {
    if (!device_ || !processor_) return std::nullopt;
    return processor_->blockPeak();
  }
  void stop() noexcept {
    if (device_) device_->stop();
    device_.reset();
    processor_.reset();
  }
  [[nodiscard]] core::Result<void> start(std::unique_ptr<platform::IAudioDevice> device,
      std::shared_ptr<const voicebank::AudioBuffer> audio, std::size_t begin, std::size_t end,
      float gain, Clock::time_point now = Clock::now()) {
    if (!device) return core::failure(core::ErrorCode::InvalidArgument, "Audition output device is missing");
    auto processor = CandidateAuditionProcessor::create(audio, begin, end, gain);
    if (!processor) return core::Result<void>{processor.error()};
    stop();
    rate_ = audio->sampleRate;
    processor_ = std::move(processor.value());
    device_ = std::move(device);
    const auto opened = device_->open({.sampleRate = rate_, .blockFrames = 256U, .outputChannels = 2U,
        .applicationName = "Project SEAM", .streamName = "Raw candidate audition"}, *processor_);
    if (!opened) { stop(); return opened; }
    if (!formatMatches()) { stop(); return core::failure(core::ErrorCode::Unsupported, "Audition output format differs from the candidate"); }
    const auto started = device_->start();
    if (!started) { stop(); return started; }
    callbacks_ = device_->stats().callbacks;
    lastProgress_ = now;
    return core::success();
  }
  // No sleeps: called by the native event loop. Injected time supports deterministic tests.
  [[nodiscard]] core::Result<bool> poll(Clock::time_point now = Clock::now()) {
    if (!device_) return false;
    const auto stats = device_->stats();
    if (processor_->failed() || !formatMatches()) return fail("Audition output format changed");
    if (stats.writeFailures != 0U) return fail("Audition output write failed");
    if (processor_->finished()) { stop(); return false; }
    if (!device_->running()) return fail("Audition output stopped unexpectedly");
    if (stats.callbacks != callbacks_) { callbacks_ = stats.callbacks; lastProgress_ = now; }
    else if (now - lastProgress_ >= std::chrono::seconds{3}) return fail("Audition output callback stalled");
    return true;
  }
private:
  bool formatMatches() const {
    const auto info = device_->info();
    return info.sampleRate == rate_ && info.outputChannels == 2U;
  }
  core::Result<bool> fail(const char* message) {
    stop();
    return core::failure<bool>(core::ErrorCode::IoError, message);
  }
  std::unique_ptr<CandidateAuditionProcessor> processor_;
  std::unique_ptr<platform::IAudioDevice> device_;
  std::uint32_t rate_{0U};
  std::uint64_t callbacks_{0U};
  Clock::time_point lastProgress_{};
};
}
