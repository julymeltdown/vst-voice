#include "test_framework.hpp"

#include "seam/application/arrangement_commands.hpp"
#include "seam/application/command.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include <memory>

TEST_CASE("command impact classifies audio scope instead of defaulting to project") {
  seam::application::RenameVocalTrackCommand rename{
      seam::domain::TrackId{1U}, "Renamed"};
  seam::application::SetVocalTrackMixCommand mix{
      seam::domain::TrackId{1U}, -1.0F, 0.25F, false, false};
  seam::application::MoveNotesCommand move{{seam::application::NoteMove{
      .noteId = seam::domain::NoteId{2U},
      .before = seam::time::Tick{0},
      .after = seam::time::Tick{120},
  }}};
  seam::application::SetTrackVoicebankCommand bank{
      seam::domain::TrackId{1U},
      seam::domain::VoicebankReference{
          .id = "bank", .version = "1.0.0", .contentHash = std::string(64U, 'a')}};
  CHECK(rename.audioImpact() ==
        seam::application::CommandAudioImpact::MetadataOnly);
  CHECK(mix.audioImpact() == seam::application::CommandAudioImpact::TrackMix);
  CHECK(move.audioImpact() ==
        seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(bank.audioImpact() ==
        seam::application::CommandAudioImpact::ProjectAudio);
}

TEST_CASE("command impact carries affected identity through editor history") {
  seam::application::MoveNotesCommand move{{seam::application::NoteMove{
      .noteId = seam::domain::NoteId{2U},
      .before = seam::time::Tick{0},
      .after = seam::time::Tick{120},
  }}};
  const auto impact = move.impact();
  CHECK(impact.scope == seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(impact.noteIds.size() == 1U);
  CHECK(impact.noteIds.front() == seam::domain::NoteId{2U});
}

TEST_CASE("editor session retains the latest impact across undo") {
  seam::application::ProjectFactory factory{1000U};
  auto project = factory.createProject("Impact history");
  const auto trackId = factory.addVocalTrack(project, "Voice");
  seam::application::EditorSession session{std::move(project)};

  CHECK(session.execute(std::make_unique<
                        seam::application::RenameVocalTrackCommand>(
      trackId, "Renamed")));
  CHECK(session.lastImpact().scope ==
        seam::application::CommandAudioImpact::MetadataOnly);
  CHECK(session.lastImpact().trackIds.size() == 1U);
  CHECK(session.lastImpact().trackIds.front() == trackId);
  CHECK(session.undo());
  CHECK(session.lastImpact().scope ==
        seam::application::CommandAudioImpact::MetadataOnly);
}

TEST_CASE("composite command merges impact scopes and identity sets") {
  auto composite = std::make_unique<seam::application::CompositeCommand>(
      "Mixed edit");
  composite->add(std::make_unique<seam::application::RenameVocalTrackCommand>(
      seam::domain::TrackId{3U}, "Voice"));
  composite->add(std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{{
          .noteId = seam::domain::NoteId{4U},
          .before = seam::time::Tick{0},
          .after = seam::time::Tick{120},
      }}));
  const auto impact = composite->impact();
  CHECK(impact.scope == seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(impact.trackIds.size() == 1U);
  CHECK(impact.noteIds.size() == 1U);
}

TEST_CASE("audio track arrangement commands are undoable and mix-scoped") {
  seam::application::ProjectFactory factory{1000U};
  auto project = factory.createProject("Audio commands");
  seam::application::EditorSession session{std::move(project)};
  const seam::domain::AudioTrack track{
      .id = seam::domain::TrackId{700U},
      .name = "Backing",
      .mediaPath = "/tmp/backing.wav",
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .startTick = seam::time::Tick{0},
      .gainDb = -3.0F,
      .pan = 0.25F,
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(0.25F),
      },
  };
  seam::application::AddAudioTrackCommand add{track};
  CHECK(add.audioImpact() ==
        seam::application::CommandAudioImpact::ProjectAudio);
  CHECK(add.impact().trackIds.front() == track.id);
  CHECK(session.execute(std::make_unique<seam::application::AddAudioTrackCommand>(
      track)));
  CHECK(session.project().audioTracks().size() == 1U);
  CHECK(session.undo());
  CHECK(session.project().audioTracks().empty());
  CHECK(session.redo());
  CHECK(session.project().audioTracks().front().name == "Backing");
  CHECK(session.execute(std::make_unique<seam::application::RenameAudioTrackCommand>(
      track.id, "Music")));
  CHECK(session.project().audioTracks().front().name == "Music");
  CHECK(session.lastImpact().scope ==
        seam::application::CommandAudioImpact::MetadataOnly);
  CHECK(session.execute(std::make_unique<seam::application::RemoveAudioTrackCommand>(
      track.id)));
  CHECK(session.project().audioTracks().empty());
  CHECK(session.undo());
  CHECK(session.project().audioTracks().front().name == "Music");
}
