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

// Where a bundle stands in the published support lifecycle. The states mirror
// docs/public/SUPPORT.md; the app records only what it can observe locally, and a state it cannot
// reach is never assumed. INTAKE is the first state a creator can produce, because submitting the
// bundle is the act that creates it; every later state belongs to the operated support path.
enum class SupportLifecycleState { NotSubmitted, Intake, Acknowledged, Triaged, Reproduced, Resolved, Withdrawn };

[[nodiscard]] std::string_view toString(SupportLifecycleState value) noexcept;

// A record of one submission attempt. The acknowledgement is the intake endpoint's signed receipt
// and is empty until one arrives, because a creator cannot acknowledge their own bundle.
struct SupportIntakeRecord final {
  std::string submissionId;
  std::string bundleSha256;
  std::string destinationId;
  std::string submittedAt;
  SupportLifecycleState state{SupportLifecycleState::NotSubmitted};
  std::string acknowledgementId;
  std::string acknowledgedBundleSha256;
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
  // Record that a bundle was submitted to a named destination. This is the act that creates the
  // lifecycle record; the acknowledgement arrives later and is a separate call, so a creator can
  // never mark their own bundle acknowledged.
  [[nodiscard]] core::Result<SupportIntakeRecord> recordIntake(
      const SupportBundleRecord& bundle, std::string_view destinationId,
      std::string_view submittedAt) const;
  // Apply the intake endpoint's signed receipt. The acknowledgement must name the exact bundle
  // that was submitted, so a receipt for another candidate cannot advance this one.
  [[nodiscard]] core::Result<SupportIntakeRecord> recordAcknowledgement(
      std::string_view submissionId, std::string_view acknowledgementId,
      std::string_view acknowledgedBundleSha256) const;
  [[nodiscard]] core::Result<std::optional<SupportIntakeRecord>> findIntake(
      std::string_view submissionId) const;
  [[nodiscard]] core::Result<std::vector<SupportIntakeRecord>> listIntakes() const;
  [[nodiscard]] core::Result<std::filesystem::path> writePrivateReport(
      std::string_view reportId, std::string_view payload) const;
  [[nodiscard]] core::Result<void> deletePrivateReport(
      const std::filesystem::path& path) const;

private:
  std::filesystem::path privateRoot_;
};

}
