#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace seam::rendering {

enum class RenderQuality { Preview, Final };

struct SelectedUnitIdentity final {
  std::string unitId;
  std::string audioSha256;

  friend bool operator==(const SelectedUnitIdentity&,
                         const SelectedUnitIdentity&) = default;
};

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
  std::shared_ptr<const domain::Project> project;
  std::shared_ptr<const voicebank::Manifest> voicebank;
  std::shared_ptr<const phonemizer::Result> phonemes;
  std::shared_ptr<const synthesis::UnitPlan> unitPlan;
  std::vector<SelectedUnitIdentity> selectedUnits;
  std::vector<synthesis::FrozenUnitAudio> frozenAudio;
  synthesis::PhraseRenderOptions renderOptions;
  std::filesystem::path bankRoot;
  std::uint32_t sampleRate{48000};
  std::string style{"original"};
};

class RenderSnapshotFactory final {
public:
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
      const synthesis::PhraseRenderOptions& renderOptions = {}) const;
};

// Kept for persisted Phase 3/4 diagnostics. New render identities use SHA-256.
[[nodiscard]] std::string fnv1aHex(std::string_view value);

}  // namespace seam::rendering
