#pragma once

#include "seam/authoring/diagnostic.hpp"

#include <string>
#include <vector>

namespace seam::native_ui {

struct DiagnosticPresentation final {
  std::string title;
  std::string impact;
  std::vector<authoring::DiagnosticAction> primaryActionKinds;
  std::vector<std::string> primaryActions;
  std::string technicalDetail;
};

[[nodiscard]] DiagnosticPresentation presentDiagnostic(
    const authoring::Diagnostic& diagnostic);
[[nodiscard]] std::string diagnosticActionLabel(
    authoring::DiagnosticAction action);

}
