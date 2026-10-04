#pragma once

#include "seam/core/error.hpp"
#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
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
  RelinkVoicebank,
  InstallVoicebank,
  RelinkMedia,
  SaveAs,
  OpenRecoveryFolder,
  CopyDiagnostic,
  RecoverAutosave,
  OpenSupport,
  ExportSupportBundle,
  OpenSupportFolder,
  DeleteSupportBundle,
  SubmitSupportBundle,
};

struct Diagnostic final {
  std::string code;
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  std::string messageKey;
  std::vector<std::string> affectedIds;
  std::vector<DiagnosticAction> actions;
  std::size_t occurrenceCount{1U};
  std::string detail;
  bool detailTruncated{false};
  bool detailEscaped{false};
  std::string detailSourceHash;
  static constexpr std::size_t maximumDetailBytes = 4096U;
  void setDetail(std::string_view text);
  [[nodiscard]] bool sameIssueAs(const Diagnostic& other) const noexcept;
  // What makes this the one issue, as a number: a fingerprint of exactly the fields sameIssueAs
  // compares, and of nothing else (the count is left out, so a repeat of the issue keeps it). Diagnostics
  // that are the same issue have equal identities. The converse is a probability and not a proof: equal
  // identities are the same issue unless two different issues collide (about one in 2^64 for issues that
  // were not built to), so it does not replace sameIssueAs and is no security boundary. An id that
  // carries it names the issue wherever it stands in a list, so a list that changed (an eviction, a
  // dismissal, a rebuild) does not let an id that was made for one issue act on another that has taken
  // its place.
  [[nodiscard]] std::uint64_t issueIdentity() const noexcept;
  void addOccurrences(std::size_t additional) noexcept;
  // Every field, how many times it came among them. sameIssueAs asks whether two are the one issue
  // and leaves the count out; this asks whether one says what the other already says, which is what
  // a host that sets the same list every frame needs to know.
  [[nodiscard]] bool operator==(const Diagnostic&) const = default;
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
