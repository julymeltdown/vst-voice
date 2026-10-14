#pragma once
#include "project_binding_commands.hpp"
#include "seam/candidate_packaging/candidate_package.hpp"
#include <stop_token>

namespace seam::voicebank_cli {
// Observes the selected procedural snapshot API, not host playback or release acceptance.
[[nodiscard]] core::Result<formats::JsonValue> observeInstalledProceduralContinuity(
    const CapturedProjectBinding& captured,
    const candidate_packaging::VerifiedInstalledCandidate& installed,
    const formats::JsonValue& installedObservation,
    const std::filesystem::path& installationDirectory,
    const std::filesystem::path& destination, std::uint32_t maximumChunkFrames,
    std::stop_token stop = {});
}
