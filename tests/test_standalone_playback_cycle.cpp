// The standalone's playback as a creator meets it: press Play, hear the whole song, press Play again.
//
// The device is the test's. It takes blocks from the editor's real audio processor exactly as the
// system device's callback would, but only when the test asks, so what the callback would play does
// not depend on a clock or on how busy the machine is. Everything else is the shipping path: the
// real app, the real transport and feeder, the real Play button of the editor's controller, and the
// real paint, which is where the app decides that the device should stop.
//
// This is deterministic-device evidence. It is not a listening test and not evidence about a
// physical output device.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/transport_controller.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/standalone/native_editor_app.hpp"
#include "seam/standalone/playback_device_policy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

using namespace std::chrono_literals;

class PumpedAudioDevice final : public seam::platform::IAudioDevice {
public:
  seam::core::Result<void> open(const seam::platform::AudioDeviceConfig& config,
                                seam::platform::IAudioProcessor& processor) override {
    processor_ = &processor;
    info_ = seam::platform::AudioDeviceInfo{.backend = "pumped-test-device",
                                            .deviceId = "pumped-test-device",
                                            .deviceName = "Pumped test device",
                                            .sampleRate = config.sampleRate,
                                            .blockFrames = config.blockFrames,
                                            .outputChannels = config.outputChannels,
                                            .physical = false};
    left_.assign(kMaximumBlock, 0.0F);
    right_.assign(kMaximumBlock, 0.0F);
    return seam::core::success();
  }
  seam::core::Result<void> start() override {
    if (processor_ == nullptr) {
      return seam::core::failure(seam::core::ErrorCode::InvalidState, "pumped device is not open");
    }
    if (failStart) {
      return seam::core::failure(seam::core::ErrorCode::IoError,
                                 "the pumped device cannot start");
    }
    running_ = true;
    ++starts_;
    return seam::core::success();
  }
  void stop() noexcept override { running_ = false; }
  bool running() const noexcept override { return running_; }
  seam::platform::AudioDeviceInfo info() const override { return info_; }
  seam::platform::AudioDeviceStats stats() const noexcept override { return {}; }

  // One callback of up to kMaximumBlock frames, as the running device would make it. Returns the
  // left channel of what the processor handed back.
  const std::vector<float>& callback(std::size_t frames) {
    frames = std::min(frames, kMaximumBlock);
    std::fill(left_.begin(), left_.end(), 0.0F);
    std::fill(right_.begin(), right_.end(), 0.0F);
    views_[0] = std::span<float>{left_}.first(frames);
    views_[1] = std::span<float>{right_}.first(frames);
    processor_->process(seam::platform::AudioProcessContext{
        .sampleRate = static_cast<double>(info_.sampleRate),
        .frameCount = frames,
        .left = views_[0],
        .right = views_[1],
        .outputs = std::span<std::span<float>>{views_.data(), 2U},
    });
    return left_;
  }
  [[nodiscard]] std::size_t starts() const noexcept { return starts_; }

  static constexpr std::size_t kMaximumBlock = 1024U;
  bool failStart{false};

private:
  seam::platform::IAudioProcessor* processor_{nullptr};
  seam::platform::AudioDeviceInfo info_;
  std::array<std::span<float>, 2U> views_{};
  std::vector<float> left_;
  std::vector<float> right_;
  bool running_{false};
  std::size_t starts_{0U};
};

bool waitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(2ms);
  }
  return predicate();
}

struct PlaybackRig final {
  std::unique_ptr<seam::standalone::NativeEditorApp> app;
  PumpedAudioDevice* device{nullptr};
  seam::native_ui::PixelSurface surface{1280U, 720U};
  std::shared_ptr<const seam::authoring::PublishedProjectAudio> published;

  explicit PlaybackRig(const std::filesystem::path& root) {
    auto handle = std::make_shared<PumpedAudioDevice*>(nullptr);
    seam::standalone::NativeEditorAppConfig config;
    config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
    config.applicationSupportRoot = root;
    config.forceThreadedAudio = true;
    config.allowDevelopmentVoicebanks = true;
    config.authoring.bindFirstAvailableVoicebank = true;
    config.authoring.allowDevelopmentVoicebanks = true;
    config.authoring.sampleRate = 48000U;
    config.authoring.outputChannels = 2U;
    config.authoring.voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
        .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
        .kind = seam::voicebank::VoicebankRootKind::Development,
    }};
    config.threadedAudioDeviceFactory = [handle] {
      auto created = std::make_unique<PumpedAudioDevice>();
      *handle = created.get();
      return created;
    };
    auto created = seam::standalone::NativeEditorApp::create(std::move(config));
    CHECK(created);
    app = std::move(created).value();
    device = *handle;
    CHECK(device != nullptr);
  }

  seam::authoring::TransportController& transport() {
    return app->authoring().runtime().transport();
  }
  void paint() {
    seam::native_ui::RasterCanvas canvas{surface, 1.0};
    app->paint(canvas);
  }
  void addNote(std::int64_t startTick, std::int64_t lengthTicks, std::u32string lyric) {
    auto [token, note] = app->authoring().runtime().document().factory().makeNote(
        seam::time::Tick{startTick}, seam::time::Tick{startTick + lengthTicks}, 64U,
        std::move(lyric), seam::domain::Language::Japanese);
    CHECK(app->authoring().runtime().execute(
        std::make_unique<seam::application::AddNoteCommand>(
            app->authoring().regionId(), std::move(token), std::move(note))));
  }
  // About two seconds of song: longer than the transport's ring, so the feeder cannot finish until
  // the device has played most of it. Returns the left channel of the render the transport holds.
  std::vector<float> writeTheSong() {
    addNote(0, 1920, U"\u3053");
    addNote(1920, 1920, U"\u306a");
    CHECK(waitUntil([this] {
      const auto state = transport().state();
      return state.available &&
             state.publishedRevision == app->authoring().runtime().document().session().revision();
    }));
    published = app->authoring().runtime().audiblePublication().audio;
    CHECK(published != nullptr);
    return leftChannel(*published);
  }
  // The creator presses the transport button: Play when stopped, Pause when playing.
  seam::core::Result<void> pressPlay() {
    paint();
    return app->authoring().controller().dispatchAccessibility(
        "toolbar.transport", seam::native_ui::SemanticAction::Activate);
  }
  // The feeder applies what the creator did on its own thread, and a person cannot press a second
  // button within a millisecond: wait until the transport shows a stop at the frame.
  bool stoppedAt(seam::time::SampleFrame frame) {
    return waitUntil([this, frame] {
      const auto state = transport().state();
      return !state.playing && state.playhead == frame;
    });
  }
  // One callback of the device, appending the frames it really played (not the padding it adds
  // when the ring runs dry) to heard.
  std::size_t pump(std::size_t frames, std::vector<float>& heard) {
    if (device == nullptr || !device->running()) return 0U;
    const auto before = app->processorStats().deliveredFrames;
    const auto& block = device->callback(frames);
    const auto delivered =
        static_cast<std::size_t>(app->processorStats().deliveredFrames - before);
    heard.insert(heard.end(), block.begin(),
                 block.begin() + static_cast<std::ptrdiff_t>(std::min(delivered, block.size())));
    return delivered;
  }
  // The device runs, a callback and a painted frame at a time as in a window, until the app stops it.
  std::vector<float> playUntilTheAppStopsTheDevice(
      std::chrono::milliseconds limit = std::chrono::seconds{30}) {
    std::vector<float> heard;
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (device->running() && std::chrono::steady_clock::now() < deadline) {
      pump(768U, heard);
      paint();
      std::this_thread::sleep_for(1ms);
    }
    return heard;
  }
  // Nothing replaced the audio while the test ran, so what was compared is one render.
  bool audioUnchanged() {
    const auto now = app->authoring().runtime().audiblePublication().audio;
    return now != nullptr && published != nullptr && now->requestId == published->requestId &&
           now->projectRevision == published->projectRevision;
  }

  static std::vector<float> leftChannel(const seam::authoring::PublishedProjectAudio& audio) {
    const auto channels = static_cast<std::size_t>(audio.result.channelCount);
    std::vector<float> left;
    left.reserve(audio.result.interleaved.size() / channels);
    for (std::size_t index = 0U; index + channels <= audio.result.interleaved.size();
         index += channels) {
      left.push_back(audio.result.interleaved[index]);
    }
    return left;
  }
};

// The frames of "heard" are the frames of "expected" from "from" to its end, once and in order. The
// feeder fills its last block with silence, so a little silence may follow, less than one block.
void checkHeardIs(const std::vector<float>& heard, const std::vector<float>& expected,
                  std::size_t from, const char* what) {
  const auto wanted = expected.size() - from;
  const bool longEnough = heard.size() >= wanted;
  if (!longEnough) {
    std::cerr << what << ": heard " << heard.size() << " frames, expected " << wanted << '\n';
  }
  CHECK(longEnough);
  std::size_t firstDifference = wanted;
  for (std::size_t index = 0U; index < wanted; ++index) {
    if (std::fabs(heard[index] - expected[from + index]) >= 1.0e-5F) {
      firstDifference = index;
      break;
    }
  }
  if (firstDifference != wanted) {
    std::cerr << what << ": the audio first differs at frame " << firstDifference << " of "
              << wanted << " (heard " << heard[firstDifference] << ", expected "
              << expected[from + firstDifference] << "); heard " << heard.size() << " frames\n";
  }
  CHECK(firstDifference == wanted);
  const auto extra = heard.size() - wanted;
  const bool silentTail = std::all_of(heard.begin() + static_cast<std::ptrdiff_t>(wanted),
                                      heard.end(), [](float value) { return value == 0.0F; });
  if (extra >= 1024U || !silentTail) {
    std::cerr << what << ": " << extra << " frames follow the song"
              << (silentTail ? "" : ", and they are not silent") << '\n';
  }
  CHECK(extra < 1024U);
  CHECK(silentTail);
  const bool audible = std::any_of(heard.begin(), heard.end(),
                                   [](float value) { return std::fabs(value) > 1.0e-4F; });
  CHECK(audible);
}

}  // namespace

TEST_CASE("standalone playback: a song is heard to its last sample") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-whole-song");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  // The device was started on a filled ring: its first callback gets every frame it asks for, and
  // the song does not begin with silence while the feeder catches up.
  std::vector<float> heard;
  CHECK(rig.pump(256U, heard) == 256U);
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  heard.insert(heard.end(), rest.begin(), rest.end());
  // The feeder reports the end of the song as soon as it has handed over the last block, which is
  // as far ahead of the speakers as the ring is deep: the device has to play that out.
  checkHeardIs(heard, expected, 0U, "playback");
  CHECK(!rig.device->running());
  CHECK(!rig.transport().state().playing);
  CHECK(rig.audioUnchanged());
}

TEST_CASE("standalone playback: Play at the end of a song plays it again from its start, every time") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-replay");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  static_cast<void>(rig.playUntilTheAppStopsTheDevice());
  CHECK(!rig.device->running());

  for (int pass = 2; pass <= 3; ++pass) {
    // At its end the song starts again: Play is not a silent no-op there, and it does not play
    // what the ring still held of the end.
    CHECK(rig.pressPlay());
    CHECK(rig.device->running());
    const auto heard = rig.playUntilTheAppStopsTheDevice();
    checkHeardIs(heard, expected, 0U, pass == 2 ? "second playback" : "third playback");
    CHECK(!rig.device->running());
  }
  CHECK(rig.audioUnchanged());
}

TEST_CASE("standalone playback: after the song has ended, a seek to its start and Play plays it again") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-seek-start");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  static_cast<void>(rig.playUntilTheAppStopsTheDevice());
  CHECK(!rig.device->running());

  // The creator clicks the ruler at the start and presses Play.
  CHECK(rig.transport().seek(0));
  CHECK(rig.stoppedAt(0));
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  const auto heard = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(heard, expected, 0U, "playback after the seek");
  CHECK(rig.audioUnchanged());
}

TEST_CASE("standalone playback: after a pause and a seek, no audio from before the seek is heard") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-pause-seek");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  std::vector<float> beforePause;
  for (int block = 0; block < 12; ++block) {
    rig.pump(768U, beforePause);
    std::this_thread::sleep_for(1ms);
  }
  CHECK(!beforePause.empty());
  // Pause: the device stops at once, with the feeder well ahead of what was played.
  CHECK(rig.pressPlay());
  CHECK(!rig.device->running());
  CHECK(rig.transport().ringBuffer().availableReadFrames() > 0U);

  // The creator clicks the ruler in the second half and presses Play.
  const auto target = static_cast<seam::time::SampleFrame>(expected.size() / 2U);
  CHECK(rig.transport().seek(target));
  CHECK(rig.stoppedAt(target));
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  const auto heard = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(heard, expected, static_cast<std::size_t>(target), "playback after the seek");
  CHECK(rig.audioUnchanged());
}

TEST_CASE("standalone playback: a Play that cannot start the device is told to the creator and leaves the transport paused") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-start-failure");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  // The device cannot start (it was unplugged, or another application holds it).
  rig.device->failStart = true;
  const auto pressed = rig.pressPlay();
  // The failure is the creator's to see: it is not dropped on the way from the host to the button.
  CHECK(!pressed);
  CHECK(!pressed.error().message.empty());
  CHECK(!rig.device->running());
  // The transport is not left playing to a device that is not there.
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && !state.playing;
  }));
  rig.paint();
  const auto& entries = rig.app->authoring().controller().diagnosticPanel().entries();
  CHECK(std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
    return entry.diagnostic.code == "AUDIO_UNAVAILABLE";
  }));
  // The button still offers Play, and when the device works again the song plays.
  rig.device->failStart = false;
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  std::vector<float> heard;
  for (int block = 0; block < 12; ++block) {
    rig.pump(768U, heard);
    std::this_thread::sleep_for(1ms);
  }
  CHECK(heard.size() > 4000U);
  CHECK(std::any_of(heard.begin(), heard.end(),
                    [](float value) { return std::fabs(value) > 1.0e-4F; }));
}

namespace {

seam::authoring::TransportState reported(bool playing, bool settled, bool available = true) {
  seam::authoring::TransportState state;
  state.playing = playing;
  state.settled = settled;
  state.available = available;
  return state;
}

}  // namespace

TEST_CASE("standalone playback policy: the device keeps running while the ring still holds the end of the song") {
  using seam::standalone::DeviceAction;
  using seam::standalone::decideDeviceAction;
  // The feeder has handed over the last of the audio and says it has stopped: what the ring holds
  // has not been heard yet.
  CHECK(decideDeviceAction(reported(false, true), 4096U, true, true) == DeviceAction::None);
  CHECK(decideDeviceAction(reported(false, true), 1U, true, true) == DeviceAction::None);
  // Once it has been played out the device stops.
  CHECK(decideDeviceAction(reported(false, true), 0U, true, true) == DeviceAction::Stop);
  // A device that is not running has nothing to stop.
  CHECK(decideDeviceAction(reported(false, true), 0U, true, false) == DeviceAction::None);
  CHECK(decideDeviceAction(reported(false, true), 4096U, true, false) == DeviceAction::None);
}

TEST_CASE("standalone playback policy: a transport that is playing starts the device and leaves a running one alone") {
  using seam::standalone::DeviceAction;
  using seam::standalone::decideDeviceAction;
  CHECK(decideDeviceAction(reported(true, true), 0U, true, false) == DeviceAction::Start);
  CHECK(decideDeviceAction(reported(true, true), 4096U, true, false) == DeviceAction::Start);
  CHECK(decideDeviceAction(reported(true, true), 0U, true, true) == DeviceAction::None);
  CHECK(decideDeviceAction(reported(true, true), 4096U, true, true) == DeviceAction::None);
  // Nothing rendered, nothing to start the device for.
  CHECK(decideDeviceAction(reported(true, true, false), 0U, true, false) == DeviceAction::None);
}

TEST_CASE("standalone playback policy: a report that does not yet include every command starts and stops nothing") {
  using seam::standalone::DeviceAction;
  using seam::standalone::decideDeviceAction;
  // The creator pressed Play and the feeder has not applied it: it still reports the end of the
  // song, and stopping on that would undo the Play.
  CHECK(decideDeviceAction(reported(false, false), 0U, true, true) == DeviceAction::None);
  // The creator pressed Pause and the feeder has not applied it: it still reports playing, and
  // starting on that would bring the device back for a Pause.
  CHECK(decideDeviceAction(reported(true, false), 0U, true, false) == DeviceAction::None);
  CHECK(decideDeviceAction(reported(true, false), 4096U, true, false) == DeviceAction::None);
}

TEST_CASE("standalone playback policy: with no device there is nothing to start or stop") {
  using seam::standalone::DeviceAction;
  using seam::standalone::decideDeviceAction;
  for (const bool playing : {false, true}) {
    for (const bool settled : {false, true}) {
      for (const std::size_t buffered : {std::size_t{0U}, std::size_t{4096U}}) {
        CHECK(decideDeviceAction(reported(playing, settled), buffered, false, false) ==
              DeviceAction::None);
      }
    }
  }
}
