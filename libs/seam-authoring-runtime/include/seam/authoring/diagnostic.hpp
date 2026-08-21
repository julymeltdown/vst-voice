#pragma once

#include "seam/core/error.hpp"
#include "seam/core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace seam::authoring {

enum class DiagnosticSeverity { Info, Warning, Error, Critical };

enum class DiagnosticAction {
  Dismiss,
  Retry,
  OpenSettings,
  ChooseVoicebank,
  RelinkMedia,
  RecoverAutosave,
  OpenSupport,
};

struct Diagnostic final {
  std::string code;
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  std::string messageKey;
  std::vector<std::string> affectedIds;
  std::vector<DiagnosticAction> actions;
  std::size_t occurrenceCount{1U};
};

class DiagnosticRegistry final {
public:
  [[nodiscard]] static bool isRegistered(std::string_view code) noexcept;
  [[nodiscard]] static DiagnosticSeverity severity(std::string_view code) noexcept;
  [[nodiscard]] static std::vector<DiagnosticAction> actions(std::string_view code);
  [[nodiscard]] static core::Result<void> validate(const Diagnostic& diagnostic);
  [[nodiscard]] static Diagnostic fromError(const core::Error& error);
};

[[nodiscard]] std::string_view toString(DiagnosticSeverity severity) noexcept;
[[nodiscard]] std::string_view toString(DiagnosticAction action) noexcept;

}
