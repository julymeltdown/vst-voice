#pragma once
#include "seam/voice_design/procedural_renderer.hpp"
#include "seam/voicebank/wav.hpp"

namespace seam::voice_design {
// Verified bytes/structure only. This type never carries approval or inferred QC.
struct ProceduralCandidate final {
  synthesis::ProceduralSingerResource recipe;
  std::string audioSha256, renderContentHash, renderAbi, style;
  std::string metadataJson;
  std::uint32_t proceduralRevision{0U}, compilerRevision{0U};
  time::SampleFrame scoreOriginFrame{0};
  std::uint32_t sampleRate{0U};
  time::SampleFrame frameCount{0};
  std::vector<ProceduralPhoneMarker> markers; // Candidate-relative, never clipped.
  std::shared_ptr<const voicebank::AudioBuffer> audio;
  std::uint32_t schemaVersion{1U}, articulationPlanRevision{0U}, fricationRevision{0U}, fricationStreamRevision{0U};
  std::uint32_t plosiveRevision{0U};
  std::uint32_t voicedPlosiveRevision{0U};
  std::uint32_t affricateRevision{0U};
  std::uint32_t approximantRevision{0U};
  std::uint32_t palatalizedRevision{0U};
};
// Parses planned gestures only; audio remains null. This is not audio verification.
[[nodiscard]] core::Result<ProceduralCandidate> parseProceduralCandidateMetadata(
    std::string_view metadataJson, const synthesis::ProceduralSingerResource& expectedRecipe,
    std::stop_token stopToken = {});
[[nodiscard]] core::Result<ProceduralCandidate> loadProceduralCandidate(
    const std::filesystem::path& metadataPath, const std::filesystem::path& audioPath,
    const synthesis::ProceduralSingerResource& expectedRecipe, std::stop_token stopToken = {});
// Stored lineage variant: validates metadata and the exact owned audio bytes.
[[nodiscard]] core::Result<ProceduralCandidate> loadProceduralCandidateFromMetadata(
    std::string_view metadataJson, const std::filesystem::path& audioPath,
    const synthesis::ProceduralSingerResource& expectedRecipe, std::stop_token stopToken = {});
}
