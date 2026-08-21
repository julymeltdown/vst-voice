#pragma once

#include "seam/rendering/project_renderer.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::authoring {

enum class ExportState {
  Preflight,
  Staging,
  Prepared,
  Publishing,
  Committed,
  Cancelled,
  Failed,
  Recovered,
};

struct ExportResult final {
  ExportState state{ExportState::Failed};
  std::filesystem::path masterPath;
  std::filesystem::path receiptPath;
  std::string masterSha256;
  std::uint64_t projectRevision{0U};
  std::string diagnostic;
};

class ExportService final {
public:
  [[nodiscard]] core::Result<ExportResult> exportProject(
      const domain::Project& project,
      std::span<const rendering::TrackVoicebankSource> voicebanks,
      domain::TrackId activeTrack, domain::RegionId activeRegion,
      std::uint64_t revision, const std::filesystem::path& destination,
      voicebank::WavSampleFormat format = voicebank::WavSampleFormat::Pcm24,
      std::stop_token stopToken = {}) const;

  [[nodiscard]] core::Result<ExportResult> commitRendered(
      const rendering::ProjectRenderResult& rendered,
      std::uint64_t revision, const std::filesystem::path& destination,
      voicebank::WavSampleFormat format = voicebank::WavSampleFormat::Pcm24,
      std::stop_token stopToken = {}) const;
};

[[nodiscard]] std::string_view exportStateName(ExportState state) noexcept;

}
