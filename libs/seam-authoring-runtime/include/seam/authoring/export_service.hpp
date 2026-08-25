#pragma once

#include "seam/rendering/project_renderer.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
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
  RollbackRequired,
};

enum class ExportPublicationPhase {
  JournalPrepared,
  PreviousMoved,
  DestinationPublished,
  ReceiptCommitted,
  BackupRemoved,
};

struct ExportSettings final {
  std::uint32_t sampleRate{48000U};
  std::uint8_t channels{2U};
  voicebank::WavSampleFormat format{voicebank::WavSampleFormat::Pcm24};
  bool includeMaster{true};
  bool includeStems{false};
  bool replaceExisting{false};
  std::function<bool(ExportPublicationPhase)> publicationFaultInjector;
};

struct ExportFileReceipt final {
  std::filesystem::path path;
  std::string sha256;
  std::uint64_t frames{0U};
  std::uint8_t channels{0U};
};

struct ExportProgress final {
  ExportState state{ExportState::Preflight};
  std::string currentOutput;
  std::uint64_t completedFiles{0U};
  std::uint64_t totalFiles{0U};
};

struct ExportResult final {
  ExportState state{ExportState::Failed};
  std::filesystem::path masterPath;
  std::filesystem::path receiptPath;
  std::string masterSha256;
  std::uint64_t projectRevision{0U};
  std::string diagnostic;
  std::vector<ExportFileReceipt> files;
  std::filesystem::path setPath;
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

  [[nodiscard]] core::Result<ExportResult> exportSet(
      const domain::Project& project,
      std::span<const rendering::TrackVoicebankSource> voicebanks,
      domain::TrackId activeTrack, domain::RegionId activeRegion,
      std::uint64_t revision, const std::filesystem::path& destination,
      ExportSettings settings = {},
      std::function<void(const ExportProgress&)> progress = {},
      std::stop_token stopToken = {}) const;

  [[nodiscard]] core::Result<ExportResult> recoverSet(
      const std::filesystem::path& destination) const;
};

[[nodiscard]] std::string_view exportStateName(ExportState state) noexcept;

}
