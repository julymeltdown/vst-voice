#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/bank_reference_registry.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/authoring/voicebank_snapshot.hpp"

#include <string_view>
#include <optional>
#include <vector>

namespace seam::authoring {

struct TrackVoicebankState final {
  domain::TrackId trackId;
  voicebank::VoicebankResolution resolution;
};

class VoicebankSession final {
public:
  explicit VoicebankSession(
      std::vector<voicebank::VoicebankSearchRoot> roots,
      bool allowDevelopmentFixtures = true);

  [[nodiscard]] core::Result<void> refresh();
  [[nodiscard]] core::Result<bool> migrateLegacyStyles(domain::Project& project) const;
  [[nodiscard]] core::Result<void> addSearchRoot(
      voicebank::VoicebankSearchRoot root);
  void setAllowDevelopmentFixtures(bool allow) noexcept {
    if (resolveOptions_.allowDevelopmentFixtures != allow) snapshot_.reset();
    resolveOptions_.allowDevelopmentFixtures = allow;
  }
  [[nodiscard]] core::Result<void> bindTrack(
      ProjectDocument& document, domain::TrackId trackId,
      const voicebank::VoicebankCandidate& candidate);
  [[nodiscard]] core::Result<void> selectTrackExact(
      ProjectDocument& document, domain::TrackId trackId,
      std::string_view id, std::string_view version,
      std::string_view contentHash);
  [[nodiscard]] core::Result<void> replaceTrackVoicebank(
      ProjectDocument& document, domain::TrackId trackId,
      const voicebank::VoicebankCandidate& candidate);
  [[nodiscard]] core::Result<voicebank::VoicebankResolution> relinkTrack(
      ProjectDocument& document, domain::TrackId trackId,
      voicebank::VoicebankSearchRoot root);

  // Transitional facade for adapters that have not yet adopted ProjectDocument.
  // Remove after EditorRuntime is converted to AuthoringRuntime in U1.7.
  [[nodiscard]] core::Result<void> bindTrack(
      application::EditorSession& session, domain::TrackId trackId,
      const voicebank::VoicebankCandidate& candidate);

  [[nodiscard]] std::vector<voicebank::VoicebankCandidate> candidates() const;
  [[nodiscard]] std::vector<TrackVoicebankState> resolveAll(
      const domain::Project& project) const;
  [[nodiscard]] voicebank::VoicebankResolution resolveTrack(
      const domain::Project& project, domain::TrackId trackId) const;
  // Owner-thread only, like refresh/resolveTrack. One-entry cache bounds retained
  // manifests; callers can retain immutable old snapshots, never treat them as current.
  [[nodiscard]] VoicebankSnapshotPtr resolveTrackSnapshot(
      const domain::Project& project, domain::TrackId trackId) const;

private:
  [[nodiscard]] core::Result<domain::VoiceStyleSelection> replacementStyle(
      const domain::Project& project, domain::TrackId trackId,
      const voicebank::VoicebankCandidate& candidate) const;
  [[nodiscard]] static core::Result<voicebank::VoicebankSearchRoot>
  normalizeRoot(voicebank::VoicebankSearchRoot root);

  voicebank::VoicebankCatalog catalog_;
  mutable VoicebankSnapshotPtr snapshot_;
  mutable domain::TrackId snapshotTrack_;
  mutable std::optional<domain::VoicebankReference> snapshotReference_;
  bool snapshotCatalogValid_{true};
  BankReferenceRegistry registry_;
  std::vector<voicebank::VoicebankSearchRoot> roots_;
  std::vector<voicebank::VoicebankCandidate> candidates_;
  voicebank::VoicebankResolveOptions resolveOptions_{
      .requireTrustedInstalled = true,
      .allowDevelopmentFixtures = true,
  };
};

}  // namespace seam::authoring
