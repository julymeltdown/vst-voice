#pragma once
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/synthesis/singer_resource.hpp"
#include "seam/core/file_io.hpp"

namespace seam::voice_design {
// Canonical draft recipe bytes, not an approved bank or executable backend.
[[nodiscard]] core::Result<synthesis::ProceduralSingerResource> freezeVoiceRecipeResource(
    const VoiceRecipe& recipe, std::stop_token stopToken = {});
[[nodiscard]] core::Result<VoiceRecipe> decodeVoiceRecipeResource(
    const synthesis::ProceduralSingerResource& resource, std::stop_token stopToken = {},
    bool allowVoicedFrication = true, bool allowVoicedStops = true);
// Explicit user-selected local recipe files. Loading never writes or upgrades
// the file; identity is over canonical recipe bytes, not JSON whitespace.
[[nodiscard]] core::Result<synthesis::ProceduralSingerResource> loadVoiceRecipeResource(
    const std::filesystem::path& path,
    std::optional<domain::SingerResourceIdentity> expected = {}, std::stop_token stopToken = {});
// Validate before touching disk. Once atomic commit starts, cancellation does
// not turn a successful save into an ambiguous cancellation result.
[[nodiscard]] core::Result<void> saveVoiceRecipeFile(
    const std::filesystem::path& path, const VoiceRecipe& recipe,
    const core::AtomicWriteOptions& options = {}, std::stop_token stopToken = {});
}
