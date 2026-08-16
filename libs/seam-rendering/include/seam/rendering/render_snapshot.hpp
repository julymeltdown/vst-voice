#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace seam::rendering {

enum class RenderQuality { Preview, Final };

// Immutable, phrase-scoped input for background rendering. The project copy
// contains only the selected track/phrase and the global musical time maps, so
// unrelated project edits do not poison this phrase's content-addressed cache.
struct RenderSnapshot final {
  std::uint64_t revision{0};
  RenderQuality quality{RenderQuality::Preview};
  std::string engineVersion;
  std::string contentHash;
  PhraseSegment segment;
  domain::TrackId trackId;
  std::shared_ptr<const domain::Project> project;
  std::shared_ptr<const voicebank::Manifest> voicebank;
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
      std::filesystem::path bankRoot = {},
      std::uint32_t sampleRate = 0,
      std::string style = {},
      std::string engineVersion = "0.3.0") const;
};

[[nodiscard]] std::string fnv1aHex(std::string_view value);

}  // namespace seam::rendering
