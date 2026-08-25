#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/native_ui/export_dialog.hpp"
#include "seam/native_ui/export_progress_panel.hpp"

TEST_CASE("export dialog preflight blocks invalid combinations") {
  seam::application::ProjectFactory factory{40000U};
  auto project = factory.createProject("Export dialog");
  seam::native_ui::ExportDialogModel dialog;
  CHECK(!dialog.preflight(project));
  CHECK(!dialog.canExport());
  CHECK(!dialog.issues().empty());

  dialog.setDestination(seam::test::support::temporaryDirectory("export-dialog") /
                        "set");
  dialog.setSettings(seam::authoring::ExportSettings{
      .sampleRate = 48000U,
      .channels = 2U,
      .format = seam::voicebank::WavSampleFormat::Pcm24,
      .includeMaster = true,
      .includeStems = false,
      .replaceExisting = false,
  });
  CHECK(dialog.preflight(project));
  CHECK(dialog.canExport());
}

TEST_CASE("export progress panel exposes bounded progress and cancellation") {
  seam::native_ui::ExportProgressPanelModel panel;
  panel.update(seam::authoring::ExportProgress{
      .state = seam::authoring::ExportState::Staging,
      .currentOutput = "master.wav",
      .completedFiles = 1U,
      .totalFiles = 4U,
  });
  CHECK_NEAR(panel.fraction(), 0.25, 1e-9);
  CHECK(panel.cancellable());
  panel.requestCancel();
  CHECK(panel.cancelRequested());
  panel.update(seam::authoring::ExportProgress{
      .state = seam::authoring::ExportState::Committed,
      .completedFiles = 4U,
      .totalFiles = 4U,
  });
  CHECK_NEAR(panel.fraction(), 1.0, 1e-9);
  CHECK(!panel.cancellable());
  panel.update(seam::authoring::ExportProgress{
      .state = seam::authoring::ExportState::Publishing,
      .completedFiles = 4U,
      .totalFiles = 4U,
  });
  CHECK(!panel.cancellable());
}
