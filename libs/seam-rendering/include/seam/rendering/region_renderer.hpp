#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/render_performance.hpp"
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
  std::string rendererIdentity{"unknown"};
  std::string fallbackDiagnostic;
};

struct RegionRenderPhraseFailure final {
  std::string phraseId;
  core::ErrorCode code{core::ErrorCode::Internal};
  std::string message;
  std::string context;
};

struct RegionRenderResult final {
  std::uint32_t sampleRate{48000U};
  std::vector<float> mono;
  std::vector<RegionRenderPhraseInfo> phrases;
  std::vector<synthesis::UnitPlanEntry> unitPlan;
  // The phone partition this region's own phrases were rendered from, in absolute project frames.
  // A presentation can follow the published mix with it without re-deriving any timing.
  std::vector<RenderedCueSpan> performanceCues;
  // One pronunciation digest per published phrase, in the same order as the phrase list. The caller
  // that knows the singer's own identity combines these into the region's performance identity.
  std::vector<std::string> phrasePronunciationDigests;
  // Effective style from the prepared phrases, including an implicit sole bank style.
  std::string resolvedStyle;
  std::vector<RegionRenderPhraseFailure> failures;
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
      std::stop_token stopToken = {},
      bool continueOnPhraseFailure = false) const;
};

}  // namespace seam::rendering
