#pragma once

#include "seam/core/log_event.hpp"
#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seam::authoring {

enum class SupportBundleEntryKind { Generated, Attachment };

enum class SupportBundlePrivacyClass {
  PublicTechnical,
  RestrictedSupportAttachment,
};

[[nodiscard]] std::string_view toString(SupportBundleEntryKind value) noexcept;
[[nodiscard]] std::string_view toString(
    SupportBundlePrivacyClass value) noexcept;

struct SupportAttachmentRequest final {
  std::filesystem::path path;
  bool consented{false};
};

struct SupportBundleRequest final {
  std::vector<core::LogEvent> events;
  std::vector<SupportAttachmentRequest> attachments;
  std::string candidateId;
  std::string createdAt;
};

struct SupportBundleEntryPreview final {
  std::string path;
  SupportBundleEntryKind kind{SupportBundleEntryKind::Generated};
  SupportBundlePrivacyClass privacy{
      SupportBundlePrivacyClass::PublicTechnical};
  std::uint64_t bytes{0U};
  std::string sha256;
  bool requiresConsent{false};
  bool consented{false};
  bool included{false};
};

struct SupportBundlePreview final {
  std::uint64_t archiveBytes{0};
  std::string archiveSha256;
  std::string candidateId;
  std::string createdAt;
  std::vector<SupportBundleEntryPreview> entries;
  bool containsRestrictedAttachments{false};
};

class PreparedSupportBundle final {
public:
  [[nodiscard]] const SupportBundlePreview& preview() const noexcept {
    return preview_;
  }

private:
  friend class SupportBundleService;

  PreparedSupportBundle(std::vector<std::byte> archive,
                        SupportBundlePreview preview)
      : archive_(std::move(archive)), preview_(std::move(preview)) {}

  std::vector<std::byte> archive_;
  SupportBundlePreview preview_;
};

struct SupportBundleExport final {
  std::filesystem::path destination;
  SupportBundlePreview preview;
};

struct SupportBundleRecord final {
  std::filesystem::path path;
  std::uint64_t bytes{0U};
  std::string sha256;
};

class SupportBundleService final {
public:
  explicit SupportBundleService(std::filesystem::path privateRoot)
      : privateRoot_(std::move(privateRoot)) {}

  [[nodiscard]] core::Result<PreparedSupportBundle> prepare(
      const SupportBundleRequest& request) const;
  [[nodiscard]] core::Result<SupportBundleExport> exportPrepared(
      const PreparedSupportBundle& prepared,
      const std::filesystem::path& directory) const;
  [[nodiscard]] core::Result<std::vector<SupportBundleRecord>> listExports(
      const std::filesystem::path& directory) const;
  [[nodiscard]] core::Result<void> deleteExport(
      const SupportBundleRecord& record,
      const std::filesystem::path& directory) const;
  [[nodiscard]] core::Result<std::filesystem::path> writePrivateReport(
      std::string_view reportId, std::string_view payload) const;
  [[nodiscard]] core::Result<void> deletePrivateReport(
      const std::filesystem::path& path) const;

private:
  std::filesystem::path privateRoot_;
};

}
