#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace seam::platform {

enum class FileDialogPurpose {
  OpenProject,
  OpenScore,
  SaveProject,
  ImportAudio,
  InstallVoicebank,
  RelinkVoicebank,
  RelinkMedia,
  ExportSet,
  ExportAudio,
  ExportScore,
  SelectProceduralRecipe,
  RelinkProceduralRecipe,
  // Names a creator-owned draft copied from an installed singer. The destination must be outside
  // every installation root, because signed content is immutable.
  CopyInstalledSingerToDraft,
  BakeProceduralCandidates,
  ImportProceduralCandidate,
  ExportPitchInspection,
  OpenGenerationJob,
  OpenGenerationBatch,
  PrepareGenerationJob,
  PrepareGenerationBatch,
  // Names a new campaign folder. The campaign definition must live in its own
  // directory, so the platform asks for that directory's name and parent and the
  // controller creates it; an existing directory is never reused.
  PlanGenerationCampaign,
  SaveDesignerRecipe,
  PublishSampleCandidate,
  OpenSampleManifest,
  CreateSampleManifestDraft,
  SourceQualityEvidence,
  SourceLicenseEvidence,
};

struct FileDialogRequest final {
  FileDialogPurpose purpose{FileDialogPurpose::OpenProject};
  std::string title;
  std::filesystem::path initialDirectory;
  std::string suggestedName;
  std::vector<std::string> extensions;
};

// Raw, explicitly entered modal fields. Language must be one of ja/en/ko;
// no default language or style is supplied by the platform or controller.
struct SampleManifestDraftIdentityInput final {
  std::string id, version, displayName, language, style;
};

enum class UnsavedSampleDecision { Cancel, Save, Discard };
struct SourceQualityDecisionInput final {
  std::string id, reviewerId, coverage, listening;
};
struct SourceRegistrationInput final {
  std::string id, kind, rights;
  // Explicit yes/no for source use, transformation, bank redistribution and
  // commercial renders, respectively. No platform may supply permissive defaults.
  std::array<std::string,4U> permissions;
};

class IFileDialog {
public:
  virtual ~IFileDialog() = default;
  struct ProductionWorkspaceInput final {
    std::filesystem::path root;
    std::string inventorySha256, operatorId;
    [[nodiscard]] core::Result<void> validate() const {
      if (root.empty() || inventorySha256.size()!=64U || operatorId.empty() || operatorId.size()>128U)
        return core::failure(core::ErrorCode::InvalidArgument,"Workspace path, inventory digest and bounded operator ID are required");
      for (const char c:inventorySha256) if (!((c>='0' && c<='9') || (c>='a' && c<='f')))
        return core::failure(core::ErrorCode::InvalidArgument,"Inventory SHA-256 must use lowercase hexadecimal");
      for (const char c:operatorId) if (static_cast<unsigned char>(c)<32U || c==127)
        return core::failure(core::ErrorCode::InvalidArgument,"Operator ID cannot contain control characters");
      return core::success();
    }
  };
  [[nodiscard]] virtual core::Result<std::optional<ProductionWorkspaceInput>> chooseProductionWorkspace() {
    return core::failure<std::optional<ProductionWorkspaceInput>>(core::ErrorCode::Unsupported,"Production workspace entry is unavailable on this platform");
  }
  [[nodiscard]] virtual core::Result<std::optional<SourceRegistrationInput>> chooseSourceRegistration(std::string_view) {
    return core::failure<std::optional<SourceRegistrationInput>>(core::ErrorCode::Unsupported,"Source registration entry is unavailable");
  }
  [[nodiscard]] virtual core::Result<std::optional<SourceQualityDecisionInput>> chooseSourceQualityDecision(
      std::string_view, const std::vector<std::string>&) {
    return core::failure<std::optional<SourceQualityDecisionInput>>(core::ErrorCode::Unsupported,"Source quality decision entry is unavailable");
  }
  [[nodiscard]] virtual core::Result<UnsavedSampleDecision> confirmUnsavedSampleChanges() {
    return UnsavedSampleDecision::Cancel;
  }
  [[nodiscard]] virtual core::Result<std::optional<SampleManifestDraftIdentityInput>> chooseSampleManifestDraftIdentity() {
    return core::failure<std::optional<SampleManifestDraftIdentityInput>>(core::ErrorCode::Unsupported,
        "Sample manifest draft identity entry is unavailable on this platform");
  }
  // Cancel is the safe default. The caller must revalidate its captured
  // producer/manifest/selection/reviewer context after this modal returns.
  [[nodiscard]] virtual core::Result<bool> confirmSampleReview(std::string_view, bool) {
    return core::failure<bool>(core::ErrorCode::Unsupported, "Sample review confirmation is unavailable on this platform");
  }
  [[nodiscard]] virtual core::Result<std::optional<std::string>> chooseSampleReviewer(const std::vector<std::string>&) {
    return core::failure<std::optional<std::string>>(core::ErrorCode::Unsupported, "Registered reviewer selection is unavailable on this platform");
  }
  [[nodiscard]] virtual core::Result<std::optional<std::string>> chooseDesignerSeed(std::string_view, bool frication = false) {
    static_cast<void>(frication);
    return core::failure<std::optional<std::string>>(core::ErrorCode::Unsupported, "Designer seed dialog is unavailable on this platform");
  }
  struct DesignerPoseIdentity final { std::string phone, style; };
  enum class DesignerPoseKind { Voiced, Frication, Plosive };
  [[nodiscard]] virtual core::Result<std::optional<DesignerPoseIdentity>> chooseDesignerPoseIdentity(DesignerPoseKind kind = DesignerPoseKind::Voiced) {
    static_cast<void>(kind);
    return core::failure<std::optional<DesignerPoseIdentity>>(core::ErrorCode::Unsupported, "Designer pose naming is unavailable on this platform");
  }
  [[nodiscard]] virtual core::Result<bool> confirmDiscardDesignerChanges() {
    return core::failure<bool>(core::ErrorCode::Unsupported, "Discard confirmation is unavailable; save the Designer draft first");
  }
  [[nodiscard]] virtual core::Result<std::vector<std::filesystem::path>> chooseGenerationJobs() {
    return core::failure<std::vector<std::filesystem::path>>(core::ErrorCode::Unsupported, "Generation job multi-selection is unavailable on this platform");
  }
  [[nodiscard]] virtual core::Result<std::optional<std::size_t>> chooseGenerationRegion(const std::vector<std::string>&) {
    return core::failure<std::optional<std::size_t>>(core::ErrorCode::Unsupported, "Generation region selection is unavailable on this platform");
  }
  [[nodiscard]] virtual core::Result<std::optional<bool>> chooseRecipePackaging() {
    return std::optional<bool>{false};
  }
  [[nodiscard]] virtual core::Result<std::optional<std::filesystem::path>> choose(
      const FileDialogRequest& request) = 0;
  [[nodiscard]] virtual core::Result<std::optional<std::string>> chooseRecipeStyle(
      const std::vector<std::string>&) {
    return core::failure<std::optional<std::string>>(core::ErrorCode::Unsupported,
        "Recipe style selection is unavailable on this platform");
  }
};

[[nodiscard]] std::unique_ptr<IFileDialog> createNativeFileDialog();

}  // namespace seam::platform
