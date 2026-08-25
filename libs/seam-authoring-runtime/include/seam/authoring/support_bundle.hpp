#pragma once

#include "seam/core/log_event.hpp"
#include "seam/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seam::authoring {

struct SupportBundleRequest final {
  std::vector<core::LogEvent> events;
  std::vector<std::filesystem::path> attachments;
  bool attachmentConsent{false};
  std::string createdAt;
};

struct SupportBundlePreview final {
  std::uint64_t archiveBytes{0};
  std::string archiveSha256;
  std::vector<std::string> entryNames;
  bool attachmentConsent{false};
};

struct SupportBundleExport final {
  std::filesystem::path destination;
  SupportBundlePreview preview;
};

class SupportBundleService final {
public:
  explicit SupportBundleService(std::filesystem::path privateRoot)
      : privateRoot_(std::move(privateRoot)) {}

  [[nodiscard]] core::Result<SupportBundlePreview> preview(
      const SupportBundleRequest& request) const;
  [[nodiscard]] core::Result<SupportBundleExport> exportBundle(
      const SupportBundleRequest& request,
      const std::filesystem::path& destination) const;
  [[nodiscard]] core::Result<std::filesystem::path> writePrivateReport(
      std::string_view reportId, std::string_view payload) const;
  [[nodiscard]] core::Result<void> deletePrivateReport(
      const std::filesystem::path& path) const;

private:
  std::filesystem::path privateRoot_;
};

}
