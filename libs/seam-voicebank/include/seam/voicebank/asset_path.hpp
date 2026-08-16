#pragma once

#include "seam/core/result.hpp"

#include <filesystem>

namespace seam::voicebank {

[[nodiscard]] core::Result<std::filesystem::path> resolveBankAsset(
    const std::filesystem::path& bankRoot,
    const std::filesystem::path& relativePath);

}  // namespace seam::voicebank
