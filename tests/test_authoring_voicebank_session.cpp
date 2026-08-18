#include "test_framework.hpp"

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/voicebank_session.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for VoicebankSession tests
#endif

namespace {

struct DocumentFixture final {
  seam::authoring::ProjectDocument document;
  seam::domain::TrackId trackId;
};

DocumentFixture makeDocument() {
  seam::application::ProjectFactory factory{100U};
  auto project = factory.createProject("Voicebank Session Test");
  const auto trackId = factory.addVocalTrack(project, "VOICE");
  static_cast<void>(factory.addRegion(project, trackId, "REGION",
                                      seam::time::Tick{0},
                                      seam::time::Tick{3840}));
  return DocumentFixture{
      seam::authoring::ProjectDocument{std::move(project), seam::application::ProjectFactory{1000U}},
      trackId,
  };
}

seam::voicebank::VoicebankSearchRoot developmentRoot() {
  return seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  };
}

seam::domain::VoicebankReference exactReference(
    const seam::voicebank::VoicebankCandidate& candidate) {
  return seam::domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
}

}  // namespace

TEST_CASE("authoring_voicebank_session_resolves_exact_identity") {
  seam::authoring::VoicebankSession session({developmentRoot()});
  CHECK(session.refresh());
  const auto candidates = session.candidates();
  CHECK(candidates.size() == 1U);

  auto fixture = makeDocument();
  fixture.document.session().project().findVocalTrack(fixture.trackId)->voicebank =
      exactReference(candidates.front());

  const auto resolution =
      session.resolveTrack(fixture.document.session().project(), fixture.trackId);
  CHECK(resolution.resolved());
  CHECK(resolution.candidate.has_value());
  CHECK(resolution.candidate->contentHash == candidates.front().contentHash);
}

TEST_CASE("authoring_voicebank_session_reports_resolution_failures_without_fallback") {
  seam::authoring::VoicebankSession session({developmentRoot()});
  CHECK(session.refresh());
  const auto candidate = session.candidates().front();
  auto fixture = makeDocument();
  auto* track = fixture.document.session().project().findVocalTrack(fixture.trackId);
  CHECK(track != nullptr);

  track->voicebank = exactReference(candidate);
  track->voicebank.id = "missing.voicebank";
  auto resolution = session.resolveTrack(fixture.document.session().project(),
                                         fixture.trackId);
  CHECK(resolution.status == seam::voicebank::VoicebankResolveStatus::Missing);
  CHECK(!resolution.candidate.has_value());

  track->voicebank = exactReference(candidate);
  track->voicebank.version = "999.0.0";
  resolution = session.resolveTrack(fixture.document.session().project(),
                                    fixture.trackId);
  CHECK(resolution.status ==
        seam::voicebank::VoicebankResolveStatus::VersionMismatch);
  CHECK(!resolution.candidate.has_value());

  track->voicebank = exactReference(candidate);
  track->voicebank.contentHash.assign(64U, '0');
  resolution = session.resolveTrack(fixture.document.session().project(),
                                    fixture.trackId);
  CHECK(resolution.status ==
        seam::voicebank::VoicebankResolveStatus::ContentMismatch);
  CHECK(!resolution.candidate.has_value());

  seam::authoring::VoicebankSession untrusted({
      seam::voicebank::VoicebankSearchRoot{
          .path = developmentRoot().path,
          .kind = seam::voicebank::VoicebankRootKind::Installed,
      },
  });
  CHECK(untrusted.refresh());
  track->voicebank = exactReference(untrusted.candidates().front());
  resolution = untrusted.resolveTrack(fixture.document.session().project(),
                                      fixture.trackId);
  CHECK(resolution.status == seam::voicebank::VoicebankResolveStatus::Untrusted);
  CHECK(!resolution.candidate.has_value());
}

TEST_CASE("authoring_voicebank_session_binding_is_undoable") {
  seam::authoring::VoicebankSession session({developmentRoot()});
  CHECK(session.refresh());
  auto fixture = makeDocument();
  const auto before = fixture.document.session().project()
                          .findVocalTrack(fixture.trackId)
                          ->voicebank;

  CHECK(session.bindTrack(fixture.document, fixture.trackId,
                          session.candidates().front()));
  const auto after = fixture.document.session().project()
                         .findVocalTrack(fixture.trackId)
                         ->voicebank;
  CHECK(after == exactReference(session.candidates().front()));
  CHECK(fixture.document.dirty());

  CHECK(fixture.document.undo());
  CHECK(fixture.document.session().project()
            .findVocalTrack(fixture.trackId)
            ->voicebank == before);
}

TEST_CASE("authoring_voicebank_session_canonical_root_addition_is_idempotent") {
  seam::authoring::VoicebankSession session({developmentRoot()});
  CHECK(session.refresh());
  CHECK(session.candidates().size() == 1U);

  auto duplicate = developmentRoot();
  duplicate.path /= ".";
  CHECK(session.addSearchRoot(std::move(duplicate)));
  CHECK(session.candidates().size() == 1U);
}

TEST_CASE("authoring_voicebank_session_resolve_all_preserves_track_order") {
  seam::authoring::VoicebankSession session({developmentRoot()});
  CHECK(session.refresh());
  auto fixture = makeDocument();
  auto& project = fixture.document.session().project();
  const auto second = fixture.document.factory().addVocalTrack(project, "SECOND");
  project.findVocalTrack(fixture.trackId)->voicebank =
      exactReference(session.candidates().front());

  const auto states = session.resolveAll(project);
  CHECK(states.size() == 2U);
  CHECK(states[0].trackId == fixture.trackId);
  CHECK(states[0].resolution.resolved());
  CHECK(states[1].trackId == second);
  CHECK(states[1].resolution.status ==
        seam::voicebank::VoicebankResolveStatus::InvalidReference);
}
