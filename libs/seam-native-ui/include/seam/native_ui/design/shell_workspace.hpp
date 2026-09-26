#pragma once

#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui::design {

// A workspace that replaces the SING score with its own body (TUNE, MIX). The shell keeps the
// header, rack and status bar, and hands the workspace everything inside `area`: painting, pointer
// gestures that start there, scrolling there, and the accessibility nodes it publishes. The score
// is covered while one is shown, so no key or accessibility action reaches the notes.
//
// Every edit goes through NativeEditorController commands, so undo, validation and render
// invalidation behave as they do in the classic editor. Node ids start with the workspace's
// prefix ("shell.tune.", "shell.mix."), which the shell uses to route actions and keys.
class ShellWorkspace {
public:
  virtual ~ShellWorkspace() = default;

  // "shell.tune." or "shell.mix.".
  [[nodiscard]] virtual std::string_view idPrefix() const noexcept = 0;

  virtual void paint(paint::Canvas2D& c, const DesignTokens& t,
                     const NativeEditorController& controller, const EditorSceneState& state,
                     ui::Rect area) const = 0;

  // A press inside `area` starts a gesture; move and up follow it wherever the pointer goes.
  virtual core::Result<void> pointerDown(NativeEditorController& controller,
                                         const PointerEvent& event, ui::Rect area) = 0;
  virtual core::Result<void> pointerMove(NativeEditorController& controller,
                                         const PointerEvent& event, ui::Rect area) = 0;
  virtual core::Result<void> pointerUp(NativeEditorController& controller,
                                       const PointerEvent& event, ui::Rect area) = 0;
  // Returns true when consumed. The shell consumes every scroll over the area either way.
  virtual bool scroll(NativeEditorController& controller, ui::Point anchor, double deltaX,
                      double deltaY, ui::Rect area) {
    static_cast<void>(controller);
    static_cast<void>(anchor);
    static_cast<void>(deltaX);
    static_cast<void>(deltaY);
    static_cast<void>(area);
    return false;
  }

  // Appends the nodes shown in `area`, in Tab order, with bounds in shell coordinates.
  virtual void semantics(const NativeEditorController& controller, const EditorSceneState& state,
                         ui::Rect area, std::vector<SemanticNode>& out) const = 0;
  // Performs an accessibility action on one of its own nodes. SetFocus is handled by the shell.
  virtual core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                                     SemanticAction action) = 0;
  // A plain key while one of its nodes holds focus. Returns true when consumed; the shell stops
  // every other plain key as well, so nothing reaches the covered score.
  virtual bool key(NativeEditorController& controller, std::string_view focusedId,
                   const KeyEvent& event) {
    static_cast<void>(controller);
    static_cast<void>(focusedId);
    static_cast<void>(event);
    return false;
  }
  // Abandons a gesture in progress without committing it (Escape, capture loss, workspace change).
  virtual void cancelGestures(NativeEditorController& controller) {
    static_cast<void>(controller);
  }
  // True while a pointer gesture started in this workspace is in progress.
  [[nodiscard]] virtual bool gestureActive() const noexcept { return false; }
  // Closes a transient surface the workspace shows over itself (a card's action menu). Returns
  // true when one was open, so the shell's Escape closes it instead of leaving the workspace.
  [[nodiscard]] virtual bool dismissTransient() { return false; }
  // The node a press just landed on, so keyboard focus follows the pointer (as for a SING knob).
  // Read once by the shell after pointerDown; empty leaves focus cleared.
  [[nodiscard]] virtual std::string takeFocusRequest() { return {}; }
  // The workspace the press that just landed asks the shell to show ("sing": a double-click on a
  // MIX arrangement region opens it there). Read once by the shell after pointerDown; empty stays.
  [[nodiscard]] virtual std::string takeWorkspaceRequest() { return {}; }
};

[[nodiscard]] std::unique_ptr<ShellWorkspace> makeTuneWorkspace();
[[nodiscard]] std::unique_ptr<ShellWorkspace> makeMixWorkspace();

}  // namespace seam::native_ui::design
