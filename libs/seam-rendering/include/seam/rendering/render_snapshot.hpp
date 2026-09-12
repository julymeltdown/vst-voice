#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/synthesis/singer_resource.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace seam::rendering {

enum class RenderQuality { Preview, Final };

using SelectedUnitIdentity = synthesis::SelectedUnitIdentity;

// Immutable, phrase-scoped input for background rendering. The snapshot owns
// the exact phoneme and unit plans used to calculate its content identity, so
// cache lookup and rendering cannot diverge after construction.
struct RenderSnapshot final {
  std::uint64_t revision{0};
  RenderQuality quality{RenderQuality::Preview};
  std::string renderAbiId;
  std::string contentHash;
  PhraseSegment segment;
  domain::TrackId trackId;
  // Scheduling/lifecycle identity, distinct from the canonicalized render
  // project's ID. Not part of content identity: identical audio may be shared.
  domain::ProjectId sourceProjectId;
  std::shared_ptr<const domain::Project> project;
  std::shared_ptr<const phonemizer::Result> phonemes;
  synthesis::SingerResource resource;
  std::uint32_t sampleRate{48000};
  std::string style{"original"};
  std::optional<domain::PronunciationIdentity> pronunciationIdentity;
  std::shared_ptr<const synthesis::CompiledScorePerformance> compiledPerformance{};
  // Absolute half-open publication window. Musical/source context remains the
  // complete segment; absence publishes the legacy full rendered extent.
  std::optional<synthesis::PhraseFrameRange> ownedFrames{};
  [[nodiscard]] const synthesis::SampleSingerResource& sample() const { return std::get<synthesis::SampleSingerResource>(resource); }
  [[nodiscard]] synthesis::SampleSingerResource& sample() { return std::get<synthesis::SampleSingerResource>(resource); }
};

class RenderSnapshotFactory final {
public:
  [[nodiscard]] core::Result<RenderSnapshot> createProcedural(
      const domain::Project& project, const synthesis::ProceduralSingerResource& resource,
      domain::TrackId trackId, domain::RegionId regionId, std::uint64_t revision,
      RenderQuality quality, std::uint32_t sampleRate, std::string style = "neutral",
      std::optional<synthesis::PhraseFrameRange> ownedFrames = {}) const;
  // Reuses frozen source context; never reopens resources or changes music.
  [[nodiscard]] core::Result<std::vector<RenderSnapshot>> splitOwnedOutput(
      const RenderSnapshot& source, synthesis::PhraseFrameRange output,
      std::uint32_t maximumChunkFrames, std::size_t maximumChunks = 4096U) const;
  [[nodiscard]] core::Result<RenderSnapshot> create(
      const domain::Project& project,
      const voicebank::Manifest& voicebank,
      domain::TrackId trackId,
      const PhraseSegment& segment,
      std::uint64_t revision,
      RenderQuality quality,
      std::filesystem::path bankRoot,
      std::uint32_t sampleRate = 0,
      std::string style = {},
      const synthesis::PhraseRenderOptions& renderOptions = {},
      std::optional<synthesis::PhraseFrameRange> ownedFrames = {}) const;
};

// Kept for persisted Phase 3/4 diagnostics. New render identities use SHA-256.
[[nodiscard]] std::string fnv1aHex(std::string_view value);

}  // namespace seam::rendering
