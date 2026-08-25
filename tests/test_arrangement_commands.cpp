#include "test_framework.hpp"

#include "seam/application/arrangement_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/render_commands.hpp"

#include <algorithm>

TEST_CASE("arrangement track and region commands are undoable") {
  seam::application::ProjectFactory factory{10U};
  auto project = factory.createProject("Arrangement");
  const auto trackId = seam::domain::TrackId{20U};
  seam::domain::VocalTrack track{.id = trackId, .name = "Lead"};
  seam::application::AddVocalTrackCommand addTrack{track};
  CHECK(addTrack.apply(project));
  CHECK(project.findVocalTrack(trackId) != nullptr);

  seam::application::RenameVocalTrackCommand rename{trackId, "Lead 2"};
  CHECK(rename.apply(project));
  CHECK(project.findVocalTrack(trackId)->name == "Lead 2");

  seam::domain::VocalRegion region{.id = seam::domain::RegionId{21U},
                                   .name = "Verse",
                                   .startTick = seam::time::Tick{0},
                                   .durationTick = seam::time::Tick{3840}};
  seam::application::AddVocalRegionCommand addRegion{trackId, region};
  CHECK(addRegion.apply(project));
  CHECK(project.findRegion(region.id) != nullptr);
  seam::application::RemoveVocalRegionCommand removeRegion{trackId, region.id};
  CHECK(removeRegion.apply(project));
  CHECK(project.findRegion(region.id) == nullptr);
}

TEST_CASE("vocal region duplicate, move, and resize commands are undoable") {
  seam::application::ProjectFactory factory{1000U};
  auto project = factory.createProject("Region editing");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{960},
                                           seam::time::Tick{3840});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{240},
                                         seam::time::Tick{480}, 64, U"la",
                                         seam::domain::Language::English);
  auto* original = project.findRegion(regionId);
  original->lyrics.push_back(lyric);
  original->notes.push_back(note);
  CHECK(original->validate());

  auto duplicate = *original;
  duplicate.id = seam::domain::RegionId{2000U};
  duplicate.name = "Verse copy";
  auto [duplicateLyric, duplicateNote] = factory.makeNote(
      seam::time::Tick{720}, seam::time::Tick{480}, 67, U"la",
      seam::domain::Language::English);
  duplicate.lyrics = {duplicateLyric};
  duplicate.notes = {duplicateNote};

  seam::application::DuplicateVocalRegionCommand duplicateCommand{
      trackId, duplicate};
  CHECK(duplicateCommand.audioImpact() ==
        seam::application::CommandAudioImpact::ProjectAudio);
  CHECK(duplicateCommand.impact().regionIds.front() == duplicate.id);
  CHECK(duplicateCommand.apply(project));
  CHECK(project.findRegion(duplicate.id) != nullptr);
  CHECK(project.findRegion(duplicate.id)->name == "Verse copy");
  CHECK(project.findRegion(duplicate.id)->notes.front().id == duplicateNote.id);
  CHECK(duplicateCommand.revert(project));
  CHECK(project.findRegion(duplicate.id) == nullptr);

  seam::application::MoveVocalRegionCommand moveCommand{
      trackId, regionId, seam::time::Tick{1440}};
  CHECK(moveCommand.apply(project));
  CHECK(project.findRegion(regionId)->startTick == seam::time::Tick{1440});
  CHECK(moveCommand.revert(project));
  CHECK(project.findRegion(regionId)->startTick == seam::time::Tick{960});

  seam::application::ResizeVocalRegionCommand resizeCommand{
      trackId, regionId, seam::time::Tick{4800}};
  CHECK(resizeCommand.apply(project));
  CHECK(project.findRegion(regionId)->durationTick == seam::time::Tick{4800});
  CHECK(resizeCommand.revert(project));
  CHECK(project.findRegion(regionId)->durationTick == seam::time::Tick{3840});

  seam::application::DuplicateVocalRegionCommand generatedDuplicate{
      trackId, regionId};
  CHECK(generatedDuplicate.apply(project));
  const auto generatedId = generatedDuplicate.impact().regionIds.front();
  CHECK(generatedId.valid());
  CHECK(generatedId != regionId);
  CHECK(project.findRegion(generatedId) != nullptr);
  CHECK(project.findRegion(generatedId)->notes.front().id != note.id);
  CHECK(project.findRegion(generatedId)->lyrics.front().id != lyric.id);
  CHECK(generatedDuplicate.revert(project));
  CHECK(project.findRegion(generatedId) == nullptr);
}

TEST_CASE("vocal track duplication generates fresh nested identities") {
  seam::application::ProjectFactory factory{5000U};
  auto project = factory.createProject("Track duplication");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{1920});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0},
                                         seam::time::Tick{960}, 60, U"la",
                                         seam::domain::Language::English);
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  CHECK(project.validate());

  seam::application::DuplicateVocalTrackCommand duplicate{trackId};
  CHECK(duplicate.apply(project));
  CHECK(project.vocalTracks().size() == 2U);
  const auto& copy = project.vocalTracks().back();
  CHECK(copy.id != trackId);
  CHECK(copy.regions.size() == 1U);
  CHECK(copy.regions.front().id != regionId);
  CHECK(copy.regions.front().notes.front().id != note.id);
  CHECK(copy.regions.front().lyrics.front().id != lyric.id);
  CHECK(project.validate());
  CHECK(duplicate.revert(project));
  CHECK(project.vocalTracks().size() == 1U);
  CHECK(duplicate.apply(project));
  CHECK(project.vocalTracks().size() == 2U);
}

TEST_CASE("audio track replacement round trips without changing identity") {
  seam::application::ProjectFactory factory{7000U};
  auto project = factory.createProject("Audio replacement");
  const auto trackId = seam::domain::TrackId{7100U};
  const seam::domain::AudioTrack original{
      .id = trackId,
      .name = "Backing",
      .mediaPath = "/tmp/backing-a.wav",
      .mediaHash = std::string(64U, 'a'),
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = "backing-a.wav",
      .sourceSampleRate = 48000U,
      .sourceChannels = 1U,
      .sourceFrameCount = 100U,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  };
  seam::application::AddAudioTrackCommand add{original};
  CHECK(add.apply(project));
  auto replacement = original;
  replacement.name = "Backing replaced";
  replacement.mediaPath = "/tmp/backing-b.wav";
  replacement.originalFilename = "backing-b.wav";
  seam::application::ReplaceAudioTrackCommand replace{trackId, replacement};
  CHECK(replace.apply(project));
  CHECK(project.audioTracks().front().name == "Backing replaced");
  CHECK(replace.revert(project));
  CHECK(project.audioTracks().front() == original);
}

TEST_CASE("audio track mix edits are undoable and preserve routing identity") {
  seam::application::ProjectFactory factory{7200U};
  auto project = factory.createProject("Audio mix");
  const auto trackId = seam::domain::TrackId{7201U};
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = trackId,
      .name = "Backing",
      .mediaPath = "/tmp/backing.wav",
      .mediaHash = "backing-hash",
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .startTick = seam::time::Tick{0},
  });
  const auto track = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [trackId](const auto& value) { return value.id == trackId; });
  CHECK(track != project.audioTracks().end());
  const auto originalRoute = track->outputRoute;
  seam::application::SetAudioTrackMixCommand command{
      trackId, -6.0F, 0.5F, true, true};
  CHECK(command.apply(project));
  CHECK_NEAR(track->gainDb, -6.0, 1e-6);
  CHECK_NEAR(track->pan, 0.5, 1e-6);
  CHECK(track->muted);
  CHECK(track->solo);
  CHECK(command.revert(project));
  CHECK_NEAR(track->gainDb, 0.0, 1e-6);
  CHECK_NEAR(track->pan, 0.0, 1e-6);
  CHECK(!track->muted);
  CHECK(!track->solo);
  CHECK(track->outputRoute == originalRoute);
}

TEST_CASE("vocal region move and resize reject invalid edits atomically") {
  seam::application::ProjectFactory factory{3000U};
  auto project = factory.createProject("Invalid region editing");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{960});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{480},
                                         seam::time::Tick{480}, 64, U"la",
                                         seam::domain::Language::English);
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  CHECK(region->validate());

  seam::application::MoveVocalRegionCommand invalidMove{
      trackId, regionId, seam::time::Tick{-1}};
  CHECK(!invalidMove.apply(project));
  CHECK(region->startTick == seam::time::Tick{0});

  seam::application::ResizeVocalRegionCommand invalidResize{
      trackId, regionId, seam::time::Tick{800}};
  CHECK(!invalidResize.apply(project));
  CHECK(region->durationTick == seam::time::Tick{960});
}

TEST_CASE("arrangement reorder and region rename are undoable") {
  seam::application::ProjectFactory factory{8000U};
  auto project = factory.createProject("Arrangement reorder");
  const auto first = factory.addVocalTrack(project, "First");
  const auto second = factory.addVocalTrack(project, "Second");
  const auto third = factory.addVocalTrack(project, "Third");
  const auto regionId = factory.addRegion(project, second, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{1920});

  seam::application::MoveVocalTrackCommand move{first, 2U};
  CHECK(move.apply(project));
  CHECK(project.vocalTracks()[0].id == second);
  CHECK(project.vocalTracks()[1].id == third);
  CHECK(project.vocalTracks()[2].id == first);
  CHECK(move.revert(project));
  CHECK(project.vocalTracks()[0].id == first);

  seam::application::RenameVocalRegionCommand rename{second, regionId,
                                                       "Verse renamed"};
  CHECK(rename.apply(project));
  CHECK(project.findRegion(regionId)->name == "Verse renamed");
  CHECK(rename.revert(project));
  CHECK(project.findRegion(regionId)->name == "Verse");
}

TEST_CASE("vocal region split preserves note ownership and round trips") {
  seam::application::ProjectFactory factory{9000U};
  auto project = factory.createProject("Arrangement split");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{1920});
  auto* region = project.findRegion(regionId);
  auto [leftLyric, leftNote] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"la",
      seam::domain::Language::English);
  auto [rightLyric, rightNote] = factory.makeNote(
      seam::time::Tick{960}, seam::time::Tick{480}, 64U, U"do",
      seam::domain::Language::English);
  region->lyrics = {leftLyric, rightLyric};
  region->notes = {leftNote, rightNote};
  region->sortNotes();
  CHECK(project.validate());

  seam::application::SplitVocalRegionCommand split{
      trackId, regionId, seam::time::Tick{960}};
  CHECK(split.apply(project));
  const auto rightId = split.splitRegionId();
  CHECK(rightId.valid());
  CHECK(project.findRegion(regionId)->durationTick == seam::time::Tick{960});
  const auto* right = project.findRegion(rightId);
  CHECK(right != nullptr);
  CHECK(right->startTick == seam::time::Tick{960});
  CHECK(right->durationTick == seam::time::Tick{960});
  CHECK(right->notes.size() == 1U);
  CHECK(right->notes.front().startTick == seam::time::Tick{0});
  CHECK(right->notes.front().id != rightNote.id);
  CHECK(project.validate());
  CHECK(split.revert(project));
  CHECK(project.findRegion(rightId) == nullptr);
  CHECK(project.findRegion(regionId)->durationTick == seam::time::Tick{1920});
  CHECK(project.findRegion(regionId)->notes.size() == 2U);
  CHECK(project.validate());
}

TEST_CASE("vocal region copy can target another track") {
  seam::application::ProjectFactory factory{9500U};
  auto project = factory.createProject("Arrangement copy");
  const auto sourceTrack = factory.addVocalTrack(project, "Lead");
  const auto targetTrack = factory.addVocalTrack(project, "Harmony");
  const auto regionId = factory.addRegion(project, sourceTrack, "Verse",
                                           seam::time::Tick{480},
                                           seam::time::Tick{960});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"la",
      seam::domain::Language::English);
  auto* source = project.findRegion(regionId);
  source->lyrics.push_back(lyric);
  source->notes.push_back(note);
  CHECK(project.validate());

  seam::application::DuplicateVocalRegionCommand copy{
      sourceTrack, targetTrack, regionId};
  CHECK(copy.apply(project));
  const auto copiedId = copy.duplicatedRegionId();
  CHECK(copiedId.valid());
  const auto* copied = project.findVocalTrack(targetTrack)->findRegion(copiedId);
  CHECK(copied != nullptr);
  CHECK(copied->notes.size() == 1U);
  CHECK(copied->notes.front().id != note.id);
  CHECK(copy.revert(project));
  CHECK(project.findRegion(copiedId) == nullptr);
  CHECK(project.validate());
}
