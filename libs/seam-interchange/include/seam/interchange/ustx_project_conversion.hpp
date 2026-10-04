#pragma once

#include "seam/application/project_factory.hpp"
#include "seam/interchange/ustx_codec.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace seam::interchange {

struct UstxImportRequest final {
  std::string projectName;
  std::string voicebankId{"ustx.unresolved.voicebank"};
  std::string voicebankVersion{"0.0.0-ustx"};
  std::string voicebankContentHash;
  std::string characterId{"ustx.unresolved.character"};
  std::string characterVersion{"0.0.0-ustx"};
  domain::Language language{domain::Language::Japanese};
  // USTX carries no singer executable, so an imported track has no material until
  // the caller names some. Attaching the caller's procedural recipe keeps the
  // imported project renderable in SEAM; without it an import of a SEAM-authored
  // score silently produced a track that could not be played back at all.
  std::optional<domain::ProceduralRecipeReference> proceduralRecipe;
};

struct UstxProjectDraft final {
  domain::Project project;
  std::vector<UstxIssue> issues;
};

struct UstxExportResult final {
  std::vector<std::uint8_t> bytes;
  std::vector<UstxIssue> issues;
};

[[nodiscard]] core::Result<UstxProjectDraft> importUstxProject(
    std::span<const std::uint8_t> bytes, application::ProjectFactory& factory,
    UstxImportRequest request = {}, UstxLimits limits = {});

[[nodiscard]] core::Result<UstxExportResult> exportUstxProject(
    const domain::Project& project, UstxLimits limits = {});

}  // namespace seam::interchange
