#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/japanese_reading_resource.hpp"
#include "seam/authoring/japanese_reading_stage.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/native_ui/editor_controller.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

namespace {

seam::core::Result<seam::authoring::StagedJapaneseReadingResource>
makeReadingResource(const std::filesystem::path& root) {
  using namespace seam;
  std::error_code error;
  std::filesystem::create_directories(root / "dictionary", error);
  if (error) return core::failure<authoring::StagedJapaneseReadingResource>(
      core::ErrorCode::IoError, "Cannot create native reading fixture", error.message());
  authoring::JapaneseReadingResourceSpec spec{
      SEAM_READING_CAPTURE_PROCESS_PROBE, {}, std::string(40U, 'a'),
      root / "dictionary", {}};
  const auto digest = core::sha256File(spec.executable, 64U * 1024U * 1024U);
  if (!digest) return core::Result<authoring::StagedJapaneseReadingResource>{digest.error()};
  spec.executableSha256 = digest.value();
  for (std::size_t i = 0U; i < 4U; ++i) {
    const auto written = core::durableAtomicWriteText(
        spec.dictionaryDirectory / authoring::kJapaneseDictionaryFiles[i], "dictionary");
    if (!written) return core::Result<authoring::StagedJapaneseReadingResource>{written.error()};
    spec.dictionarySha256[i] = core::sha256Hex("dictionary");
  }
  const auto verified = authoring::VerifiedJapaneseReadingResource::verify(spec);
  if (!verified) return core::Result<authoring::StagedJapaneseReadingResource>{verified.error()};
  return authoring::StagedJapaneseReadingResource::prepare(verified.value(), root);
}

struct NativeReadingFixture final {
  seam::application::ProjectFactory factory{881000U};
  seam::domain::RegionId regionId{};
  seam::domain::NoteId noteId{};
  seam::application::EditorSession session;

  NativeReadingFixture() : session(makeProject()) {}

  seam::domain::Project makeProject() {
    auto project = factory.createProject("Native Japanese reading");
    const auto track = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{960});
    auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60U,
                                          U"漢", seam::domain::Language::Japanese);
    noteId = note.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }
};

const seam::native_ui::SemanticNode* childNamed(
    const seam::native_ui::SemanticNode& root, std::string_view name) {
  const auto found = std::find_if(root.children.begin(), root.children.end(),
      [name](const auto& child) { return child.name == name; });
  return found == root.children.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("native Japanese reading review is inspectable, safely gated, and undoable") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam;
  NativeReadingFixture fixture;
  const auto sourceRoot = test::support::temporaryDirectory("native-reading-source");
  native_ui::NativeEditorController controller{fixture.session, fixture.factory,
                                                fixture.regionId, {}};
  controller.setJapaneseReadingResourceResolver([sourceRoot] {
    return makeReadingResource(sourceRoot);
  });

  CHECK(controller.openJapaneseReadingReview());
  CHECK(controller.replacementReviewOpen());
  CHECK(!controller.sceneState().replacementReview.enabled[3]);

  bool ready = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline) {
    controller.pollReplacementReview();
    const auto view = controller.sceneState().replacementReview;
    if (view.enabled[3]) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(ready);
  controller.rebuildAccessibilityTree();
  const auto& root = controller.accessibilityTree().root();
  CHECK(root.name == "Japanese contextual reading; Apply reading");
  const auto* row = childNamed(root, "Review row 1");
  CHECK(row != nullptr);
  CHECK(row->value.find("漢") != std::string::npos);
  const auto rowId = row->id;
  CHECK(controller.dispatchAccessibility(rowId, native_ui::SemanticAction::Activate));

  controller.rebuildAccessibilityTree();
  const auto* back = childNamed(controller.accessibilityTree().root(), "Back to readings");
  CHECK(back != nullptr);
  CHECK(controller.dispatchAccessibility(back->id, native_ui::SemanticAction::Activate));
  controller.rebuildAccessibilityTree();
  const auto* apply = childNamed(controller.accessibilityTree().root(), "Apply reading");
  CHECK(apply != nullptr);
  CHECK(apply->enabled);
  const auto before = fixture.session.project();
  CHECK(controller.dispatchAccessibility(apply->id, native_ui::SemanticAction::Activate));
  CHECK(!controller.replacementReviewOpen());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findNote(fixture.noteId)->phoneticHint == "k a N");
  CHECK(fixture.session.revision() == 1U);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project() == before);
  CHECK(fixture.session.redo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findNote(fixture.noteId)->phoneticHint == "k a N");

  // Closing while the helper is still preparing must retire the worker even
  // though the UI no longer retains its resource identity.
  CHECK(controller.openJapaneseReadingReview());
  CHECK(controller.replacementReviewAction(4U));
  const auto retireDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  bool reopened = false;
  while (std::chrono::steady_clock::now() < retireDeadline && !reopened) {
    const auto retry = controller.openJapaneseReadingReview();
    if (retry) {
      reopened = true;
      break;
    }
    controller.pollReplacementReview();
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(reopened);
  CHECK(controller.replacementReviewOpen());
  CHECK(controller.replacementReviewAction(4U));
#endif
}
