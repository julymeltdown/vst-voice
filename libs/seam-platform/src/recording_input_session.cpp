#include "seam/platform/recording_input_session.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace seam::platform {

RecordingInputSession::RecordingInputSession(RecordingSession& recording,
    RecordingInputMode mode, RecordingInputFactories factories,
    AudioInputDeviceConfig config, Now now)
    : recording_(recording), mode_(mode), factories_(std::move(factories)),
      config_(std::move(config)), now_(std::move(now)) {}

RecordingInputSession::~RecordingInputSession() {
  if (device_) device_->stop();
  recording_.stop();
}

bool RecordingInputSession::capturing() const noexcept { return state_ == State::Capturing; }
bool RecordingInputSession::pending() const noexcept { return state_ == State::Pending; }

AudioInputDeviceInfo RecordingInputSession::info() const {
  return device_ ? device_->info() : AudioInputDeviceInfo{};
}
AudioInputDeviceStats RecordingInputSession::stats() const noexcept {
  return device_ ? device_->stats() : AudioInputDeviceStats{};
}

core::Result<void> RecordingInputSession::reject(core::Error error) {
  if (device_) device_->stop();
  recording_.clear();
  state_ = State::Failed;
  error_ = std::move(error);
  return *error_;
}

core::Result<void> RecordingInputSession::validateDeviceInfo() const {
  if (!device_)
    return core::failure(core::ErrorCode::InvalidState, "Recording input is unavailable; retry Record");
  const auto current = device_->info();
  const bool synthetic = mode_ == RecordingInputMode::SyntheticTest;
  if (current.physical == synthetic || current.backend.empty() || current.backend == "unopened" ||
      current.deviceName.empty() || current.sampleRate != config_.sampleRate ||
      current.blockFrames != config_.blockFrames ||
      (synthetic && (current.backend != "Threaded Silence Input" || current.deviceName != "synthetic-silence")) ||
      (!synthetic && current.backend == "Threaded Silence Input"))
    return core::failure(core::ErrorCode::Conflict,
        "Recording input identity does not match the selected physical/test mode or format");
  if (current.backend != openedInfo_.backend || current.deviceName != openedInfo_.deviceName ||
      current.physical != openedInfo_.physical || current.sampleRate != openedInfo_.sampleRate ||
      current.blockFrames != openedInfo_.blockFrames)
    return core::failure(core::ErrorCode::Conflict, "Recording input changed during capture; retry Record");
  return core::success();
}

core::Result<void> RecordingInputSession::validateCapture() const {
  const auto identity = validateDeviceInfo();
  if (!identity) return identity;
  const auto current = device_->stats();
  if (current.readFailures != initialStats_.readFailures ||
      current.frames < initialStats_.frames || current.callbacks < initialStats_.callbacks)
    return core::failure(core::ErrorCode::IoError,
        "Recording input reported a read failure or discontinuity; no take was published, retry Record");
  if (invalidCallback_.load(std::memory_order_acquire))
    return core::failure(core::ErrorCode::IoError,
        "Recording input delivered invalid samples or format; no take was published, retry Record");
  if (recording_.overflowed())
    return core::failure(core::ErrorCode::IoError,
        "Recording exceeded its capture limit; no truncated take was published, retry Record");
  return core::success();
}

core::Result<void> RecordingInputSession::begin() {
  if (capturing() || pending() || recording_.armed() || recording_.recordedFrames() != 0U)
    return core::failure(core::ErrorCode::Conflict,
        "Finish or publish the pending recording before starting another take");
  if (!now_ || config_.sampleRate != recording_.sampleRate() ||
      config_.blockFrames == 0U || config_.blockFrames > 16384U)
    return core::failure(core::ErrorCode::InvalidArgument, "Recording input configuration is invalid");
  try {
    if (device_) device_->stop();
    device_.reset();
    error_.reset();
    invalidCallback_.store(false, std::memory_order_release);
    auto& factory = mode_ == RecordingInputMode::SyntheticTest ? factories_.synthetic : factories_.physical;
    if (!factory) return reject({core::ErrorCode::InvalidState, "The selected recording input factory is unavailable"});
    device_ = factory();
    if (!device_) return reject({core::ErrorCode::InvalidState, "The selected recording input is unavailable; retry Record"});
    const auto opened = device_->open(config_, *this);
    if (!opened) return reject(opened.error());
    openedInfo_ = device_->info();
    const auto identity = validateDeviceInfo();
    if (!identity) return reject(identity.error());
    initialStats_ = device_->stats();
    if (initialStats_.readFailures != 0U)
      return reject({core::ErrorCode::IoError, "Recording input already reported errors before capture; retry Record"});
    lastFrames_ = initialStats_.frames;
    lastProgress_ = now_();
    const auto armed = recording_.arm();
    if (!armed) return reject(armed.error());
    const auto started = device_->start();
    if (!started) return reject(started.error());
    state_ = State::Capturing;
    return poll();
  } catch (const std::exception& error) {
    return reject({core::ErrorCode::Internal, "Cannot start recording input", error.what()});
  } catch (...) {
    return reject({core::ErrorCode::Internal, "Cannot start recording input"});
  }
}

void RecordingInputSession::process(AudioInputProcessContext context) noexcept {
  // Callback work is bounded by the device's declared maximum. No allocation or
  // file access is performed here; the owner stops and diagnoses invalid capture.
  if (context.frameCount == 0U || context.frameCount > 16384U ||
      context.frameCount != context.mono.size() ||
      context.sampleRate != static_cast<double>(recording_.sampleRate()) ||
      !std::all_of(context.mono.begin(), context.mono.end(), [](float value) { return std::isfinite(value); })) {
    invalidCallback_.store(true, std::memory_order_release);
    return;
  }
  recording_.process(context);
}

core::Result<void> RecordingInputSession::poll() {
  if (!capturing()) return core::success();
  const auto health = validateCapture();
  if (!health) return reject(health.error());
  if (!device_->running())
    return reject({core::ErrorCode::IoError, "Recording input stopped unexpectedly; no take was published, retry Record"});
  const auto frames = device_->stats().frames;
  const auto now = now_();
  if (frames != lastFrames_) {
    lastFrames_ = frames;
    lastProgress_ = now;
  } else if (now - lastProgress_ >= std::chrono::seconds{2}) {
    return reject({core::ErrorCode::IoError, "Recording input stopped delivering audio; no take was published, retry Record"});
  }
  return core::success();
}

core::Result<void> RecordingInputSession::finish() {
  if (state_ == State::Idle || pending()) return core::success();
  if (state_ == State::Failed) return *error_;
  const auto health = poll();
  if (!health) return health;
  device_->stop();
  recording_.stop();
  // stop() joins/drains callbacks. Check again so a last failing read cannot race
  // the earlier check and turn a partial capture into a successful take.
  const auto finalHealth = validateCapture();
  if (!finalHealth) return reject(finalHealth.error());
  const auto finalStats = device_->stats();
  if (recording_.recordedFrames() == 0U || finalStats.callbacks == initialStats_.callbacks)
    return reject({core::ErrorCode::IoError, "Recording captured no audio; no take was published, retry Record"});
  if (finalStats.frames - initialStats_.frames != recording_.recordedFrames())
    return reject({core::ErrorCode::IoError, "Recording frame accounting is incomplete; no take was published, retry Record"});
  state_ = State::Pending;
  return core::success();
}

core::Result<void> RecordingInputSession::exportPending(
    const std::filesystem::path& path, voicebank::WavSampleFormat format) const {
  if (!pending())
    return core::failure(core::ErrorCode::InvalidState, "Only a healthy completed recording can be exported");
  return recording_.exportWav(path, format, false);
}

core::Result<void> RecordingInputSession::acknowledgePublished() {
  if (!pending())
    return core::failure(core::ErrorCode::InvalidState, "There is no completed recording to acknowledge");
  recording_.clear();
  state_ = State::Idle;
  return core::success();
}

}  // namespace seam::platform
