#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/core/file_io.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

namespace {

struct Fixture final {
  seam::authoring::ProjectDocument document;
  seam::domain::NoteId noteId;
};

Fixture makeFixture() {
  seam::application::ProjectFactory factory{100U};
  auto project = factory.createProject("Autosave Project");
  const auto track = factory.addVocalTrack(project, "Voice");
  const auto region = factory.addRegion(project, track, "Region",
                                        seam::time::Tick{0},
                                        seam::time::Tick{3840});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0},
                                        seam::time::Tick{960}, 60U, U"あ");
  const auto noteId = note.id;
  project.findRegion(region)->lyrics.push_back(std::move(lyric));
  project.findRegion(region)->notes.push_back(std::move(note));
  return Fixture{
      seam::authoring::ProjectDocument{
          std::move(project),
          seam::application::ProjectFactory{factory.nextIdValue()}},
      noteId,
  };
}

std::unique_ptr<seam::application::ICommand> move(
    seam::domain::NoteId noteId, seam::time::Tick before,
    seam::time::Tick after) {
  return std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{
          seam::application::NoteMove{
              .noteId = noteId,
              .before = before,
              .after = after,
              .beforeKey = 60U,
              .afterKey = 60U,
          }});
}

}  // namespace

TEST_CASE("autosave_service_writes_snapshot_off_document_path_and_recovers_dirty_copy") {
  const auto root = seam::test::support::temporaryDirectory("autosave-basic");
  const auto explicitPath = root / "original.seam";
  auto fixture = makeFixture();
  seam::authoring::ProjectLifecycleService lifecycle;
  CHECK(lifecycle.saveAs(fixture.document, explicitPath));
  CHECK(fixture.document.execute(move(fixture.noteId, seam::time::Tick{0},
                                      seam::time::Tick{240})));

  seam::authoring::AutosaveService service({
      .root = root / "autosaves",
      .maximumGenerations = 5U,
      .faultInjector = {},
      .wallClock = {},
  });
  CHECK(service.request(fixture.document));
  CHECK(service.flush());

  const auto candidates = service.discover();
  CHECK(candidates);
  CHECK(candidates.value().size() == 1U);
  const auto& candidate = candidates.value().front();
  CHECK(candidate.recoverable);
  CHECK(candidate.originalProjectPath == explicitPath);
  CHECK(candidate.revision == fixture.document.session().revision());
  CHECK(candidate.autosavePath != explicitPath);
  CHECK(std::filesystem::exists(candidate.autosavePath));
  CHECK(std::filesystem::exists(explicitPath));

  auto target = makeFixture();
  CHECK(service.recover(target.document, candidate));
  CHECK(target.document.dirty());
  CHECK(target.document.identity().projectPath == explicitPath);
  CHECK(target.document.identity().autosavePath == candidate.autosavePath);
  CHECK(target.document.session().project() == fixture.document.session().project());
  CHECK(std::filesystem::exists(explicitPath));
}

TEST_CASE("autosave_service_triggers_at_command_and_interval_thresholds") {
  const auto root = seam::test::support::temporaryDirectory("autosave-threshold");
  auto fixture = makeFixture();
  CHECK(fixture.document.execute(move(fixture.noteId, seam::time::Tick{0},
                                      seam::time::Tick{240})));
  seam::authoring::AutosaveService service({
      .root = root,
      .interval = std::chrono::seconds{60},
      .commandThreshold = 25U,
      .minimumCommandDelay = std::chrono::seconds{15},
      .maximumGenerations = 5U,
      .faultInjector = {},
      .wallClock = {},
  });
  const auto start = std::chrono::steady_clock::time_point{};
  for (std::size_t index = 0; index < 24U; ++index) {
    CHECK(service.onSuccessfulCommand(fixture.document,
                                      start + std::chrono::seconds{15}));
  }
  CHECK(service.flush());
  CHECK(service.discover().value().empty());

  CHECK(service.onSuccessfulCommand(fixture.document,
                                    start + std::chrono::seconds{15}));
  CHECK(service.flush());
  CHECK(service.discover().value().size() == 1U);

  CHECK(fixture.document.execute(move(fixture.noteId, seam::time::Tick{240},
                                      seam::time::Tick{480})));
  CHECK(service.tick(fixture.document, start + std::chrono::seconds{75}));
  CHECK(service.flush());
  CHECK(service.discover().value().size() == 2U);
}

TEST_CASE("autosave_service_keeps_only_five_newest_generations") {
  const auto root = seam::test::support::temporaryDirectory("autosave-prune");
  auto fixture = makeFixture();
  seam::authoring::AutosaveService service({
      .root = root,
      .maximumGenerations = 5U,
      .faultInjector = {},
      .wallClock = {},
  });
  auto before = seam::time::Tick{0};
  for (std::size_t index = 0; index < 7U; ++index) {
    const auto after = seam::time::Tick{static_cast<std::int64_t>((index + 1U) * 120U)};
    CHECK(fixture.document.execute(move(fixture.noteId, before, after)));
    before = after;
    CHECK(service.request(fixture.document));
    CHECK(service.flush());
  }
  const auto candidates = service.discover();
  CHECK(candidates);
  CHECK(candidates.value().size() == 5U);
  CHECK(candidates.value().back().revision == fixture.document.session().revision());
}

TEST_CASE("autosave_service_write_failure_and_corruption_do_not_mutate_document") {
  const auto root = seam::test::support::temporaryDirectory("autosave-fault");
  auto fixture = makeFixture();
  CHECK(fixture.document.execute(move(fixture.noteId, seam::time::Tick{0},
                                      seam::time::Tick{240})));
  seam::authoring::AutosaveService service({
      .root = root,
      .maximumGenerations = 5U,
      .faultInjector = [](seam::core::AtomicWriteStage stage) {
        if (stage == seam::core::AtomicWriteStage::BeforeReplace) {
          return seam::core::failure(seam::core::ErrorCode::IoError,
                                     "autosave fault");
        }
        return seam::core::success();
      },
      .wallClock = {},
  });
  CHECK(service.request(fixture.document));
  const auto flushed = service.flush();
  CHECK(!flushed);
  CHECK(service.discover().value().empty());

  const auto corruptDirectory = root / "corrupt";
  std::filesystem::create_directories(corruptDirectory);
  const auto corruptAutosave = corruptDirectory / "broken.seam.autosave";
  CHECK(seam::core::durableAtomicWriteText(corruptAutosave, "not json"));
  const auto metadata = corruptDirectory / "broken.meta.json";
  CHECK(seam::core::durableAtomicWriteText(
      metadata,
      R"({"format":"com.project-seam.autosave","schemaVersion":1,"projectId":"1","revision":9,"createdAtUnixMs":1,"projectFile":"broken.seam.autosave","originalProjectPath":""})"));

  seam::authoring::AutosaveService reader({
      .root = root,
      .interval = std::chrono::seconds{60},
      .commandThreshold = 25U,
      .minimumCommandDelay = std::chrono::seconds{15},
      .maximumGenerations = 5U,
      .faultInjector = {},
      .wallClock = {},
  });
  const auto candidates = reader.discover();
  CHECK(candidates);
  CHECK(candidates.value().size() == 1U);
  CHECK(!candidates.value().front().recoverable);
  const auto before = fixture.document.session().project();
  const auto recovered = reader.recover(fixture.document,
                                        candidates.value().front());
  CHECK(!recovered);
  CHECK(fixture.document.session().project() == before);
}
