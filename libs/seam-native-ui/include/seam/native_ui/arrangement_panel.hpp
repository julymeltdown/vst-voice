#pragma once

#include "seam/application/selection.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"

#include <string>
#include <vector>

namespace seam::native_ui {

struct ArrangementRegionItem final {
  domain::RegionId id;
  std::string name;
  time::Tick startTick;
  time::Tick durationTick;
  std::size_t noteCount{0U};
  bool selected{false};
};

struct ArrangementTrackItem final {
  domain::TrackId id;
  std::string name;
  bool vocal{true};
  bool selected{false};
  std::vector<ArrangementRegionItem> regions;
};

class ArrangementPanelModel final {
public:
  void rebuild(const domain::Project& project,
               domain::TrackId selectedTrack = {},
               domain::RegionId selectedRegion = {});
  [[nodiscard]] core::Result<void> selectTrack(
      const domain::Project& project, domain::TrackId trackId);
  [[nodiscard]] core::Result<void> selectRegion(
      const domain::Project& project, domain::RegionId regionId);
  [[nodiscard]] const std::vector<ArrangementTrackItem>& tracks()
      const noexcept {
    return tracks_;
  }
  [[nodiscard]] domain::TrackId selectedTrack() const noexcept {
    return selectedTrack_;
  }
  [[nodiscard]] domain::RegionId selectedRegion() const noexcept {
    return selectedRegion_;
  }

private:
  std::vector<ArrangementTrackItem> tracks_;
  domain::TrackId selectedTrack_;
  domain::RegionId selectedRegion_;
};

}
