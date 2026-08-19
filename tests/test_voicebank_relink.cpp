#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/voicebank_session.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>

namespace {
struct Fixture {
  seam::authoring::ProjectDocument document;
  seam::domain::TrackId trackId;
};
Fixture projectFixture() {
  seam::application::ProjectFactory factory{500U};
  auto project = factory.createProject("Relink");
  const auto track = factory.addVocalTrack(project, "VOICE");
  static_cast<void>(factory.addRegion(project, track, "REGION",
                                      seam::time::Tick{0}, seam::time::Tick{3840}));
  return {seam::authoring::ProjectDocument{std::move(project),
                                           seam::application::ProjectFactory{1000U}},
          track};
}
std::filesystem::path bankRoot(const std::filesystem::path& parent,
                               double frequency) {
  const auto root = parent / std::to_string(static_cast<int>(frequency));
  std::filesystem::create_directories(root / "audio");
  const auto samples = seam::test::support::sineWave(48000U, frequency, 0.1);
  CHECK(seam::voicebank::writePcm16Wav(root / "audio/a.wav", 48000U, 1U,
                                       samples));
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
                                    seam::voicebank::UnitKind::Sustain,
                                    samples.size())});
  manifest.id = "relink.bank";
  manifest.version = "1.0.0";
  seam::voicebank::ManifestJsonCodec codec;
  CHECK(codec.save(manifest, root / "manifest.json"));
  return root;
}
seam::domain::VoicebankReference reference(
    const seam::voicebank::VoicebankCandidate& candidate) {
  return {.id = candidate.manifest.id,
          .version = candidate.manifest.version,
          .contentHash = candidate.contentHash};
}
}  // namespace

TEST_CASE("voicebank_exact_selection_and_replacement_are_undoable") {
  const auto root = seam::test::support::temporaryDirectory("u3-select");
  const auto bankA = bankRoot(root, 220.0);
  const auto bankB = bankRoot(root, 330.0);
  seam::authoring::VoicebankSession session({
      {.path = bankA, .kind = seam::voicebank::VoicebankRootKind::Development},
      {.path = bankB, .kind = seam::voicebank::VoicebankRootKind::Development}}, true);
  CHECK(session.refresh());
  CHECK(session.candidates().size() == 2U);
  auto fixture = projectFixture();
  const auto first = session.candidates()[0];
  const auto second = session.candidates()[1];
  CHECK(session.selectTrackExact(fixture.document, fixture.trackId,
                                 first.manifest.id, first.manifest.version,
                                 first.contentHash));
  CHECK(fixture.document.session().project().findVocalTrack(fixture.trackId)
            ->voicebank == reference(first));
  CHECK(session.replaceTrackVoicebank(fixture.document, fixture.trackId, second));
  CHECK(fixture.document.session().project().findVocalTrack(fixture.trackId)
            ->voicebank == reference(second));
  CHECK(fixture.document.undo());
  CHECK(fixture.document.session().project().findVocalTrack(fixture.trackId)
            ->voicebank == reference(first));
}

TEST_CASE("voicebank_relink_preserves_requested_identity_and_requires_exact_match") {
  const auto root = seam::test::support::temporaryDirectory("u3-relink");
  const auto exactRoot = bankRoot(root, 220.0);
  seam::authoring::VoicebankSession source({
      {.path = exactRoot, .kind = seam::voicebank::VoicebankRootKind::Development}}, true);
  CHECK(source.refresh());
  auto fixture = projectFixture();
  fixture.document.session().project().findVocalTrack(fixture.trackId)->voicebank =
      reference(source.candidates().front());
  const auto before = fixture.document.session().project()
                          .findVocalTrack(fixture.trackId)->voicebank;

  seam::authoring::VoicebankSession relink({}, true);
  CHECK(relink.refresh());
  auto missing = relink.resolveTrack(fixture.document.session().project(),
                                     fixture.trackId);
  CHECK(missing.status == seam::voicebank::VoicebankResolveStatus::Missing);
  auto resolved = relink.relinkTrack(
      fixture.document.session().project(), fixture.trackId,
      {.path = exactRoot, .kind = seam::voicebank::VoicebankRootKind::Development});
  CHECK(resolved);
  CHECK(resolved.value().resolved());
  CHECK(fixture.document.session().project().findVocalTrack(fixture.trackId)
            ->voicebank == before);
}

TEST_CASE("voicebank_content_mismatch_reports_expected_and_actual_hashes") {
  const auto root = seam::test::support::temporaryDirectory("u3-hash-mismatch");
  const auto expectedRoot = bankRoot(root, 220.0);
  const auto actualRoot = bankRoot(root, 330.0);
  seam::authoring::VoicebankSession expected({
      {.path = expectedRoot, .kind = seam::voicebank::VoicebankRootKind::Development}}, true);
  CHECK(expected.refresh());
  auto fixture = projectFixture();
  fixture.document.session().project().findVocalTrack(fixture.trackId)->voicebank =
      reference(expected.candidates().front());

  seam::authoring::VoicebankSession actual({
      {.path = actualRoot, .kind = seam::voicebank::VoicebankRootKind::Development}}, true);
  CHECK(actual.refresh());
  const auto result = actual.resolveTrack(fixture.document.session().project(),
                                          fixture.trackId);
  CHECK(result.status == seam::voicebank::VoicebankResolveStatus::ContentMismatch);
  CHECK(result.expectedContentHash ==
        fixture.document.session().project().findVocalTrack(fixture.trackId)
            ->voicebank.contentHash);
  CHECK(result.actualContentHashes.size() == 1U);
  CHECK(result.actualContentHashes.front() == actual.candidates().front().contentHash);
}
