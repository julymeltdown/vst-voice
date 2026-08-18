#include "test_framework.hpp"

#include "seam/authoring/project_document.hpp"
#include "seam/application/note_commands.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace {

struct ProjectFixture final {
  seam::domain::Project project;
  seam::domain::RegionId regionId;
  seam::domain::NoteId noteId;
};

ProjectFixture makeProject(std::uint64_t firstId = 100U) {
  seam::application::ProjectFactory factory{firstId};
  auto project = factory.createProject("Document Test");
  const auto trackId = factory.addVocalTrack(project, "VOICE");
  const auto regionId = factory.addRegion(
      project, trackId, "REGION", seam::time::Tick{0}, seam::time::Tick{3840});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ",
      seam::domain::Language::Japanese);
  const auto noteId = note.id;
  auto* region = project.findRegion(regionId);
  if (region == nullptr) throw seam::test::Failure{"test project region missing"};
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  return ProjectFixture{std::move(project), regionId, noteId};
}

std::unique_ptr<seam::application::ICommand> moveNote(
    seam::domain::NoteId noteId, seam::time::Tick before,
    seam::time::Tick after) {
  return std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{seam::application::NoteMove{
          .noteId = noteId,
          .before = before,
          .after = after,
          .beforeKey = 60U,
          .afterKey = 60U,
      }});
}

class FailingCommand final : public seam::application::ICommand {
public:
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Failing command";
  }
  [[nodiscard]] seam::core::Result<void> apply(
      seam::domain::Project&) override {
    return seam::core::failure(seam::core::ErrorCode::Internal,
                               "intentional test failure");
  }
  [[nodiscard]] seam::core::Result<void> revert(
      seam::domain::Project&) override {
    return seam::core::success();
  }
};

}  // namespace

TEST_CASE("project_document_successful_command_marks_document_dirty") {
  auto fixture = makeProject();
  seam::authoring::ProjectDocument document(
      std::move(fixture.project), seam::application::ProjectFactory{1000U});
  CHECK(!document.dirty());
  CHECK(document.execute(moveNote(fixture.noteId, seam::time::Tick{0},
                                  seam::time::Tick{240})));
  CHECK(document.dirty());
  CHECK(document.identity().lastSavedRevision == 0U);
}

TEST_CASE("project_document_mark_saved_clears_dirty_at_current_revision") {
  auto fixture = makeProject();
  seam::authoring::ProjectDocument document(
      std::move(fixture.project), seam::application::ProjectFactory{1000U});
  CHECK(document.execute(moveNote(fixture.noteId, seam::time::Tick{0},
                                  seam::time::Tick{240})));
  const auto path = std::filesystem::path{"/tmp/document-test.seam"};
  document.markSaved(path);
  CHECK(!document.dirty());
  CHECK(document.identity().projectPath == path);
  CHECK(document.identity().lastSavedRevision == document.session().revision());
  CHECK(!document.identity().autosavePath.has_value());
}

TEST_CASE("project_document_undo_after_save_is_dirty_again") {
  auto fixture = makeProject();
  seam::authoring::ProjectDocument document(
      std::move(fixture.project), seam::application::ProjectFactory{1000U});
  CHECK(document.execute(moveNote(fixture.noteId, seam::time::Tick{0},
                                  seam::time::Tick{240})));
  document.markSaved("/tmp/document-test.seam");
  CHECK(document.undo());
  CHECK(document.dirty());
  CHECK(document.session().project().findNote(fixture.noteId)->startTick ==
        seam::time::Tick{0});
}

TEST_CASE("project_document_replace_project_synchronizes_factory_ids") {
  auto fixture = makeProject();
  seam::authoring::ProjectDocument document(
      std::move(fixture.project), seam::application::ProjectFactory{1U});
  auto replacement = makeProject(9000U);
  CHECK(document.replaceProject(std::move(replacement.project)));
  CHECK(document.dirty());
  CHECK(document.factory().nextIdValue() > 9000U);
  auto [lyric, note] = document.factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 62U, U"い");
  static_cast<void>(lyric);
  CHECK(note.id.value() >= document.factory().nextIdValue() - 2U);
  CHECK(note.id.value() > 9000U);
}

TEST_CASE("project_document_failed_command_preserves_clean_state") {
  auto fixture = makeProject();
  seam::authoring::ProjectDocument document(
      std::move(fixture.project), seam::application::ProjectFactory{1000U});
  const auto revision = document.session().revision();
  const auto project = document.session().project();
  const auto result = document.execute(std::make_unique<FailingCommand>());
  CHECK(!result);
  CHECK(!document.dirty());
  CHECK(document.session().revision() == revision);
  CHECK(document.session().project() == project);
}

TEST_CASE("project_document_recovery_path_marks_unsaved_recovered_content") {
  auto fixture = makeProject();
  seam::authoring::ProjectDocument document(
      std::move(fixture.project), seam::application::ProjectFactory{1000U});
  document.markRecovered("/tmp/document-test.autosave");
  CHECK(document.dirty());
  CHECK(document.identity().autosavePath ==
        std::filesystem::path{"/tmp/document-test.autosave"});
}
