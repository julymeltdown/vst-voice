#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::rendering {

struct RegionRenderPhraseInfo final {
  std::string phraseId;
  std::string contentHash;
  std::size_t unitCount{0U};
  std::size_t fallbackCount{0U};
  bool cacheHit{false};
};

struct RegionRenderResult final {
  std::uint32_t sampleRate{48000U};
  std::vector<float> mono;
  std::vector<RegionRenderPhraseInfo> phrases;
  std::vector<synthesis::UnitPlanEntry> unitPlan;
  std::size_t unitCount{0U};
  std::size_t fallbackCount{0U};
  std::size_t cacheHits{0U};
};

class ProductionRegionRenderer final {
public:
  [[nodiscard]] core::Result<RegionRenderResult> render(
      const domain::Project& project,
      const voicebank::Manifest& manifest,
      const std::filesystem::path& bankRoot,
      domain::TrackId trackId,
      domain::RegionId regionId,
      std::uint64_t revision,
      std::uint32_t sampleRate,
      RenderQuality quality = RenderQuality::Preview,
      std::string style = {},
      const synthesis::PhraseRenderOptions& options = {},
      PcmCache* cache = nullptr,
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::rendering
