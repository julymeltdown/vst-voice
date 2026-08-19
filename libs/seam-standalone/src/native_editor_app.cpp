#include "seam/standalone/native_editor_app.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace seam::standalone {

core::Result<std::unique_ptr<NativeEditorApp>> NativeEditorApp::create(
    NativeEditorAppConfig config) {
  auto app = std::unique_ptr<NativeEditorApp>{
      new NativeEditorApp(std::move(config))};
  auto initialized = app->initialize();
  if (!initialized) {
    return core::Result<std::unique_ptr<NativeEditorApp>>{initialized.error()};
  }
  return app;
}

NativeEditorApp::~NativeEditorApp() {
  shutdownAudio();
  authoring_.reset();
}

core::Result<void> NativeEditorApp::initialize() {
  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = [this] {
        if (window_ != nullptr) window_->requestRepaint();
      },
      .beginTextInput = [this](const native_ui::TextInputRequest& request) {
        if (window_ != nullptr) window_->beginTextInput(request);
      },
      .endTextInput = [this] {
        if (window_ != nullptr) window_->endTextInput();
      },
      .setPlaying = [this](bool) {
        if (window_ != nullptr) window_->requestRepaint();
      },
      .documentChanged = [this] {
        if (window_ != nullptr) window_->requestRepaint();
      },
  };
  auto created = AuthoringSession::create(config_.authoring,
                                          std::move(callbacks));
  if (!created) {
    return core::Result<void>{created.error()};
  }
  authoring_ = std::move(created).value();

  const auto character = character_.load(config_.characterPackage);
  if (character) {
    authoring_->controller().setCharacterMetadata(
        character_.displayName(), character_.styleName());
  } else {
    lastError_ = character.error().message;
  }
  return initializeAudio();
}

core::Result<void> NativeEditorApp::initializeAudio() {
  auto& runtime = authoring_->runtime();
  processor_ = std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
      runtime.transport().ringBuffer(),
      std::max<std::size_t>(config_.audioBlockFrames, 4096U));
  platform::AudioDeviceConfig deviceConfig{
      .sampleRate = runtime.transport().sampleRate(),
      .blockFrames = config_.audioBlockFrames,
      .outputChannels = runtime.transport().outputChannels(),
      .applicationName = "Project SEAM",
      .streamName = "Production standalone preview",
  };

  std::string physicalError;
  if (!config_.forceThreadedAudio) {
    auto physical = platform::createSystemAudioDevice();
    auto opened = physical->open(deviceConfig, *processor_);
    if (opened) {
      audioDevice_ = std::move(physical);
    } else {
      physicalError = opened.error().message;
      if (!opened.error().context.empty()) {
        physicalError += ": " + opened.error().context;
      }
    }
  }
  if (audioDevice_ == nullptr) {
    auto fallback = platform::createThreadedAudioDevice();
    auto opened = fallback->open(deviceConfig, *processor_);
    if (!opened) return opened;
    audioDevice_ = std::move(fallback);
  }

  auto started = audioDevice_->start();
  if (!started && audioDevice_->info().physical) {
    physicalError = started.error().message;
    audioDevice_->stop();
    auto fallback = platform::createThreadedAudioDevice();
    auto opened = fallback->open(deviceConfig, *processor_);
    if (!opened) return opened;
    started = fallback->start();
    if (!started) return started;
    audioDevice_ = std::move(fallback);
  } else if (!started) {
    return started;
  }

  if (!config_.startPaused) {
    const auto played = runtime.transport().play();
    if (!played) return played;
  }
  const auto info = audioDevice_->info();
  authoring_->controller().setAudioState(info.physical, info.backend);
  if (!physicalError.empty()) {
    std::cerr << "Physical audio unavailable, using callback-clock fallback: "
              << physicalError << '\n';
  }
  return core::success();
}

void NativeEditorApp::setWindow(native_ui::INativeWindow& window) noexcept {
  window_ = &window;
}

void NativeEditorApp::shutdownAudio() noexcept {
  if (audioDevice_ != nullptr) audioDevice_->stop();
  if (authoring_ != nullptr) {
    static_cast<void>(authoring_->runtime().transport().pause());
  }
}

void NativeEditorApp::paint(native_ui::RasterCanvas& canvas) noexcept {
  if (authoring_ == nullptr) return;
  const auto transport = authoring_->runtime().transport().state();
  const auto& project = authoring_->runtime().document().session().project();
  const auto tick = project.tempoMap().tickAtSampleFrame(
      transport.playhead, authoring_->runtime().transport().sampleRate());
  auto state = authoring_->controller().sceneState();
  state.playheadPixel =
      authoring_->controller().pianoRoll().timeline().tickToPixel(tick);
  character_.setDisplayMode(state.characterMode);
  character_.setState(state.characterState);
  state.characterPortrait = character_.portrait();
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  painter_.paint(canvas, authoring_->controller().pianoRoll(), state);
}

void NativeEditorApp::resized(double logicalWidth, double logicalHeight,
                              double) noexcept {
  authoring_->controller().resize(logicalWidth, logicalHeight);
}

void NativeEditorApp::pointerDown(
    const native_ui::PointerEvent& event) noexcept {
  record(authoring_->controller().pointerDown(event));
}
void NativeEditorApp::pointerMove(
    const native_ui::PointerEvent& event) noexcept {
  record(authoring_->controller().pointerMove(event));
}
void NativeEditorApp::pointerUp(
    const native_ui::PointerEvent& event) noexcept {
  record(authoring_->controller().pointerUp(event));
}
void NativeEditorApp::scroll(double deltaX, double deltaY, ui::Point anchor,
                             native_ui::InputModifiers modifiers) noexcept {
  authoring_->controller().scroll(deltaX, deltaY, anchor, modifiers);
}
void NativeEditorApp::keyDown(const native_ui::KeyEvent& event) noexcept {
  record(authoring_->controller().keyDown(event));
}
void NativeEditorApp::textComposition(
    std::u32string text,
    ui::CompositionSelection selection) noexcept {
  record(authoring_->controller().updateTextComposition(std::move(text),
                                                        selection));
}
void NativeEditorApp::textCommit(std::u32string text) noexcept {
  record(authoring_->controller().commitTextComposition(std::move(text)));
}
void NativeEditorApp::textCancel() noexcept {
  authoring_->controller().cancelTextComposition();
}
bool NativeEditorApp::wantsClose() const noexcept {
  return closeRequested_.load(std::memory_order_acquire);
}

platform::AudioDeviceInfo NativeEditorApp::audioInfo() const {
  return audioDevice_ == nullptr ? platform::AudioDeviceInfo{}
                                 : audioDevice_->info();
}
platform::AudioDeviceStats NativeEditorApp::audioStats() const noexcept {
  return audioDevice_ == nullptr ? platform::AudioDeviceStats{}
                                 : audioDevice_->stats();
}
platform::MultichannelRingProcessorStats
NativeEditorApp::processorStats() const noexcept {
  return processor_ == nullptr ? platform::MultichannelRingProcessorStats{}
                               : processor_->stats();
}

void NativeEditorApp::record(const core::Result<void>& result) noexcept {
  if (!result) {
    lastError_ = result.error().message;
    if (!result.error().context.empty()) {
      lastError_ += ": " + result.error().context;
    }
  }
}

}  // namespace seam::standalone
