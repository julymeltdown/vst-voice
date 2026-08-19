#pragma once

#include "seam/clap_editor/editor_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace seam::clap_editor::detail {

inline std::filesystem::path previewCacheRoot() {
  if (const auto* configured = std::getenv("SEAM_PREVIEW_CACHE_ROOT");
      configured != nullptr && *configured != '\0') {
    return std::filesystem::path{configured};
  }
#if defined(_WIN32)
  if (const auto* local = std::getenv("LOCALAPPDATA");
      local != nullptr && *local != '\0') {
    return std::filesystem::path{local} / "ProjectSEAM" / "Cache" /
           "PluginPreview";
  }
#elif defined(__APPLE__)
  if (const auto* home = std::getenv("HOME");
      home != nullptr && *home != '\0') {
    return std::filesystem::path{home} / "Library" / "Caches" /
           "ProjectSEAM" / "PluginPreview";
  }
#else
  if (const auto* xdg = std::getenv("XDG_CACHE_HOME");
      xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path{xdg} / "project-seam" / "plugin-preview";
  }
  if (const auto* home = std::getenv("HOME");
      home != nullptr && *home != '\0') {
    return std::filesystem::path{home} / ".cache" / "project-seam" /
           "plugin-preview";
  }
#endif
  std::error_code error;
  auto root = std::filesystem::temp_directory_path(error);
  if (error) root = std::filesystem::current_path(error);
  return root / "project-seam" / "plugin-preview";
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

}  // namespace seam::clap_editor::detail
