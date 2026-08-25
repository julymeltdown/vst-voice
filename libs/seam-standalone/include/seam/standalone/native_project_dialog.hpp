#pragma once

#include "seam/authoring/project_lifecycle.hpp"
#include "seam/core/result.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace seam::standalone {

struct NativeNewProjectDialogConfig final {
  std::vector<voicebank::VoicebankCandidate> candidates;
  std::filesystem::path initialDirectory;
  std::string suggestedName{"Untitled.seam"};
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
};

class INativeNewProjectDialog {
public:
  virtual ~INativeNewProjectDialog() = default;
  [[nodiscard]] virtual core::Result<
      std::optional<authoring::NewProjectRequest>> choose(
      NativeNewProjectDialogConfig config) = 0;
};

[[nodiscard]] std::unique_ptr<INativeNewProjectDialog>
createNativeNewProjectDialog();

}
