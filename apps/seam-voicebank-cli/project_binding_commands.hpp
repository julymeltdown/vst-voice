#pragma once
#include <optional>
#include <filesystem>
#include <string_view>
#include "seam/formats/json_value.hpp"
namespace seam::voicebank_cli {
struct ProjectBindingRequest final {
  std::filesystem::path projectPath;
  std::string_view projectSha256, trackId, regionId, family;
  std::string_view resourceId, resourceVersion, resourceContentHash, languages;
};
// Captures project bytes once; never opens a resource path carried by the project.
[[nodiscard]] core::Result<formats::JsonValue> verifyProjectBindingRecord(const ProjectBindingRequest& request);

[[nodiscard]] std::optional<int> runProjectBindingCommand(int argc, char** argv);
void printProjectBindingUsage();
}
