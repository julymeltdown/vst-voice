#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/multichannel_routing.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/shared_pcm_buffer.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
#include <variant>

namespace seam::rendering {

struct TrackVoicebankSource final {
  domain::TrackId trackId;
  voicebank::Manifest manifest;
  std::filesystem::path bankRoot;
  std::string contentHash;
  voicebank::VoicebankTrust trust{voicebank::VoicebankTrust::UntrustedInstalled};
};

struct TrackProceduralSource final {
  domain::TrackId trackId;
  synthesis::ProceduralSingerResource resource;
  std::string style{"neutral"};
};
struct TrackRecipeFileSource final {
  domain::TrackId trackId;
  domain::ProceduralRecipeReference reference;
  std::optional<std::filesystem::path> projectDirectory;
};
// A prepared neural bundle chosen by the application, with the runner that can
// execute it. The source never carries a helper path or command: the runner
// already holds the resolved first-party selection and its process budgets.
struct TrackNeuralSource final {
  domain::TrackId trackId;
  std::shared_ptr<const neural_synthesis::AdmittedNeuralBundle> bundle;
  NeuralRenderProvenance provenance;
  std::shared_ptr<const NeuralPhraseRunner> runner;
};
using TrackSingerSource = std::variant<TrackVoicebankSource, TrackProceduralSource, TrackRecipeFileSource,
                                       TrackNeuralSource>;

struct ProjectRenderDiagnostic final {
  domain::TrackId trackId;
  domain::RegionId regionId;
  std::string phraseId;
  core::ErrorCode code{core::ErrorCode::Internal};
  std::string message;
  std::string context;
};

struct ProjectRenderResult final {
  std::uint32_t sampleRate{48000U};
  std::uint8_t channelCount{2U};
  SharedPcmBuffer interleaved;
  std::vector<std::string> phraseContentHashes;
  std::vector<synthesis::UnitPlanEntry> activeUnitPlan;
  // The active track and region's phone partition, in absolute project frames, taken from the same
  // prepared snapshots the audible mix was rendered from. Empty when the request named no active
  // region or when its render published no phone timeline.
  std::vector<RenderedCueSpan> performanceCues;
  std::vector<ProjectRenderDiagnostic> diagnostics;
  std::size_t trackCount{0U};
  std::size_t regionCount{0U};
  std::size_t phraseCount{0U};
  std::size_t unitCount{0U};
  std::size_t fallbackCount{0U};
  std::size_t cacheHits{0U};
};

class ProductionProjectRenderer final {
public:
  [[nodiscard]] core::Result<ProjectRenderResult> renderWithSources(
      const domain::Project& project, std::span<const TrackSingerSource> sources,
      domain::TrackId activeTrack, domain::RegionId activeRegion,
      std::uint64_t revision, std::uint32_t sampleRate,
      RenderQuality quality = RenderQuality::Preview,
      const synthesis::PhraseRenderOptions& sampleOptions = {},
      PcmCache* cache = nullptr, std::stop_token stopToken = {}) const;
  [[nodiscard]] core::Result<ProjectRenderResult> render(
      const domain::Project& project,
      std::span<const TrackVoicebankSource> voicebanks,
      domain::TrackId activeTrack,
      domain::RegionId activeRegion,
      std::uint64_t revision,
      std::uint32_t sampleRate,
      RenderQuality quality = RenderQuality::Preview,
      const synthesis::PhraseRenderOptions& options = {},
      PcmCache* cache = nullptr,
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::rendering
