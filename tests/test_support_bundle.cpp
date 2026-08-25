#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/support_bundle.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

seam::core::LogEvent safeEvent() {
  return seam::core::LogEvent{
      .code = "RENDER_TIMEOUT",
      .level = seam::core::LogLevel::Warning,
      .category = "render",
      .message = "/Users/sentinel/private-project.seam",
      .fields = {
          {"buildId", "external-beta.20260822.1", seam::core::LogPrivacyClass::ExportSafe},
          {"artifactSha256", std::string(64U, 'a'), seam::core::LogPrivacyClass::ExportSafe},
          {"path", "/Users/sentinel/private-project.seam", seam::core::LogPrivacyClass::LocalPrivate},
          {"secret", "do-not-export", seam::core::LogPrivacyClass::Forbidden},
      },
      .occurrenceCount = 2U,
  };
}

}

TEST_CASE("support bundle preview matches export and excludes private diagnostics") {
  const auto root = seam::test::support::temporaryDirectory("support-bundle");
  seam::authoring::SupportBundleService service(root / "PrivateReports");
  const seam::authoring::SupportBundleRequest request{
      .events = {safeEvent()},
      .attachments = {},
      .attachmentConsent = false,
      .createdAt = "2026-08-22T00:00:00Z"};
  auto preview = service.preview(request);
  CHECK(preview);
  const auto destination = root / "support.zip";
  auto exported = service.exportBundle(request, destination);
  CHECK(exported);
  CHECK(exported.value().preview.archiveSha256 == preview.value().archiveSha256);
  CHECK(exported.value().preview.archiveBytes == preview.value().archiveBytes);
  CHECK(std::filesystem::is_regular_file(destination));
  auto bytes = seam::core::readFileBytesLimited(destination, 8U * 1024U * 1024U);
  CHECK(bytes);
  const std::string payload(reinterpret_cast<const char*>(bytes.value().data()),
                            bytes.value().size());
  CHECK(payload.find("private-project.seam") == std::string::npos);
  CHECK(payload.find("do-not-export") == std::string::npos);
  CHECK(payload.find("RENDER_TIMEOUT") != std::string::npos);
}

TEST_CASE("support bundle requires per-file consent and owns private reports") {
  const auto root = seam::test::support::temporaryDirectory("support-bundle-consent");
  seam::authoring::SupportBundleService service(root / "PrivateReports");
  const auto attachment = root / "selected.txt";
  std::ofstream(attachment) << "deliberately selected";
  seam::authoring::SupportBundleRequest request{
      .events = {safeEvent()},
      .attachments = {attachment},
      .attachmentConsent = false,
      .createdAt = "2026-08-22T00:00:00Z"};
  CHECK(!service.preview(request));
  request.attachmentConsent = true;
  auto preview = service.preview(request);
  CHECK(preview);
  CHECK(preview.value().entryNames.size() == 3U);
  const auto report = service.writePrivateReport("report-1", "local-private");
  CHECK(report);
  CHECK(std::filesystem::exists(report.value()));
  CHECK(service.deletePrivateReport(report.value()));
  CHECK(!std::filesystem::exists(report.value()));
  const auto outside = root / "outside.json";
  std::ofstream(outside) << "keep";
  CHECK(!service.deletePrivateReport(outside));
  CHECK(std::filesystem::exists(outside));
}
