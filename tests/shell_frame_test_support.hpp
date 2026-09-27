#pragma once

// Paints one editor frame the way every host does now that the SING shell is the only editor
// surface: an activated shell prepares the frame and paints it, and a platform without the vector
// backend gets the "editor unavailable" notice. Tests use it for optional PPM captures and for the
// few checks that need the painted frame. The controller's piano-roll viewport and hosted geometry
// are restored afterwards, so a capture never changes what the rest of a test observes.

#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"

namespace seam::test {

inline bool paintEditorFrame(native_ui::RasterCanvas& canvas,
                             native_ui::NativeEditorController& controller) {
  const auto viewport = controller.pianoRoll().viewport();
  const auto hosted = controller.hostedGrid();
  const auto restore = [&] {
    controller.pianoRoll().setViewport(viewport);
    controller.pianoRoll().rebuildIndex();
    controller.setHostedGrid(hosted);
  };
  native_ui::design::SingShell shell;
  shell.activate({}, native_ui::design::DesignPreferences{});
  if (shell.prepareFrame(controller, canvas.logicalWidth(), canvas.logicalHeight()) &&
      shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick())) {
    restore();
    return true;
  }
  restore();
  native_ui::paintEditorUnavailable(canvas);
  return false;
}

}  // namespace seam::test
