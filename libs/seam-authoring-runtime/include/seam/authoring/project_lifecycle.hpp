#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/voicebank_session.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/result.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seam::authoring {

struct NewProjectRequest final {
  std::string name;
  double tempoBpm{120.0};
  std::uint8_t meterNumerator{4U};
  std::uint8_t meterDenominator{4U};
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
  bool createInitialVocalTrack{true};
  std::optional<voicebank::VoicebankCandidate> initialVoicebank;
  std::optional<std::filesystem::path> projectPath;
};

struct ProjectSaveOptions final {
  core::AtomicWriteFaultInjector faultInjector;
};

struct OpenProjectResult final {
  std::vector<TrackVoicebankState> voicebanks;
};

class ProjectLifecycleService final {
public:
  explicit ProjectLifecycleService(VoicebankSession* voicebanks = nullptr)
      : voicebanks_(voicebanks) {}

  [[nodiscard]] core::Result<void> createNew(
      ProjectDocument& document, const NewProjectRequest& request) const;
  [[nodiscard]] core::Result<OpenProjectResult> open(
      ProjectDocument& document, const std::filesystem::path& path) const;
  [[nodiscard]] core::Result<void> save(
      ProjectDocument& document,
      const ProjectSaveOptions& options = {}) const;
  [[nodiscard]] core::Result<void> saveAs(
      ProjectDocument& document, const std::filesystem::path& path,
      const ProjectSaveOptions& options = {}) const;

private:
  [[nodiscard]] core::Result<std::string> write(
      const domain::Project& project, const std::filesystem::path& path,
      const ProjectSaveOptions& options) const;

  VoicebankSession* voicebanks_{nullptr};
  formats::ProjectJsonCodec codec_;
};

}  // namespace seam::authoring
