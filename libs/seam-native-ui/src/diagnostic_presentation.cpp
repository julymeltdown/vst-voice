#include "seam/native_ui/diagnostic_presentation.hpp"

#include <algorithm>
#include <array>

namespace seam::native_ui {
namespace {

std::string actionLabel(authoring::DiagnosticAction action) {
  switch (action) {
    case authoring::DiagnosticAction::ChooseVoicebank: return "Choose voicebank";
    case authoring::DiagnosticAction::RelinkVoicebank: return "Relink voicebank";
    case authoring::DiagnosticAction::InstallVoicebank: return "Install voicebank";
    case authoring::DiagnosticAction::Retry: return "Retry";
    case authoring::DiagnosticAction::OpenSettings: return "Open settings";
    case authoring::DiagnosticAction::RelinkMedia: return "Relink media";
    case authoring::DiagnosticAction::SaveAs: return "Save a copy";
    case authoring::DiagnosticAction::RecoverAutosave: return "Recover autosave";
    case authoring::DiagnosticAction::OpenRecoveryFolder: return "Open recovery folder";
    case authoring::DiagnosticAction::OpenSupport: return "Get support";
    case authoring::DiagnosticAction::Dismiss: return "Dismiss";
    case authoring::DiagnosticAction::CopyDiagnostic: return "Copy details";
  }
  return "Details";
}

}

DiagnosticPresentation presentDiagnostic(const authoring::Diagnostic& diagnostic) {
  DiagnosticPresentation result;
  if (diagnostic.code == "BANK_MISSING") {
    result.title = "Voicebank needs attention";
    result.impact = "This track cannot render until its exact voicebank is available.";
  } else if (diagnostic.code == "MEDIA_MISSING") {
    result.title = "Backing media is missing";
    result.impact = "Relink the source file to restore this arrangement track.";
  } else if (diagnostic.code == "RENDER_FAILED") {
    result.title = "Render did not complete";
    result.impact = "Your project remains editable; retry after resolving the reported issue.";
  } else {
    result.title = "Project needs attention";
    result.impact = diagnostic.messageKey.empty() ? "Review the available recovery action."
                                                  : diagnostic.messageKey;
  }
  const std::array priority{
      authoring::DiagnosticAction::ChooseVoicebank,
      authoring::DiagnosticAction::RelinkVoicebank,
      authoring::DiagnosticAction::InstallVoicebank,
      authoring::DiagnosticAction::Retry,
      authoring::DiagnosticAction::OpenSettings,
      authoring::DiagnosticAction::RelinkMedia,
      authoring::DiagnosticAction::RecoverAutosave,
      authoring::DiagnosticAction::SaveAs,
      authoring::DiagnosticAction::OpenRecoveryFolder,
  };
  for (const auto action : priority) {
    if (result.primaryActions.size() == 2U) break;
    if (std::find(diagnostic.actions.begin(), diagnostic.actions.end(), action) ==
        diagnostic.actions.end()) continue;
    result.primaryActionKinds.push_back(action);
    result.primaryActions.push_back(actionLabel(action));
  }
  result.technicalDetail = diagnostic.code +
                           (diagnostic.messageKey.empty() ? "" : " / " + diagnostic.messageKey);
  return result;
}

std::string diagnosticActionLabel(authoring::DiagnosticAction action) {
  return actionLabel(action);
}

}
