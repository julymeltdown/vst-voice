#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/support_bundle.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

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
      .candidateId = "candidate-build-1",
      .createdAt = "2026-08-22T00:00:00Z"};
  auto prepared = service.prepare(request);
  CHECK(prepared);
  const auto& preview = prepared.value().preview();
  CHECK(preview.candidateId == "candidate-build-1");
  CHECK(!preview.containsRestrictedAttachments);
  const auto exportRoot = root / "Exports";
  auto first = service.exportPrepared(prepared.value(), exportRoot);
  auto second = service.exportPrepared(prepared.value(), exportRoot);
  CHECK(first);
  CHECK(second);
  CHECK(first.value().destination != second.value().destination);
  CHECK(first.value().preview.archiveSha256 == preview.archiveSha256);
  CHECK(first.value().preview.archiveBytes == preview.archiveBytes);
  CHECK(std::filesystem::is_regular_file(first.value().destination));
  CHECK(std::filesystem::is_regular_file(second.value().destination));
  auto bytes = seam::core::readFileBytesLimited(first.value().destination,
                                                8U * 1024U * 1024U);
  CHECK(bytes);
  const std::string payload(reinterpret_cast<const char*>(bytes.value().data()),
                            bytes.value().size());
  CHECK(payload.find("private-project.seam") == std::string::npos);
  CHECK(payload.find("do-not-export") == std::string::npos);
  CHECK(payload.find("RENDER_TIMEOUT") != std::string::npos);
  auto reports = service.listExports(exportRoot);
  CHECK(reports);
  CHECK(reports.value().size() == 2U);
  const auto firstRecord = std::find_if(
      reports.value().begin(), reports.value().end(), [&](const auto& record) {
        return record.path == first.value().destination;
      });
  CHECK(firstRecord != reports.value().end());
  CHECK(firstRecord->sha256 == preview.archiveSha256);
  CHECK(service.deleteExport(*firstRecord, exportRoot));
  auto remaining = service.listExports(exportRoot);
  CHECK(remaining);
  CHECK(remaining.value().size() == 1U);
  CHECK(!std::filesystem::exists(first.value().destination));
  CHECK(std::filesystem::exists(second.value().destination));
}

TEST_CASE("support bundle binds per-file consent and prepared attachment bytes") {
  const auto root = seam::test::support::temporaryDirectory("support-bundle-consent");
  seam::authoring::SupportBundleService service(root / "PrivateReports");
  const auto included = root / "included.txt";
  const auto excluded = root / "excluded.txt";
  std::ofstream(included) << "previewed attachment bytes";
  std::ofstream(excluded) << "unconsented attachment bytes";
  const seam::authoring::SupportBundleRequest request{
      .events = {safeEvent()},
      .attachments = {
          {.path = included, .consented = true},
          {.path = excluded, .consented = false},
      },
      .candidateId = "candidate-build-2",
      .createdAt = "2026-08-22T00:00:00Z"};
  auto prepared = service.prepare(request);
  CHECK(prepared);
  const auto& entries = prepared.value().preview().entries;
  const auto includedEntry = std::find_if(
      entries.begin(), entries.end(), [](const auto& entry) {
        return entry.path == "attachments/included.txt";
      });
  const auto excludedEntry = std::find_if(
      entries.begin(), entries.end(), [](const auto& entry) {
        return entry.path == "attachments/excluded.txt";
      });
  CHECK(includedEntry != entries.end());
  CHECK(excludedEntry != entries.end());
  CHECK(includedEntry->kind == seam::authoring::SupportBundleEntryKind::Attachment);
  CHECK(includedEntry->privacy ==
        seam::authoring::SupportBundlePrivacyClass::RestrictedSupportAttachment);
  CHECK(includedEntry->requiresConsent);
  CHECK(includedEntry->consented);
  CHECK(includedEntry->included);
  CHECK(!excludedEntry->consented);
  CHECK(!excludedEntry->included);
  CHECK(!excludedEntry->sha256.empty());
  std::ofstream(included, std::ios::trunc) << "changed after preview";
  auto exported = service.exportPrepared(prepared.value(), root / "Exports");
  CHECK(exported);
  auto bytes = seam::core::readFileBytesLimited(exported.value().destination,
                                                8U * 1024U * 1024U);
  CHECK(bytes);
  const std::string payload(reinterpret_cast<const char*>(bytes.value().data()),
                            bytes.value().size());
  CHECK(payload.find("previewed attachment bytes") != std::string::npos);
  CHECK(payload.find("changed after preview") == std::string::npos);
  CHECK(payload.find("unconsented attachment bytes") == std::string::npos);
  CHECK(payload.find("RestrictedSupportAttachment") != std::string::npos);
  CHECK(payload.find("\"privacyClass\":\"ExportSafe\"") == std::string::npos);

  auto reports = service.listExports(root / "Exports");
  CHECK(reports);
  CHECK(reports.value().size() == 1U);
  std::ofstream(exported.value().destination, std::ios::app) << "tampered";
  CHECK(!service.deleteExport(reports.value().front(), root / "Exports"));
  CHECK(std::filesystem::exists(exported.value().destination));

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

TEST_CASE("support bundle rejects unsafe attachments before preview") {
  const auto root =
      seam::test::support::temporaryDirectory("support-bundle-hostile-attachment");
  seam::authoring::SupportBundleService service(root / "PrivateReports");
  const auto requestFor = [&](const std::filesystem::path& attachment) {
    return seam::authoring::SupportBundleRequest{
        .events = {safeEvent()},
        .attachments = {{.path = attachment, .consented = true}},
        .candidateId = "candidate-build-hostile",
        .createdAt = "2026-08-22T00:00:00Z"};
  };

  const auto oversized = root / "oversized.txt";
  {
    std::ofstream stream{oversized, std::ios::binary};
    stream.seekp(1024U * 1024U);
    stream.put('x');
  }
  CHECK(!service.prepare(requestFor(oversized)));

  const auto missing = root / "missing.txt";
  CHECK(!service.prepare(requestFor(missing)));

#ifndef _WIN32
  const auto regular = root / "regular.txt";
  const auto linked = root / "linked.txt";
  std::ofstream(regular) << "bounded";
  std::error_code error;
  std::filesystem::create_symlink(regular, linked, error);
  CHECK(!error);
  CHECK(!service.prepare(requestFor(linked)));

  CHECK(::chmod(regular.c_str(), 0) == 0);
  const auto unreadable = service.prepare(requestFor(regular));
  CHECK(::chmod(regular.c_str(), S_IRUSR | S_IWUSR) == 0);
  CHECK(!unreadable);
#endif

  const auto firstDirectory = root / "one";
  const auto secondDirectory = root / "two";
  std::filesystem::create_directories(firstDirectory);
  std::filesystem::create_directories(secondDirectory);
  const auto first = firstDirectory / "same.txt";
  const auto second = secondDirectory / "same.txt";
  std::ofstream(first) << "first";
  std::ofstream(second) << "second";
  CHECK(!service.prepare(seam::authoring::SupportBundleRequest{
      .events = {safeEvent()},
      .attachments = {{.path = first, .consented = true},
                      {.path = second, .consented = true}},
      .candidateId = "candidate-build-duplicate",
      .createdAt = "2026-08-22T00:00:00Z"}));
}
