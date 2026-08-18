#include "seam/authoring/voicebank_session.hpp"

#include "seam/application/render_commands.hpp"

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
    std::vector<voicebank::VoicebankSearchRoot> roots) {
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
  auto scanned = catalog_.scan(roots_);
  if (!scanned) return core::Result<void>{scanned.error()};
  candidates_ = std::move(scanned).value();
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

core::Result<void> VoicebankSession::bindTrack(
    ProjectDocument& document, domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) {
  return document.execute(
      std::make_unique<application::SetTrackVoicebankCommand>(
          trackId, referenceFor(candidate)));
}

core::Result<void> VoicebankSession::bindTrack(
    application::EditorSession& session, domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) {
  return session.execute(
      std::make_unique<application::SetTrackVoicebankCommand>(
          trackId, referenceFor(candidate)));
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

}  // namespace seam::authoring
