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
  // The device is the member that is destroyed first and the processor it reads the last, so a
  // session that could not stop its device still gives the device (whose destructor makes a last
  // attempt) a processor to read until it is gone.
  ~CandidateAuditionSession() { static_cast<void>(stop()); }
  [[nodiscard]] bool active() const noexcept { return device_ != nullptr; }
  // The measured peak of the block the device last played, or nothing when no audition plays.
  [[nodiscard]] std::optional<float> level() const noexcept {
    if (!device_ || !processor_) return std::nullopt;
    return processor_->blockPeak();
  }
  // Lets the device and the processor go once the platform says that the device has stopped. When
  // it does not say so, a callback may still be reading the processor: the session keeps both, is
  // still active, and returns the error. Asking again tries again.
  [[nodiscard]] core::Result<void> stop() noexcept {
    if (device_) {
      auto stopped = device_->stop();
      if (!stopped) return stopped;
    }
    device_.reset();
    processor_.reset();
    return core::success();
  }
  [[nodiscard]] core::Result<void> start(std::unique_ptr<platform::IAudioDevice> device,
      std::shared_ptr<const voicebank::AudioBuffer> audio, std::size_t begin, std::size_t end,
      float gain, Clock::time_point now = Clock::now()) {
    if (!device) return core::failure(core::ErrorCode::InvalidArgument, "Audition output device is missing");
    auto processor = CandidateAuditionProcessor::create(audio, begin, end, gain);
    if (!processor) return core::Result<void>{processor.error()};
    // The audition that plays now goes first. If its device does not say that it has stopped it
    // is still playing, and nothing of it is replaced.
    auto previous = stop();
    if (!previous) return previous;
    rate_ = audio->sampleRate;
    processor_ = std::move(processor.value());
    device_ = std::move(device);
    const auto opened = device_->open({.sampleRate = rate_, .blockFrames = 256U, .outputChannels = 2U,
        .applicationName = "Project SEAM", .streamName = "Raw candidate audition"}, *processor_);
    // A device that is not running stops by contract, so the stops of a start that does not get
    // going cannot fail.
    if (!opened) { static_cast<void>(stop()); return opened; }
    if (!formatMatches()) { static_cast<void>(stop()); return core::failure(core::ErrorCode::Unsupported, "Audition output format differs from the candidate"); }
    const auto started = device_->start();
    if (!started) { static_cast<void>(stop()); return started; }
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
    if (processor_->finished()) {
      // The session stays active when its device does not say that it has stopped, and the next
      // poll asks again.
      auto stopped = stop();
      if (!stopped) return core::Result<bool>{stopped.error()};
      return false;
    }
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
    const auto stopped = stop();
    return core::failure<bool>(core::ErrorCode::IoError, message,
                               stopped ? std::string{}
                                       : "the output did not stop: " + stopped.error().message);
  }
  std::unique_ptr<CandidateAuditionProcessor> processor_;
  std::unique_ptr<platform::IAudioDevice> device_;
  std::uint32_t rate_{0U};
  std::uint64_t callbacks_{0U};
  Clock::time_point lastProgress_{};
};
}
