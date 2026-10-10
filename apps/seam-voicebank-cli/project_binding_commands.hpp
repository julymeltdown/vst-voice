#pragma once
#include <optional>
#include <filesystem>
#include <string_view>
#include <utility>
#include "seam/formats/json_value.hpp"
#include "seam/domain/project.hpp"
namespace seam::voicebank_cli {
struct ProjectBindingRequest final {
  std::filesystem::path projectPath;
  std::string_view projectSha256, trackId, regionId, family;
  std::string_view resourceId, resourceVersion, resourceContentHash, languages;
  std::optional<domain::ProceduralInstallationReference> verifiedInstallation{};
};
// Owns the decoded project and record from one held read. Later operations must
// use this project rather than reopening the path after verifying its binding.
// Reference verification alone does not admit resources or prove execution.
class CapturedProjectBinding final {
public:
  [[nodiscard]] const domain::Project& project() const noexcept { return project_; }
  [[nodiscard]] const formats::JsonValue& record() const noexcept { return record_; }
  [[nodiscard]] const std::filesystem::path& projectDirectory() const noexcept { return directory_; }
  [[nodiscard]] domain::TrackId trackId() const noexcept { return trackId_; }
  [[nodiscard]] domain::RegionId regionId() const noexcept { return regionId_; }
private:
  CapturedProjectBinding(domain::Project project, formats::JsonValue record,
      std::filesystem::path directory, domain::TrackId trackId, domain::RegionId regionId)
      : project_(std::move(project)), record_(std::move(record)), directory_(std::move(directory)),
        trackId_(trackId), regionId_(regionId) {}
  domain::Project project_;
  formats::JsonValue record_;
  std::filesystem::path directory_;
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  friend core::Result<CapturedProjectBinding> captureVerifiedProjectBinding(const ProjectBindingRequest&);
};
[[nodiscard]] core::Result<CapturedProjectBinding> captureVerifiedProjectBinding(const ProjectBindingRequest& request);
// Captures project bytes once; never opens a resource path carried by the project.
[[nodiscard]] core::Result<formats::JsonValue> verifyProjectBindingRecord(const ProjectBindingRequest& request);

[[nodiscard]] std::optional<int> runProjectBindingCommand(int argc, char** argv);
void printProjectBindingUsage();
}
