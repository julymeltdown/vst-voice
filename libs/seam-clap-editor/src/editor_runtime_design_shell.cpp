#include "seam/clap_editor/editor_runtime.hpp"

namespace seam::clap_editor {

// The EMO/SCENE SING workspace in the embedded editor. The shell owns the chrome around the grid;
// points inside the grid reach the same controller commands (and undo history) as the classic
// editor, and an edit made through the shell invalidates the prepared bounce like any other edit.

void EditorRuntime::activateDesignShell() {
  activateDesignShellWith(std::nullopt);
}

// The header output meter: the plug-in's audio thread measures what it returns to the host, and
// the GUI timer hands the reading to the controller here.
void EditorRuntime::setOutputLevel(
    std::optional<native_ui::EditorSceneState::OutputLevel> level) {
  std::lock_guard lock(mutex_);
  if (controller_ != nullptr) controller_->setOutputLevel(std::move(level));
}

void EditorRuntime::setOutputClipResetCallback(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  outputClipResetCallback_ = std::move(callback);
}

void EditorRuntime::resetOutputClip() {
  std::function<void()> reset;
  {
    std::lock_guard lock(mutex_);
    reset = outputClipResetCallback_;
  }
  if (reset) reset();
}

void EditorRuntime::activateDesignShell(native_ui::design::DesignPreferences preferences) {
  activateDesignShellWith(preferences);
}

void EditorRuntime::activateDesignShellWith(
    std::optional<native_ui::design::DesignPreferences> preferences) {
  std::lock_guard lock(mutex_);
  shell_.setRepaintCallback([this] { requestRepaint(); });
  // A plug-in does not write files from the editor: the DAW renders and exports the track.
  shell_.setHostActions(native_ui::design::ShellHostActions{
      .exportSet = {},
      .exportPlan = {},
      .exportUnavailable =
          "In a plug-in, export from your DAW: render or bounce this track there.",
      .exportBusy = {},
      // Paint runs under this runtime's lock; the cache never waits on a worker.
      .regionWaveform =
          [this] {
            const auto audible = authoring_->audiblePublication();
            return native_ui::bindRegionWaveform(
                native_ui::RegionWaveformRequest{.audio = audible.audio,
                                                 .stale = audible.stale,
                                                 .documentRevision = session_.revision(),
                                                 .track = authoring_->selectedTrack(),
                                                 .region = authoring_->selectedRegion()},
                waveforms_);
          },
      // What EditorRuntime::keyDown handles itself (Command-Shift-O/E score interchange) and the
      // editor's undo and redo. A plug-in has no New/Open/Save/Quit: the DAW owns those keys.
      .applicationShortcut =
          [](const native_ui::KeyEvent& event) {
            if (event.modifiers.alt) return false;
            if (event.modifiers.command && event.modifiers.shift &&
                (event.key == native_ui::NativeKey::O || event.key == native_ui::NativeKey::E))
              return true;
            return event.modifiers.primaryShortcut() &&
                   (event.key == native_ui::NativeKey::Z || event.key == native_ui::NativeKey::Y);
          },
  });
  if (preferences.has_value()) {
    shell_.activate(native_ui::design::locateDesignAssets(), *preferences);
  } else {
    shell_.activate();
  }
  requestRepaint();
}

void EditorRuntime::cancelPointerGestures() {
  std::lock_guard lock(mutex_);
  shell_.cancelGestures(*controller_);
}

void EditorRuntime::resize(double logicalWidth, double logicalHeight) noexcept {
  std::lock_guard lock(mutex_);
  logicalWidth_ = std::max(480.0, logicalWidth);
  logicalHeight_ = std::max(320.0, logicalHeight);
  controller_->resize(logicalWidth_, logicalHeight_);
  // The shell's layout follows the size at once, so accessibility and input validate against the
  // controls that are on screen now rather than the previous frame's.
  static_cast<void>(shell_.prepareFrame(*controller_, logicalWidth_, logicalHeight_));
}

bool EditorRuntime::routeShellPointerLocked(ShellPointerPhase phase,
                                            const native_ui::PointerEvent& event) {
  if (!shell_.presentedLastFrame()) return false;
  const auto before = session_.revision();
  switch (phase) {
    case ShellPointerPhase::Down:
      static_cast<void>(shell_.pointerDown(*controller_, event));
      break;
    case ShellPointerPhase::Move:
      static_cast<void>(shell_.pointerMove(*controller_, event));
      break;
    case ShellPointerPhase::Up:
      static_cast<void>(shell_.pointerUp(*controller_, event));
      break;
  }
  if (session_.revision() != before) requestRenderAfterEdit();
  return true;
}

// A focused shell control edits from the keyboard too (arrows on a knob); like a pointer edit, a
// changed document invalidates the prepared bounce.
bool EditorRuntime::shellKeyLocked(const native_ui::KeyEvent& event) {
  const auto before = session_.revision();
  const auto handled = shell_.handleShellKey(*controller_, event);
  if (session_.revision() != before) requestRenderAfterEdit();
  return handled;
}

}  // namespace seam::clap_editor
