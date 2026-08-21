#include "test_framework.hpp"

#include "seam/application/arrangement_commands.hpp"
#include "seam/application/project_factory.hpp"

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
