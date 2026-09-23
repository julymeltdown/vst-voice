#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/interchange/smf_codec.hpp"
#include "seam/interchange/ustx_codec.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/unit_selection.hpp"

#include <chrono>
#include <atomic>
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

// The embedded surface reaches interchange only through host handoffs, because a plugin cannot own a
// file dialog. These cases cover the states a host actually drives: no chooser connected, the user
// cancelling, the reviewer declining, the reviewer accepting, and an unsupported extension.
TEST_CASE("CLAP interchange handoffs keep the song unchanged when unconnected, cancelled or declined") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-handoff");
  auto runtime = runtimeFixture();
  const auto before = runtime.projectCopy();
  const auto beforeRevision = runtime.revision();

  // An offered action with no chooser must say so rather than report a success that did nothing.
  const auto unconnectedImport = runtime.requestInterchangeImport();
  CHECK(!unconnectedImport);
  CHECK(unconnectedImport.error().message.find("host file chooser") != std::string::npos);
  const auto unconnectedExport = runtime.requestInterchangeExport();
  CHECK(!unconnectedExport);
  CHECK(unconnectedExport.error().message.find("host file chooser") != std::string::npos);
  CHECK(runtime.revision() == beforeRevision);

  // Write a real score to import, then drive the handoff.
  const auto source = root / "score.ustx";
  authoring::InterchangeExportRequest exportRequest;
  exportRequest.format = authoring::InterchangeFormat::Ustx;
  exportRequest.destination = source;
  CHECK(runtime.exportInterchange(exportRequest).hasValue());

  // A cancelled chooser is the user closing the dialog: nothing happens and nothing is an error.
  bool importReviewCalled = false;
  runtime.setInterchangeImportHandoff([] { return std::optional<std::filesystem::path>{}; });
  runtime.setInterchangeReviewHandoff([&](const authoring::InterchangeImportDraft&) {
    importReviewCalled = true;
    return true;
  });
  const auto cancelled = runtime.requestInterchangeImport();
  CHECK(cancelled.hasValue());
  // The review is not reached when the user cancels, so no conversion work or decision is implied.
  CHECK(!importReviewCalled);
  CHECK(runtime.revision() == beforeRevision);
  CHECK(runtime.projectCopy().id() == before.id());

  // A declined review keeps the song the creator already had, and is not an error.
  runtime.setInterchangeImportHandoff([&] {
    return std::optional<std::filesystem::path>{source};
  });
  authoring::InterchangeImportDraft reviewedDraft;
  runtime.setInterchangeReviewHandoff([&](const authoring::InterchangeImportDraft& draft) {
    reviewedDraft = draft;
    return false;
  });
  const auto declined = runtime.requestInterchangeImport();
  CHECK(declined.hasValue());
  CHECK(!reviewedDraft.project.vocalTracks().empty());
  CHECK(runtime.revision() == beforeRevision);
  CHECK(runtime.projectCopy().id() == before.id());

  // Accepting adopts the reviewed draft, and the live document is now the imported one.
  runtime.setInterchangeReviewHandoff([](const authoring::InterchangeImportDraft&) { return true; });
  const auto accepted = runtime.requestInterchangeImport();
  CHECK(accepted.hasValue());
  CHECK(runtime.projectCopy().id() == reviewedDraft.project.id());

  // An export to an extension the interchange subset cannot write is refused, and a cancellation is
  // not an error.
  runtime.setInterchangeExportHandoff([] { return std::optional<std::filesystem::path>{}; });
  CHECK(runtime.requestInterchangeExport().hasValue());
  runtime.setInterchangeExportHandoff([&] {
    return std::optional<std::filesystem::path>{root / "score.txt"};
  });
  const auto unsupported = runtime.requestInterchangeExport();
  CHECK(!unsupported);
  CHECK(unsupported.error().message.find("ustx") != std::string::npos);
  CHECK(!std::filesystem::exists(root / "score.txt"));

  // A MIDI destination is accepted and writes a real file.
  const auto midi = root / "score.mid";
  runtime.setInterchangeExportHandoff([&] {
    return std::optional<std::filesystem::path>{midi};
  });
  const auto unreviewed = runtime.requestInterchangeExport();
  CHECK(!unreviewed);
  CHECK(unreviewed.error().code == core::ErrorCode::Unsupported);
  CHECK(!std::filesystem::exists(midi));
  std::size_t exportReviews = 0U;
  runtime.setInterchangeExportReviewHandoff(
      [&](const authoring::InterchangeExportDraft& draft) -> core::Result<bool> {
        ++exportReviews;
        CHECK(!std::filesystem::exists(draft.destination));
        CHECK(!draft.bytes.empty());
        return exportReviews != 1U;
      });
  CHECK(runtime.requestInterchangeExport().hasValue());
  CHECK(exportReviews == 1U);
  CHECK(!std::filesystem::exists(midi));
  const auto exportedMidi = runtime.requestInterchangeExport();
  if (!exportedMidi) throw std::runtime_error(exportedMidi.error().message);
  CHECK(exportedMidi.hasValue());
  CHECK(exportReviews == 2U);
  CHECK(std::filesystem::exists(midi));
  CHECK(std::filesystem::file_size(midi) > 14U);
}

TEST_CASE("CLAP interchange refuses stale picker and review approvals") {
  using namespace seam;
  for (const bool mutateDuringReview : {false, true}) {
    const auto root = test::support::temporaryDirectory("clap-interchange-stale-approval");
    auto runtime = runtimeFixture();
    const auto original = runtime.projectCopy();
    const auto source = root / "score.ustx";
    CHECK(runtime.exportInterchange({
        .format = authoring::InterchangeFormat::Ustx,
        .destination = source}).hasValue());

    std::uint64_t newerRevision = 0U;
    const auto makeNewerDocument = [&] {
      auto current = runtime.projectCopy();
      CHECK(runtime.replaceProject(std::move(current)).hasValue());
      newerRevision = runtime.revision();
    };
    bool reviewCalled = false;
    runtime.setInterchangeImportHandoff([&] {
      if (!mutateDuringReview) makeNewerDocument();
      return std::optional<std::filesystem::path>{source};
    });
    runtime.setInterchangeReviewHandoff([&](const authoring::InterchangeImportDraft&) {
      reviewCalled = true;
      if (mutateDuringReview) makeNewerDocument();
      return true;
    });

    const auto imported = runtime.requestInterchangeImport();
    CHECK(!imported);
    CHECK(imported.error().code == core::ErrorCode::Conflict);
    CHECK(reviewCalled == mutateDuringReview);
    CHECK(runtime.revision() == newerRevision);
    CHECK(runtime.projectCopy().id() == original.id());
  }
}

TEST_CASE("CLAP interchange export refuses a stale picker and accepts uppercase score suffixes") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-stale-export");
  auto runtime = runtimeFixture();
  const auto destination = root / "score.USTX";
  const auto earlierRevision = runtime.revision();
  runtime.setInterchangeExportHandoff([&] {
    auto current = runtime.projectCopy();
    CHECK(runtime.replaceProject(std::move(current)).hasValue());
    return std::optional<std::filesystem::path>{destination};
  });
  const auto refused = runtime.requestInterchangeExport();
  CHECK(!refused);
  CHECK(refused.error().code == core::ErrorCode::Conflict);
  CHECK(runtime.revision() > earlierRevision);
  CHECK(!std::filesystem::exists(destination));

  runtime.setInterchangeExportHandoff([&] {
    return std::optional<std::filesystem::path>{destination};
  });
  runtime.setInterchangeExportReviewHandoff(
      [](const authoring::InterchangeExportDraft&) { return true; });
  const auto exported = runtime.requestInterchangeExport();
  if (!exported) throw std::runtime_error(exported.error().message);
  CHECK(exported.hasValue());
  CHECK(std::filesystem::exists(destination));
  CHECK(std::filesystem::file_size(destination) > 0U);
}

TEST_CASE("CLAP interchange export rejects stale review approval before writing") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-stale-export-review");
  auto runtime = runtimeFixture();
  const auto destination = root / "score.mid";
  runtime.setInterchangeExportHandoff([&] {
    return std::optional<std::filesystem::path>{destination};
  });
  runtime.setInterchangeExportReviewHandoff(
      [&](const authoring::InterchangeExportDraft& draft) -> core::Result<bool> {
        CHECK(!std::filesystem::exists(draft.destination));
        auto current = runtime.projectCopy();
        CHECK(runtime.replaceProject(std::move(current)).hasValue());
        return true;
      });
  const auto stale = runtime.requestInterchangeExport();
  CHECK(!stale);
  CHECK(stale.error().code == core::ErrorCode::Conflict);
  CHECK(!std::filesystem::exists(destination));
}

TEST_CASE("CLAP persistent state changes signal revisions and direct bounce settings only once") {
  using namespace seam;
  auto runtime = runtimeFixture();
  std::atomic<std::uint32_t> signals{0U};
  runtime.setPersistentStateChangeCallback([&] {
    signals.fetch_add(1U, std::memory_order_relaxed);
  });
  runtime.resize(780.0, 560.0);
  CHECK(signals.load(std::memory_order_relaxed) == 0U);

  runtime.setOfflineTimingAuthority(clap_editor::OfflineTimingAuthority::FollowHost);
  CHECK(signals.load(std::memory_order_relaxed) == 1U);
  runtime.setOfflineTimingAuthority(clap_editor::OfflineTimingAuthority::FollowHost);
  CHECK(signals.load(std::memory_order_relaxed) == 1U);

  const auto project = runtime.projectCopy();
  const auto* track = project.findVocalTrack(runtime.trackId());
  CHECK(track != nullptr);
  if (track == nullptr) return;
  CHECK(runtime.setTrackMix(track->id, track->gainDb + 1.0F,
                            track->pan, track->muted, track->solo).hasValue());
  CHECK(signals.load(std::memory_order_relaxed) == 2U);
  runtime.setOfflineTimingAuthority(clap_editor::OfflineTimingAuthority::FixedAudio);
  CHECK(signals.load(std::memory_order_relaxed) == 3U);
}

// A conversion that lost information must not reach the live document without a human decision. The
// embedded runtime has to fail closed when no review surface is connected, because a host that never
// wired one would otherwise adopt a lossy score silently.
TEST_CASE("CLAP interchange refuses a lossy draft when no review surface is connected") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-fail-closed");
  auto runtime = runtimeFixture();
  const auto before = runtime.projectCopy();
  const auto beforeRevision = runtime.revision();

  // A score whose nondefault velocity and channel the interchange subset drops: the conversion owes
  // an explicit loss, so it is exactly the case that must not be adopted unreviewed.
  interchange::SmfScore score;
  score.ppq = 480U;
  score.tempos = {{time::Tick{0}, 120.0}};
  score.meters = {{time::Tick{0}, 4U, 2U}};
  score.texts = {{time::Tick{0}, "a", true}};
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 90U, 2U}};
  const auto encoded = interchange::encodeSmf(score);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  const auto source = root / "lossy.mid";
  {
    std::ofstream out{source, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(encoded.value().data()),
              static_cast<std::streamsize>(encoded.value().size()));
  }

  runtime.setInterchangeImportHandoff([&] {
    return std::optional<std::filesystem::path>{source};
  });
  // No review handoff is set. The draft has losses, so the import must be refused and the song kept.
  const auto refused = runtime.requestInterchangeImport();
  CHECK(!refused);
  CHECK(refused.error().message.find("review surface") != std::string::npos);
  CHECK(runtime.revision() == beforeRevision);
  CHECK(runtime.projectCopy().id() == before.id());
  CHECK(runtime.projectCopy().vocalTracks().size() == before.vocalTracks().size());

  // With the review connected and accepting, the same file is imported: the refusal was about the
  // missing decision, not about the file.
  runtime.setInterchangeReviewHandoff([](const authoring::InterchangeImportDraft& draft) {
    // The losses are still reported to the reviewer; acceptance is an informed choice.
    return std::any_of(draft.issues.begin(), draft.issues.end(),
                       [](const authoring::InterchangeIssue& issue) { return issue.loss; });
  });
  const auto accepted = runtime.requestInterchangeImport();
  CHECK(accepted.hasValue());
  CHECK(runtime.projectCopy().id() != before.id());
}

TEST_CASE("CLAP interchange requires review even when conversion reports no losses") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-lossless-review");
  auto runtime = runtimeFixture();
  const auto original = runtime.projectCopy();
  const auto revision = runtime.revision();
  const auto source = root / "score.ustx";
  interchange::UstxDocument score;
  score.name = "Neutral score";
  score.tempos.push_back({time::Tick{0}, 120.0});
  score.meters.push_back({0, 4U, 4U});
  score.tracks.push_back({.name = "Lead"});
  interchange::UstxPart part;
  part.name = "Verse";
  part.duration = time::Tick{960};
  interchange::UstxNote note;
  note.position = time::Tick{0};
  note.duration = time::Tick{480};
  note.lyric = "a";
  note.snapFirst = false;
  part.notes.push_back(std::move(note));
  score.parts.push_back(std::move(part));
  const auto encoded = interchange::encodeUstx(score);
  CHECK(encoded);
  if (!encoded) return;
  {
    std::ofstream out{source, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(encoded.value().data()),
              static_cast<std::streamsize>(encoded.value().size()));
  }
  const auto draft = runtime.prepareInterchangeImport(source, {.projectName = "score"});
  CHECK(draft);
  if (!draft) return;
  CHECK(draft.value().issues.empty());
  runtime.setInterchangeImportHandoff([&] {
    return std::optional<std::filesystem::path>{source};
  });

  const auto refused = runtime.requestInterchangeImport();
  CHECK(!refused);
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("review surface") != std::string::npos);
  CHECK(runtime.revision() == revision);
  CHECK(runtime.projectCopy().id() == original.id());
}

// The embedded surface reaches interchange through its keyboard, so the shortcut itself is part of
// the contract: a wired runtime nobody can trigger is not a usable feature.
TEST_CASE("CLAP interchange shortcuts reach the host handoffs and leave other keys alone") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-shortcut");
  auto runtime = runtimeFixture();
  const auto score = root / "shortcut.ustx";
  // The shortcut must reach a real conversion, so a score exists to import.
  authoring::InterchangeExportRequest writeScore;
  writeScore.format = authoring::InterchangeFormat::Ustx;
  writeScore.destination = score;
  CHECK(runtime.exportInterchange(writeScore).hasValue());
  int importCalls = 0;
  int exportCalls = 0;
  runtime.setInterchangeImportHandoff([&] {
    ++importCalls;
    return std::optional<std::filesystem::path>{score};
  });
  runtime.setInterchangeExportHandoff([&] {
    ++exportCalls;
    return std::optional<std::filesystem::path>{};
  });
  runtime.setInterchangeReviewHandoff([](const authoring::InterchangeImportDraft&) { return true; });

  // A plain key must not be swallowed by the interchange path.
  runtime.keyDown({.key = native_ui::NativeKey::O, .modifiers = {}});
  runtime.keyDown({.key = native_ui::NativeKey::E, .modifiers = {.command = true}});
  CHECK(importCalls == 0);
  CHECK(exportCalls == 0);

  // Command-Shift-E writes a score.
  runtime.keyDown({.key = native_ui::NativeKey::E,
                   .modifiers = {.shift = true, .command = true}});
  CHECK(exportCalls == 1);
  // A repeat must not fire the dialog again while the key is held down.
  runtime.keyDown({.key = native_ui::NativeKey::E,
                   .modifiers = {.shift = true, .command = true}, .repeat = true});
  CHECK(exportCalls == 1);

  // Command-Shift-O opens one: the handoff runs, the conversion is reviewed and adopted.
  const auto before = runtime.projectCopy();
  runtime.keyDown({.key = native_ui::NativeKey::O,
                   .modifiers = {.shift = true, .command = true}});
  CHECK(importCalls == 1);
  CHECK(runtime.projectCopy().id() != before.id());
}

TEST_CASE("CLAP interchange shortcuts report failures but not cancellations") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("clap-interchange-shortcut-errors");
  auto runtime = runtimeFixture();
  const auto before = runtime.projectCopy();
  const auto revision = runtime.revision();
  std::vector<std::string> reported;
  runtime.setInterchangeErrorHandoff([&](std::string_view title, const core::Error& error) {
    reported.emplace_back(std::string{title} + ": " + error.message);
  });
  const auto importKey = native_ui::KeyEvent{
      .key = native_ui::NativeKey::O, .modifiers = {.shift = true, .command = true}};
  const auto exportKey = native_ui::KeyEvent{
      .key = native_ui::NativeKey::E, .modifiers = {.shift = true, .command = true}};

  runtime.setInterchangeImportHandoff([] {
    return std::optional<std::filesystem::path>{};
  });
  runtime.keyDown(importKey);
  CHECK(reported.empty());

  const auto malformed = root / "broken.ustx";
  { std::ofstream out{malformed}; out << "not a USTX score\n"; }
  runtime.setInterchangeImportHandoff([&] {
    return std::optional<std::filesystem::path>{malformed};
  });
  runtime.keyDown(importKey);
  CHECK(reported.size() == 1U);
  CHECK(reported.back().find("Could not import score") != std::string::npos);

  runtime.setInterchangeExportHandoff([&] {
    return std::optional<std::filesystem::path>{root / "invalid.txt"};
  });
  runtime.keyDown(exportKey);
  CHECK(reported.size() == 2U);
  CHECK(reported.back().find("Could not export score") != std::string::npos);
  CHECK(runtime.revision() == revision);
  CHECK(runtime.projectCopy() == before);
}
