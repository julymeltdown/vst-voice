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

}  // namespace seam::clap_editor
