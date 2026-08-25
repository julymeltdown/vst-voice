#pragma once

#include "seam/voicebank/voicebank.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
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

struct DryTakeInspection final {
  std::string sourceSha256;
  std::uint32_t sampleRate{0U};
  std::uint16_t channels{0U};
  std::uint16_t bitsPerSample{0U};
  std::int32_t expectedRootMidi{0};
  std::optional<std::int32_t> analyzedRootMidi;
  float peak{0.0F};
  double rms{0.0};
  double dcOffset{0.0};
  bool formatValid{false};
  bool finite{false};
  bool clippingFree{false};
  bool silenceFree{false};
  bool dcOffsetFree{false};
  bool rootPitchValid{false};

  [[nodiscard]] bool accepted() const noexcept {
    return formatValid && finite && clippingFree && silenceFree &&
           dcOffsetFree && rootPitchValid;
  }
};

class BankValidator final {
public:
  [[nodiscard]] ValidationReport validate(
      const Manifest& manifest,
      const std::filesystem::path& bankRoot) const;
};

[[nodiscard]] core::Result<DryTakeInspection> inspectDryTake(
    const std::filesystem::path& path, std::int32_t expectedRootMidi);

[[nodiscard]] std::string_view issueSeverityName(IssueSeverity severity) noexcept;
[[nodiscard]] std::string_view issueCodeName(IssueCode code) noexcept;

}  // namespace seam::voicebank
