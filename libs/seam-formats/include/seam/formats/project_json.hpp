#pragma once

#include "seam/domain/project.hpp"
#include "seam/formats/json_value.hpp"

#include <filesystem>
#include <string>

namespace seam::formats {

class ProjectJsonCodec final {
public:
  static constexpr std::int32_t kSchemaVersion = 5;

  [[nodiscard]] core::Result<std::string> encode(const domain::Project& project) const;
  [[nodiscard]] core::Result<domain::Project> decode(std::string_view json) const;
  [[nodiscard]] core::Result<void> save(const domain::Project& project,
                                        const std::filesystem::path& path) const;
  [[nodiscard]] core::Result<domain::Project> load(const std::filesystem::path& path) const;
};

}  // namespace seam::formats
