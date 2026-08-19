#pragma once

#include "seam/core/result.hpp"

#include <filesystem>

namespace seam::platform {

[[nodiscard]] core::Result<std::filesystem::path> applicationSupportDirectory();

}  // namespace seam::platform
