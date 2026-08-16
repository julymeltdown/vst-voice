#pragma once

#include "seam/voicebank/voicebank.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace seam::voicebank {

enum class IssueSeverity { Info, Warning, Error };

enum class IssueCode {
  ManifestInvalid,
  DuplicateAlias,
  MissingAudio,
  AudioUnreadable,
  ChannelMismatch,
  SampleRateMismatch,
  MarkerInvalid,
  Clipping,
  DcOffset,
  RootPitchMismatch,
  LoopDiscontinuity,
  MissingSustain,
};

struct ValidationIssue final {
  IssueSeverity severity{IssueSeverity::Error};
  IssueCode code{IssueCode::ManifestInvalid};
  std::string unitId;
  std::string message;
};

struct ValidationReport final {
  std::vector<ValidationIssue> issues;
  std::size_t unitsChecked{0};

  [[nodiscard]] std::size_t errorCount() const noexcept;
  [[nodiscard]] std::size_t warningCount() const noexcept;
  [[nodiscard]] bool ok() const noexcept { return errorCount() == 0; }
};

class BankValidator final {
public:
  [[nodiscard]] ValidationReport validate(
      const Manifest& manifest,
      const std::filesystem::path& bankRoot) const;
};

[[nodiscard]] std::string_view issueSeverityName(IssueSeverity severity) noexcept;
[[nodiscard]] std::string_view issueCodeName(IssueCode code) noexcept;

}  // namespace seam::voicebank
