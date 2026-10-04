// The diagnostics the editor raises about itself, shared by the controller's translation units.
//
// These were functions and constants in editor_controller.cpp's anonymous namespace. Once the
// accessibility, overlay and edit-command clusters moved into their own files, three translation
// units needed the same notice vocabulary, so it lives here once rather than being copied. A
// copied notice builder is a silent-drift risk: the severity and the action set come from the
// registry by code, so two copies could disagree about how a code is presented without either
// failing to compile.
//
// Internal to the editor controller's implementation; not part of the library's surface.
#pragma once

#include "seam/authoring/diagnostic.hpp"

#include <cstddef>
#include <string_view>

namespace seam::native_ui {

inline constexpr std::string_view kEditRefusedCode = "EDIT_REFUSED";

// How many refusals of different wording the stack keeps; the oldest goes first.
inline constexpr std::size_t kMaximumRefusalNotices = 8U;

// Build a notice the editor raises itself. Severity and actions are read from the registry by code,
// so a notice can never advertise a recovery action its code does not define.
[[nodiscard]] inline authoring::Diagnostic makeEditorNotice(std::string_view code,
                                                           std::string_view messageKey,
                                                           std::string_view detail) {
  authoring::Diagnostic notice{
      .code = std::string{code},
      .severity = authoring::DiagnosticRegistry::severity(code),
      .messageKey = std::string{messageKey},
      .affectedIds = {},
      .actions = authoring::DiagnosticRegistry::actions(code),
      .occurrenceCount = 1U,
  };
  notice.setDetail(detail);
  return notice;
}

}  // namespace seam::native_ui
