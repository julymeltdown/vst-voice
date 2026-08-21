#include "seam/authoring/diagnostic.hpp"

#include <array>
#include <algorithm>
#include <initializer_list>
#include <span>
#include <utility>

namespace seam::authoring {
namespace {

struct Definition final {
  std::string_view code;
  DiagnosticSeverity severity;
  std::span<const DiagnosticAction> actions;
};

constexpr DiagnosticAction kOpenSupport[]{DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kChooseBankSupport[]{DiagnosticAction::ChooseVoicebank,
                                                 DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kChooseBank[]{DiagnosticAction::ChooseVoicebank};
constexpr DiagnosticAction kRetrySupport[]{DiagnosticAction::Retry,
                                            DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kRetry[]{DiagnosticAction::Retry};
constexpr DiagnosticAction kRelinkSupport[]{DiagnosticAction::RelinkMedia,
                                             DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kOpenSettings[]{DiagnosticAction::OpenSettings};
constexpr DiagnosticAction kRecoverSupport[]{DiagnosticAction::RecoverAutosave,
                                              DiagnosticAction::OpenSupport};

constexpr std::array definitions{
    Definition{"PROJECT_NOT_FOUND", DiagnosticSeverity::Error, kOpenSupport},
    Definition{"BANK_UNTRUSTED", DiagnosticSeverity::Error, kChooseBankSupport},
    Definition{"BANK_COVERAGE_MISSING", DiagnosticSeverity::Warning, kChooseBank},
    Definition{"RENDER_FAILED", DiagnosticSeverity::Error, kRetrySupport},
    Definition{"RENDER_STALE", DiagnosticSeverity::Warning, kRetry},
    Definition{"MEDIA_MISSING", DiagnosticSeverity::Error, kRelinkSupport},
    Definition{"AUDIO_UNAVAILABLE", DiagnosticSeverity::Error, kOpenSettings},
    Definition{"PERSISTENCE_FAILED", DiagnosticSeverity::Critical, kRecoverSupport},
    Definition{"RECOVERY_FAILED", DiagnosticSeverity::Critical, kRecoverSupport},
    Definition{"EXPORT_FAILED", DiagnosticSeverity::Error, kRetrySupport},
    Definition{"PLUGIN_STATE_INVALID", DiagnosticSeverity::Error, kRetrySupport},
    Definition{"UPDATE_UNTRUSTED", DiagnosticSeverity::Error, kOpenSupport},
    Definition{"INSTALL_FAILED", DiagnosticSeverity::Error, kRetrySupport},
};

const Definition* find(std::string_view code) noexcept {
  for (const auto& definition : definitions) {
    if (definition.code == code) return &definition;
  }
  return nullptr;
}

}

std::string_view toString(DiagnosticSeverity severity) noexcept {
  switch (severity) {
    case DiagnosticSeverity::Info: return "INFO";
    case DiagnosticSeverity::Warning: return "WARNING";
    case DiagnosticSeverity::Error: return "ERROR";
    case DiagnosticSeverity::Critical: return "CRITICAL";
  }
  return "UNKNOWN";
}

std::string_view toString(DiagnosticAction action) noexcept {
  switch (action) {
    case DiagnosticAction::Dismiss: return "DISMISS";
    case DiagnosticAction::Retry: return "RETRY";
    case DiagnosticAction::OpenSettings: return "OPEN_SETTINGS";
    case DiagnosticAction::ChooseVoicebank: return "CHOOSE_VOICEBANK";
    case DiagnosticAction::RelinkMedia: return "RELINK_MEDIA";
    case DiagnosticAction::RecoverAutosave: return "RECOVER_AUTOSAVE";
    case DiagnosticAction::OpenSupport: return "OPEN_SUPPORT";
  }
  return "UNKNOWN";
}

bool DiagnosticRegistry::isRegistered(std::string_view code) noexcept {
  return find(code) != nullptr;
}

DiagnosticSeverity DiagnosticRegistry::severity(std::string_view code) noexcept {
  const auto* definition = find(code);
  return definition == nullptr ? DiagnosticSeverity::Error : definition->severity;
}

std::vector<DiagnosticAction> DiagnosticRegistry::actions(std::string_view code) {
  const auto* definition = find(code);
  if (definition == nullptr) return {DiagnosticAction::Dismiss};
  return {definition->actions.begin(), definition->actions.end()};
}

core::Result<void> DiagnosticRegistry::validate(const Diagnostic& diagnostic) {
  if (!isRegistered(diagnostic.code)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic code is not registered");
  }
  if (diagnostic.messageKey.empty() || diagnostic.messageKey.size() > 128U ||
      diagnostic.occurrenceCount == 0U || diagnostic.affectedIds.size() > 32U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic fields exceed the bounded contract");
  }
  const auto expected = actions(diagnostic.code);
  for (const auto action : diagnostic.actions) {
    if (std::find(expected.begin(), expected.end(), action) == expected.end()) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Diagnostic exposes an unregistered recovery action");
    }
  }
  return core::success();
}

Diagnostic DiagnosticRegistry::fromError(const core::Error& error) {
  std::string code = "PERSISTENCE_FAILED";
  switch (error.code) {
    case core::ErrorCode::NotFound: code = "PROJECT_NOT_FOUND"; break;
    case core::ErrorCode::IoError: code = "PERSISTENCE_FAILED"; break;
    case core::ErrorCode::Unsupported: code = "EXPORT_FAILED"; break;
    case core::ErrorCode::InvalidArgument:
    case core::ErrorCode::InvalidState:
    case core::ErrorCode::InvariantViolation:
    case core::ErrorCode::Conflict:
    case core::ErrorCode::ParseError:
    case core::ErrorCode::Internal: break;
  }
  return Diagnostic{.code = std::move(code),
                    .severity = severity(code),
                    .messageKey = "generic.failure",
                    .affectedIds = {},
                    .actions = actions(code),
                    .occurrenceCount = 1U};
}

}
