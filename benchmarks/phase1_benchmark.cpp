#include "seam/application/project_factory.hpp"
#include "seam/ui/note_spatial_index.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>

int main() {
  seam::application::ProjectFactory factory{1};
  auto project = factory.createProject("Benchmark");
  const auto track = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, track, "Region", seam::time::Tick{0}, seam::time::Tick{3000000});
  auto* region = project.findRegion(regionId);
  region->lyrics.reserve(10000);
  region->notes.reserve(10000);
  for (std::size_t index = 0; index < 10000; ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(index) * 240},
        seam::time::Tick{180},
        static_cast<std::uint8_t>(40 + index % 40),
        U"a");
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }

  seam::ui::NoteSpatialIndex index;
  const auto rebuildStart = std::chrono::steady_clock::now();
  index.rebuild(project);
  const auto rebuildEnd = std::chrono::steady_clock::now();

  std::size_t totalVisible = 0;
  const auto queryStart = std::chrono::steady_clock::now();
  for (std::int64_t frame = 0; frame < 1000; ++frame) {
    const auto tick = seam::time::Tick{frame * 120};
    totalVisible += index.query(tick, tick + seam::time::Tick{15360}, 36, 84).size();
  }
  const auto queryEnd = std::chrono::steady_clock::now();

  const auto rebuildMs = std::chrono::duration<double, std::milli>(rebuildEnd - rebuildStart).count();
  const auto queryMs = std::chrono::duration<double, std::milli>(queryEnd - queryStart).count();
  std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"notes\": " << index.size() << ",\n"
            << "  \"rebuildMs\": " << rebuildMs << ",\n"
            << "  \"queries\": 1000,\n"
            << "  \"queryTotalMs\": " << queryMs << ",\n"
            << "  \"queryAverageUs\": " << queryMs * 1000.0 / 1000.0 << ",\n"
            << "  \"visibleAccumulator\": " << totalVisible << "\n"
            << "}\n";
  return 0;
}
