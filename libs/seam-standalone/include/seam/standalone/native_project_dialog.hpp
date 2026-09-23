#pragma once

#include "seam/authoring/project_lifecycle.hpp"
#include "seam/authoring/interchange_service.hpp"
#include "seam/core/result.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

class INativeInterchangeReviewDialog {
public:
  virtual ~INativeInterchangeReviewDialog() = default;
  // True explicitly accepts the displayed draft. False leaves the current
  // document untouched. This review neither installs nor replaces singers.
  [[nodiscard]] virtual core::Result<bool> review(
      const authoring::InterchangeImportDraft& draft) = 0;
};

// macOS has a native review surface. Other platforms return Unsupported;
// absence of a review UI is never implicit approval of any import.
[[nodiscard]] std::unique_ptr<INativeInterchangeReviewDialog>
createNativeInterchangeReviewDialog();

#if defined(__APPLE__)
// Embedded keyboard commands have no synchronous result surface. Present their failures on the
// AppKit main thread, including when a host delivers the command from another thread.
void presentNativeInterchangeFailure(std::string_view title, std::string_view detail);
#endif

}
