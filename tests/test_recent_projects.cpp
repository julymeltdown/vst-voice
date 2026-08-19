#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/project_lifecycle.hpp"
#include "seam/authoring/recent_projects.hpp"
#include "seam/application/note_commands.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

seam::authoring::ProjectDocument documentWithName(std::string name) {
  seam::application::ProjectFactory factory{100U};
  auto project = factory.createProject(std::move(name));
  const auto track = factory.addVocalTrack(project, "Voice");
  static_cast<void>(factory.addRegion(project, track, "Region",
                                      seam::time::Tick{0},
                                      seam::time::Tick{3840}));
  return seam::authoring::ProjectDocument{
      std::move(project),
      seam::application::ProjectFactory{factory.nextIdValue()}};
}

void markDirty(seam::authoring::ProjectDocument& document) {
  const auto regionId = document.session().project().vocalTracks().front().regions.front().id;
  auto [lyric, note] = document.factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ",
      seam::domain::Language::Japanese);
  CHECK(document.execute(std::make_unique<seam::application::AddNoteCommand>(
      regionId, std::move(lyric), std::move(note))));
}

}  // namespace

TEST_CASE("recent_projects_canonicalizes_deduplicates_orders_and_bounds_entries") {
  const auto root = seam::test::support::temporaryDirectory("recent-projects");
  const auto statePath = root / "recent.json";
  seam::authoring::RecentProjectsStore store{statePath};
  const auto base = std::chrono::system_clock::time_point{};

  for (int index = 0; index < 12; ++index) {
    const auto project = root / ("song-" + std::to_string(index) + ".seam");
    std::ofstream(project) << "{}";
    CHECK(store.record(project, "Song " + std::to_string(index),
                       base + std::chrono::seconds{index}));
  }
  const auto duplicate = root / "." / "song-11.seam";
  CHECK(store.record(duplicate, "Song Eleven",
                     base + std::chrono::seconds{30}));
  CHECK(store.entries().size() == 10U);
  CHECK(store.entries().front().displayName == "Song Eleven");
  CHECK(store.entries().front().path ==
        std::filesystem::weakly_canonical(root / "song-11.seam"));
  CHECK(store.entries().back().displayName == "Song 2");
  CHECK(store.save());

  seam::authoring::RecentProjectsStore loaded{statePath};
  CHECK(loaded.load());
  CHECK(loaded.entries() == store.entries());
}

TEST_CASE("recent_projects_load_preserves_missing_until_explicit_refresh") {
  const auto root = seam::test::support::temporaryDirectory("recent-missing");
  const auto statePath = root / "recent.json";
  const auto project = root / "gone.seam";
  std::ofstream(project) << "{}";
  seam::authoring::RecentProjectsStore store{statePath};
  CHECK(store.record(project, "Gone", std::chrono::system_clock::now()));
  CHECK(store.save());
  std::filesystem::remove(project);

  seam::authoring::RecentProjectsStore loaded{statePath};
  CHECK(loaded.load());
  CHECK(loaded.entries().size() == 1U);
  CHECK(loaded.entries().front().missing);
  CHECK(loaded.refreshMissing());
  CHECK(loaded.entries().empty());
}

TEST_CASE("unsaved_close_policy_save_discard_cancel_and_failed_save") {
  const auto root = seam::test::support::temporaryDirectory("close-policy");
  seam::authoring::ProjectLifecycleService lifecycle;

  auto clean = documentWithName("Clean");
  CHECK(seam::authoring::resolveUnsavedClose(
            clean, seam::authoring::CloseChoice::Cancel, lifecycle)
            .value() == seam::authoring::CloseDisposition::Close);

  auto dirty = documentWithName("Dirty");
  markDirty(dirty);
  CHECK(dirty.dirty());
  CHECK(seam::authoring::resolveUnsavedClose(
            dirty, seam::authoring::CloseChoice::Cancel, lifecycle)
            .value() == seam::authoring::CloseDisposition::RemainOpen);
  CHECK(seam::authoring::resolveUnsavedClose(
            dirty, seam::authoring::CloseChoice::Discard, lifecycle)
            .value() == seam::authoring::CloseDisposition::Close);

  auto savable = documentWithName("Savable");
  markDirty(savable);
  CHECK(lifecycle.saveAs(savable, root / "savable.seam"));
  markDirty(savable);
  const auto saved = seam::authoring::resolveUnsavedClose(
      savable, seam::authoring::CloseChoice::Save, lifecycle);
  CHECK(saved);
  CHECK(saved.value() == seam::authoring::CloseDisposition::Close);
  CHECK(!savable.dirty());

  auto unsaved = documentWithName("No Path");
  markDirty(unsaved);
  const auto failed = seam::authoring::resolveUnsavedClose(
      unsaved, seam::authoring::CloseChoice::Save, lifecycle);
  CHECK(!failed);
  CHECK(unsaved.dirty());
}
