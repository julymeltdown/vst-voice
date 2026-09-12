#include "seam/authoring/voicebank_session.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/voicebank/style_resolution.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

namespace seam::authoring {
namespace {

domain::VoicebankReference referenceFor(
    const voicebank::VoicebankCandidate& candidate) {
  return domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
}

voicebank::VoicebankResolution invalidTrackResolution(
    domain::TrackId trackId) {
  voicebank::VoicebankResolution result;
  result.status = voicebank::VoicebankResolveStatus::InvalidReference;
  result.diagnostic = trackId.valid()
                          ? "Project does not contain the requested vocal track"
                          : "Vocal track ID is invalid";
  return result;
}

}  // namespace

VoicebankSession::VoicebankSession(
    std::vector<voicebank::VoicebankSearchRoot> roots,
    bool allowDevelopmentFixtures) {
  resolveOptions_.allowDevelopmentFixtures = allowDevelopmentFixtures;
  roots_.reserve(roots.size());
  for (auto& root : roots) {
    auto normalized = normalizeRoot(std::move(root));
    if (!normalized) continue;
    const auto duplicate = std::find_if(
        roots_.begin(), roots_.end(), [&normalized](const auto& existing) {
          return existing.path == normalized.value().path &&
                 existing.kind == normalized.value().kind;
        });
    if (duplicate == roots_.end()) {
      roots_.push_back(std::move(normalized).value());
    }
  }
}

core::Result<voicebank::VoicebankSearchRoot> VoicebankSession::normalizeRoot(
    voicebank::VoicebankSearchRoot root) {
  if (root.path.empty()) {
    return core::failure<voicebank::VoicebankSearchRoot>(
        core::ErrorCode::InvalidArgument,
        "Voicebank search root cannot be empty");
  }

  std::error_code error;
  auto absolute = std::filesystem::absolute(root.path, error);
  if (error) {
    absolute = root.path.lexically_normal();
    error.clear();
  }
  auto canonical = std::filesystem::weakly_canonical(absolute, error);
  root.path = (error ? absolute : canonical).lexically_normal();
  return root;
}

core::Result<void> VoicebankSession::refresh() {
  snapshot_.reset(); snapshotCatalogValid_ = false;
  auto scanned = catalog_.scan(roots_);
  if (!scanned) return core::Result<void>{scanned.error()};
  auto candidates = std::move(scanned).value();
  registry_.clear();
  for (const auto& candidate : candidates) {
    auto registered = registry_.registerCandidate(candidate);
    static_cast<void>(registered);
  }
  candidates_ = std::move(candidates);
  snapshotCatalogValid_ = true;
  return core::success();
}

core::Result<void> VoicebankSession::addSearchRoot(
    voicebank::VoicebankSearchRoot root) {
  auto normalized = normalizeRoot(std::move(root));
  if (!normalized) return core::Result<void>{normalized.error()};

  const auto duplicate = std::find_if(
      roots_.begin(), roots_.end(), [&normalized](const auto& existing) {
        return existing.path == normalized.value().path &&
               existing.kind == normalized.value().kind;
      });
  if (duplicate == roots_.end()) {
    roots_.push_back(std::move(normalized).value());
  }
  return refresh();
}

core::Result<bool> VoicebankSession::migrateLegacyStyles(domain::Project& project) const {
  std::vector<std::pair<std::size_t, domain::VoiceStyleSelection>> migrations;
  for (std::size_t index = 0U; index < project.vocalTracks().size(); ++index) {
    const auto& track = project.vocalTracks()[index];
    if (track.styleSelection.origin != domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution) {
      continue;
    }
    const auto bank = catalog_.resolve(track.voicebank, candidates_, resolveOptions_);
    auto style = voicebank::resolveVoiceStyle(track.voicebank, track.styleSelection, bank);
    if (!style) return core::Result<bool>{style.error()};
    if (style.value().status == voicebank::VoiceStyleStatus::Resolved) {
      migrations.emplace_back(index, std::move(style.value().selection));
    }
  }
  for (auto& [index, selection] : migrations) {
    project.vocalTracks()[index].styleSelection = std::move(selection);
  }
  return !migrations.empty();
}

core::Result<void> VoicebankSession::bindTrack(
    ProjectDocument& document, domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) {
  const auto style = replacementStyle(document.session().project(), trackId, candidate);
  if (!style) return core::Result<void>{style.error()};
  return document.execute(
      std::make_unique<application::SetTrackVoicebankCommand>(
          trackId, referenceFor(candidate), style.value()));
}

core::Result<domain::VoiceStyleSelection> VoicebankSession::replacementStyle(
    const domain::Project& project, domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) const {
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr) {
    return core::failure<domain::VoiceStyleSelection>(core::ErrorCode::NotFound,
        "Track for voicebank replacement was not found", trackId.toString());
  }
  const auto reference = referenceFor(candidate);
  const bool preserve = track->voicebank == reference ||
      (track->voicebank.id == reference.id && !track->styleSelection.styleId.empty());
  const auto selection = preserve ? track->styleSelection : domain::VoiceStyleSelection{};
  const auto bank = catalog_.resolve(reference, candidates_, resolveOptions_);
  const auto resolved = voicebank::resolveVoiceStyle(reference, selection, bank);
  if (!resolved) return core::Result<domain::VoiceStyleSelection>{resolved.error()};
  return resolved.value().selection;
}

core::Result<void> VoicebankSession::selectTrackExact(
    ProjectDocument& document, domain::TrackId trackId,
    std::string_view id, std::string_view version,
    std::string_view contentHash) {
  const domain::VoicebankReference reference{
      .id = std::string{id},
      .version = std::string{version},
      .contentHash = std::string{contentHash},
  };
  const auto resolution = catalog_.resolve(reference, candidates_, resolveOptions_);
  if (!resolution.resolved()) {
    return core::failure(resolution.status ==
                                 voicebank::VoicebankResolveStatus::Untrusted
                             ? core::ErrorCode::Conflict
                             : core::ErrorCode::NotFound,
                         resolution.diagnostic,
                         std::string{voicebank::voicebankResolveStatusName(
                             resolution.status)});
  }
  return bindTrack(document, trackId, *resolution.candidate);
}

core::Result<void> VoicebankSession::replaceTrackVoicebank(
    ProjectDocument& document, domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) {
  return bindTrack(document, trackId, candidate);
}

core::Result<voicebank::VoicebankResolution> VoicebankSession::relinkTrack(
    ProjectDocument& document, domain::TrackId trackId,
    voicebank::VoicebankSearchRoot root) {
  auto added = addSearchRoot(std::move(root));
  if (!added) {
    return core::Result<voicebank::VoicebankResolution>{added.error()};
  }
  const auto& project = document.session().project();
  auto resolution = resolveTrack(project, trackId);
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr || track->styleSelection.origin !=
                              domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution) {
    return resolution;
  }
  const auto style = voicebank::resolveVoiceStyle(track->voicebank,
                                                  track->styleSelection, resolution);
  if (!style) return core::Result<voicebank::VoicebankResolution>{style.error()};
  if (style.value().status == voicebank::VoiceStyleStatus::Resolved) {
    const auto migrated = document.execute(
        std::make_unique<application::EditPerformanceCommand>(
            std::vector<application::NoteExpressionEdit>{},
            std::vector<application::RegionDynamicsEdit>{},
            std::vector<application::TrackStyleEdit>{{trackId, style.value().selection}}));
    if (!migrated) return core::Result<voicebank::VoicebankResolution>{migrated.error()};
  }
  return resolution;
}

core::Result<void> VoicebankSession::bindTrack(
    application::EditorSession& session, domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) {
  const auto style = replacementStyle(session.project(), trackId, candidate);
  if (!style) return core::Result<void>{style.error()};
  return session.execute(
      std::make_unique<application::SetTrackVoicebankCommand>(
          trackId, referenceFor(candidate), style.value()));
}

std::vector<voicebank::VoicebankCandidate> VoicebankSession::candidates() const {
  return candidates_;
}

std::vector<TrackVoicebankState> VoicebankSession::resolveAll(
    const domain::Project& project) const {
  std::vector<TrackVoicebankState> result;
  result.reserve(project.vocalTracks().size());
  for (const auto& track : project.vocalTracks()) {
    result.push_back(TrackVoicebankState{
        .trackId = track.id,
        .resolution = catalog_.resolve(track.voicebank, candidates_,
                                       resolveOptions_),
    });
  }
  return result;
}

voicebank::VoicebankResolution VoicebankSession::resolveTrack(
    const domain::Project& project, domain::TrackId trackId) const {
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr) return invalidTrackResolution(trackId);
  return catalog_.resolve(track->voicebank, candidates_, resolveOptions_);
}

VoicebankSnapshotPtr VoicebankSession::resolveTrackSnapshot(
    const domain::Project& project, domain::TrackId trackId) const {
  const auto* track = project.findVocalTrack(trackId);
  const auto reference = track ? std::optional{track->voicebank} : std::nullopt;
  if (snapshot_ && snapshotTrack_ == trackId && snapshotReference_ == reference) return snapshot_;
  auto resolution = snapshotCatalogValid_ ? resolveTrack(project, trackId) : voicebank::VoicebankResolution{};
  if (!snapshotCatalogValid_) resolution.diagnostic = "Refresh the voicebank catalog successfully before editing styles";
  auto snapshot = VoicebankSnapshotPtr{new VoicebankSnapshot{std::move(resolution)}};
  snapshotTrack_ = trackId; snapshotReference_ = reference; snapshot_ = std::move(snapshot);
  return snapshot_;
}

}  // namespace seam::authoring
