#include "seam/domain/project.hpp"
#include "seam/formats/project_json.hpp"

int main(int argc, char** argv) {
  if (argc != 2) return 1;
  seam::domain::Project project{seam::domain::ProjectId{400U}, "Historical schema 4 writer"};
  seam::domain::VocalRegion region;
  region.id = seam::domain::RegionId{402U};
  region.name = "Historical phrase";
  region.durationTick = seam::time::Tick{1920};
  region.lyrics = {{seam::domain::LyricTokenId{404U}, U"か", seam::domain::Language::Japanese}};
  region.notes = {{.id = seam::domain::NoteId{403U}, .startTick = seam::time::Tick{240},
                  .durationTick = seam::time::Tick{480}, .midiKey = 64U,
                  .lyricTokenId = seam::domain::LyricTokenId{404U}}};
  region.phonemeOverrides = {{.key = {seam::domain::NoteId{403U}, 1U},
                             .timing = {.startOffset = -1200}, .locked = true}};
  seam::domain::VocalTrack track;
  track.id = seam::domain::TrackId{401U};
  track.name = "Historical singer";
  track.regions = {region};
  project.vocalTracks().push_back(track);
  seam::formats::ProjectJsonCodec codec;
  if (!codec.save(project, argv[1])) return 2;
  const auto loaded = codec.load(argv[1]);
  return loaded && loaded.value() == project ? 0 : 3;
}
