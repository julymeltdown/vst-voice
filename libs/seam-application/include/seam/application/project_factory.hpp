#pragma once

#include "seam/core/id.hpp"
#include "seam/domain/project.hpp"

#include <string>

namespace seam::application {

class ProjectFactory final {
public:
  explicit ProjectFactory(std::uint64_t firstId = 1) : ids_(firstId) {}

  [[nodiscard]] domain::Project createProject(std::string name);
  void synchronizeWith(const domain::Project& project) noexcept;
  [[nodiscard]] std::uint64_t nextIdValue() const noexcept { return ids_.nextValue(); }
  [[nodiscard]] domain::TrackId addVocalTrack(domain::Project& project, std::string name);
  [[nodiscard]] domain::RegionId addRegion(domain::Project& project,
                                            domain::TrackId trackId,
                                            std::string name,
                                            time::Tick start,
                                            time::Tick duration);
  [[nodiscard]] std::pair<domain::LyricToken, domain::Note> makeNote(
      time::Tick start,
      time::Tick duration,
      std::uint8_t midiKey,
      std::u32string lyric,
      domain::Language language = domain::Language::Japanese);

private:
  core::IdGenerator ids_;
};

}  // namespace seam::application
