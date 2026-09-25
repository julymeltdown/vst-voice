#pragma once

#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/result.hpp"
#include "seam/interchange/smf_project_conversion.hpp"
#include "seam/interchange/ustx_project_conversion.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seam::authoring {

enum class InterchangeFormat { Ustx, Smf };

struct InterchangeIssue final {
  InterchangeFormat format{InterchangeFormat::Ustx};
  bool loss{false};
  std::string path;
  std::string message;

  friend bool operator==(const InterchangeIssue&, const InterchangeIssue&) = default;
};

struct InterchangeImportRequest final {
  InterchangeFormat format{InterchangeFormat::Ustx};
  std::string projectName;
  domain::Language language{domain::Language::Japanese};
  std::string voicebankId{"interchange.unresolved.voicebank"};
  std::string voicebankVersion{"0.0.0-interchange"};
  std::string voicebankContentHash;
  std::string characterId{"interchange.unresolved.character"};
  std::string characterVersion{"0.0.0-interchange"};
};

struct InterchangeImportDraft final {
  domain::Project project;
  InterchangeFormat format{InterchangeFormat::Ustx};
  std::filesystem::path sourcePath;
  std::string sourceHash;
  std::vector<InterchangeIssue> issues;
};

struct InterchangeExportRequest final {
  InterchangeFormat format{InterchangeFormat::Ustx};
  std::filesystem::path destination;
  // For SMF, omit both to export the complete project; provide both to export
  // one region. Supplying only one is invalid. USTX conversion is project-wide.
  std::optional<domain::TrackId> trackId;
  std::optional<domain::RegionId> regionId;
};

struct InterchangeExportReceipt final {
  InterchangeFormat format{InterchangeFormat::Ustx};
  std::filesystem::path destination;
  std::string contentHash;
  std::vector<InterchangeIssue> issues;
};

// A complete, bounded conversion held in memory until the creator reviews its
// diagnostic report. Preparing a draft never creates or replaces a file.
struct InterchangeExportDraft final {
  InterchangeFormat format{InterchangeFormat::Ustx};
  std::filesystem::path destination;
  std::string contentHash;
  std::vector<InterchangeIssue> issues;
  std::vector<std::uint8_t> bytes;
};

// Stateless file boundary for native import/export.  Import returns an
// unsaved draft; callers decide whether to replace the current document after
// showing the bounded loss report.  Export is create-new and never mutates the
// canonical project.
class InterchangeService final {
public:
  [[nodiscard]] core::Result<InterchangeImportDraft> importFile(
      const std::filesystem::path& source,
      application::ProjectFactory& factory,
      InterchangeImportRequest request = {},
      interchange::UstxLimits ustxLimits = {},
      interchange::SmfLimits smfLimits = {},
      const core::HeldReadFaultInjector& readFaultInjector = {}) const;

  [[nodiscard]] core::Result<InterchangeExportReceipt> exportFile(
      const domain::Project& project,
      InterchangeExportRequest request,
      interchange::UstxLimits ustxLimits = {},
      interchange::SmfLimits smfLimits = {}) const;

  [[nodiscard]] core::Result<InterchangeExportDraft> prepareExport(
      const domain::Project& project,
      InterchangeExportRequest request,
      interchange::UstxLimits ustxLimits = {},
      interchange::SmfLimits smfLimits = {}) const;

  [[nodiscard]] core::Result<InterchangeExportReceipt> writeExport(
      const InterchangeExportDraft& draft) const;
};

}  // namespace seam::authoring
