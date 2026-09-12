#include "seam/authoring/diagnostic.hpp"
#include "seam/core/sha256.hpp"

#include <array>
#include <algorithm>
#include <initializer_list>
#include <span>
#include <utility>
#include <limits>

namespace seam::authoring {
namespace {

struct Definition final {
  std::string_view code;
  DiagnosticSeverity severity;
  std::span<const DiagnosticAction> actions;
};

constexpr DiagnosticAction kOpenSupport[]{DiagnosticAction::OpenSupport,
                                           DiagnosticAction::CopyDiagnostic};
constexpr DiagnosticAction kChooseBankSupport[]{DiagnosticAction::ChooseVoicebank,
                                                 DiagnosticAction::CopyDiagnostic,
                                                 DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kChooseBank[]{DiagnosticAction::ChooseVoicebank,
                                          DiagnosticAction::CopyDiagnostic};
constexpr DiagnosticAction kBankRecovery[]{DiagnosticAction::InstallVoicebank,
                                            DiagnosticAction::RelinkVoicebank,
                                            DiagnosticAction::ChooseVoicebank,
                                            DiagnosticAction::CopyDiagnostic,
                                            DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kRetrySupport[]{DiagnosticAction::Retry,
                                            DiagnosticAction::CopyDiagnostic,
                                            DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kRetry[]{DiagnosticAction::Retry,
                                    DiagnosticAction::CopyDiagnostic};
constexpr DiagnosticAction kRelinkSupport[]{DiagnosticAction::RelinkMedia,
                                             DiagnosticAction::CopyDiagnostic,
                                             DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kOpenSettings[]{DiagnosticAction::OpenSettings,
                                            DiagnosticAction::CopyDiagnostic};
constexpr DiagnosticAction kRecoverSupport[]{DiagnosticAction::RecoverAutosave,
                                              DiagnosticAction::SaveAs,
                                              DiagnosticAction::OpenRecoveryFolder,
                                              DiagnosticAction::CopyDiagnostic,
                                              DiagnosticAction::OpenSupport};
constexpr DiagnosticAction kCrashSupport[]{DiagnosticAction::RecoverAutosave,
                                            DiagnosticAction::OpenRecoveryFolder,
                                            DiagnosticAction::CopyDiagnostic,
                                            DiagnosticAction::OpenSupport,
                                            DiagnosticAction::Dismiss};
constexpr DiagnosticAction kSupportPreview[]{
    DiagnosticAction::ExportSupportBundle,
    DiagnosticAction::Dismiss};
constexpr DiagnosticAction kSupportExported[]{
    DiagnosticAction::OpenSupportFolder,
    DiagnosticAction::DeleteSupportBundle,
    DiagnosticAction::Dismiss};

constexpr std::array definitions{
    Definition{"PROJECT_NOT_FOUND", DiagnosticSeverity::Error, kOpenSupport},
    Definition{"BANK_MISSING", DiagnosticSeverity::Error, kBankRecovery},
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
    Definition{"CRASH_RECOVERY_AVAILABLE", DiagnosticSeverity::Warning, kCrashSupport},
    Definition{"SUPPORT_BUNDLE_PREVIEW_READY", DiagnosticSeverity::Info,
               kSupportPreview},
    Definition{"SUPPORT_BUNDLE_EXPORTED", DiagnosticSeverity::Info,
               kSupportExported},
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
    case DiagnosticAction::RelinkVoicebank: return "RELINK_VOICEBANK";
    case DiagnosticAction::InstallVoicebank: return "INSTALL_VOICEBANK";
    case DiagnosticAction::RelinkMedia: return "RELINK_MEDIA";
    case DiagnosticAction::SaveAs: return "SAVE_AS";
    case DiagnosticAction::OpenRecoveryFolder: return "OPEN_RECOVERY_FOLDER";
    case DiagnosticAction::CopyDiagnostic: return "COPY_DIAGNOSTIC";
    case DiagnosticAction::RecoverAutosave: return "RECOVER_AUTOSAVE";
    case DiagnosticAction::OpenSupport: return "OPEN_SUPPORT";
    case DiagnosticAction::ExportSupportBundle: return "EXPORT_SUPPORT_BUNDLE";
    case DiagnosticAction::OpenSupportFolder: return "OPEN_SUPPORT_FOLDER";
    case DiagnosticAction::DeleteSupportBundle: return "DELETE_SUPPORT_BUNDLE";
  }
  return "UNKNOWN";
}

bool DiagnosticRegistry::isRegistered(std::string_view code) noexcept {
  return find(code) != nullptr;
}

bool Diagnostic::sameIssueAs(const Diagnostic& other) const noexcept {
  return code == other.code && messageKey == other.messageKey && severity == other.severity &&
      affectedIds == other.affectedIds && actions == other.actions && detail == other.detail &&
      detailTruncated == other.detailTruncated && detailEscaped == other.detailEscaped && detailSourceHash == other.detailSourceHash;
}

namespace {
std::size_t printableUtf8Sequence(std::string_view text, std::size_t i) {
  const auto c = static_cast<unsigned char>(text[i]);
  if (c < 0x80U) return (c >= 0x20U && c != 0x7fU) || c == '\n' || c == '\r' || c == '\t' ? 1U : 0U;
  const auto length = c >= 0xc2U && c <= 0xdfU ? 2U : c >= 0xe0U && c <= 0xefU ? 3U : c >= 0xf0U && c <= 0xf4U ? 4U : 0U;
  if (length == 0U || length > text.size() - i) return 0U;
  for (std::size_t j = 1U; j < length; ++j) {
    const auto next = static_cast<unsigned char>(text[i + j]); if (next < 0x80U || next > 0xbfU) return 0U;
  }
  const auto second = static_cast<unsigned char>(text[i + 1U]);
  if ((c == 0xe0U && second < 0xa0U) || (c == 0xedU && second > 0x9fU) ||
      (c == 0xf0U && second < 0x90U) || (c == 0xf4U && second > 0x8fU)) return 0U;
  return length;
}
}

void Diagnostic::setDetail(std::string_view text) {
  const auto sourceHash = text.empty() ? std::string{} : core::sha256Hex(text);
  std::string output; output.reserve(std::min(text.size(), maximumDetailBytes));
  bool escaped = false, truncated = false;
  constexpr char hex[] = "0123456789ABCDEF";
  for (std::size_t i = 0U; i < text.size();) {
    const auto length = printableUtf8Sequence(text, i);
    const auto required = length == 0U ? 4U : length;
    if (required > maximumDetailBytes - output.size()) { truncated = true; break; }
    if (length != 0U) { output.append(text.substr(i, length)); i += length; }
    else {
      const auto byte = static_cast<unsigned char>(text[i++]);
      output += "\\x"; output.push_back(hex[byte >> 4U]); output.push_back(hex[byte & 15U]); escaped = true;
    }
  }
  detail = std::move(output); detailTruncated = truncated; detailEscaped = escaped; detailSourceHash = sourceHash;
}

void Diagnostic::addOccurrences(std::size_t additional) noexcept {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  occurrenceCount = additional > maximum - occurrenceCount ? maximum : occurrenceCount + additional;
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
      diagnostic.occurrenceCount == 0U || diagnostic.affectedIds.size() > 32U || diagnostic.detail.size() > Diagnostic::maximumDetailBytes) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic fields exceed the bounded contract");
  }
  for (std::size_t i = 0U; i < diagnostic.detail.size();) {
    const auto length = printableUtf8Sequence(diagnostic.detail, i);
    if (length == 0U) return core::failure(core::ErrorCode::InvalidArgument, "Diagnostic detail must be bounded display-safe UTF-8");
    i += length;
  }
  if (!diagnostic.detailSourceHash.empty() && (diagnostic.detailSourceHash.size() != 64U ||
      !std::all_of(diagnostic.detailSourceHash.begin(), diagnostic.detailSourceHash.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      }))) return core::failure(core::ErrorCode::InvalidArgument, "Diagnostic detail identity is invalid");
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
  const auto diagnosticSeverity = severity(code);
  const auto diagnosticActions = actions(code);
  Diagnostic diagnostic{.code = std::move(code),
                    .severity = diagnosticSeverity,
                    .messageKey = "generic.failure",
                    .affectedIds = {},
                    .actions = diagnosticActions,
                    .occurrenceCount = 1U};
  diagnostic.setDetail(error.message);
  return diagnostic;
}

}
