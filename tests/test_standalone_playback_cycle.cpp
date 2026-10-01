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

class PumpedAudioDevice;

// The pumped devices that are alive, oldest first, and what the next one that is made does when it
// is opened. A change of audio settings makes a new device and drops the old one, or drops the new
// one and keeps the old when the new cannot be opened.
struct PumpedDevices final {
  std::vector<PumpedAudioDevice*> alive;
  bool failNextOpen{false};
  // Called as a device is destroyed, before it is gone from the list.
  std::function<void(const PumpedAudioDevice&)> onDestroyed;
};

class PumpedAudioDevice final : public seam::platform::IAudioDevice {
public:
  explicit PumpedAudioDevice(PumpedDevices& devices) : devices_(&devices) {
    failOpen = devices.failNextOpen;
    devices.failNextOpen = false;
    devices.alive.push_back(this);
  }
  ~PumpedAudioDevice() override {
    if (devices_->onDestroyed) devices_->onDestroyed(*this);
    std::erase(devices_->alive, this);
  }
  PumpedAudioDevice(const PumpedAudioDevice&) = delete;
  PumpedAudioDevice& operator=(const PumpedAudioDevice&) = delete;

  seam::core::Result<void> open(const seam::platform::AudioDeviceConfig& config,
                                seam::platform::IAudioProcessor& processor) override {
    if (failOpen) {
      return seam::core::failure(seam::core::ErrorCode::IoError, "the pumped device cannot open");
    }
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
  seam::core::Result<void> stop() noexcept override {
    ++stopAttempts_;
    // A stop that the platform does not report as done: the device goes on running, and a callback
    // may still be in flight.
    if (failStop && running_) {
      return seam::core::failure(seam::core::ErrorCode::IoError, "the pumped device cannot stop");
    }
    running_ = false;
    return seam::core::success();
  }
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
  [[nodiscard]] std::size_t stopAttempts() const noexcept { return stopAttempts_; }

  static constexpr std::size_t kMaximumBlock = 1024U;
  bool failStart{false};
  bool failOpen{false};
  bool failStop{false};

private:
  PumpedDevices* devices_;
  seam::platform::IAudioProcessor* processor_{nullptr};
  seam::platform::AudioDeviceInfo info_;
  std::array<std::span<float>, 2U> views_{};
  std::vector<float> left_;
  std::vector<float> right_;
  bool running_{false};
  std::size_t starts_{0U};
  std::size_t stopAttempts_{0U};
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
  // Declared before the app, so that it outlives every device the app drops.
  std::shared_ptr<PumpedDevices> devices = std::make_shared<PumpedDevices>();
  std::unique_ptr<seam::standalone::NativeEditorApp> app;
  PumpedAudioDevice* device{nullptr};
  seam::native_ui::PixelSurface surface{1280U, 720U};
  std::shared_ptr<const seam::authoring::PublishedProjectAudio> published;

  explicit PlaybackRig(const std::filesystem::path& root) {
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
    config.threadedAudioDeviceFactory = [registry = devices] {
      return std::make_unique<PumpedAudioDevice>(*registry);
    };
    auto created = seam::standalone::NativeEditorApp::create(std::move(config));
    CHECK(created);
    app = std::move(created).value();
    refreshDevice();
    CHECK(device != nullptr);
  }

  seam::authoring::TransportController& transport() {
    return app->authoring().runtime().transport();
  }
  // The device the app has now. A change of audio settings replaces it, or keeps the old one when
  // the new one cannot be opened.
  void refreshDevice() { device = devices->alive.empty() ? nullptr : devices->alive.back(); }
  // What the transport was last told about the device is what the device does. A painted frame
  // tells it too, so this is asked straight after something the app did, with no frame between.
  bool transportKnowsTheDevice() {
    return transport().consumerRunning() == (device != nullptr && device->running());
  }
  // The creator picks another buffer size in the audio settings: the editor stops its device,
  // builds the transport again and opens a new device, and puts the old one back when it cannot.
  seam::core::Result<seam::authoring::AudioSettings> changeBlockSize() {
    const auto current = app->audioSettings();
    CHECK(current);
    auto requested = current.value();
    requested.blockFrames = requested.blockFrames == 128U ? 256U : 128U;
    auto applied = app->applyAudioSettings(requested);
    refreshDevice();
    return applied;
  }
  // A frame is painted at a time, as in a window, until the predicate holds.
  bool paintUntil(const std::function<bool()>& predicate,
                  std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
    return waitUntil([this, &predicate] {
      paint();
      return predicate();
    }, timeout);
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
  // Three seconds of song (144000 frames at 48 kHz): longer than the transport's ring, so the
  // feeder cannot finish until the device has played most of it. Returns the left channel of the
  // render the transport holds.
  std::vector<float> writeTheSong() {
    addNote(0, 1920, U"\u3053");
    addNote(1920, 1920, U"\u306a");
    return awaitTheRender();
  }
  // A song that fits in the transport's ring: the feeder hands all of it over in one turn, and
  // reports that it has stopped before anyone has heard any of it. Returns the left channel of the
  // render the transport holds.
  std::vector<float> writeAShortSong() {
    addNote(0, 480, U"\u3053");
    auto rendered = awaitTheRender();
    CHECK(rendered.size() > 4000U);
    CHECK(rendered.size() + 1024U < transport().ringBuffer().capacityFrames());
    return rendered;
  }
  // Waits until the transport holds the render of the document as it is, and returns its left channel.
  std::vector<float> awaitTheRender() {
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

// What the creator hears of a replacement render from "from" to its end, once and in order. The
// last 64 frames of a replacement are faded out, so they are left out of the comparison.
void checkRestOfReplacementHeardIs(const std::vector<float>& rest,
                                   const std::vector<float>& rendered, std::size_t from,
                                   const char* what) {
  constexpr std::size_t kFade = 64U;
  CHECK(rest.size() > kFade);
  CHECK(rendered.size() > kFade);
  if (rest.size() <= kFade || rendered.size() <= kFade) return;
  const std::vector<float> heard(rest.begin(), rest.end() - static_cast<std::ptrdiff_t>(kFade));
  const std::vector<float> wanted(rendered.begin(),
                                  rendered.end() - static_cast<std::ptrdiff_t>(kFade));
  checkHeardIs(heard, wanted, from, what);
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
  // The device that did not start is made sure of, once: it is not left to the platform.
  CHECK(rig.device->stopAttempts() == 1U);
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

seam::authoring::TransportState reported(bool playing, bool settled, bool available = true,
                                         bool playAwaits = false) {
  seam::authoring::TransportState state;
  state.playing = playing;
  state.settled = settled;
  state.available = available;
  state.playAwaitsConsumer = playAwaits;
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

TEST_CASE("standalone playback policy: a Play that no device has taken up starts the device for the audio the feeder has handed over") {
  using seam::standalone::DeviceAction;
  using seam::standalone::decideDeviceAction;
  // The feeder handed over all of a short song in one turn and reports that it is not playing: the
  // ring holds the song and nobody has played any of it.
  CHECK(decideDeviceAction(reported(false, true, true, true), 4096U, true, false) ==
        DeviceAction::Start);
  CHECK(decideDeviceAction(reported(false, true, true, true), 1U, true, false) ==
        DeviceAction::Start);
  // An empty ring has nothing to start a device for.
  CHECK(decideDeviceAction(reported(false, true, true, true), 0U, true, false) ==
        DeviceAction::None);
  // Nor has a transport with nothing rendered, and a report that does not include every command
  // says nothing about what the creator asked last.
  CHECK(decideDeviceAction(reported(false, true, false, true), 4096U, true, false) ==
        DeviceAction::None);
  CHECK(decideDeviceAction(reported(false, false, true, true), 4096U, true, false) ==
        DeviceAction::None);
  // A device that runs has taken the Play up: it plays the ring out, and is stopped when it has.
  CHECK(decideDeviceAction(reported(false, true, true, true), 4096U, true, true) ==
        DeviceAction::None);
  CHECK(decideDeviceAction(reported(false, true, true, true), 0U, true, true) == DeviceAction::Stop);
  // The same ring with no Play waiting for a device (one that ran and stopped on its own, or one
  // that was paused) starts nothing.
  CHECK(decideDeviceAction(reported(false, true, true, false), 4096U, true, false) ==
        DeviceAction::None);
  // A transport that is playing starts a device as it always did, waiting Play or not.
  CHECK(decideDeviceAction(reported(true, true, true, true), 4096U, true, false) ==
        DeviceAction::Start);
  CHECK(decideDeviceAction(reported(true, false, true, true), 4096U, true, false) ==
        DeviceAction::None);
}

TEST_CASE("standalone playback policy: with no device there is nothing to start or stop") {
  using seam::standalone::DeviceAction;
  using seam::standalone::decideDeviceAction;
  for (const bool playing : {false, true}) {
    for (const bool settled : {false, true}) {
      for (const bool awaits : {false, true}) {
        for (const std::size_t buffered : {std::size_t{0U}, std::size_t{4096U}}) {
          CHECK(decideDeviceAction(reported(playing, settled, true, awaits), buffered, false,
                                   false) == DeviceAction::None);
        }
      }
    }
  }
}

namespace {

// The device plays some callbacks of the song, a millisecond apart. Returns what it played.
std::vector<float> playBlocks(PlaybackRig& rig, int blocks) {
  std::vector<float> heard;
  for (int block = 0; block < blocks; ++block) {
    rig.pump(768U, heard);
    std::this_thread::sleep_for(1ms);
  }
  return heard;
}

// Presses Play and lets the device play until the feeder has handed over the last of the song.
// The device is still playing then: the ring holds the end of the song, and the transport no longer
// reports that it is playing.
std::vector<float> playUntilTheFeederHasFinished(PlaybackRig& rig) {
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  std::vector<float> heard;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto state = rig.transport().state();
    if (state.settled && !state.playing) break;
    rig.pump(256U, heard);
    std::this_thread::sleep_for(1ms);
  }
  return heard;
}

// The creator pauses while the song plays, in the moment before the feeder has applied it. The
// feeder's service is stopped, so that the Pause stays queued, as it would for the moment it takes.
void pauseBeforeTheFeederHasApplied(PlaybackRig& rig) {
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  static_cast<void>(playBlocks(rig, 4));
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && state.playing;
  }));
  rig.transport().shutdown();
  CHECK(rig.pressPlay());
  CHECK(!rig.device->running());
  // The feeder still reports what it was doing before the Pause.
  const auto state = rig.transport().state();
  CHECK(state.playing);
  CHECK(!state.settled);
}

}  // namespace

TEST_CASE("standalone audio settings: a change while the device plays the end of the song lets it be heard to its last sample") {
  const auto root = seam::test::support::temporaryDirectory("settings-tail");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  const auto heardBefore = playUntilTheFeederHasFinished(rig).size();
  // The feeder has handed over the whole song and the device has not yet played the end of it.
  CHECK(!rig.transport().state().playing);
  CHECK(rig.transport().ringBuffer().availableReadFrames() > 0U);
  CHECK(heardBefore < expected.size());

  CHECK(rig.changeBlockSize());
  // The creator was listening to the song: it goes on from where they were, once and in order.
  CHECK(rig.paintUntil([&] { return rig.device->running(); }));
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(rest, expected, heardBefore, "the end of the song after the change");
  CHECK(!rig.device->running());
}

namespace {

// The notices about audio that the creator is shown.
std::size_t audioNotices(PlaybackRig& rig) {
  rig.paint();
  const auto& entries = rig.app->authoring().controller().diagnosticPanel().entries();
  return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
    return entry.diagnostic.code == "AUDIO_UNAVAILABLE";
  }));
}

}  // namespace

TEST_CASE("standalone audio settings: a device that does not say that it has stopped keeps the audio as it was") {
  const auto root = seam::test::support::temporaryDirectory("settings-stop-fails");
  // What the app tells the transport about the consumer, in order. Declared before the rig, which
  // reports as it is torn down.
  std::vector<bool> told;
  PlaybackRig rig{root};
  rig.transport().setConsumerReportProbe([&told](bool running) { told.push_back(running); });
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  const auto heardBefore = playBlocks(rig, 12).size();
  CHECK(heardBefore > 4000U);
  const auto before = rig.app->audioSettings();
  CHECK(before);
  auto* const original = rig.device;
  const auto commandsBefore = rig.transport().feederStats().controlCommands;
  const auto configBefore = rig.transport().config();

  // The platform does not say that the device has stopped: it may still be calling back, so nothing
  // that its callback reads is touched. The creator is told, and nothing has changed.
  const auto toldBefore = told.size();
  original->failStop = true;
  const auto refused = rig.changeBlockSize();
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::IoError);
  CHECK(rig.app->audioSettings().value().blockFrames == before.value().blockFrames);
  CHECK(rig.devices->alive.size() == 1U);
  CHECK(rig.device == original);
  CHECK(original->running());
  CHECK(rig.transport().config().blockFrames == configBefore.blockFrames);
  CHECK(rig.transport().feederStats().controlCommands == commandsBefore);
  CHECK(rig.transport().state().playing);
  // The transport is told what is true: the device that did not say that it has stopped is the
  // consumer still, and it was not reported as gone on the way to being put back.
  CHECK(rig.transport().consumerRunning());
  CHECK(rig.transportKnowsTheDevice());
  // And at no point: the old device is the consumer until it says that it has stopped, so the
  // transport was never told that it was gone, not even for as long as the change was under way.
  CHECK(told.size() > toldBefore);
  CHECK(std::all_of(told.begin() + static_cast<std::ptrdiff_t>(toldBefore), told.end(),
                    [](bool running) { return running; }));

  // Asked again once the device says that it stops, the change goes through, and the song goes on
  // from where the creator was.
  original->failStop = false;
  CHECK(rig.changeBlockSize());
  CHECK(rig.app->audioSettings().value().blockFrames != before.value().blockFrames);
  CHECK(rig.paintUntil([&] { return rig.device->running(); }));
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(rest, expected, heardBefore, "playback after the change that was asked again");
}

TEST_CASE("standalone playback: a device that does not say that it has stopped is asked again by the next frame") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-stop-fails");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  CHECK(rig.pressPlay());
  rig.device->failStop = true;

  // The song plays out. Each frame that finds the ring empty asks the device to stop, and it does
  // not say that it has: it goes on running, and is asked again.
  std::vector<float> heard;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (rig.device->stopAttempts() < 3U && std::chrono::steady_clock::now() < deadline) {
    rig.pump(768U, heard);
    rig.paint();
    std::this_thread::sleep_for(1ms);
  }
  CHECK(rig.device->stopAttempts() >= 3U);
  CHECK(rig.device->running());
  checkHeardIs(heard, expected, 0U, "playback while the device does not stop");
  // The creator is told once, and not by every frame that asks again.
  CHECK(audioNotices(rig) == 1U);
  CHECK(!rig.app->lastError().empty());

  // The device stops when it can, and what the creator was told of it goes.
  rig.device->failStop = false;
  CHECK(rig.paintUntil([&] { return !rig.device->running(); }));
  CHECK(audioNotices(rig) == 0U);
}

TEST_CASE("standalone playback: a Pause whose device does not stop is told to the creator and pauses the transport") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-pause-stop-fails");
  PlaybackRig rig{root};
  static_cast<void>(rig.writeTheSong());
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  static_cast<void>(playBlocks(rig, 4));

  rig.device->failStop = true;
  const auto paused = rig.pressPlay();
  CHECK(!paused);
  CHECK(!paused.error().message.empty());
  // The song is paused, though the device runs on: it is the consumer of the ring until it says that
  // it has stopped.
  CHECK(rig.device->running());
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && !state.playing;
  }));
  CHECK(audioNotices(rig) == 1U);

  // The consumer answers the feeder's request to drop the ring, and the device stops once it says
  // that it can.
  std::vector<float> heard;
  rig.pump(256U, heard);
  CHECK(rig.device->running());
  rig.device->failStop = false;
  CHECK(rig.paintUntil([&] { return !rig.device->running(); }));
  CHECK(audioNotices(rig) == 0U);
  // Play after that plays.
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
}

TEST_CASE("standalone playback: a device that does not say that it has stopped is destroyed before the ring its callback reads") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-teardown-stop-fails");
  PlaybackRig rig{root};
  // The transport owns the ring that the device's callback reads, and owns this probe: the token
  // lives exactly as long as the transport does, so it says whether the ring is still there without
  // touching it.
  auto token = std::make_shared<int>(0);
  const std::weak_ptr<int> ringIsThere = token;
  rig.transport().setConsumerReportProbe([keep = std::move(token)](bool) {});
  struct AtDestruction final {
    bool seen{false};
    bool ringIsThere{false};
    bool running{false};
    std::size_t stopAttempts{0U};
  } atDestruction;
  rig.devices->onDestroyed = [&](const PumpedAudioDevice& device) {
    atDestruction = {true, !ringIsThere.expired(), device.running(), device.stopAttempts()};
  };
  static_cast<void>(rig.writeTheSong());
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());

  // The creator closes the app while the platform does not say that the device has stopped: a
  // callback may still be reading the ring when the app lets the device go, and the ring must
  // outlive that.
  rig.device->failStop = true;
  rig.app.reset();
  CHECK(atDestruction.seen);
  CHECK(atDestruction.running);
  CHECK(atDestruction.stopAttempts >= 1U);
  CHECK(atDestruction.ringIsThere);
  // The ring went with the runtime afterwards, so the token did follow it.
  CHECK(ringIsThere.expired());
}

TEST_CASE("standalone audio settings: a Pause the feeder has not applied yet is not undone by a change") {
  const auto root = seam::test::support::temporaryDirectory("settings-pending-pause");
  PlaybackRig rig{root};
  static_cast<void>(rig.writeTheSong());
  pauseBeforeTheFeederHasApplied(rig);

  CHECK(rig.changeBlockSize());
  // The creator paused: the song does not play and nothing starts the device.
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
}

TEST_CASE("standalone audio settings: a device that had stopped is not started by a change though the ring holds the end of the song") {
  const auto root = seam::test::support::temporaryDirectory("settings-device-stopped");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  static_cast<void>(playUntilTheFeederHasFinished(rig));
  CHECK(rig.transport().ringBuffer().availableReadFrames() > 0U);
  // The device stops by itself, as when it is unplugged: nobody is playing the end of the song.
  CHECK(rig.device->stop());
  CHECK(!rig.device->running());

  CHECK(rig.changeBlockSize());
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
}

TEST_CASE("standalone audio settings: a Pause the feeder has applied stays a Pause through a change") {
  const auto root = seam::test::support::temporaryDirectory("settings-settled-pause");
  PlaybackRig rig{root};
  static_cast<void>(rig.writeTheSong());
  CHECK(rig.pressPlay());
  static_cast<void>(playBlocks(rig, 4));
  CHECK(rig.pressPlay());
  CHECK(!rig.device->running());
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && !state.playing;
  }));

  CHECK(rig.changeBlockSize());
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
}

TEST_CASE("standalone audio settings: a song that has been played to its end is not started again by a change") {
  const auto root = seam::test::support::temporaryDirectory("settings-after-the-end");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  CHECK(rig.pressPlay());

  // The device plays all of it, and no frame has been painted since: the app has not yet noticed
  // that the song is over, and the device is still running.
  std::vector<float> heard;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto state = rig.transport().state();
    if (state.settled && !state.playing &&
        rig.transport().ringBuffer().availableReadFrames() == 0U) {
      break;
    }
    rig.pump(768U, heard);
    std::this_thread::sleep_for(1ms);
  }
  checkHeardIs(heard, expected, 0U, "playback");
  CHECK(rig.device->running());
  CHECK(!rig.transport().state().playing);

  CHECK(rig.changeBlockSize());
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
}

TEST_CASE("standalone audio settings: a device that cannot be opened is given up and playback goes on from where it was") {
  const auto root = seam::test::support::temporaryDirectory("settings-open-fails");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  const auto heardBefore = playBlocks(rig, 12).size();
  CHECK(heardBefore > 4000U);
  const auto before = rig.app->audioSettings();
  CHECK(before);

  rig.devices->failNextOpen = true;
  CHECK(!rig.changeBlockSize());
  CHECK(rig.app->audioSettings().value().blockFrames == before.value().blockFrames);
  // The device that was there is back, and the song goes on from where the creator had got to.
  CHECK(rig.device != nullptr);
  CHECK(rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(rest, expected, heardBefore, "playback after the old device was put back");
}

TEST_CASE("standalone audio settings: a Pause the feeder has not applied yet is kept when the new device cannot be opened") {
  const auto root = seam::test::support::temporaryDirectory("settings-open-fails-paused");
  PlaybackRig rig{root};
  static_cast<void>(rig.writeTheSong());
  pauseBeforeTheFeederHasApplied(rig);

  rig.devices->failNextOpen = true;
  CHECK(!rig.changeBlockSize());
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
}

TEST_CASE("standalone audio settings: a change that cannot tell where the creator is changes nothing and playback goes on") {
  const auto root = seam::test::support::temporaryDirectory("settings-place-unreadable");
  PlaybackRig rig{root};
  static_cast<void>(rig.writeTheSong());
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  static_cast<void>(playBlocks(rig, 12));
  const auto before = rig.app->audioSettings();
  CHECK(before);

  // From here the test moves the feeder: its service stops. The ring cannot be read for where the
  // device is when the device plays one frame and the feeder mixes one at every attempt.
  rig.transport().shutdown();
  seam::rendering::MultichannelPlaybackFeeder* feeder = nullptr;
  rig.transport().setStateSampleProbe(
      [&feeder](seam::rendering::MultichannelPlaybackFeeder& found) { feeder = &found; });
  static_cast<void>(rig.transport().state());
  rig.transport().setStateSampleProbe({});
  CHECK(feeder != nullptr);
  auto& ring = rig.transport().ringBuffer();
  std::size_t attempts = 0U;
  ring.setSnapshotProbe([&](int point) {
    if (point != 3) return;
    ++attempts;
    std::vector<float> frame(2U);
    static_cast<void>(ring.readFrames(frame));
    static_cast<void>(feeder->feedOnce());
  });
  const auto changed = rig.changeBlockSize();
  ring.setSnapshotProbe({});

  CHECK(!changed);
  CHECK(changed.error().code == seam::core::ErrorCode::Conflict);
  CHECK(attempts > 1U);
  // Nothing was changed: the settings are as they were, the device that was stopped for the change
  // runs again, and the transport is as it was.
  CHECK(rig.app->audioSettings().value().blockFrames == before.value().blockFrames);
  CHECK(rig.device != nullptr);
  CHECK(rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
  const auto state = rig.transport().state();
  CHECK(state.settled);
  CHECK(state.playing);
}

TEST_CASE("standalone playback: a render that arrives after the song has ended and been rewound does not play it again") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-render-after-the-end");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  CHECK(rig.pressPlay());
  static_cast<void>(rig.playUntilTheAppStopsTheDevice());
  CHECK(!rig.device->running());

  // The creator puts the playhead back at the start, and does not press Play. They edit a note,
  // and the render that follows replaces the audio.
  CHECK(rig.transport().seek(0));
  CHECK(rig.stoppedAt(0));
  rig.addNote(3840, 960, U"\u306b");
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.available &&
           state.publishedRevision == rig.app->authoring().runtime().document().session().revision();
  }));
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
}

TEST_CASE("standalone playback: a render that arrives while the device plays the end of the song goes on with the rest of it") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-render-during-the-end");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);

  const auto heardBefore = playUntilTheFeederHasFinished(rig).size();
  // The feeder has handed over the whole song and the device has not yet played the end of it.
  CHECK(!rig.transport().state().playing);
  CHECK(rig.transport().ringBuffer().availableReadFrames() > 0U);
  CHECK(heardBefore < expected.size());

  // The creator adds a note after the end of the song, and the render that follows replaces the
  // audio while the device has the end of the song still to play. The device is not played until
  // the feeder has applied the replacement, so that what is heard is what the replacement makes of
  // the song and not what was in the ring.
  rig.addNote(4800, 960, U"\u3053");
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.available && state.settled &&
           state.publishedRevision == rig.app->authoring().runtime().document().session().revision();
  }));
  const auto replacement = rig.app->authoring().runtime().audiblePublication().audio;
  CHECK(replacement != nullptr);
  const auto rendered = PlaybackRig::leftChannel(*replacement);
  // The new note makes the song longer, and changes the audio after the place the creator is at:
  // what is heard says whether it is the replacement or the end of the song that was in the ring.
  CHECK(rendered.size() > expected.size());

  // The creator was listening to the song: it goes on from where they were, once and in order, to
  // the end of the song as it now is.
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  checkRestOfReplacementHeardIs(rest, rendered, heardBefore, "the rest of the song after the render");
  CHECK(!rig.device->running());
}

TEST_CASE("standalone playback: the transport is told what the app does with the device as it does it") {
  const auto root =
      seam::test::support::temporaryDirectory("playback-cycle-transport-knows-the-device");
  PlaybackRig rig{root};
  static_cast<void>(rig.writeTheSong());
  CHECK(rig.transportKnowsTheDevice());

  // Play starts the device and Pause stops it, and no frame is painted after either.
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
  CHECK(rig.pressPlay());
  CHECK(!rig.device->running());
  CHECK(rig.transportKnowsTheDevice());

  // A Play whose device cannot start leaves the device stopped, and the transport knows it.
  // The feeder applies the Pause on its own thread, and a person cannot press a second button
  // within a millisecond: the button shows Play once it has.
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && !state.playing;
  }));
  rig.device->failStart = true;
  CHECK(!rig.pressPlay());
  CHECK(!rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
  rig.device->failStart = false;
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && !state.playing;
  }));

  // The device stops by itself while the song plays and no frame is painted, and the creator
  // changes the buffer size. The device the change leaves is not running, and the transport is
  // told so by the change and not by the next frame.
  CHECK(rig.pressPlay());
  CHECK(rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
  CHECK(rig.device->stop());
  CHECK(rig.changeBlockSize());
  CHECK(!rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
}

TEST_CASE("standalone playback: a render that arrives after the device has stopped on its own does not start it again") {
  const auto root =
      seam::test::support::temporaryDirectory("playback-cycle-render-after-the-device-stopped");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  static_cast<void>(playUntilTheFeederHasFinished(rig));
  // The feeder has handed over the whole song and the ring holds the end of it.
  CHECK(!rig.transport().state().playing);
  CHECK(rig.transport().ringBuffer().availableReadFrames() > 0U);

  // The device stops by itself, as when it is unplugged, and the next frame is painted: nobody is
  // going to play the end of the song, and the creator did not press Pause.
  CHECK(rig.device->stop());
  rig.paint();
  CHECK(!rig.device->running());
  const auto starts = rig.device->starts();

  // The creator adds a note after the end of the song, and the render that follows replaces the
  // audio. A Play carried to the replacement would start the device again.
  rig.addNote(4800, 1920, U"\u3053");
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.available && state.settled &&
           state.publishedRevision == rig.app->authoring().runtime().document().session().revision();
  }));
  CHECK(!waitUntil([&] { return rig.transport().state().playing; }, 300ms));
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
  CHECK(rig.device->starts() == starts);
}

namespace {

// The creator changes the buffer size while the device plays the end of the song. The device that
// the change makes is not started, because there is no audio for it until the render for the new
// transport lands, and the render and its feeder then finish before any frame is painted: the end
// of the song that they hand over has not been heard by anyone. Returns what the creator had heard
// before the change.
std::size_t changeSettingsDuringTheEndOfTheSong(PlaybackRig& rig) {
  const auto heardBefore = playUntilTheFeederHasFinished(rig).size();
  CHECK(!rig.transport().state().playing);
  CHECK(rig.transport().ringBuffer().availableReadFrames() > 0U);
  CHECK(rig.changeBlockSize());
  CHECK(!rig.device->running());
  CHECK(rig.transportKnowsTheDevice());
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.available && state.settled && !state.playing &&
           rig.transport().ringBuffer().availableReadFrames() > 0U;
  }));
  CHECK(!rig.device->running());
  return heardBefore;
}

}  // namespace

TEST_CASE("standalone audio settings: the Play that waits for the new device is still the creator's when the render lands before the device starts") {
  const auto root =
      seam::test::support::temporaryDirectory("settings-play-waits-for-the-new-device");
  PlaybackRig rig{root};
  CHECK(rig.writeTheSong().size() > 40000U);
  static_cast<void>(changeSettingsDuringTheEndOfTheSong(rig));

  // Nobody has played any of the end of the song that the new feeder handed over, and no device
  // has taken the creator's Play up. A second change of settings finds the creator still playing,
  // and the song goes on through it.
  CHECK(rig.changeBlockSize());
  CHECK(waitUntil([&] { return rig.transport().state().playing; }, 500ms));
}

TEST_CASE("standalone audio settings: an edit that lands before the new device has started goes on with the unheard rest of the song") {
  const auto root =
      seam::test::support::temporaryDirectory("settings-edit-before-the-new-device-starts");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  const auto heardBefore = changeSettingsDuringTheEndOfTheSong(rig);
  CHECK(heardBefore < expected.size());

  // The creator adds a note after the end of the song, and the render that follows replaces the
  // audio. The Play that waited for the new device is still the creator's: the replacement carries
  // it, and the next frame starts the device.
  rig.addNote(4800, 960, U"\u3053");
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.available && state.settled &&
           state.publishedRevision == rig.app->authoring().runtime().document().session().revision();
  }));
  const auto replacement = rig.app->authoring().runtime().audiblePublication().audio;
  CHECK(replacement != nullptr);
  const auto rendered = PlaybackRig::leftChannel(*replacement);
  CHECK(rendered.size() > expected.size());
  CHECK(rig.paintUntil([&] { return rig.device->running(); }));
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  checkRestOfReplacementHeardIs(rest, rendered, heardBefore,
                                "the rest of the song after a settings change and an edit");
  CHECK(!rig.device->running());
}

TEST_CASE("standalone audio settings: a change in the last ring of the song, with no edit, starts the new device once and the rest of the song is heard") {
  const auto root = seam::test::support::temporaryDirectory("settings-no-edit-new-device-starts");
  PlaybackRig rig{root};
  const auto expected = rig.writeTheSong();
  CHECK(expected.size() > 40000U);
  const auto heardBefore = changeSettingsDuringTheEndOfTheSong(rig);
  CHECK(heardBefore < expected.size());

  // The new feeder has handed over the rest of the song and reports that it has stopped, and no
  // frame has been painted since: it is not playing when a frame looks, and the new device has not
  // started. The Play that it has not taken up is all that says the creator is waiting to hear the
  // end of the song.
  CHECK(!rig.transport().state().playing);
  CHECK(rig.transport().state().playAwaitsConsumer);
  CHECK(rig.device->starts() == 0U);

  // Painted frames alone start the device, once, and it plays what the creator had not heard, once
  // and in order.
  CHECK(rig.paintUntil([&] { return rig.device->running(); }));
  CHECK(rig.device->starts() == 1U);
  CHECK(!rig.transport().state().playAwaitsConsumer);
  const auto rest = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(rest, expected, heardBefore,
               "the rest of the song after a settings change with no edit");
  CHECK(!rig.device->running());
  // The song has been played out, and nothing starts the device for it again.
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
  CHECK(rig.device->starts() == 1U);
}

namespace {

// The feeder hands over a song that fits in the ring in one turn and reports that it has stopped,
// and with no device running nobody has played any of it. No frame is painted meanwhile, so the
// first one that is painted finds a transport that is not playing.
void waitForTheFeederToHandOver(PlaybackRig& rig, std::size_t songFrames) {
  CHECK(waitUntil([&] {
    const auto state = rig.transport().state();
    return state.settled && !state.playing &&
           rig.transport().ringBuffer().availableReadFrames() >= songFrames;
  }));
  CHECK(!rig.device->running());
}

}  // namespace

TEST_CASE("standalone playback: a Play that only the transport was asked for starts the device, and the song is heard to its end") {
  // The Transport menu's Play sends the transport a Play and the device nothing (see
  // StandaloneApplicationController::dispatch, TogglePlayback), so it is the painted frame that
  // starts the device. A feeder that hands over a song in one turn is never seen playing by a frame.
  for (const bool shortSong : {true, false}) {
    const auto root = seam::test::support::temporaryDirectory(
        shortSong ? "playback-cycle-menu-play-short" : "playback-cycle-menu-play-long");
    PlaybackRig rig{root};
    const auto expected = shortSong ? rig.writeAShortSong() : rig.writeTheSong();
    CHECK(rig.transport().play());
    CHECK(!rig.device->running());
    if (shortSong) waitForTheFeederToHandOver(rig, expected.size());
    CHECK(rig.paintUntil([&] { return rig.device->running(); }));
    CHECK(rig.device->starts() == 1U);
    const auto heard = rig.playUntilTheAppStopsTheDevice();
    checkHeardIs(heard, expected, 0U, shortSong ? "a short song" : "a long song");
    CHECK(!rig.device->running());
    CHECK(rig.device->starts() == 1U);
  }
}

TEST_CASE("standalone playback: a Pause or a Stop that comes before the device has started takes a waiting Play away") {
  for (const bool stop : {false, true}) {
    const auto root = seam::test::support::temporaryDirectory(
        stop ? "playback-cycle-waiting-play-stop" : "playback-cycle-waiting-play-pause");
    PlaybackRig rig{root};
    const auto expected = rig.writeAShortSong();
    CHECK(rig.transport().play());
    waitForTheFeederToHandOver(rig, expected.size());
    CHECK(rig.transport().state().playAwaitsConsumer);
    // The ring still holds the song when the creator pauses: it is not theirs to hear any more.
    if (stop) {
      CHECK(rig.transport().stop());
    } else {
      CHECK(rig.transport().pause());
    }
    CHECK(!rig.transport().state().playAwaitsConsumer);
    CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
    CHECK(rig.device->starts() == 0U);
  }
}

TEST_CASE("standalone playback: a device that ran for a waiting Play and stopped on its own is not started again for the end of the song") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-waiting-play-served");
  PlaybackRig rig{root};
  const auto expected = rig.writeAShortSong();
  CHECK(rig.transport().play());
  waitForTheFeederToHandOver(rig, expected.size());
  CHECK(rig.paintUntil([&] { return rig.device->running(); }));
  CHECK(rig.device->starts() == 1U);
  std::vector<float> heard;
  rig.pump(256U, heard);
  CHECK(heard.size() == 256U);

  // The device stops by itself, as when it is unplugged, with the end of the song still in the
  // ring. It took the creator's Play up when it ran, and nobody is going to play that end.
  CHECK(rig.device->stop());
  CHECK(!rig.paintUntil([&] { return rig.device->running(); }, 300ms));
  CHECK(rig.device->starts() == 1U);
  CHECK(!rig.transport().state().playAwaitsConsumer);
}

TEST_CASE("standalone playback: a Play that waited for a device that cannot start is told to the creator, and starts it when it can") {
  const auto root = seam::test::support::temporaryDirectory("playback-cycle-waiting-play-no-device");
  PlaybackRig rig{root};
  const auto expected = rig.writeAShortSong();
  rig.device->failStart = true;
  CHECK(rig.transport().play());
  waitForTheFeederToHandOver(rig, expected.size());

  // The frame that starts the device is the one that learns it cannot start, and the creator is
  // told. The Play is theirs still: no device took it up.
  CHECK(rig.paintUntil([&] {
    const auto& entries = rig.app->authoring().controller().diagnosticPanel().entries();
    return std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
      return entry.diagnostic.code == "AUDIO_UNAVAILABLE";
    });
  }));
  CHECK(!rig.device->running());
  CHECK(rig.transport().state().playAwaitsConsumer);

  // The device works again: the next frame starts it, and the song is heard to its end.
  rig.device->failStart = false;
  CHECK(rig.paintUntil([&] { return rig.device->running(); }));
  CHECK(rig.device->starts() == 1U);
  const auto heard = rig.playUntilTheAppStopsTheDevice();
  checkHeardIs(heard, expected, 0U, "a short song after a start that failed");
  CHECK(!rig.device->running());
}
