#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace seam::voicebank {

// Expected encoded bytes for consumed bank-relative assets. The caller must
// establish the map's provenance; it does not itself confer signature trust.
using ExpectedAssetDigests = std::map<std::string, std::string, std::less<>>;

[[nodiscard]] core::Result<std::filesystem::path> resolveBankAsset(
    const std::filesystem::path& bankRoot,
    const std::filesystem::path& relativePath);

}  // namespace seam::voicebank
