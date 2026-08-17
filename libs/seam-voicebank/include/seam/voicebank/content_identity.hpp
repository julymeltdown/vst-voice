#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <filesystem>
#include <string>

namespace seam::voicebank {

// Computes the synthesis-relevant on-disk identity of an installed or bundled
// Voicebank. Presentation assets, character data and license text are
// deliberately excluded. The digest covers the canonical Manifest v3 JSON,
// each unique Unit audio path and the SHA-256 of the corresponding file.
[[nodiscard]] core::Result<std::string> computeVoicebankContentHash(
    const Manifest& manifest,
    const std::filesystem::path& bankRoot);

}  // namespace seam::voicebank
