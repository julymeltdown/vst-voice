#include "seam/clap_editor/editor_runtime.hpp"

namespace seam::clap_editor {

// The EMO/SCENE SING workspace in the embedded editor. The shell owns the chrome around the grid;
// points inside the grid reach the same controller commands (and undo history) as the classic
// editor, and an edit made through the shell invalidates the prepared bounce like any other edit.

void EditorRuntime::activateDesignShell() {
  std::lock_guard lock(mutex_);
  shell_.setRepaintCallback([this] { requestRepaint(); });
  shell_.activate();
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
