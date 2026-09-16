#pragma once

#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/core/environment.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace seam::clap_editor::detail {

inline std::filesystem::path previewCacheRoot() {
  if (const auto configured = core::environmentVariable("SEAM_PREVIEW_CACHE_ROOT");
      configured && !configured->empty()) {
    return std::filesystem::path{*configured};
  }
#if defined(_WIN32)
  if (const auto local = core::environmentVariable("LOCALAPPDATA");
      local && !local->empty()) {
    return std::filesystem::path{*local} / "ProjectSEAM" / "Cache" /
           "PluginPreview";
  }
#elif defined(__APPLE__)
  if (const auto home = core::environmentVariable("HOME");
      home && !home->empty()) {
    return std::filesystem::path{*home} / "Library" / "Caches" /
           "ProjectSEAM" / "PluginPreview";
  }
#else
  if (const auto xdg = core::environmentVariable("XDG_CACHE_HOME");
      xdg && !xdg->empty()) {
    return std::filesystem::path{*xdg} / "project-seam" / "plugin-preview";
  }
  if (const auto home = core::environmentVariable("HOME");
      home && !home->empty()) {
    return std::filesystem::path{*home} / ".cache" / "project-seam" /
           "plugin-preview";
  }
#endif
  std::error_code error;
  auto root = std::filesystem::temp_directory_path(error);
  if (error) root = std::filesystem::current_path(error);
  return root / "project-seam" / "plugin-preview";
}

inline bool targetRuntimeFixtureEnabled() {
  const auto configured = core::environmentVariable("SEAM_TARGET_RUNTIME_FIXTURE_ROOT");
  return configured && !configured->empty();
}

inline PreviewStatus previewStatusFor(
    authoring::RenderFailureKind failure) noexcept {
  switch (failure) {
    case authoring::RenderFailureKind::None: return PreviewStatus::Ready;
    case authoring::RenderFailureKind::VoicebankMissing:
    case authoring::RenderFailureKind::InvalidProject:
      return PreviewStatus::VoicebankMissing;
    case authoring::RenderFailureKind::VoicebankVersionMismatch:
      return PreviewStatus::VoicebankVersionMismatch;
    case authoring::RenderFailureKind::VoicebankContentHashMissing:
      return PreviewStatus::VoicebankContentHashMissing;
    case authoring::RenderFailureKind::VoicebankContentMismatch:
      return PreviewStatus::VoicebankContentMismatch;
    case authoring::RenderFailureKind::VoicebankUntrusted:
      return PreviewStatus::VoicebankUntrusted;
    case authoring::RenderFailureKind::RenderFailed:
    case authoring::RenderFailureKind::PublicationBusy:
    case authoring::RenderFailureKind::NeuralSourceMissing:
      return PreviewStatus::Failed;
  }
  return PreviewStatus::Failed;
}

inline std::vector<voicebank::VoicebankSearchRoot> runtimeVoicebankRoots(
    std::vector<voicebank::VoicebankSearchRoot> roots) {
  auto defaults = voicebank::defaultVoicebankSearchRoots();
  roots.insert(roots.end(), defaults.begin(), defaults.end());
#ifdef SEAM_SOURCE_PRODUCTION_VOICEBANK
  const auto sourceFixture =
      std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  if (!sourceFixture.empty()) {
    roots.push_back(voicebank::VoicebankSearchRoot{
        .path = sourceFixture,
        .kind = voicebank::VoicebankRootKind::Development,
    });
  }
#endif
  if (const auto targetFixture =
          core::environmentVariable("SEAM_TARGET_RUNTIME_FIXTURE_ROOT");
      targetFixture && !targetFixture->empty()) {
    roots.push_back(voicebank::VoicebankSearchRoot{
        .path = std::filesystem::path{*targetFixture},
        .kind = voicebank::VoicebankRootKind::Development,
    });
  }
  std::vector<voicebank::VoicebankSearchRoot> unique;
  for (auto& root : roots) {
    if (root.path.empty()) continue;
    root.path = root.path.lexically_normal();
    const auto duplicate = std::find_if(
        unique.begin(), unique.end(), [&root](const auto& candidate) {
          return candidate.path == root.path && candidate.kind == root.kind;
        });
    if (duplicate == unique.end()) unique.push_back(std::move(root));
  }
  return unique;
}

inline std::string voicebankStatusLabel(
    const voicebank::VoicebankResolution& resolution) {
  if (resolution.resolved() && resolution.candidate.has_value()) {
    return "BANK " + resolution.candidate->manifest.displayName + " [" +
           std::string{voicebank::voicebankTrustName(
               resolution.candidate->trust)} +
           "]";
  }
  return "BANK " + resolution.diagnostic;
}

inline std::optional<domain::PhonemeKey> primaryPhonemeKey(
    const application::EditorSession& session,
    domain::RegionId regionId) {
  const auto* region = session.project().findRegion(regionId);
  if (region == nullptr || region->notes.empty()) return std::nullopt;
  const auto selected = session.selection().noteIds();
  const auto noteId = selected.empty() ? region->notes.front().id
                                       : selected.front();
  return domain::PhonemeKey{.noteId = noteId, .ordinal = 0U};
}

inline native_ui::EditorSceneLayout::TechnicalLaneGeometry
adaptiveTechnicalLaneGeometry(const native_ui::EditorSceneLayout& layout,
                              const domain::Project& project,
                              const domain::VocalRegion& region,
                              std::size_t phonemeCount,
                              double logicalHeight) noexcept {
  const auto technical = native_ui::resolveTechnicalLaneHeights(
      native_ui::TechnicalLaneLayoutInput{
          .presentation = project.settings().technicalLanes,
          .populated = {phonemeCount != 0U, !region.unitSelectionOverrides.empty(),
                        !region.seamOverrides.empty(),
                        !region.pitchAutomation.points().empty()},
          .previewHeights = {layout.phonemeLaneHeight, layout.unitLaneHeight,
                             layout.seamLaneHeight, layout.automationLaneHeight},
          .contentTop = layout.contentTop(),
          .contentBottom = logicalHeight - layout.statusHeight,
      });
  return native_ui::EditorSceneLayout::TechnicalLaneGeometry{
      .pianoBottom = technical.pianoBottom,
      .phonemeTop = technical.pianoBottom,
      .phonemeHeight = technical.values[0U],
      .unitTop = technical.pianoBottom + technical.values[0U],
      .unitHeight = technical.values[1U],
      .seamTop = technical.pianoBottom + technical.values[0U] + technical.values[1U],
      .seamHeight = technical.values[2U],
      .pitchTop = technical.pianoBottom + technical.values[0U] + technical.values[1U] + technical.values[2U],
      .pitchHeight = technical.values[3U],
      .bottom = logicalHeight - layout.statusHeight,
  };
}

}  // namespace seam::clap_editor::detail
