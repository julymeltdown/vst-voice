#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/interchange/smf_codec.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/unit_selection.hpp"

#include <chrono>
#include <fstream>
#include <thread>
#include <utility>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {
using namespace seam;
clap_editor::EditorRuntime runtimeFixture() {
  return clap_editor::EditorRuntime{std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK}, voicebank::VoicebankRootKind::Development}}};
}
std::shared_ptr<const clap_editor::RenderedPreview> ready(clap_editor::EditorRuntime& runtime) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto preview = runtime.renderedPreview();
    if (preview && preview->revision == runtime.revision() && preview->status == clap_editor::PreviewStatus::Ready) return preview;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  throw test::Failure{"CLAP microscope fixture did not publish a current ready preview"};
}
domain::PhonemeKey keyFor(clap_editor::EditorRuntime& runtime, const clap_editor::RenderedPreview& preview) {
  const auto project = runtime.projectCopy();
  const auto* region = project.findRegion(runtime.regionId()); CHECK(region != nullptr);
  const auto phones = phonemizer::inspectPronunciation(*region);
  CHECK(!preview.unitPlan.empty()); CHECK(preview.unitPlan.front().tokenStart < phones.tokens.size());
  return phones.tokens[preview.unitPlan.front().tokenStart].key;
}
void click(clap_editor::EditorRuntime& runtime, ui::Rect bounds) {
  runtime.pointerDown({{bounds.x + 1.0, bounds.y + 1.0}, native_ui::PointerButton::Left, {}, 1});
}
}

TEST_CASE("CLAP microscope shares full captured selection details and working pointer keyboard and AX controls") {
  using namespace seam; auto runtime = runtimeFixture();
  const auto preview = ready(runtime); const auto key = keyFor(runtime, *preview);
  const auto project = runtime.projectCopy(); const auto revision = runtime.revision();
  runtime.resize(480.0, 320.0); CHECK(runtime.openSampleMicroscope(key));
  CHECK(runtime.controller().sampleMicroscopeOpen()); CHECK(runtime.sampleMicroscope() != nullptr);
  CHECK(runtime.selectedUnitId() == std::optional<std::string>{preview->unitPlan.front().unitId});
  CHECK(runtime.dispatchAccessibility("microscope.details", native_ui::SemanticAction::Activate));
  auto view = *runtime.controller().sceneState().sampleMicroscope;
  CHECK(view.detailsVisible);
  CHECK(view.detailsText.find(synthesis::describeUnitSelection(preview->unitPlan.front())) != std::string::npos);
  const auto expected = view.detailsText; std::string complete;
  for (;;) {
    view = *runtime.controller().sceneState().sampleMicroscope;
    for (const auto& line : view.detailsLines) complete += line;
    if (view.detailsPage + 1U == view.detailsPageCount) break;
    CHECK(runtime.dispatchAccessibility("microscope.next", native_ui::SemanticAction::Activate));
  }
  CHECK(complete == expected);
  const native_ui::EditorSceneLayout layout;
  click(runtime, layout.microscopeDetailsToggleBounds(480.0, 320.0));
  CHECK(!runtime.controller().sceneState().sampleMicroscope->detailsVisible);
  runtime.keyDown({.key = native_ui::NativeKey::D});
  CHECK(runtime.controller().sceneState().sampleMicroscope->detailsVisible);
  runtime.resize(700.0, 480.0);
  CHECK(runtime.controller().sceneState().sampleMicroscope->detailsText == expected);
  runtime.resize(480.0, 320.0);
  CHECK(runtime.sampleMicroscope()->spectrogramBounds().bottom() <= layout.microscopePanelBounds(480.0, 320.0).bottom());
  click(runtime, layout.microscopeCloseBounds(480.0, 320.0));
  CHECK(!runtime.sampleMicroscopeOpen()); CHECK(!runtime.selectedUnitId());
  CHECK(runtime.projectCopy() == project); CHECK(runtime.revision() == revision);
}

TEST_CASE("CLAP microscope isolates complete focus traversal retained SetValue and background edit shortcuts") {
  using namespace seam; auto runtime = runtimeFixture();
  const auto preview = ready(runtime); const auto key = keyFor(runtime, *preview);
  const auto project = runtime.projectCopy(); const auto revision = runtime.revision();
  CHECK(runtime.openSampleMicroscope(key)); runtime.keyDown({.key = native_ui::NativeKey::D});
  CHECK(runtime.accessibilitySnapshot().virtualizedNoteCount == 0U);
  CHECK(runtime.accessibilityNotes(0U, 100U).empty());
  for (const auto reverse : {false, true}) {
    for (std::size_t i = 0U; i < 12U; ++i) {
      runtime.keyDown({.key = native_ui::NativeKey::Tab, .modifiers = {.shift = reverse}});
      const auto focus = runtime.accessibilityFocusedNode(); CHECK(focus);
      CHECK(focus->id.starts_with("microscope."));
    }
  }
  for (const auto& [id, value] : {std::pair{"note." + key.noteId.toString(), "あ"},
      {std::string{"note.fffffffffffffff0"}, "stale reference"},
      {std::string{"toolbar.tempo"}, "130"}, {std::string{"toolbar.meter"}, "7/8"}}) {
    const auto changed = runtime.setAccessibilityValue(id, value);
    CHECK(!changed); CHECK(changed.error().code == core::ErrorCode::Conflict);
    CHECK(!runtime.controller().textInputActive());
  }
  for (const auto keyCode : {native_ui::NativeKey::S, native_ui::NativeKey::R,
      native_ui::NativeKey::Delete, native_ui::NativeKey::Z}) runtime.keyDown({.key = keyCode});
  CHECK(runtime.projectCopy() == project); CHECK(runtime.revision() == revision);
  runtime.keyDown({.key = native_ui::NativeKey::Escape}); CHECK(runtime.sampleMicroscopeOpen());
  runtime.keyDown({.key = native_ui::NativeKey::Escape}); CHECK(!runtime.sampleMicroscopeOpen());
  CHECK(runtime.accessibilitySnapshot().virtualizedNoteCount > 0U);
  CHECK(!runtime.dispatchAccessibility("microscope.details", native_ui::SemanticAction::Activate));
}

// U32 for the embedded surface: a DAW session must be able to open a USTX/SMF score and write one
// back through the same boundary the standalone editor uses. The review step is the point of the
// test: an import is a draft until it is explicitly accepted, and a rejection has to leave the song
// the host is holding exactly as it was.
TEST_CASE("CLAP interchange imports a score as a reviewable draft and rejects without touching the song") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange");
  auto runtime = runtimeFixture();
  const auto before = runtime.projectCopy();
  const auto beforeRevision = runtime.revision();
  const auto beforeTrack = runtime.trackId();

  // Export the live project so the import has a real USTX file to read rather than a hand-written one.
  const auto destination = root / "score.ustx";
  authoring::InterchangeExportRequest exportRequest;
  exportRequest.format = authoring::InterchangeFormat::Ustx;
  exportRequest.destination = destination;
  const auto exported = runtime.exportInterchange(exportRequest);
  CHECK(exported.hasValue());
  if (!exported) return;
  CHECK(std::filesystem::exists(destination));
  CHECK(exported.value().contentHash.size() == 64U);
  // The service reports the canonical destination it actually wrote: it resolves the parent chain so
  // an intermediate symlink cannot silently redirect the write (on macOS a temporary directory under
  // /var is the /private/var path). The receipt is compared against that same resolution.
  const auto expectedDestination =
      std::filesystem::weakly_canonical(destination.parent_path()) / destination.filename();
  CHECK(exported.value().destination == expectedDestination);
  // Export is create-new and must not have disturbed the live document.
  CHECK(runtime.revision() == beforeRevision);
  CHECK(runtime.projectCopy().id() == before.id());
  // Writing the same destination again is a collision rather than a silent overwrite.
  CHECK(!runtime.exportInterchange(exportRequest));

  // Import returns a draft. Nothing about the live session may change before acceptance.
  authoring::InterchangeImportRequest importRequest;
  importRequest.projectName = "Imported score";
  const auto draft = runtime.prepareInterchangeImport(destination, importRequest);
  CHECK(draft.hasValue());
  if (!draft) return;
  CHECK(draft.value().format == authoring::InterchangeFormat::Ustx);
  // The recorded source path is the canonical real location, matching the export receipt above.
  CHECK(draft.value().sourcePath == expectedDestination);
  CHECK(draft.value().sourceHash.size() == 64U);
  CHECK(!draft.value().project.vocalTracks().empty());
  // The draft is built from a factory seeded past the live identifiers, so accepting it cannot
  // collide with an id the current song already used.
  CHECK(draft.value().project.id() != before.id());
  CHECK(runtime.revision() == beforeRevision);
  CHECK(runtime.projectCopy().id() == before.id());

  // A rejection is simply not calling accept: the song the host holds is untouched.
  CHECK(runtime.revision() == beforeRevision);
  CHECK(runtime.projectCopy().vocalTracks().size() == before.vocalTracks().size());
  CHECK(runtime.trackId() == beforeTrack);

  // Acceptance adopts the draft, and the adopted document is unsaved: an import is a new document,
  // not an overwrite of whatever the host previously had open.
  const auto accepted = runtime.acceptInterchangeImport(draft.value());
  CHECK(accepted.hasValue());
  if (!accepted) return;
  const auto after = runtime.projectCopy();
  CHECK(after.id() == draft.value().project.id());
  CHECK(runtime.trackId().valid());
  CHECK(runtime.regionId().valid());
  // The imported document is selectable and drawable, which is what makes it usable rather than
  // merely stored: a portrait of it must render without throwing.
  runtime.resize(900.0, 560.0);
  native_ui::PixelSurface surface{900U, 560U};
  native_ui::RasterCanvas canvas{surface, 1.0};
  runtime.paint(canvas);
  CHECK(surface.checksum() != 0U);
}

TEST_CASE("CLAP interchange imports SMF and reports conversion losses instead of truncating them") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-smf");
  auto runtime = runtimeFixture();

  // The file is written with the interchange codec rather than by hand, so the test cannot pass on a
  // malformed fixture. It carries a lyric and a nondefault velocity and channel, which the subset
  // cannot represent, so the importer owes an explicit loss report rather than silence.
  interchange::SmfScore score;
  score.ppq = 480U;
  score.tempos = {{time::Tick{0}, 120.0}};
  score.meters = {{time::Tick{0}, 4U, 2U}};
  score.texts = {{time::Tick{0}, "a", true}};
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 90U, 2U}};
  const auto encoded = interchange::encodeSmf(score);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  const auto source = root / "phrase.mid";
  {
    std::ofstream out{source, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(encoded.value().data()),
              static_cast<std::streamsize>(encoded.value().size()));
  }
  authoring::InterchangeImportRequest request;
  request.format = authoring::InterchangeFormat::Smf;
  request.projectName = "Imported MIDI";
  const auto before = runtime.projectCopy();
  const auto draft = runtime.prepareInterchangeImport(source, request);
  CHECK(draft.hasValue());
  if (!draft) return;
  CHECK(draft.value().format == authoring::InterchangeFormat::Smf);
  // The draft has to be a usable project, and the live song is still the one the host had.
  CHECK(draft.value().project.vocalTracks().size() == before.vocalTracks().size());
  CHECK(runtime.projectCopy().id() == before.id());
  // The nondefault velocity and channel the subset cannot carry must be disclosed, not dropped quietly.
  CHECK(!draft.value().issues.empty());
  // Every reported issue names where it came from, so a review surface can point at the source.
  for (const auto& issue : draft.value().issues) CHECK(!issue.path.empty());
  CHECK(runtime.acceptInterchangeImport(draft.value()).hasValue());
  CHECK(runtime.projectCopy().id() == draft.value().project.id());
}
