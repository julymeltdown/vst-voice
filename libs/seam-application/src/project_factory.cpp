#include "seam/application/project_factory.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace seam::application {

void ProjectFactory::synchronizeWith(const domain::Project& project) noexcept {
  std::uint64_t maximum = project.id().value();
  const auto observe = [&maximum](std::uint64_t value) {
    maximum = std::max(maximum, value);
  };
  for (const auto& track : project.vocalTracks()) {
    observe(track.id.value());
    for (const auto& region : track.regions) {
      observe(region.id.value());
      for (const auto& lyric : region.lyrics) {
        observe(lyric.id.value());
      }
      for (const auto& note : region.notes) {
        observe(note.id.value());
      }
    }
  }
  for (const auto& track : project.audioTracks()) {
    observe(track.id.value());
  }
  ids_.reserveAtLeast(maximum == std::numeric_limits<std::uint64_t>::max()
                          ? maximum
                          : maximum + 1U);
}

domain::Project ProjectFactory::createProject(std::string name) {
  return domain::Project{ids_.next<domain::ProjectTag>(), std::move(name)};
}

domain::TrackId ProjectFactory::addVocalTrack(domain::Project& project, std::string name) {
  const auto id = ids_.next<domain::TrackTag>();
  project.vocalTracks().push_back(domain::VocalTrack{
      .id = id,
      .name = std::move(name),
      .voicebank = {},
      .character = {},
      .regions = {},
  });
  return id;
}

domain::RegionId ProjectFactory::addRegion(domain::Project& project,
                                            domain::TrackId trackId,
                                            std::string name,
                                            time::Tick start,
                                            time::Tick duration) {
  auto* track = project.findVocalTrack(trackId);
  if (track == nullptr) {
    throw std::invalid_argument("Vocal track does not exist");
  }
  const auto id = ids_.next<domain::RegionTag>();
  track->regions.push_back(domain::VocalRegion{
      .id = id,
      .name = std::move(name),
      .startTick = start,
      .durationTick = duration,
      .lyrics = {},
      .notes = {},
      .phonemeOverrides = {},
  });
  return id;
}

std::pair<domain::LyricToken, domain::Note> ProjectFactory::makeNote(
    time::Tick start,
    time::Tick duration,
    std::uint8_t midiKey,
    std::u32string lyric,
    domain::Language language) {
  const auto lyricId = ids_.next<domain::LyricTag>();
  domain::LyricToken token{lyricId, std::move(lyric), language};
  domain::Note note{
      .id = ids_.next<domain::NoteTag>(),
      .startTick = start,
      .durationTick = duration,
      .midiKey = midiKey,
      .lyricTokenId = lyricId,
      .articulation = domain::NoteArticulation::Normal,
      .slurGroup = std::nullopt,
  };
  return {std::move(token), std::move(note)};
}

}  // namespace seam::application
