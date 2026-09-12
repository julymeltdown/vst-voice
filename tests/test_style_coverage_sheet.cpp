#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/style_coverage_sheet.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/core/file_io.hpp"
#include <cstdlib>
#include <chrono>
#include <iostream>

namespace {
struct Fixture {
  seam::application::ProjectFactory factory{381000U};
  seam::domain::Project project{factory.createProject("Style sheet")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  seam::domain::RegionId region{factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{960})};
  seam::voicebank::VoicebankResolution bank;
  Fixture() {
    using namespace seam;
    auto original = test::support::makeUnit("a", {"a"}, "audio/a.wav", 60, voicebank::UnitKind::Sustain);
    auto soft = original; soft.id = "soft-a"; soft.style = "soft"; soft.enabled = false;
    auto manifest = test::support::makeManifest({original, soft}); manifest.styles = {"original", "soft"};
    bank.status = voicebank::VoicebankResolveStatus::Resolved;
    bank.candidate = voicebank::VoicebankCandidate{manifest, {}, std::string(64U, 'a'), voicebank::VoicebankTrust::TrustedInstalled, {}, {}};
    project.findVocalTrack(track)->voicebank = {manifest.id, manifest.version, bank.candidate->contentHash};
    project.findVocalTrack(track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"あ", domain::Language::Japanese);
    note.vibrato.enabled = true; project.findRegion(region)->lyrics = {lyric}; project.findRegion(region)->notes = {note};
  }
};
}

TEST_CASE("style coverage previews declared inventory and applies only explicit style intent with exact undo") {
  using namespace seam; Fixture f; application::EditorSession session{f.project};
  auto sheet = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(sheet);
  CHECK(sheet.value().styles().size() == 2U); CHECK(sheet.value().styles()[0].enabled == 1U);
  CHECK(sheet.value().styles()[1].enabled == 0U); CHECK(sheet.value().styles()[1].disabled == 1U);
  CHECK(sheet.value().coverage()); CHECK(sheet.value().coverage()->complete());
  CHECK(sheet.value().choose("soft")); CHECK(!sheet.value().choose("absent"));
  CHECK(sheet.value().selection().styleId == "soft"); CHECK(sheet.value().coverage()); CHECK(!sheet.value().coverage()->complete());
  CHECK(session.project() == f.project); CHECK(!session.canUndo());
  CHECK(sheet.value().apply(session, f.track, f.region, f.bank));
  auto expected = f.project; expected.findVocalTrack(f.track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "soft"};
  CHECK(session.project() == expected); CHECK(!sheet.value().choose("original"));
  CHECK(session.undo()); CHECK(session.project() == f.project); CHECK(session.redo()); CHECK(session.project() == expected);
  auto noop = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(noop);
  const auto revision = session.revision(); CHECK(noop.value().apply(session, f.track, f.region, f.bank)); CHECK(session.revision() == revision);
}

TEST_CASE("style sheet preserves missing choice and rejects stale untrusted and changed resources") {
  using namespace seam; Fixture f;
  f.project.findVocalTrack(f.track)->styleSelection = {};
  application::EditorSession session{f.project};
  auto sheet = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(sheet);
  CHECK(sheet.value().matches(session, f.track, f.region, f.bank)); CHECK(!sheet.value().coverage());
  CHECK(!sheet.value().apply(session, f.track, f.region, f.bank)); CHECK(sheet.value().choose("soft"));
  auto changed = f.bank; changed.candidate->trust = voicebank::VoicebankTrust::UntrustedInstalled;
  CHECK(!native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, changed)); CHECK(!sheet.value().apply(session, f.track, f.region, changed));
  changed = f.bank; changed.candidate->manifest.units[0].enabled = false;
  CHECK(!sheet.value().matches(session, f.track, f.region, changed));
  changed = f.bank; changed.candidate->contentHash = std::string(64U, 'b');
  CHECK(!sheet.value().apply(session, f.track, f.region, changed));
  CHECK(session.replaceProject(f.project)); CHECK(!sheet.value().apply(session, f.track, f.region, f.bank));
  auto fresh = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(fresh);
  fresh.value().cancel(); CHECK(!fresh.value().choose("original")); CHECK(session.project() == f.project);
}

TEST_CASE("style sheet does not invent coverage for empty unsupported or procedural inputs") {
  using namespace seam; Fixture f;
  f.bank.candidate->manifest.language = domain::Language::English;
  application::EditorSession session{f.project};
  auto language = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(language);
  CHECK(!language.value().coverage()); CHECK(language.value().diagnostic().find("language adapter") != std::string::npos);
  CHECK(language.value().choose("soft")); CHECK(language.value().apply(session, f.track, f.region, f.bank));
  f.bank.candidate->manifest.language = domain::Language::Japanese;
  f.project.findRegion(f.region)->notes.clear(); f.project.findRegion(f.region)->lyrics.clear();
  CHECK(session.replaceProject(f.project));
  auto empty = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(empty);
  CHECK(!empty.value().coverage()); CHECK(empty.value().diagnostic().find("No phonemes") != std::string::npos);
  auto oversized = f.bank; oversized.candidate->manifest.styles.resize(257U, "extra");
  const auto bounded = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, oversized);
  CHECK(!bounded); CHECK(bounded.error().message.find("256-style") != std::string::npos);
  session.project().findVocalTrack(f.track)->proceduralRecipe = domain::ProceduralRecipeReference{
      {domain::SingerResourceKind::Procedural, "recipe", "1", std::string(64U, 'a')}, "recipe.json", "neutral"};
  const auto procedural = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank);
  CHECK(!procedural); CHECK(procedural.error().message.find("recipe controls") != std::string::npos);
}

TEST_CASE("style coverage uses the selected English phonemizer when the bank and region agree") {
  using namespace seam;
  Fixture f;
  auto* region = f.project.findRegion(f.region);
  CHECK(region != nullptr);
  auto* note = f.project.findNote(region->notes.front().id);
  CHECK(note != nullptr);
  f.bank.candidate->manifest.language = domain::Language::English;
  f.bank.candidate->manifest.units.front().phones = {"ah1"};
  f.bank.candidate->manifest.units.front().id = "english-ah1";
  f.project.findVocalTrack(f.track)->voicebank.contentHash = f.bank.candidate->contentHash;
  auto* lyric = region->findLyric(note->lyricTokenId);
  CHECK(lyric != nullptr);
  lyric->language = domain::Language::English;
  lyric->surface = U"a";
  note->phoneticHint = "ah1";
  application::EditorSession session{f.project};
  const auto sheet = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank);
  CHECK(sheet);
  CHECK(sheet.value().coverage());
  CHECK(sheet.value().coverage()->complete());
}

TEST_CASE("native style sheet stages rows and rejects changed bank before exact apply undo") {
  using namespace seam; Fixture f; f.project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
  application::EditorSession session{f.project};
  native_ui::NativeEditorController controller{session, f.factory, f.region};
  CHECK(!controller.openStyleCoverageSheet());
  controller.setStyleBankResolver([&](domain::TrackId track) { CHECK(track == f.track); return f.bank; });
  controller.resize(960.0, 640.0); controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility("inspector.style", native_ui::SemanticAction::SetFocus));
  const auto* entry = controller.accessibilityTree().focusedNode(); CHECK(entry); CHECK(entry->id == "inspector.style");
  const auto bounds = entry->bounds;
  CHECK(controller.pointerDown({.position = {bounds.x + 5.0, bounds.y + 5.0}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.replacementReviewOpen());
  controller.resize(480.0, 320.0);
  controller.rebuildAccessibilityTree();
  CHECK(controller.accessibilityTree().root().name.find("Style") != std::string::npos);
  const auto oldRow = controller.accessibilityTree().root().children.at(1U).id;
  CHECK(controller.openReplacementRow(1U)); CHECK(session.project() == f.project);
  CHECK(!controller.dispatchAccessibility(oldRow, native_ui::SemanticAction::Activate));
  CHECK(controller.sceneState().replacementReview.summary.find("0/1") != std::string::npos);
  if (const auto* capture = std::getenv("SEAM_STYLE_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U, 320U}; native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(!controller.openReplacementRow(6U)); CHECK(!controller.openDynamicsInspector());
  auto trusted = f.bank; f.bank.candidate->trust = voicebank::VoicebankTrust::UntrustedInstalled;
  CHECK(!controller.replacementReviewAction(3U)); CHECK(session.project() == f.project);
  f.bank = trusted; CHECK(controller.replacementReviewAction(3U)); CHECK(!controller.replacementReviewOpen());
  auto expected = f.project; expected.findVocalTrack(f.track)->styleSelection = {domain::VoiceStyleOrigin::Explicit, "soft"};
  CHECK(session.project() == expected); CHECK(session.undo()); CHECK(session.project() == f.project);
  CHECK(session.redo()); CHECK(session.project() == expected);
  CHECK(controller.openStyleCoverageSheet()); CHECK(controller.openReplacementRow(0U));
  CHECK(session.replaceProject(expected)); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.replacementReviewAction(5U)); CHECK(controller.replacementReviewAction(4U));
  CHECK(controller.openDynamicsInspector()); CHECK(controller.replacementReviewAction(4U));
  CHECK(session.project() == expected);
}

TEST_CASE("native style sheet pages declared styles without selecting or applying on navigation") {
  using namespace seam; Fixture f;
  for (std::size_t i = 2U; i < 9U; ++i) f.bank.candidate->manifest.styles.push_back("style-" + std::to_string(i));
  application::EditorSession session{f.project}; native_ui::NativeEditorController controller{session, f.factory, f.region};
  controller.setStyleBankResolver([&](domain::TrackId) { return f.bank; });
  CHECK(controller.openStyleCoverageSheet()); CHECK(controller.replacementReviewAction(1U));
  CHECK(controller.sceneState().replacementReview.rows.size() == 3U);
  CHECK(!controller.openReplacementRow(3U)); CHECK(controller.openReplacementRow(2U));
  CHECK(session.project() == f.project); CHECK(controller.replacementReviewAction(0U));
  CHECK(controller.replacementReviewAction(3U));
  CHECK(session.project().findVocalTrack(f.track)->styleSelection.styleId == "style-8");
  CHECK(session.undo()); CHECK(session.project() == f.project);
}

TEST_CASE("native coverage issues expose full paged diagnostics without changing score or selection") {
  using namespace seam; Fixture f;
  for (std::int64_t i = 0; i < 7; ++i) {
    auto [lyric, note] = f.factory.makeNote(time::Tick{480 + i * 60}, time::Tick{60}, 60U, U"あ", domain::Language::Japanese);
    f.project.findRegion(f.region)->lyrics.push_back(lyric); f.project.findRegion(f.region)->notes.push_back(note);
  }
  application::EditorSession session{f.project};
  native_ui::NativeEditorController controller{session, f.factory, f.region}; controller.resize(480.0, 320.0);
  controller.setStyleBankResolver([&](domain::TrackId) { return f.bank; });
  CHECK(controller.openStyleCoverageSheet()); CHECK(controller.openReplacementRow(1U));
  CHECK(controller.replacementReviewAction(2U));
  CHECK(controller.sceneState().replacementReview.rows.at(0U).find("disabled") != std::string::npos);
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.sceneState().replacementReview.rows.size() == 2U);
  controller.rebuildAccessibilityTree(); const auto oldIssue = controller.accessibilityTree().root().children.at(1U).id;
  CHECK(controller.dispatchAccessibility(oldIssue, native_ui::SemanticAction::Activate));
  CHECK(!controller.dispatchAccessibility(oldIssue, native_ui::SemanticAction::Activate));
  CHECK(!controller.openReplacementRow(0U));
  if (const auto* capture = std::getenv("SEAM_STYLE_DETAIL_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U, 320U}; native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  std::string reconstructed;
  for (;;) {
    const auto view = controller.sceneState().replacementReview;
    CHECK(!view.rowsInspectable); CHECK(view.rows.size() <= 6U);
    for (const auto& line : view.rows) reconstructed += line;
    if (!view.enabled[1]) break;
    CHECK(controller.replacementReviewAction(1U));
  }
  CHECK(reconstructed.find("disabled-unit") != std::string::npos);
  CHECK(reconstructed.find("soft-a") != std::string::npos);
  CHECK(reconstructed.find(f.project.findRegion(f.region)->notes.back().id.toString()) != std::string::npos);
  CHECK(session.project() == f.project); CHECK(session.selection().empty()); CHECK(!session.canUndo());
  CHECK(controller.replacementReviewAction(2U)); // Detail -> original issue page.
  CHECK(controller.sceneState().replacementReview.rows.size() == 2U);
  CHECK(controller.sceneState().replacementReview.rows.at(0U).find("disabled") != std::string::npos);
  CHECK(controller.replacementReviewAction(2U)); // Issues -> styles, draft preserved.
  CHECK(controller.sceneState().replacementReview.rows.at(1U).starts_with("Selected:"));
  CHECK(controller.replacementReviewAction(2U)); CHECK(session.replaceProject(f.project));
  CHECK(!controller.openReplacementRow(0U)); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.replacementReviewAction(5U)); CHECK(controller.replacementReviewAction(2U));
  CHECK(controller.sceneState().replacementReview.rows.empty());
  CHECK(controller.sceneState().replacementReview.summary.find("audio QA still required") != std::string::npos);
  CHECK(controller.replacementReviewAction(4U));
}

TEST_CASE("native coverage unavailable explanation is fully readable and is never a successful empty report") {
  using namespace seam; Fixture f; f.bank.candidate->manifest.language = domain::Language::English;
  application::EditorSession session{f.project}; native_ui::NativeEditorController controller{session, f.factory, f.region};
  controller.setStyleBankResolver([&](domain::TrackId) { return f.bank; });
  CHECK(controller.openStyleCoverageSheet()); CHECK(controller.replacementReviewAction(2U));
  CHECK(controller.sceneState().replacementReview.summary.find("unavailable") != std::string::npos);
  CHECK(controller.openReplacementRow(0U));
  std::string explanation;
  for (;;) {
    const auto view = controller.sceneState().replacementReview;
    for (const auto& line : view.rows) explanation += line;
    if (!view.enabled[1]) break;
    CHECK(controller.replacementReviewAction(1U));
  }
  CHECK(explanation.find("language adapter") != std::string::npos);
  CHECK(session.project() == f.project); CHECK(!session.canUndo());
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(!controller.replacementReviewOpen());
}

TEST_CASE("style sheet maximum admitted inventory preserves counts and cached pronunciation across choices") {
  using namespace seam; Fixture f;
  auto& manifest = f.bank.candidate->manifest;
  for (std::size_t i = 2U; i < 256U; ++i) manifest.styles.push_back("style-" + std::to_string(i));
  const auto unit = manifest.units.front(); manifest.units.clear();
  for (std::size_t i = 0U; i < 16384U; ++i) {
    auto entry = unit; entry.id = "unit-" + std::to_string(i); entry.style = manifest.styles[i / 64U]; entry.enabled = i % 2U == 0U;
    manifest.units.push_back(std::move(entry));
  }
  application::EditorSession session{f.project};
  const auto started = std::chrono::steady_clock::now();
  auto sheet = native_ui::StyleCoverageSheet::prepare(session, f.track, f.region, f.bank); CHECK(sheet);
  const auto prepared = std::chrono::steady_clock::now();
  CHECK(sheet.value().styles().size() == 256U);
  for (std::size_t i = 0U; i < 256U; ++i) {
    CHECK(sheet.value().styles()[i].id == manifest.styles[i]);
    CHECK(sheet.value().styles()[i].enabled == 32U); CHECK(sheet.value().styles()[i].disabled == 32U);
  }
  CHECK(sheet.value().coverage()); CHECK(sheet.value().coverage()->complete());
  CHECK(sheet.value().choose("soft")); CHECK(sheet.value().coverage()); CHECK(sheet.value().coverage()->complete());
  const auto chosen = std::chrono::steady_clock::now();
  for (std::size_t i = 0U; i < 1000U; ++i) CHECK(sheet.value().choose("soft"));
  const auto repeated = std::chrono::steady_clock::now();
  CHECK(sheet.value().choose("original")); CHECK(!sheet.value().hasChanges());
  CHECK(session.project() == f.project); CHECK(!session.canUndo());
  native_ui::NativeEditorController controller{session, f.factory, f.region};
  controller.setStyleBankResolver([&](domain::TrackId) { return f.bank; });
  CHECK(controller.openStyleCoverageSheet());
  const auto viewStart = std::chrono::steady_clock::now();
  for (std::size_t i = 0U; i < 20U; ++i) CHECK(controller.sceneState().replacementReview.rows.size() == 6U);
  const auto ended = std::chrono::steady_clock::now();
  const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
  std::cout << "[style-capacity-ms] prepare=" << ms(prepared - started) << " choose=" << ms(chosen - prepared)
      << " same-choice-1000=" << ms(repeated - chosen) << " view-average=" << ms(ended - viewStart) / 20.0 << '\n';
  CHECK(controller.replacementReviewAction(4U));
  const auto root = test::support::temporaryDirectory("style-snapshot-capacity");
  const auto source = root / "source"; std::filesystem::create_directories(source / "audio");
  CHECK(voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U, test::support::sineWave(48000U, 220.0, 0.5)));
  CHECK(voicebank::ManifestJsonCodec{}.save(manifest, source / "manifest.json"));
  CHECK(core::durableAtomicWriteText(source / "license.txt", "Synthetic snapshot test fixture\n"));
  const auto key = distribution::generateSigningKeyPair(); CHECK(key);
  CHECK(distribution::packSeambank(source, root / "bank.seambank", key.value()));
  authoring::VoicebankSession banks({{root / "installed", voicebank::VoicebankRootKind::Installed}}, false);
  authoring::VoicebankInstallerService installer{banks, root / "installed"};
  const auto installed = installer.install({.packagePath = root / "bank.seambank", .trustedPublicKeys = {key.value().publicKey}}); CHECK(installed);
  auto installedProject = f.project; installedProject.findVocalTrack(f.track)->voicebank.contentHash = installed.value().candidate.contentHash;
  CHECK(session.replaceProject(installedProject));
  const auto snapshot = banks.resolveTrackSnapshot(session.project(), f.track); CHECK(snapshot); CHECK(snapshot->resolution().resolved());
  CHECK(snapshot == banks.resolveTrackSnapshot(session.project(), f.track));
  controller.setStyleBankSnapshotResolver([&](domain::TrackId id) { return banks.resolveTrackSnapshot(session.project(), id); });
  CHECK(controller.openStyleCoverageSheet());
  const auto snapshotStart = std::chrono::steady_clock::now();
  for (std::size_t i = 0U; i < 20U; ++i) CHECK(controller.sceneState().replacementReview.rows.size() == 6U);
  const auto snapshotEnd = std::chrono::steady_clock::now();
  std::cout << "[style-snapshot-view-ms] average=" << ms(snapshotEnd - snapshotStart) / 20.0 << '\n';
  CHECK(controller.openReplacementRow(1U));
  banks.setAllowDevelopmentFixtures(false); CHECK(snapshot == banks.resolveTrackSnapshot(session.project(), f.track));
  banks.setAllowDevelopmentFixtures(true); CHECK(snapshot != banks.resolveTrackSnapshot(session.project(), f.track));
  CHECK(!controller.replacementReviewAction(3U)); CHECK(session.project() == installedProject);
  CHECK(controller.replacementReviewAction(5U)); CHECK(controller.openReplacementRow(1U));
  const auto beforeRefresh = banks.resolveTrackSnapshot(session.project(), f.track);
  CHECK(banks.refresh()); CHECK(beforeRefresh != banks.resolveTrackSnapshot(session.project(), f.track));
  CHECK(beforeRefresh->resolution().candidate->manifest == manifest); // Retained immutable payload remains readable.
  CHECK(!controller.openReplacementRow(0U)); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.replacementReviewAction(5U)); CHECK(controller.openReplacementRow(1U)); CHECK(controller.replacementReviewAction(3U));
  CHECK(session.project().findVocalTrack(f.track)->styleSelection.styleId == "soft"); CHECK(session.undo()); CHECK(session.project() == installedProject);
  auto missing = installedProject; missing.findVocalTrack(f.track)->voicebank.contentHash = std::string(64U, 'f');
  const auto unavailable = banks.resolveTrackSnapshot(missing, f.track); CHECK(!unavailable->resolution().resolved());
  CHECK(banks.resolveTrackSnapshot(installedProject, f.track)->resolution().resolved());
  CHECK(controller.openStyleCoverageSheet()); CHECK(controller.openReplacementRow(1U));
  session.project().findVocalTrack(f.track)->name = "Direct unrevisioned edit";
  CHECK(!controller.sceneState().replacementReview.enabled[3]); CHECK(!controller.replacementReviewAction(3U));
  session.project().findVocalTrack(f.track)->name = installedProject.findVocalTrack(f.track)->name;
  const auto validSnapshot = banks.resolveTrackSnapshot(session.project(), f.track);
  const auto installedManifest = installed.value().candidate.bankRoot / "manifest.json";
  const auto manifestBytes = core::readTextFileLimited(installedManifest, 32U * 1024U * 1024U); CHECK(manifestBytes);
  CHECK(core::durableAtomicWriteText(installedManifest, "{ invalid fixture manifest"));
  CHECK(!banks.refresh());
  const auto failedSnapshot = banks.resolveTrackSnapshot(session.project(), f.track);
  CHECK(failedSnapshot != validSnapshot); CHECK(!failedSnapshot->resolution().resolved());
  CHECK(validSnapshot->resolution().resolved()); CHECK(!controller.replacementReviewAction(3U));
  CHECK(core::durableAtomicWriteText(installedManifest, manifestBytes.value())); CHECK(banks.refresh());
  CHECK(controller.replacementReviewAction(5U)); CHECK(controller.openReplacementRow(1U));
  CHECK(session.replaceProject(installedProject)); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.replacementReviewAction(4U));
}
