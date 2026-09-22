#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/platform/recording_input_session.hpp"

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace seam;

struct InputControl final {
  platform::IAudioInputProcessor* processor{nullptr};
  platform::AudioInputDeviceInfo info{.backend = "Injected physical input", .deviceName = "test microphone",
      .sampleRate = 48000U, .blockFrames = 256U, .physical = true};
  platform::AudioInputDeviceStats stats;
  bool openFailure{false}, startFailure{false}, failDuringStop{false}, running{false};
  std::size_t openCalls{0U}, startCalls{0U}, stopCalls{0U};
  bool emitBeforeStartFailure{false};

  void emit(std::span<const float> samples, std::optional<double> rate = {},
            std::optional<std::size_t> count = {}) {
    CHECK(processor != nullptr);
    CHECK(running);
    processor->process({.sampleRate = rate.value_or(static_cast<double>(info.sampleRate)),
        .frameCount = count.value_or(samples.size()), .mono = samples});
    ++stats.callbacks;
    stats.frames += static_cast<std::uint64_t>(samples.size());
  }
};

class InputDevice final : public platform::IAudioInputDevice {
public:
  explicit InputDevice(std::shared_ptr<InputControl> state) : state_(std::move(state)) {
    state_->stats = {};
    state_->running = false;
    state_->processor = nullptr;
  }
  core::Result<void> open(const platform::AudioInputDeviceConfig&, platform::IAudioInputProcessor& processor) override {
    ++state_->openCalls;
    if (state_->openFailure) return core::failure(core::ErrorCode::IoError, "Microphone permission denied or device unavailable");
    state_->processor = &processor;
    return core::success();
  }
  core::Result<void> start() override {
    ++state_->startCalls;
    state_->running = true;
    if (state_->startFailure) {
      if (state_->emitBeforeStartFailure) {
        const std::array<float, 4U> partial{0.1F, 0.2F, -0.1F, -0.2F};
        state_->emit(partial);
      }
      return core::failure(core::ErrorCode::IoError, "Cannot start capture");
    }
    return core::success();
  }
  void stop() noexcept override {
    ++state_->stopCalls;
    if (state_->running && state_->failDuringStop) ++state_->stats.readFailures;
    state_->running = false;
  }
  bool running() const noexcept override { return state_->running; }
  platform::AudioInputDeviceInfo info() const override { return state_->info; }
  platform::AudioInputDeviceStats stats() const noexcept override { return state_->stats; }
private:
  std::shared_ptr<InputControl> state_;
};

struct Fixture final {
  std::shared_ptr<InputControl> physical{std::make_shared<InputControl>()};
  std::shared_ptr<InputControl> synthetic{std::make_shared<InputControl>()};
  std::size_t physicalCalls{0U}, syntheticCalls{0U};
  platform::RecordingInputSession::Clock::time_point now{};

  Fixture() {
    synthetic->info.backend = "Threaded Silence Input";
    synthetic->info.deviceName = "synthetic-silence";
    synthetic->info.physical = false;
  }
  platform::RecordingInputFactories factories() {
    return {[this] { ++physicalCalls; return std::make_unique<InputDevice>(physical); },
            [this] { ++syntheticCalls; return std::make_unique<InputDevice>(synthetic); }};
  }
  platform::RecordingInputSession::Now clock() { return [this] { return now; }; }
  void audio() {
    const std::array<float, 8U> samples{0.1F, -0.1F, 0.2F, -0.2F, 0.3F, -0.3F, 0.2F, -0.2F};
    physical->emit(samples);
  }
};
}  // namespace

TEST_CASE("recording input is lazy and physical capture publishes only after explicit completion") {
  Fixture fixture;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
  CHECK(fixture.physicalCalls == 0U && fixture.syntheticCalls == 0U);
  CHECK(input.finish());
  CHECK(input.begin());
  CHECK(input.capturing());
  CHECK(input.info().physical);
  CHECK(fixture.physicalCalls == 1U && fixture.syntheticCalls == 0U);
  fixture.audio();
  CHECK(!input.begin());
  const auto root = test::support::temporaryDirectory("recording-input-success");
  CHECK(!input.exportPending(root / "premature.wav"));
  CHECK(!std::filesystem::exists(root / "premature.wav"));
  CHECK(input.finish());
  CHECK(!input.capturing() && input.pending());
  CHECK(input.exportPending(root / "take.wav"));
  const auto audio = voicebank::readWav(root / "take.wav");
  CHECK(audio);
  CHECK(audio.value().frameCount() == 8U);
  CHECK(audio.value().bitsPerSample == 24U);
  CHECK(input.pending());
  CHECK(input.acknowledgePublished());
  CHECK(!input.pending() && buffer.recordedFrames() == 0U);
  CHECK(fixture.syntheticCalls == 0U);
}

TEST_CASE("denied physical open never tries synthetic input and Record can retry") {
  Fixture fixture;
  fixture.physical->openFailure = true;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
  const auto denied = input.begin();
  CHECK(!denied && denied.error().code == core::ErrorCode::IoError);
  CHECK(fixture.physicalCalls == 1U && fixture.syntheticCalls == 0U);
  CHECK(fixture.physical->startCalls == 0U);
  CHECK(!input.capturing() && !input.pending() && !buffer.armed());
  CHECK(!input.finish());
  fixture.physical->openFailure = false;
  CHECK(input.begin());
  fixture.audio();
  CHECK(input.finish());
  CHECK(fixture.physicalCalls == 2U && fixture.syntheticCalls == 0U);
}

TEST_CASE("physical start failure discards partial callbacks without synthetic fallback and can retry") {
  Fixture fixture;
  fixture.physical->startFailure = true;
  fixture.physical->emitBeforeStartFailure = true;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
  CHECK(!input.begin());
  CHECK(!fixture.physical->running && !buffer.armed());
  CHECK(buffer.recordedFrames() == 0U && !input.pending());
  CHECK(fixture.syntheticCalls == 0U);
  fixture.physical->startFailure = false;
  CHECK(input.begin());
  fixture.audio();
  CHECK(input.finish());
}

TEST_CASE("only explicit synthetic test mode selects and accepts the silence input") {
  Fixture fixture;
  fixture.physical->openFailure = true;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::SyntheticTest, fixture.factories(), {}, fixture.clock()};
  CHECK(input.begin());
  CHECK(fixture.physicalCalls == 0U && fixture.syntheticCalls == 1U);
  CHECK(!input.info().physical);
  const std::array<float, 16U> silence{};
  fixture.synthetic->emit(silence);
  CHECK(input.finish());
  CHECK(buffer.recordedFrames() == silence.size());
  CHECK(input.pending());
}

TEST_CASE("recording rejects absent factories devices and mismatched backend identity before start") {
  for (int scenario = 0; scenario < 7; ++scenario) {
    Fixture fixture;
    auto factories = fixture.factories();
    if (scenario == 0) fixture.physical->info.physical = false;
    if (scenario == 1) fixture.physical->info.backend.clear();
    if (scenario == 2) fixture.physical->info.sampleRate = 44100U;
    if (scenario == 3) fixture.physical->info.blockFrames = 512U;
    if (scenario == 4) fixture.physical->info.backend = "Threaded Silence Input";
    if (scenario == 5) factories.physical = {};
    if (scenario == 6) factories.physical = [] { return std::unique_ptr<platform::IAudioInputDevice>{}; };
    platform::RecordingSession buffer;
    platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, factories, {}, fixture.clock()};
    CHECK(!input.begin());
    CHECK(fixture.physical->startCalls == 0U && fixture.syntheticCalls == 0U);
    CHECK(buffer.recordedFrames() == 0U && !input.pending());
  }
  Fixture fixture;
  fixture.synthetic->info.physical = true;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::SyntheticTest, fixture.factories(), {}, fixture.clock()};
  CHECK(!input.begin());
  CHECK(fixture.physicalCalls == 0U && fixture.synthetic->startCalls == 0U);
}

TEST_CASE("a read failure including the final draining callback prevents capture publication") {
  for (const bool failOnStop : {false, true}) {
    Fixture fixture;
    platform::RecordingSession buffer;
    platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
    CHECK(input.begin());
    fixture.audio();
    if (failOnStop) fixture.physical->failDuringStop = true;
    else ++fixture.physical->stats.readFailures;
    CHECK(!input.finish());
    CHECK(!input.pending() && buffer.recordedFrames() == 0U);
    const auto root = test::support::temporaryDirectory("recording-read-failure");
    CHECK(!input.exportPending(root / "bad.wav"));
    CHECK(!std::filesystem::exists(root / "bad.wav"));
    fixture.physical->failDuringStop = false;
    CHECK(input.begin());
    fixture.audio();
    CHECK(input.finish());
    CHECK(fixture.syntheticCalls == 0U);
  }
}

TEST_CASE("disconnected changed and stalled input cannot publish an earlier partial recording") {
  for (int scenario = 0; scenario < 3; ++scenario) {
    Fixture fixture;
    platform::RecordingSession buffer;
    platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
    CHECK(input.begin());
    fixture.audio();
    CHECK(input.poll());
    if (scenario == 0) fixture.physical->running = false;
    if (scenario == 1) fixture.physical->info.deviceName = "replacement microphone";
    if (scenario == 2) fixture.now += std::chrono::seconds{3};
    CHECK(!input.poll());
    CHECK(!input.finish());
    CHECK(!input.pending() && buffer.recordedFrames() == 0U);
    CHECK(!fixture.physical->running);
    fixture.physical->info.deviceName = "test microphone";
    CHECK(input.begin());
    fixture.audio();
    CHECK(input.finish());
  }
}

TEST_CASE("overflow and empty recordings fail without publishing and permit a fresh take") {
  for (const bool overflow : {false, true}) {
    Fixture fixture;
    fixture.physical->info.sampleRate = 8000U;
    platform::RecordingSession buffer{8000U, 1U};
    platform::AudioInputDeviceConfig config;
    config.sampleRate = 8000U;
    platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), config, fixture.clock()};
    CHECK(input.begin());
    if (overflow) {
      const std::vector<float> tooLong(8001U, 0.2F);
      fixture.physical->emit(tooLong);
    }
    CHECK(!input.finish());
    CHECK(!input.pending() && buffer.recordedFrames() == 0U);
    CHECK(input.begin());
    fixture.audio();
    CHECK(input.finish());
  }
}

TEST_CASE("invalid callback format and nonfinite samples never enter an accepted recording") {
  for (int scenario = 0; scenario < 6; ++scenario) {
    Fixture fixture;
    platform::RecordingSession buffer;
    platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
    CHECK(input.begin());
    std::vector<float> samples(4U, 0.2F);
    if (scenario == 0) fixture.physical->emit(samples, 44100.0);
    else if (scenario == 1) fixture.physical->emit(samples, {}, 3U);
    else {
      if (scenario == 2) samples.front() = std::numeric_limits<float>::quiet_NaN();
      if (scenario == 3) samples.front() = std::numeric_limits<float>::infinity();
      if (scenario == 4) samples.resize(16385U);
      if (scenario == 5) samples.clear();
      fixture.physical->emit(samples);
    }
    CHECK(!input.finish());
    CHECK(!input.pending() && buffer.recordedFrames() == 0U);
  }
}

TEST_CASE("failed WAV export preserves valid pending audio and never overwrites an existing destination") {
  Fixture fixture;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
  CHECK(input.begin());
  fixture.audio();
  CHECK(input.finish());
  const auto original = std::vector<float>(buffer.samples().begin(), buffer.samples().end());
  const auto root = test::support::temporaryDirectory("recording-export-retry");
  const auto preserved = root / "existing.wav";
  CHECK(core::durableAtomicWriteText(preserved, "existing user bytes"));
  const auto before = core::sha256File(preserved);
  CHECK(before);
  CHECK(!input.exportPending(preserved));
  CHECK(!input.exportPending(preserved / "take.wav"));
  CHECK(input.pending());
  CHECK(!input.begin());
  CHECK(std::vector<float>(buffer.samples().begin(), buffer.samples().end()) == original);
  CHECK(core::sha256File(preserved).value() == before.value());
  CHECK(input.finish()); // Retrying downstream publication does not capture again.
  CHECK(input.exportPending(root / "recovered.wav"));
  CHECK(input.pending());
  CHECK(!input.begin());
    CHECK(input.acknowledgePublished());
    CHECK(input.begin());
    fixture.audio();
    CHECK(input.finish());
    CHECK(fixture.physicalCalls == 2U && fixture.syntheticCalls == 0U);
}

TEST_CASE("a capture that cannot be published can be explicitly discarded without deleting files") {
  Fixture fixture;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical,
      fixture.factories(), {}, fixture.clock()};
  CHECK(input.begin());
  fixture.audio();
  CHECK(input.finish());
  const auto root = test::support::temporaryDirectory("recording-discard-recovery");
  const auto published = root / "take.wav";
  CHECK(input.exportPending(published));
  CHECK(input.pending());
  // The caller writes the file but cannot verify its identity. Discarding must
  // free the session, keep the written file byte-identical, and allow a new take.
  const auto writtenHash = core::sha256File(published);
  CHECK(writtenHash);
  CHECK(input.discardPending());
  CHECK(!input.pending() && !input.capturing());
  CHECK(core::sha256File(published).value() == writtenHash.value());
  CHECK(!input.discardPending());
  CHECK(!input.acknowledgePublished());
  CHECK(input.begin());
  fixture.audio();
  CHECK(input.finish());
  CHECK(input.pending());
  CHECK(input.discardPending());
  CHECK(fixture.physicalCalls == 2U && fixture.syntheticCalls == 0U);
  CHECK(buffer.recordedFrames() == 0U);
}

TEST_CASE("input frame accounting and buffer format mismatch are refused") {
  Fixture fixture;
  platform::RecordingSession buffer;
  platform::RecordingInputSession input{buffer, platform::RecordingInputMode::Physical, fixture.factories(), {}, fixture.clock()};
  CHECK(input.begin());
  fixture.audio();
  ++fixture.physical->stats.frames;
  CHECK(!input.finish());
  CHECK(!input.pending());
  platform::AudioInputDeviceConfig wrongFormat;
  wrongFormat.sampleRate = 44100U;
  platform::RecordingInputSession wrong{buffer, platform::RecordingInputMode::Physical, fixture.factories(), wrongFormat, fixture.clock()};
  CHECK(!wrong.begin());
  CHECK(fixture.physicalCalls == 1U && fixture.syntheticCalls == 0U);
}
