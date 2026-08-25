#include "test_framework.hpp"

#include "seam/native_ui/diagnostic_panel.hpp"

TEST_CASE("diagnostic panel coalesces typed errors and validates actions") {
  seam::native_ui::DiagnosticPanelModel panel;
  panel.add(seam::authoring::Diagnostic{
      .code = "MEDIA_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "media.missing",
      .actions = {seam::authoring::DiagnosticAction::RelinkMedia},
  });
  panel.add(seam::authoring::Diagnostic{
      .code = "MEDIA_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "media.missing",
      .actions = {seam::authoring::DiagnosticAction::RelinkMedia},
      .occurrenceCount = 2U,
  });
  CHECK(panel.entries().size() == 1U);
  CHECK(panel.entries().front().diagnostic.occurrenceCount == 3U);
  CHECK(panel.errorCount() == 1U);
  CHECK(panel.activate(0U, seam::authoring::DiagnosticAction::RelinkMedia));
  CHECK(!panel.activate(0U, seam::authoring::DiagnosticAction::Retry));
}
