#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/bank_reference_registry.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/voicebank/catalog.hpp"

#include <string_view>
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
  [[nodiscard]] core::Result<void> addSearchRoot(
      voicebank::VoicebankSearchRoot root);
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
      const domain::Project& project, domain::TrackId trackId,
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

private:
  [[nodiscard]] static core::Result<voicebank::VoicebankSearchRoot>
  normalizeRoot(voicebank::VoicebankSearchRoot root);

  voicebank::VoicebankCatalog catalog_;
  BankReferenceRegistry registry_;
  std::vector<voicebank::VoicebankSearchRoot> roots_;
  std::vector<voicebank::VoicebankCandidate> candidates_;
  voicebank::VoicebankResolveOptions resolveOptions_{
      .requireTrustedInstalled = true,
      .allowDevelopmentFixtures = true,
  };
};

}  // namespace seam::authoring
