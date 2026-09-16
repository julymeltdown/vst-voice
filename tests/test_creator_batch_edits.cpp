#include "test_framework.hpp"
#include "seam/ui/note_search_model.hpp"
#include "seam/ui/note_search_navigation.hpp"
#include "seam/ui/note_search_job.hpp"
#include "seam/ui/lyric_replacement_review.hpp"
#include "seam/ui/lyric_replacement_job.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/ui/vibrato_clear_preview.hpp"
#include "seam/ui/vibrato_model.hpp"
#include "seam/ui/note_cleanup_preview.hpp"
#include "seam/ui/dynamics_clear_preview.hpp"
#include "seam/ui/dynamics_lane_model.hpp"
#include "seam/application/tempo_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include <chrono>
#include <iostream>
#include <limits>

TEST_CASE("dynamics lane drafts region points atomically and preserves unrelated intent through undo redo") {
  using namespace seam;
  application::ProjectFactory factory{311000U}; auto project = factory.createProject("Dynamics draft");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "A", time::Tick{0}, time::Tick{1920});
  const auto other = factory.addRegion(project, track, "B", time::Tick{1920}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"あ", domain::Language::Japanese);
  note.vibrato.enabled = true; note.phoneticHint = "a";
  auto* region = project.findRegion(regionId); region->lyrics = {lyric}; region->notes = {note};
  CHECK(region->pitchAutomation.upsert({time::Tick{0}, 12.0F}));
  CHECK(region->dynamicsAutomation.replacePoints({{time::Tick{0}, 0.25F}, {time::Tick{480}, 0.75F}}));
  CHECK(project.findRegion(other)->dynamicsAutomation.upsert({time::Tick{0}, 0.4F}));
  region->performance.ownership = {{domain::PerformanceChannel::Pitch, note.id, domain::ManualPerformanceMode::Replace, {}}};
  application::EditorSession session{std::move(project)}; const auto before = session.project();
  auto draft = ui::DynamicsLaneModel::prepare(session, regionId); CHECK(draft);
  CHECK(draft.value().curve().valueAt(time::Tick{240}) == 0.5F);
  CHECK(draft.value().move(time::Tick{480}, {time::Tick{960}, 1.0F}));
  CHECK(draft.value().upsert({time::Tick{480}, 0.0F}));
  const auto valid = draft.value().curve();
  CHECK(!draft.value().move(time::Tick{960}, {time::Tick{0}, 0.5F}));
  CHECK(!draft.value().move(time::Tick{960}, {time::Tick{-1}, 0.5F}));
  CHECK(!draft.value().move(time::Tick{500}, {time::Tick{600}, 0.5F}));
  CHECK(!draft.value().upsert({time::Tick{600}, std::numeric_limits<float>::quiet_NaN()}));
  CHECK(!draft.value().upsert({time::Tick{600}, domain::kMaximumDynamicsGain + 1.0F}));
  CHECK(!draft.value().erase(time::Tick{500})); CHECK(draft.value().curve() == valid);
  CHECK(session.project() == before); CHECK(!session.canUndo());
  CHECK(draft.value().erase(time::Tick{0}));
  CHECK(draft.value().curve().valueAt(time::Tick{0}) == 0.0F); // Endpoint extension is explicit region behavior.
  CHECK(draft.value().curve().valueAt(time::Tick{720}) == 0.5F);
  CHECK(draft.value().curve().valueAt(time::Tick{1500}) == 1.0F);
  session.selection().selectOnly(note.id); CHECK(draft.value().matches(session, regionId));
  CHECK(!draft.value().apply(session, other));
  auto expected = before; expected.findRegion(regionId)->dynamicsAutomation = draft.value().curve();
  CHECK(draft.value().apply(session, regionId)); CHECK(session.project() == expected);
  CHECK(!draft.value().reset()); CHECK(!draft.value().erase(time::Tick{480}));
  CHECK(!draft.value().apply(session, regionId));
  CHECK(session.undo()); CHECK(session.project() == before); CHECK(!session.canUndo());
  CHECK(session.redo()); CHECK(session.project() == expected);
}

TEST_CASE("dynamics lane reset noop cancellation generation and capacity guards are exact") {
  using namespace seam;
  application::ProjectFactory factory{321000U}; auto project = factory.createProject("Dynamics limits");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "A", time::Tick{0}, time::Tick{20000});
  std::vector<domain::DynamicsAutomationPoint> points;
  for (std::size_t i = 0; i < domain::kMaximumDynamicsPoints; ++i)
    points.push_back({time::Tick{static_cast<std::int64_t>(i)}, 1.0F});
  CHECK(project.findRegion(regionId)->dynamicsAutomation.replacePoints(std::move(points)));
  application::EditorSession session{std::move(project)}; const auto before = session.project();
  auto draft = ui::DynamicsLaneModel::prepare(session, regionId); CHECK(draft);
  CHECK(!draft.value().upsert({time::Tick{19000}, 0.5F})); CHECK(!draft.value().hasChanges());
  CHECK(draft.value().move(time::Tick{0}, {time::Tick{19000}, 0.5F}));
  CHECK(draft.value().curve().points().size() == domain::kMaximumDynamicsPoints);
  CHECK(draft.value().reset()); CHECK(!draft.value().hasChanges());
  CHECK(draft.value().apply(session, regionId)); CHECK(!session.canUndo());
  auto stale = ui::DynamicsLaneModel::prepare(session, regionId); CHECK(stale);
  CHECK(stale.value().upsert({time::Tick{0}, 0.0F}));
  CHECK(session.replaceProject(before)); CHECK(!stale.value().matches(session, regionId));
  CHECK(!stale.value().apply(session, regionId)); CHECK(session.project() == before);
  auto cancelled = ui::DynamicsLaneModel::prepare(session, regionId); CHECK(cancelled);
  std::stop_source stop; stop.request_stop();
  CHECK(!ui::DynamicsLaneModel::prepare(session, regionId, stop.get_token()));
  CHECK(!cancelled.value().apply(session, regionId, stop.get_token()));
  cancelled.value().cancel(); CHECK(!cancelled.value().upsert({time::Tick{0}, 0.0F}));
  CHECK(!cancelled.value().apply(session, regionId)); CHECK(!session.canUndo());
}

TEST_CASE("performance expression indexing rejects ambiguous missing and repeated note targets atomically") {
  using namespace seam;
  application::ProjectFactory factory{301000U}; auto project = factory.createProject("Expression index");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto a = factory.addRegion(project, track, "A", time::Tick{0}, time::Tick{960});
  const auto b = factory.addRegion(project, track, "B", time::Tick{960}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la", domain::Language::English);
  for (const auto region : {a,b}) { project.findRegion(region)->lyrics = {lyric}; project.findRegion(region)->notes = {note}; }
  auto vibrato = note.vibrato; vibrato.enabled = true;
  const application::NoteExpressionEdit edit{note.id, vibrato, note.phoneticHint}; const auto ambiguous = project;
  application::EditPerformanceCommand command{{edit}}; CHECK(!command.apply(project)); CHECK(project == ambiguous); CHECK(!command.revert(project));
  project.findRegion(b)->notes.clear(); const auto unique = project;
  application::EditPerformanceCommand repeated{{edit,edit}}; CHECK(!repeated.apply(project)); CHECK(project == unique);
  auto missing = edit; missing.noteId = domain::NoteId{999999U};
  application::EditPerformanceCommand absent{{edit,missing}}; CHECK(!absent.apply(project)); CHECK(project == unique);
  application::EditPerformanceCommand valid{{edit}}; CHECK(valid.apply(project)); CHECK(project.findNote(note.id)->vibrato.enabled);
  CHECK(valid.revert(project)); CHECK(project == unique);
}

TEST_CASE("vibrato applies 10000 selected notes with exact undo redo and reports local timings") {
  using namespace seam;
  application::ProjectFactory factory{291000U}; auto project = factory.createProject("Large vibrato edit");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{20000});
  std::vector<domain::NoteId> selected;
  for (int i = 0; i < 10000; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i}, time::Tick{1}, 60U, U"la", domain::Language::English);
    selected.push_back(note.id); project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)}; session.selection().replace(selected); const auto before = session.project();
  const auto start = std::chrono::steady_clock::now();
  auto preview = ui::VibratoModel::prepare(session, regionId, {.enabled = true, .depthCents = 75.0F}); CHECK(preview);
  CHECK(preview.value().edits().size() == 10000U); const auto prepared = std::chrono::steady_clock::now();
  CHECK(preview.value().apply(session, regionId)); const auto applied = std::chrono::steady_clock::now();
  const auto after = session.project();
  for (const auto& note : after.findRegion(regionId)->notes) { CHECK(note.vibrato.enabled); CHECK(note.vibrato.depthCents == 75.0F); }
  CHECK(session.undo()); const auto undone = std::chrono::steady_clock::now(); CHECK(session.project() == before);
  CHECK(session.redo()); const auto redone = std::chrono::steady_clock::now(); CHECK(session.project() == after);
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::cout << "large-vibrato-ms prepare=" << ms(start, prepared) << " apply=" << ms(prepared, applied)
      << " undo=" << ms(applied, undone) << " redo=" << ms(undone, redone) << '\n';
}

TEST_CASE("vibrato selection previews mixed values and patches only explicit fields in one undo group") {
  using namespace seam;
  application::ProjectFactory factory{281000U}; auto project = factory.createProject("Mixed vibrato");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto* region = project.findRegion(regionId); std::vector<domain::NoteId> ids;
  for (int i = 0; i < 3; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i * 480}, time::Tick{480}, 60U, U"か", domain::Language::Japanese);
    note.phoneticHint = "k a"; note.vibrato.enabled = i == 1;
    note.vibrato.depthCents = i == 0 ? 20.0F : 60.0F; note.vibrato.phaseTurns = i == 0 ? 0.1F : 0.7F;
    if (i == 1) note.vibrato.fadeOutFraction = 0.5F;
    ids.push_back(note.id); region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  CHECK(region->pitchAutomation.upsert({time::Tick{120}, 12.0F})); CHECK(region->dynamicsAutomation.upsert({time::Tick{0}, 0.3F}));
  region->performance.ownership = {{domain::PerformanceChannel::Pitch, ids[0], domain::ManualPerformanceMode::Replace, {}}};
  region->unitSelectionOverrides = {{.startKey = {ids[0], 0U}, .tokenCount = 2U, .unitId = "ka"}};
  region->seamOverrides = {{.incomingStartKey = {ids[0], 1U}, .seamAmount = 0.3F}};
  application::EditorSession session{std::move(project)}; session.selection().replace({ids[1], ids[0]});
  const auto before = session.project();
  auto preview = ui::VibratoModel::prepare(session, regionId, {.enabled = true, .depthCents = 80.0F}); CHECK(preview);
  CHECK(preview.value().selectedCount() == 2U); CHECK(preview.value().edits().size() == 2U);
  CHECK(!preview.value().values().enabled); CHECK(!preview.value().values().depthCents); CHECK(!preview.value().values().phaseTurns);
  CHECK(preview.value().values().periodMilliseconds == 180.0F); CHECK(preview.value().edits()[0].noteId == ids[0]);
  CHECK(session.project() == before);
  CHECK(!ui::VibratoModel::prepare(session, regionId, {.fadeInFraction = 0.6F})); // Invalid only for the second target's retained fade-out.
  CHECK(!ui::VibratoModel::prepare(session, regionId, {.depthCents = std::numeric_limits<float>::quiet_NaN()}));
  CHECK(session.project() == before);
  CHECK(preview.value().apply(session, regionId));
  auto expected = before;
  for (std::size_t i = 0U; i < 2U; ++i) { auto* note = expected.findNote(ids[i]); note->vibrato.enabled = true; note->vibrato.depthCents = 80.0F; }
  CHECK(session.project() == expected); CHECK(session.revision() == 1U); CHECK(!preview.value().apply(session, regionId));
  const auto baselinePerformance = synthesis::compileScorePerformance(before, *before.findRegion(regionId), 48000U); CHECK(baselinePerformance);
  const auto editedPerformance = synthesis::compileScorePerformance(session.project(), *session.project().findRegion(regionId), 48000U); CHECK(editedPerformance);
  const auto& firstSpan = baselinePerformance.value().notes().front();
  const auto frame = firstSpan.startFrame + (firstSpan.endFrame - firstSpan.startFrame) * 4 / 5;
  CHECK(baselinePerformance.value().at(frame).vibratoCents == 0.0);
  CHECK(editedPerformance.value().at(frame).vibratoCents != 0.0);
  CHECK(editedPerformance.value().at(frame).scoreFrequencyHz != baselinePerformance.value().at(frame).scoreFrequencyHz);
  CHECK(session.undo()); CHECK(session.project() == before); CHECK(session.redo()); CHECK(session.project() == expected);
  auto noop = ui::VibratoModel::prepare(session, regionId, {.enabled = true, .depthCents = 80.0F}); CHECK(noop); CHECK(noop.value().edits().empty());
  const auto revision = session.revision(); CHECK(noop.value().apply(session, regionId)); CHECK(session.revision() == revision);
  auto stale = ui::VibratoModel::prepare(session, regionId, {.enabled = false}); CHECK(stale);
  session.selection().selectOnly(ids[2]); CHECK(!stale.value().apply(session, regionId)); CHECK(session.project() == expected);
  session.selection().replace({ids[0], ids[1]});
  auto replaced = ui::VibratoModel::prepare(session, regionId, {.depthCents = 0.0F}); CHECK(replaced);
  CHECK(session.replaceProject(expected)); session.selection().replace({ids[0], ids[1]});
  CHECK(!replaced.value().matches(session, regionId)); CHECK(!replaced.value().apply(session, regionId));
  auto cancelled = ui::VibratoModel::prepare(session, regionId); CHECK(cancelled); cancelled.value().cancel();
  CHECK(!cancelled.value().apply(session, regionId)); CHECK(session.project() == expected);
}

TEST_CASE("10000-note Japanese derived search preserves CV output context and bounded rejection") {
  using namespace seam;
  application::ProjectFactory factory{241000U}; auto project = factory.createProject("Large Japanese Find");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{20000});
  auto* region = project.findRegion(regionId);
  for (int i = 0; i < 10000; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i}, time::Tick{1}, 60U,
        i == 9999 ? U"ー" : U"か", domain::Language::Japanese);
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  const auto before = project;
  const auto started = std::chrono::steady_clock::now();
  const auto resolved = phonemizer::resolveJapanesePronunciation(*region); CHECK(resolved);
  CHECK(resolved.value().pronunciation.tokens.size() == 19999U);
  CHECK(resolved.value().pronunciation.tokens.back().symbol == "a");
  CHECK(resolved.value().pronunciation.tokens.back().key.noteId == region->notes.back().id);
  const auto found = ui::NoteSearchModel::search(project, regionId, 1U, "a", ui::NoteSearchField::GeneratedPhoneme); CHECK(found);
  CHECK(found.value().scannedNotes == 10000U); CHECK(found.value().hits.size() == 10000U);
  CHECK(found.value().hits.front().matchedText == U"k a"); CHECK(found.value().hits.back().matchedText == U"a");
  const auto warnings = ui::NoteSearchModel::search(project, regionId, 1U, "Unsupported", ui::NoteSearchField::PronunciationDiagnostic); CHECK(warnings);
  CHECK(warnings.value().scannedNotes == 10000U); CHECK(warnings.value().hits.empty()); CHECK(project == before);
  std::cout << "large-japanese-resolve-and-two-searches-ms=" <<
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
  application::EditorSession session{project}; ui::NoteSearchJob job;
  CHECK(job.start(session, regionId, "a", ui::NoteSearchField::GeneratedPhoneme));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (job.preparing() && std::chrono::steady_clock::now() < deadline) {
    CHECK(job.poll(session, regionId)); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(!job.preparing()); auto navigation = job.takeReady(); CHECK(navigation);
  CHECK(navigation->select(session, regionId, 9999U).value() == region->notes.back().id);
  CHECK(session.project() == before); CHECK(session.revision() == 0U);
  auto [extraLyric, extraNote] = factory.makeNote(time::Tick{10000}, time::Tick{1}, 60U, U"か", domain::Language::Japanese);
  region->lyrics.push_back(extraLyric); region->notes.push_back(extraNote);
  CHECK(!phonemizer::resolveJapanesePronunciation(*region)); region->lyrics.pop_back(); region->notes.pop_back();
  // Bounded source text can still expand past the output-token budget: reject it.
  for (auto& lyric : region->lyrics) lyric.surface = U"かかかか";
  const auto excessive = phonemizer::resolveJapanesePronunciation(*region); CHECK(!excessive);
  CHECK(excessive.error().message == "Resolved pronunciation exceeds token bounds");
  CHECK(!ui::NoteSearchModel::search(project, regionId, 1U, "a", ui::NoteSearchField::GeneratedPhoneme));
}

TEST_CASE("cancellable pronunciation preserves contextual output and override order without source mutation") {
  using namespace seam;
  application::ProjectFactory factory{231000U}; auto project = factory.createProject("Cancellable pronunciation");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"か", domain::Language::Japanese);
  auto [continuationLyric, continuation] = factory.makeNote(time::Tick{480}, time::Tick{480}, 60U, U"ー", domain::Language::Japanese);
  auto* region = project.findRegion(regionId); region->lyrics = {lyric, continuationLyric}; region->notes = {continuation, note};
  region->phonemeOverrides = {{.key = {note.id, 2U}, .symbol = "i"}, {.key = {note.id, 2U}, .symbol = "u"}};
  const auto before = *region;
  phonemizer::JapaneseKanaPhonemizer adapter;
  const auto ordinary = adapter.phonemize(*region);
  const auto cancellable = adapter.phonemize(*region, {}); CHECK(cancellable);
  CHECK(cancellable.value().tokens == ordinary.tokens); CHECK(cancellable.value().warnings == ordinary.warnings);
  CHECK(ordinary.tokens.size() == 4U); CHECK(ordinary.tokens[0].symbol == "k"); CHECK(ordinary.tokens[1].symbol == "a");
  CHECK(ordinary.tokens[2].symbol == "u"); CHECK(ordinary.tokens[3].symbol == "u"); // Ordered overrides still feed continuation.
  CHECK(adapter.phonemize(*region, {}, 4U));
  CHECK(!adapter.phonemize(*region, {}, 3U)); CHECK(!adapter.phonemize(*region, {}, 0U));
  const auto resolved = phonemizer::resolveJapanesePronunciation(*region); CHECK(resolved);
  const auto stoppable = phonemizer::resolveJapanesePronunciation(*region, {}); CHECK(stoppable);
  CHECK(stoppable.value().identity == resolved.value().identity);
  CHECK(stoppable.value().pronunciation.tokens == resolved.value().pronunciation.tokens);
  std::stop_source stop; stop.request_stop();
  const auto cancelledAdapter = adapter.phonemize(*region, stop.get_token()); CHECK(!cancelledAdapter);
  CHECK(cancelledAdapter.error().code == core::ErrorCode::Conflict);
  CHECK(!phonemizer::resolveJapanesePronunciation(*region, stop.get_token()));
  CHECK(*region == before);
}

TEST_CASE("Find jobs publish source-bound results and retire cancellation without stale selection") {
  using namespace seam;
  application::ProjectFactory factory{221000U}; auto project = factory.createProject("Find worker");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{20000});
  for (int i = 0; i < 10000; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i}, time::Tick{1}, 60U, U"la", domain::Language::English);
    project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)}; const auto source = session.project();
  ui::NoteSearchJob job;
  const auto retire = [&] {
    auto result = core::success(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (job.preparing() && std::chrono::steady_clock::now() < deadline) {
      result = job.poll(session, region);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(!job.preparing()); return result;
  };
  CHECK(job.start(session, region, "la", ui::NoteSearchField::Lyric)); CHECK(job.preparing()); CHECK(!job.takeReady());
  CHECK(!job.start(session, region, "other", ui::NoteSearchField::Lyric));
  CHECK(retire()); auto ready = job.takeReady(); CHECK(ready); CHECK(!job.takeReady());
  CHECK(ready->result().hits.size() == 10000U); CHECK(session.project() == source); CHECK(session.selection().empty());
  CHECK(job.start(session, region, "la", ui::NoteSearchField::Lyric)); job.cancel();
  CHECK(retire()); CHECK(job.state() == ui::NoteSearchJob::State::Cancelled); CHECK(!job.takeReady());
  CHECK(job.start(session, region, "la", ui::NoteSearchField::Lyric));
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!retire()); CHECK(job.state() == ui::NoteSearchJob::State::Failed); CHECK(!job.takeReady());
  CHECK(job.start(session, region, "la", ui::NoteSearchField::Lyric));
  const auto same = session.project(); CHECK(session.replaceProject(same)); CHECK(!retire()); CHECK(!job.takeReady());
  CHECK(job.start(session, region, std::string(1U, static_cast<char>(0xff)), ui::NoteSearchField::GeneratedPhoneme)); // Worker-side malformed input is an error, not zero matches.
  CHECK(!retire()); CHECK(!job.takeReady());
  CHECK(job.start(session, region, "la", ui::NoteSearchField::Lyric)); CHECK(retire());
  CHECK(!job.start(session, region, "", ui::NoteSearchField::Lyric)); CHECK(!job.takeReady()); // Invalid new request invalidates old Ready data.
  CHECK(session.project() == same); CHECK(session.selection().empty());
}

TEST_CASE("captured Find navigation wraps without musical history and rejects stale or cancelled targets") {
  using namespace seam;
  application::ProjectFactory factory{211000U}; auto project = factory.createProject("Find navigation");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  std::vector<domain::NoteId> ids;
  for (const auto start : {960,0,480}) {
    auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{240}, 60U, U"la", domain::Language::English);
    ids.push_back(note.id); project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)}; const auto before = session.project();
  auto find = ui::NoteSearchNavigation::prepare(session, regionId, "la", ui::NoteSearchField::Lyric); CHECK(find);
  CHECK(!find.value().currentIndex()); CHECK(session.selection().empty());
  CHECK(find.value().step(session, regionId).value() == ids[1]);
  CHECK(find.value().step(session, regionId).value() == ids[2]);
  CHECK(find.value().step(session, regionId).value() == ids[0]);
  CHECK(find.value().step(session, regionId).value() == ids[1]); // Forward wrap.
  CHECK(find.value().step(session, regionId, true).value() == ids[0]); // Backward wrap.
  CHECK(find.value().currentIndex() == 2U); CHECK(session.selection().noteIds() == std::vector<domain::NoteId>{ids[0]});
  CHECK(session.project() == before); CHECK(session.revision() == 0U); CHECK(!session.undo());
  std::stop_source stop; stop.request_stop();
  CHECK(!find.value().step(session, regionId, false, stop.get_token())); CHECK(find.value().currentIndex() == 2U);
  CHECK(!find.value().select(session, {}, 0U)); CHECK(!find.value().select(session, regionId, 3U));
  CHECK(session.selection().noteIds() == std::vector<domain::NoteId>{ids[0]});
  auto backwards = ui::NoteSearchNavigation::prepare(session, regionId, "la", ui::NoteSearchField::Lyric); CHECK(backwards);
  CHECK(backwards.value().step(session, regionId, true).value() == ids[0]);
  backwards.value().close(); CHECK(!backwards.value().step(session, regionId));
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!find.value().matches(session, regionId)); CHECK(!find.value().step(session, regionId));
  auto refreshed = ui::NoteSearchNavigation::prepare(session, regionId, "la", ui::NoteSearchField::Lyric); CHECK(refreshed);
  CHECK(refreshed.value().select(session, regionId, 1U).value() == ids[2]);
  const auto identical = session.project(); CHECK(session.replaceProject(identical));
  const auto selection = session.selection().noteIds();
  CHECK(!refreshed.value().matches(session, regionId)); CHECK(!refreshed.value().step(session, regionId));
  CHECK(session.selection().noteIds() == selection);
  auto empty = ui::NoteSearchNavigation::prepare(session, regionId, "absent", ui::NoteSearchField::Lyric); CHECK(empty);
  CHECK(!empty.value().step(session, regionId)); CHECK(!empty.value().currentIndex());
  CHECK(session.project() == identical);
}

TEST_CASE("region dynamics clearing preserves other regions expressions ownership and generated takes") {
  using namespace seam;
  application::ProjectFactory factory{181000U}; auto project = factory.createProject("Clear region dynamics");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  const auto otherId = factory.addRegion(project, track, "Other", time::Tick{9600}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"か", domain::Language::Japanese);
  note.vibrato.enabled = true; note.phoneticHint = "k a";
  auto* region = project.findRegion(regionId); region->lyrics = {lyric}; region->notes = {note};
  CHECK(region->pitchAutomation.upsert({time::Tick{120}, 18.0F}));
  CHECK(region->dynamicsAutomation.upsert({time::Tick{0}, 0.2F}));
  CHECK(region->dynamicsAutomation.upsert({time::Tick{480}, 1.8F}));
  CHECK(project.findRegion(otherId)->dynamicsAutomation.upsert({time::Tick{0}, 0.4F}));
  region->performance.ownership = {{domain::PerformanceChannel::Pitch, note.id, domain::ManualPerformanceMode::Replace, {}}};
  region->unitSelectionOverrides = {{.startKey = {note.id, 0U}, .tokenCount = 2U, .unitId = "ka"}};
  region->seamOverrides = {{.incomingStartKey = {note.id, 1U}, .seamAmount = 0.3F}};
  const auto pronunciation = phonemizer::resolveJapanesePronunciation(*region); CHECK(pronunciation);
  region->performance.pronunciation = pronunciation.value().identity;
  region->performance.takes = {{.id = "generated-dynamics", .sourceRegionId = regionId,
      .capturedRevision = region->performance.revision,
      .resource = {domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity, .generatorId = "fixture", .generatorVersion = "1",
      .range = {time::Tick{0}, time::Tick{960}},
      .lanes = {{domain::PerformanceChannel::Dynamics, {{time::Tick{0}, 0.7}}}}}};
  region->performance.accepted = {{"generated-dynamics", domain::PerformanceChannel::Dynamics, note.id, time::Tick{0}}};
  application::EditorSession session{std::move(project)}; const auto before = session.project();
  auto preview = ui::DynamicsClearPreview::prepare(session, regionId); CHECK(preview);
  CHECK(preview.value().regionNoteCount() == 1U); CHECK(preview.value().points().size() == 2U);
  CHECK(preview.value().retainedGeneratedSelections() == 1U); CHECK(session.project() == before);
  CHECK(!preview.value().apply(session, otherId));
  std::stop_source stop; stop.request_stop(); CHECK(!preview.value().apply(session, regionId, stop.get_token()));
  CHECK(preview.value().apply(session, regionId));
  auto expected = before; expected.findRegion(regionId)->dynamicsAutomation = {};
  CHECK(session.project() == expected);
  CHECK(session.project().findRegion(regionId)->dynamicsAutomation.valueAt(time::Tick{300}) == 1.0F);
  CHECK(session.undo()); CHECK(session.project() == before); CHECK(session.redo()); CHECK(session.project() == expected);
  auto noop = ui::DynamicsClearPreview::prepare(session, regionId); CHECK(noop); CHECK(!noop.value().hasChanges());
  const auto revision = session.revision(); CHECK(noop.value().apply(session, regionId)); CHECK(session.revision() == revision);
  auto stale = ui::DynamicsClearPreview::prepare(session, regionId); CHECK(stale);
  CHECK(session.replaceProject(expected)); CHECK(!stale.value().apply(session, regionId));
  auto cancelled = ui::DynamicsClearPreview::prepare(session, regionId); CHECK(cancelled); cancelled.value().cancel();
  CHECK(!cancelled.value().apply(session, regionId)); CHECK(session.project() == expected);
}

TEST_CASE("auto legato connects eligible selected pairs and preserves rests staccato and slur boundaries") {
  using namespace seam;
  application::ProjectFactory factory{161000U}; auto project = factory.createProject("Auto legato");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  project.settings().snapGrid = time::Tick{240};
  std::vector<domain::NoteId> ids;
  for (const auto start : {0,480,1440,1920,2400,2880,3360,3600}) {
    auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{240}, 60U, U"la", domain::Language::English);
    note.vibrato.enabled = true;
    if (start == 1920) note.articulation = domain::NoteArticulation::Staccato;
    if (start == 2400) note.slurGroup = 1U;
    if (start >= 2880) note.slurGroup = 2U;
    ids.push_back(note.id); project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)}; session.selection().replace(ids); const auto before = session.project();
  auto preview = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::AutoLegato); CHECK(preview);
  CHECK(preview.value().changedCount() == 5U); CHECK(session.project() == before);
  CHECK(preview.value().rows()[1].outcome == ui::NoteCleanupOutcome::ExplicitSeparation);
  CHECK(preview.value().rows()[2].outcome == ui::NoteCleanupOutcome::Staccato);
  CHECK(preview.value().rows()[4].outcome == ui::NoteCleanupOutcome::ExplicitSeparation);
  CHECK(preview.value().rows().back().beforeArticulation == domain::NoteArticulation::Normal);
  CHECK(preview.value().rows().back().afterArticulation == domain::NoteArticulation::Legato);
  CHECK(preview.value().apply(session, regionId)); CHECK(session.revision() == 1U);
  for (std::size_t i = 0U; i < ids.size(); ++i) {
    const auto* old = before.findNote(ids[i]); const auto* now = session.project().findNote(ids[i]);
    CHECK(now->startTick == old->startTick); CHECK(now->midiKey == old->midiKey);
    CHECK(now->lyricTokenId == old->lyricTokenId); CHECK(now->slurGroup == old->slurGroup); CHECK(now->vibrato == old->vibrato);
    CHECK(now->durationTick == time::Tick{(i == 0U || i == 5U) ? 480 : 240});
    CHECK(now->articulation == ((i == 0U || i == 1U || i >= 5U) ? domain::NoteArticulation::Legato : old->articulation));
  }
  const auto after = session.project(); CHECK(session.undo()); CHECK(session.project() == before);
  CHECK(session.redo()); CHECK(session.project() == after);
  auto noop = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::AutoLegato); CHECK(noop); CHECK(noop.value().changedCount() == 0U);
  const auto revision = session.revision(); CHECK(noop.value().apply(session, regionId)); CHECK(session.revision() == revision);
}

TEST_CASE("auto legato does not bridge unselected notes or polyphonic overlaps") {
  using namespace seam;
  application::ProjectFactory factory{171000U}; auto project = factory.createProject("Legato exclusions");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  project.settings().snapGrid = time::Tick{240}; std::vector<domain::NoteId> ids;
  for (const auto start : {0,480,960}) {
    auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{240}, 60U, U"la", domain::Language::English);
    if (start == 0) note.durationTick = time::Tick{1440};
    ids.push_back(note.id); project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)}; session.selection().replace(ids);
  auto overlapping = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::AutoLegato); CHECK(overlapping);
  CHECK(overlapping.value().changedCount() == 0U); CHECK(overlapping.value().rows()[1].outcome == ui::NoteCleanupOutcome::ExistingOverlap);
  session.project().findNote(ids[0])->durationTick = time::Tick{240};
  session.selection().remove(ids[1]);
  auto separated = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::AutoLegato); CHECK(separated);
  CHECK(separated.value().changedCount() == 0U); CHECK(separated.value().rows()[0].outcome == ui::NoteCleanupOutcome::UnselectedNeighbor);
}

TEST_CASE("dependency comparison distinguishes absence and rejects duplicate keys bounds and cancellation") {
  using namespace seam;
  domain::VocalRegion before; before.id = domain::RegionId{1U};
  before.phonemeOverrides = {{.key = {domain::NoteId{2U}, 0U}, .locked = true}};
  auto after = before; after.phonemeOverrides.clear();
  after.unitSelectionOverrides = {{.startKey = {domain::NoteId{2U}, 0U}, .unitId = "new", .unresolved = true}};
  auto outcomes = ui::compareDependentEdits(before, after); CHECK(outcomes); CHECK(outcomes.value().size() == 2U);
  CHECK(outcomes.value()[0].kind == ui::DependentEditKind::Phoneme);
  CHECK(outcomes.value()[0].beforeUnresolved == false); CHECK(!outcomes.value()[0].afterUnresolved.has_value());
  CHECK(outcomes.value()[1].kind == ui::DependentEditKind::Unit);
  CHECK(!outcomes.value()[1].beforeUnresolved.has_value()); CHECK(outcomes.value()[1].afterUnresolved == true);
  before.phonemeOverrides.push_back(before.phonemeOverrides.front()); CHECK(!ui::compareDependentEdits(before, after));
  before.phonemeOverrides.pop_back(); after.unitSelectionOverrides.push_back(after.unitSelectionOverrides.front());
  CHECK(!ui::compareDependentEdits(before, after));
  after.unitSelectionOverrides.pop_back();
  std::stop_source stop; stop.request_stop(); CHECK(!ui::compareDependentEdits(before, after, stop.get_token()));
  before.phonemeOverrides.resize(30001U); CHECK(!ui::compareDependentEdits(before, after));
}

TEST_CASE("overlap and auto-legato cleanup preview canonical melisma dependencies before publication") {
  using namespace seam;
  for (const auto kind : {ui::NoteCleanupKind::RemoveOverlap, ui::NoteCleanupKind::AutoLegato}) {
  application::ProjectFactory factory{151000U}; auto project = factory.createProject("Cleanup dependencies");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  project.settings().snapGrid = time::Tick{240};
  auto [lyric, first] = factory.makeNote(time::Tick{0}, kind == ui::NoteCleanupKind::RemoveOverlap ? time::Tick{720} : time::Tick{240}, 60U, U"か", domain::Language::Japanese);
  auto [unused, second] = factory.makeNote(time::Tick{480}, time::Tick{480}, 64U, U"か", domain::Language::Japanese);
  static_cast<void>(unused); second.lyricTokenId = lyric.id;
  if (kind == ui::NoteCleanupKind::RemoveOverlap) {
    first.articulation = domain::NoteArticulation::Legato; second.articulation = domain::NoteArticulation::Legato;
  }
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {first, second};
  application::UpsertPhonemeOverrideCommand lock{regionId,
      domain::PhonemeOverride{.key = {second.id, 0U}, .timing = {.startOffset = -1000}, .locked = true}};
  CHECK(lock.apply(project));
  project.findRegion(regionId)->unitSelectionOverrides = {{.startKey = {second.id, 0U}, .tokenCount = 2U, .unitId = "old-ka"}};
  project.findRegion(regionId)->seamOverrides = {{.incomingStartKey = {second.id, 1U}, .seamAmount = 0.3F}};
  application::EditorSession session{std::move(project)}; session.selection().replace({first.id, second.id});
  const auto before = session.project();
  auto preview = ui::NoteCleanupPreview::prepare(session, regionId, kind); CHECK(preview);
  CHECK(preview.value().changedCount() == (kind == ui::NoteCleanupKind::RemoveOverlap ? 1U : 2U)); CHECK(session.project() == before); CHECK(!session.canUndo());
  CHECK(preview.value().dependencies().size() == 3U);
  for (const auto& outcome : preview.value().dependencies()) {
    CHECK(outcome.beforeUnresolved == false); CHECK(outcome.afterUnresolved == true);
  }
  CHECK(preview.value().apply(session, regionId));
  const auto actual = ui::compareDependentEdits(*before.findRegion(regionId), *session.project().findRegion(regionId)); CHECK(actual);
  CHECK(std::vector<ui::DependentEditOutcome>(preview.value().dependencies().begin(), preview.value().dependencies().end()) == actual.value());
  CHECK(session.project().findRegion(regionId)->phonemeOverrides.front().unresolved);
  CHECK(session.undo()); CHECK(session.project() == before);
  }
}

TEST_CASE("note cleanup previews exact duration changes and uses canonical undoable timing edits") {
  using namespace seam;
  for (const auto kind : {ui::NoteCleanupKind::RemoveOverlap, ui::NoteCleanupKind::CloseGap}) {
    application::ProjectFactory factory{131000U}; auto project = factory.createProject("Cleanup");
    const auto track = factory.addVocalTrack(project, "Lead");
    const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
    project.settings().snapGrid = time::Tick{240};
    std::vector<domain::NoteId> ids;
    for (const auto& [start, duration] : std::vector<std::pair<int, int>>{{0,720},{480,240},{1200,240},{1550,100},{1920,240}}) {
      auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{duration}, 60U, U"la", domain::Language::English);
      note.vibrato.enabled = true; ids.push_back(note.id);
      project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
    }
    CHECK(project.findRegion(regionId)->pitchAutomation.upsert({time::Tick{120}, 18.0F}));
    application::EditorSession session{std::move(project)}; session.selection().replace(ids);
    const auto before = session.project();
    auto preview = ui::NoteCleanupPreview::prepare(session, regionId, kind); CHECK(preview);
    CHECK(session.project() == before); CHECK(preview.value().rows().size() == ids.size());
    CHECK(preview.value().changedCount() == (kind == ui::NoteCleanupKind::RemoveOverlap ? 1U : 2U));
    if (kind == ui::NoteCleanupKind::RemoveOverlap) CHECK(preview.value().rows()[0].afterDuration == time::Tick{480});
    else {
      CHECK(preview.value().rows()[1].afterDuration == time::Tick{720});
      CHECK(preview.value().rows()[2].outcome == ui::NoteCleanupOutcome::OffGridSuccessor);
      CHECK(preview.value().rows()[3].afterDuration == time::Tick{370});
    }
    CHECK(preview.value().rows().back().outcome == ui::NoteCleanupOutcome::NoSuccessor);
    std::vector<application::NoteResize> edits;
    for (const auto& row : preview.value().rows()) if (row.outcome == ui::NoteCleanupOutcome::Changed) {
      const auto start = before.findNote(row.noteId)->startTick;
      edits.push_back({row.noteId, start, row.beforeDuration, start, row.afterDuration});
    }
    auto expected = before; application::ResizeNotesCommand oracle{edits}; CHECK(oracle.apply(expected));
    std::stop_source stop; stop.request_stop(); CHECK(!preview.value().apply(session, regionId, stop.get_token()));
    CHECK(!preview.value().apply(session, domain::RegionId{999U}));
    auto copied = preview.value(); CHECK(preview.value().apply(session, regionId)); CHECK(session.project() == expected);
    CHECK(!preview.value().apply(session, regionId));
    auto noop = ui::NoteCleanupPreview::prepare(session, regionId, kind); CHECK(noop); CHECK(noop.value().changedCount() == 0U);
    const auto revision = session.revision(); CHECK(noop.value().apply(session, regionId)); CHECK(session.revision() == revision);
    CHECK(session.undo()); CHECK(session.project() == before); CHECK(!copied.apply(session, regionId));
    CHECK(session.redo()); CHECK(session.project() == expected);
  }
}

TEST_CASE("note cleanup reports ambiguous or unsafe pairs and rejects stale grids and overflowing notes") {
  using namespace seam;
  application::ProjectFactory factory{141000U}; auto project = factory.createProject("Cleanup boundaries");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  project.settings().snapGrid = time::Tick{240};
  std::vector<domain::NoteId> ids;
  for (const auto start : {0,100,480,480,960,1440,1920}) {
    auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{200}, 60U, U"la", domain::Language::English);
    if (start == 960) note.articulation = domain::NoteArticulation::Staccato;
    ids.push_back(note.id); project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)}; session.selection().replace(ids);
  auto overlap = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::RemoveOverlap); CHECK(overlap);
  CHECK(overlap.value().rows()[0].outcome == ui::NoteCleanupOutcome::NonPositiveDuration);
  CHECK(overlap.value().rows()[2].outcome == ui::NoteCleanupOutcome::SimultaneousStarts);
  CHECK(overlap.value().rows()[3].outcome == ui::NoteCleanupOutcome::SimultaneousStarts);
  session.selection().remove(ids.back());
  auto gap = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::CloseGap); CHECK(gap);
  CHECK(gap.value().rows()[4].outcome == ui::NoteCleanupOutcome::Staccato);
  CHECK(gap.value().rows()[5].outcome == ui::NoteCleanupOutcome::UnselectedNeighbor);
  const auto before = session.project(); gap.value().cancel(); CHECK(!gap.value().apply(session, regionId));
  auto stale = ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::RemoveOverlap); CHECK(stale);
  session.project().settings().snapGrid = time::Tick{480}; CHECK(!stale.value().apply(session, regionId));
  session.project().settings().snapGrid = time::Tick{240}; CHECK(session.project() == before);
  session.selection().add(domain::NoteId{999999U}); CHECK(!ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::CloseGap));
  session.selection().replace(ids);
  session.project().findNote(ids[0])->startTick = time::Tick{std::numeric_limits<std::int64_t>::max() - 1};
  session.project().findNote(ids[0])->durationTick = time::Tick{2};
  CHECK(!ui::NoteCleanupPreview::prepare(session, regionId, ui::NoteCleanupKind::CloseGap));
}

TEST_CASE("reviewed vibrato clear preserves every other musical field and rejects stale targets") {
  using namespace seam;
  application::ProjectFactory factory{121000U}; auto project = factory.createProject("Clear vibrato");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"か", domain::Language::Japanese);
  note.vibrato.enabled = true; note.vibrato.depthCents = 83.0F; note.vibrato.phaseTurns = 0.3F; note.phoneticHint = "k a";
  auto* region = project.findRegion(regionId); region->lyrics = {lyric}; region->notes = {note};
  CHECK(region->pitchAutomation.upsert({time::Tick{120}, 18.0F}));
  CHECK(region->dynamicsAutomation.upsert({time::Tick{0}, 0.8F}));
  region->performance.ownership = {{domain::PerformanceChannel::Pitch, note.id, domain::ManualPerformanceMode::Replace, {}}};
  region->unitSelectionOverrides = {{.startKey = {note.id, 0U}, .tokenCount = 2U, .unitId = "ka"}};
  region->seamOverrides = {{.incomingStartKey = {note.id, 1U}, .seamAmount = 0.3F}};
  auto [otherLyric, other] = factory.makeNote(time::Tick{1440}, time::Tick{960}, 64U, U"い", domain::Language::Japanese);
  other.vibrato.enabled = true; region->lyrics.push_back(otherLyric); region->notes.push_back(other);
  application::EditorSession session{std::move(project)}; session.selection().selectOnly(note.id);
  const auto before = session.project();
  auto preview = ui::VibratoClearPreview::prepare(session, regionId); CHECK(preview);
  CHECK(preview.value().selectedCount() == 1U); CHECK(preview.value().edits().size() == 1U);
  CHECK(preview.value().edits().front().after.depthCents == 83.0F); CHECK(session.project() == before);
  session.selection().clear(); CHECK(!preview.value().apply(session, regionId));
  session.selection().selectOnly(note.id);
  std::stop_source stop; stop.request_stop(); CHECK(!preview.value().apply(session, regionId, stop.get_token()));
  auto copiedPreview = preview.value();
  CHECK(preview.value().apply(session, regionId));
  auto expected = before; expected.findNote(note.id)->vibrato.enabled = false;
  CHECK(session.project() == expected); CHECK(!preview.value().apply(session, regionId));
  CHECK(session.undo()); CHECK(session.project() == before); CHECK(!copiedPreview.apply(session, regionId));
  CHECK(session.redo()); CHECK(session.project() == expected);
  auto noop = ui::VibratoClearPreview::prepare(session, regionId); CHECK(noop); CHECK(noop.value().edits().empty());
  const auto revision = session.revision(); CHECK(noop.value().apply(session, regionId)); CHECK(session.revision() == revision);
  CHECK(session.undo());
  auto stale = ui::VibratoClearPreview::prepare(session, regionId); CHECK(stale);
  const auto identical = session.project(); CHECK(session.replaceProject(identical)); session.selection().selectOnly(note.id);
  CHECK(!stale.value().apply(session, regionId));
  auto cancelled = ui::VibratoClearPreview::prepare(session, regionId); CHECK(cancelled);
  cancelled.value().cancel(); CHECK(!cancelled.value().apply(session, regionId));
  CHECK(!ui::VibratoClearPreview::prepare(session, regionId, stop.get_token()));
  session.selection().add(domain::NoteId{999999U}); CHECK(!ui::VibratoClearPreview::prepare(session, regionId));
  CHECK(session.project() == identical);
}

TEST_CASE("distribution planning and review remain immutable until explicit guarded application") {
  using namespace seam;
  application::ProjectFactory factory{111000U}; auto project = factory.createProject("Distribution review");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"la", domain::Language::English);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::EditorSession session{std::move(project)};
  session.selection().selectOnly(note.id); const auto before = session.project();
  auto plan = ui::PianoRollModel::planLyricDistribution(session.project(), regionId, {note.id}, U"li"); CHECK(plan);
  CHECK(!plan.value().report.committed); CHECK(plan.value().edits.size() == 1U);
  CHECK(session.project() == before); CHECK(!session.canUndo());
  auto preview = ui::NoteSearchModel::previewLyricDistribution(session.project(), regionId, session.revision(), {note.id}, "li"); CHECK(preview);
  session.selection().clear(); CHECK(!preview.value().apply(session));
  session.selection().selectOnly(note.id);
  ui::LyricReplacementJob job;
  const auto wait = [&]() -> core::Result<bool> {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
      const auto result = job.poll(session, regionId);
      if (!result || result.value()) return result;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw test::Failure{"Distribution review did not finish"};
  };
  CHECK(job.start(session, regionId, "", "li", session.selection().noteIds()));
  CHECK(wait()); CHECK(session.project() == before); CHECK(job.review());
  session.selection().clear(); CHECK(!job.apply(session, regionId));
  session.selection().selectOnly(note.id); CHECK(job.apply(session, regionId));
  CHECK(session.project().findRegion(regionId)->lyrics.front().surface == U"li");
  CHECK(session.project().findRegion(regionId)->lyrics.front().language == domain::Language::English);
  CHECK(session.undo()); CHECK(session.project() == before);
  CHECK(job.start(session, regionId, "", "too many", session.selection().noteIds()));
  CHECK(!wait()); CHECK(job.error()); CHECK(job.error()->context == "requested=2, target=1");
  CHECK(session.project() == before);
  CHECK(job.start(session, regionId, "", "li", session.selection().noteIds())); job.cancel();
  CHECK(wait()); CHECK(!job.review()); CHECK(session.project() == before);
}

TEST_CASE("replacement jobs prepare private snapshots and reject edits cancellation and document replacement") {
  using namespace seam;
  application::ProjectFactory factory{101000U}; auto project = factory.createProject("Async review");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"la", domain::Language::English);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::EditorSession session{std::move(project)};
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 100.0)));
  ui::LyricReplacementJob job;
  const auto awaitResult = [&]() -> core::Result<bool> {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
      auto polled = job.poll(session, regionId);
      if (!polled || polled.value()) return polled;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw test::Failure{"Replacement worker did not finish"};
  };
  CHECK(job.start(session, regionId, "la", "li"));
  CHECK(!job.start(session, regionId, "la", "lu"));
  CHECK(!job.apply(session, regionId));
  CHECK(awaitResult()); CHECK(job.state() == ui::LyricReplacementJob::State::Ready);
  CHECK(job.review()); CHECK(job.review()->preview().edits().front().before == U"la");
  CHECK(job.apply(session, regionId)); // Captured revision was nonzero, not a new session's zero.
  CHECK(session.project().findRegion(regionId)->lyrics.front().surface == U"li");
  CHECK(!job.apply(session, regionId)); CHECK(session.undo());
  CHECK(job.start(session, regionId, "la", "li"));
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!awaitResult()); CHECK(job.state() == ui::LyricReplacementJob::State::Failed);
  CHECK(!job.review()); CHECK(session.project().findRegion(regionId)->lyrics.front().surface == U"la");
  CHECK(job.start(session, regionId, "la", "li")); job.cancel();
  CHECK(awaitResult()); CHECK(job.state() == ui::LyricReplacementJob::State::Cancelled);
  CHECK(!job.review()); CHECK(!job.apply(session, regionId));
  CHECK(job.start(session, regionId, "la", "li")); CHECK(awaitResult());
  const auto identicalProject = session.project(); CHECK(session.replaceProject(identicalProject));
  CHECK(!job.apply(session, regionId)); // Same content still belongs to a replaced document generation.
  CHECK(job.start(session, regionId, "la", "li"));
  CHECK(session.replaceProject(identicalProject)); CHECK(!awaitResult());
  CHECK(job.start(session, regionId, "la", "li")); CHECK(awaitResult());
  CHECK(!job.start(session, regionId, "", "li"));
  CHECK(!job.review()); CHECK(!job.apply(session, regionId));
  CHECK(job.start(session, regionId, "la", "li")); CHECK(awaitResult());
  job.cancel(); CHECK(!job.review()); CHECK(!job.apply(session, regionId));
  CHECK(job.start(session, regionId, "la", std::string{"\xff"}));
  CHECK(!awaitResult()); CHECK(job.error());
  CHECK(session.project() == identicalProject);
  { ui::LyricReplacementJob retiring; CHECK(retiring.start(session, regionId, "la", "li")); }
  CHECK(session.project() == identicalProject); // Destruction joins before snapshot/result storage dies.
}

TEST_CASE("replacement review reports canonical dependency outcomes without publishing and rejects stale review") {
  using namespace seam;
  application::ProjectFactory factory{81000U}; auto project = factory.createProject("Review");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"き", domain::Language::Japanese);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::UpsertPhonemeOverrideCommand lock{regionId,
      domain::PhonemeOverride{.key = {note.id, 0U}, .timing = {.startOffset = -1000}, .locked = true}};
  CHECK(lock.apply(project));
  project.findRegion(regionId)->unitSelectionOverrides = {{.startKey = {note.id, 0U}, .tokenCount = 2U, .unitId = "old-ki"}};
  application::EditorSession session{std::move(project)};
  const auto before = session.project();
  auto review = ui::LyricReplacementReview::prepare(session, regionId, "き", "あ"); CHECK(review);
  CHECK(session.project() == before); CHECK(session.revision() == 0U);
  CHECK(review.value().preview().changedNotes() == 1U);
  CHECK(review.value().dependencyCount() == 2U);
  for (const auto& outcome : review.value().dependenciesPage(0U)) {
    CHECK(outcome.beforeUnresolved == false); CHECK(outcome.afterUnresolved == true);
  }
  CHECK(!review.value().apply(session, domain::RegionId{999U}));
  CHECK(review.value().apply(session, regionId));
  CHECK(review.value().state() == ui::LyricReplacementReview::State::Applied);
  CHECK(!review.value().apply(session, regionId));
  CHECK(session.project().findRegion(regionId)->phonemeOverrides.front().unresolved);
  CHECK(session.project().findRegion(regionId)->unitSelectionOverrides.front().unresolved);
  CHECK(session.undo()); CHECK(session.project() == before);
  CHECK(!review.value().apply(session, regionId));
  auto cancelled = ui::LyricReplacementReview::prepare(session, regionId, "き", "あ"); CHECK(cancelled);
  cancelled.value().cancel(); CHECK(!cancelled.value().apply(session, regionId));
  auto stale = ui::LyricReplacementReview::prepare(session, regionId, "き", "あ"); CHECK(stale);
  session.project().findNote(note.id)->phoneticHint = "k i";
  CHECK(!stale.value().matches(session, regionId)); CHECK(!stale.value().apply(session, regionId));
  std::stop_source stop; stop.request_stop();
  CHECK(!ui::LyricReplacementReview::prepare(session, regionId, "き", "あ", stop.get_token()));
}

TEST_CASE("replacement review pages every target safely and cancellation leaves a no-op history neutral") {
  using namespace seam;
  application::ProjectFactory factory{91000U}; auto project = factory.createProject("Review pages");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  for (int i = 0; i < 13; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i * 480}, time::Tick{480}, 60U, U"la", domain::Language::English);
    project.findRegion(regionId)->lyrics.push_back(lyric); project.findRegion(regionId)->notes.push_back(note);
  }
  application::EditorSession session{std::move(project)};
  auto review = ui::LyricReplacementReview::prepare(session, regionId, "la", "li"); CHECK(review);
  CHECK(review.value().editsPage(0U).size() == 6U); CHECK(review.value().editsPage(1U).size() == 6U);
  CHECK(review.value().editsPage(2U).size() == 1U); CHECK(review.value().editsPage(3U).empty());
  CHECK(review.value().editsPage(std::numeric_limits<std::size_t>::max()).empty());
  CHECK(review.value().dependenciesPage(std::numeric_limits<std::size_t>::max()).empty());
  CHECK(review.value().editsPage(2U).front().after == U"li");
  std::stop_source stop; stop.request_stop();
  CHECK(!review.value().apply(session, regionId, stop.get_token()));
  CHECK(review.value().state() == ui::LyricReplacementReview::State::Ready); CHECK(session.revision() == 0U);
  CHECK(review.value().apply(session, regionId)); CHECK(session.revision() == 1U);
  CHECK(session.undo()); CHECK(!session.undo());
  auto noop = ui::LyricReplacementReview::prepare(session, regionId, "la", "la"); CHECK(noop);
  const auto revision = session.revision();
  CHECK(noop.value().editsPage(0U).empty()); CHECK(noop.value().apply(session, regionId));
  CHECK(session.revision() == revision); CHECK(!noop.value().apply(session, regionId));
}

TEST_CASE("hint commands reconcile changed phones preserve displayed lyrics and undo exactly") {
  using namespace seam;
  application::ProjectFactory factory{71000U}; auto project = factory.createProject("Hint commands");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"き", domain::Language::Japanese);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::UpsertPhonemeOverrideCommand lock{regionId,
      domain::PhonemeOverride{.key = {note.id, 0U}, .timing = {.startOffset = -1000}, .locked = true}};
  CHECK(lock.apply(project));
  project.findRegion(regionId)->unitSelectionOverrides = {{.startKey = {note.id, 0U}, .tokenCount = 2U, .unitId = "old-ki"}};
  const auto before = project;
  application::EditorSession session{std::move(project)};
  CHECK(session.execute(std::make_unique<application::SetNoteHintsCommand>(
      std::vector<application::NoteHintEdit>{{note.id, std::nullopt, "sh a"}})));
  const auto* changed = session.project().findRegion(regionId);
  CHECK(changed->lyrics == before.findRegion(regionId)->lyrics);
  CHECK(changed->phonemeOverrides.size() == 1U); CHECK(changed->phonemeOverrides.front().unresolved);
  CHECK(changed->unitSelectionOverrides.front().unresolved);
  CHECK(changed->performance.revision.pronunciation == before.findRegion(regionId)->performance.revision.pronunciation + 1U);
  const auto phones = phonemizer::resolveJapanesePronunciation(*changed); CHECK(phones);
  CHECK(phones.value().pronunciation.tokens[0].symbol == "sh"); CHECK(phones.value().pronunciation.tokens[1].symbol == "a");
  CHECK(session.lastImpact().scope == application::CommandAudioImpact::PhraseAudio);
  const auto after = session.project(); CHECK(session.undo()); CHECK(session.project() == before);
  CHECK(session.redo()); CHECK(session.project() == after);
  CHECK(session.execute(std::make_unique<application::SetNoteHintsCommand>(
      std::vector<application::NoteHintEdit>{{note.id, "sh a", std::nullopt}})));
  CHECK(!session.project().findNote(note.id)->phoneticHint);
  CHECK(session.undo()); CHECK(session.project() == after);
}

TEST_CASE("hint commands reject stale unsupported and repeated targets before mutation") {
  using namespace seam;
  application::ProjectFactory factory{72000U}; auto project = factory.createProject("Hint admission");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"あ", domain::Language::Japanese);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  const auto before = project;
  for (const auto& edits : std::vector<std::vector<application::NoteHintEdit>>{
      {}, {{note.id, "a", "i"}}, {{note.id, std::nullopt, "unknown-phone"}},
      {{note.id, std::nullopt, "a"}, {note.id, std::nullopt, "i"}}}) {
    application::SetNoteHintsCommand command{edits}; CHECK(!command.apply(project)); CHECK(project == before);
  }
  project.findRegion(regionId)->lyrics.front().language = domain::Language::English;
  application::SetNoteHintsCommand unsupported{{{note.id, std::nullopt, "a"}}};
  CHECK(!unsupported.apply(project)); CHECK(!project.findNote(note.id)->phoneticHint);
}

TEST_CASE("hint commands validate and publish English and Korean pronunciation identities") {
  using namespace seam;
  struct Case final {
    domain::Language language;
    std::u32string lyric;
    std::string hint;
    std::string resolver;
  };
  for (const auto& value : std::vector<Case>{
      {domain::Language::English, U"a", "ah1", "seam-builtin-en"},
      {domain::Language::Korean, U"한", "h a n", "seam-builtin-ko"}}) {
    application::ProjectFactory factory{73000U + static_cast<unsigned>(value.language)};
    auto project = factory.createProject("Language hint command");
    const auto track = factory.addVocalTrack(project, "Lead");
    const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{960});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U,
        value.lyric, value.language);
    project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
    application::EditorSession session{std::move(project)};
    CHECK(session.execute(std::make_unique<application::SetNoteHintsCommand>(
        std::vector<application::NoteHintEdit>{{note.id, std::nullopt, value.hint}})));
    const auto* changed = session.project().findRegion(regionId);
    CHECK(changed != nullptr);
    CHECK(changed->findNote(note.id)->phoneticHint == value.hint);
    CHECK(changed->performance.pronunciation);
    CHECK(changed->performance.pronunciation->resolverId == value.resolver);
    CHECK(session.undo());
    CHECK(!session.project().findNote(note.id)->phoneticHint);
  }
}

TEST_CASE("reviewed replacement applies 10000 lyric edits as one exact undo redo transaction") {
  using namespace seam;
  application::ProjectFactory factory{51000U}; auto project = factory.createProject("Large replacement");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{20000});
  auto* region = project.findRegion(regionId);
  region->lyrics.reserve(10000U); region->notes.reserve(10000U);
  for (int i = 0; i < 10000; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i}, time::Tick{1}, 60U, U"la", domain::Language::English);
    region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note));
  }
  const auto before = project; application::EditorSession session{std::move(project)};
  const auto start = std::chrono::steady_clock::now();
  const auto preview = ui::NoteSearchModel::previewLyricReplacement(session, regionId, "la", "li"); CHECK(preview);
  const auto prepared = std::chrono::steady_clock::now();
  CHECK(preview.value().edits().size() == 10000U); CHECK(preview.value().changedNotes() == 10000U);
  CHECK(preview.value().apply(session)); const auto applied = std::chrono::steady_clock::now();
  CHECK(session.revision() == 1U);
  for (const auto& lyric : session.project().findRegion(regionId)->lyrics) CHECK(lyric.surface == U"li");
  const auto after = session.project();
  CHECK(session.undo()); const auto undone = std::chrono::steady_clock::now(); CHECK(session.project() == before);
  CHECK(!session.undo()); CHECK(session.redo()); const auto redone = std::chrono::steady_clock::now(); CHECK(session.project() == after);
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b-a).count(); };
  std::cout << "large-replacement-ms preview=" << ms(start, prepared) << " apply=" << ms(prepared, applied)
            << " undo=" << ms(applied, undone) << " redo=" << ms(undone, redone) << '\n';
  const auto reviewStart = std::chrono::steady_clock::now();
  auto review = ui::LyricReplacementReview::prepare(session, regionId, "li", "la"); CHECK(review);
  const auto reviewEnd = std::chrono::steady_clock::now();
  CHECK(review.value().editsPage(1666U).size() == 4U);
  CHECK(review.value().editsPage(1667U).empty());
  CHECK(review.value().apply(session, regionId));
  CHECK(session.project().findRegion(regionId)->lyrics == before.findRegion(regionId)->lyrics);
  CHECK(session.undo()); CHECK(session.project() == after);
  std::cout << "large-replacement-review-prepare-ms=" << ms(reviewStart, reviewEnd) << '\n';
  ui::PianoRollModel roll{session, factory, regionId};
  for (const auto& note : session.project().findRegion(regionId)->notes) session.selection().add(note.id);
  std::u32string distributedText; for (int i = 0; i < 10000; ++i) distributedText += U"la ";
  const auto distributionStart = std::chrono::steady_clock::now();
  const auto distribution = roll.distributeSelectedLyrics(distributedText); CHECK(distribution);
  const auto distributionEnd = std::chrono::steady_clock::now();
  CHECK(distribution.value().targetLyrics == 10000U); CHECK(distribution.value().changedLyrics == 10000U);
  CHECK(session.project().findRegion(regionId)->lyrics == before.findRegion(regionId)->lyrics);
  const auto acceptedRevision = session.revision();
  const auto noChange = roll.distributeSelectedLyrics(distributedText); CHECK(noChange);
  CHECK(noChange.value().changedLyrics == 0U); CHECK(session.revision() == acceptedRevision);
  CHECK(session.undo()); CHECK(session.project() == after);
  std::cout << "large-distribution-ms=" << ms(distributionStart, distributionEnd) << '\n';
}

TEST_CASE("batch lyric indexing rejects ambiguous targets without rejecting unrelated reused ids") {
  using namespace seam;
  application::ProjectFactory factory{61000U}; auto project = factory.createProject("Target index");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto first = factory.addRegion(project, track, "A", time::Tick{0}, time::Tick{960});
  const auto second = factory.addRegion(project, track, "B", time::Tick{960}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la", domain::Language::English);
  project.findRegion(first)->lyrics = {lyric}; project.findRegion(first)->notes = {note};
  project.findRegion(second)->lyrics = {lyric};
  application::BatchSetLyricsCommand ambiguous{{{lyric.id, U"la", U"li", lyric.language, lyric.language}}};
  const auto before = project; CHECK(!ambiguous.apply(project)); CHECK(project == before);
  auto [other, unusedNote] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"other", domain::Language::English);
  project.findRegion(second)->lyrics = {other}; project.findRegion(first)->lyrics.push_back(other);
  CHECK(project.validate());
  application::BatchSetLyricsCommand valid{{{lyric.id, U"la", U"li", lyric.language, lyric.language}}};
  const auto unrelated = project;
  CHECK(valid.apply(project)); CHECK(project.findRegion(second)->lyrics.front() == other);
  CHECK(valid.revert(project)); CHECK(project == unrelated);
}

TEST_CASE("reviewed lyric replacement deduplicates shared tokens and commits one undo group") {
  using namespace seam;
  application::ProjectFactory factory{41000U}; auto project = factory.createProject("Replace preview");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, a] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"aaaa", domain::Language::English);
  auto [unused, b] = factory.makeNote(time::Tick{480}, time::Tick{480}, 62U, U"unused", domain::Language::English);
  b.lyricTokenId = lyric.id; a.phoneticHint = "keep";
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {a,b};
  const auto before = project;
  application::EditorSession session{std::move(project)};
  const auto preview = ui::NoteSearchModel::previewLyricReplacement(session, regionId, "aa", "b"); CHECK(preview);
  CHECK(session.project() == before); CHECK(session.revision() == 0U);
  CHECK(preview.value().matchedNotes() == 2U); CHECK(preview.value().changedNotes() == 2U);
  CHECK(preview.value().edits().size() == 1U); CHECK(preview.value().replacements() == 2U);
  CHECK(preview.value().edits().front().after == U"bb");
  std::stop_source cancel; cancel.request_stop();
  CHECK(!preview.value().apply(session, cancel.get_token())); CHECK(session.project() == before);
  CHECK(preview.value().apply(session)); CHECK(session.revision() == 1U);
  CHECK(session.project().findRegion(regionId)->findLyric(lyric.id)->surface == U"bb");
  CHECK(session.project().findNote(a.id)->phoneticHint == a.phoneticHint);
  CHECK(session.project().findNote(b.id)->lyricTokenId == lyric.id);
  CHECK(!preview.value().apply(session));
  const auto replaced = session.project();
  CHECK(session.undo()); CHECK(session.project() == before);
  CHECK(!preview.value().apply(session));
  CHECK(session.redo()); CHECK(session.project() == replaced);
}

TEST_CASE("replacement preview rejects stale values empty outputs and expansion overflow") {
  using namespace seam;
  application::ProjectFactory factory{42000U}; auto project = factory.createProject("Replace bounds");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la", domain::Language::English);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::EditorSession session{std::move(project)};
  CHECK(!ui::NoteSearchModel::previewLyricReplacement(session, regionId, "", "new"));
  CHECK(!ui::NoteSearchModel::previewLyricReplacement(session, regionId, "la", ""));
  const auto noOp = ui::NoteSearchModel::previewLyricReplacement(session, regionId, "la", "la"); CHECK(noOp);
  CHECK(noOp.value().edits().empty()); CHECK(noOp.value().apply(session)); CHECK(session.revision() == 0U);
  const auto preview = ui::NoteSearchModel::previewLyricReplacement(session, regionId, "la", "li"); CHECK(preview);
  session.project().findRegion(regionId)->lyrics.front().surface = U"changed";
  CHECK(!preview.value().apply(session)); CHECK(session.revision() == 0U);
  session.project().findRegion(regionId)->lyrics.front().surface = std::u32string(16385U, U'a');
  CHECK(!ui::NoteSearchModel::previewLyricReplacement(session, regionId, "a", std::string(256U, 'b')));
  CHECK(session.project().findRegion(regionId)->lyrics.front().surface.size() == 16385U);
}

TEST_CASE("note search indexes IDs resolved phones and pronunciation warnings without rewriting lyrics") {
  using namespace seam;
  application::ProjectFactory factory{201000U}; auto project = factory.createProject("Derived search");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyricA, a] = factory.makeNote(time::Tick{960}, time::Tick{480}, 60U, U"か", domain::Language::Japanese);
  auto [lyricB, b] = factory.makeNote(time::Tick{0}, time::Tick{480}, 62U, U"か", domain::Language::Japanese);
  auto [lyricC, c] = factory.makeNote(time::Tick{1920}, time::Tick{480}, 64U, U"🌸", domain::Language::Japanese);
  a.phoneticHint = "sh a";
  auto* region = project.findRegion(regionId); region->lyrics = {lyricA, lyricB, lyricC}; region->notes = {c,a,b};
  const auto before = project;
  const auto ids = ui::NoteSearchModel::search(project, regionId, 7U, a.id.toString(), ui::NoteSearchField::NoteId); CHECK(ids);
  CHECK(ids.value().hits.size() == 1U); CHECK(ids.value().hits.front().noteId == a.id);
  CHECK(domain::toUtf8(ids.value().hits.front().matchedText) == a.id.toString());
  const auto phones = ui::NoteSearchModel::search(project, regionId, 7U, " a", ui::NoteSearchField::GeneratedPhoneme); CHECK(phones);
  CHECK(phones.value().hits.size() == 2U); CHECK(phones.value().hits[0].noteId == b.id);
  CHECK(phones.value().hits[0].matchedText == U"k a"); CHECK(phones.value().hits[1].matchedText == U"sh a");
  CHECK(phones.value().hits[1].firstMatch == 2U); CHECK(phones.value().field == ui::NoteSearchField::GeneratedPhoneme);
  const auto warnings = ui::NoteSearchModel::search(project, regionId, 7U, "Unsupported Japanese lyric character",
      ui::NoteSearchField::PronunciationDiagnostic); CHECK(warnings);
  CHECK(warnings.value().hits.size() == 1U); CHECK(warnings.value().hits.front().noteId == c.id);
  CHECK(project == before);
  std::stop_source stop; stop.request_stop();
  CHECK(!ui::NoteSearchModel::search(project, regionId, 7U, "a", ui::NoteSearchField::GeneratedPhoneme, stop.get_token()));
  CHECK(!ui::NoteSearchModel::search(project, regionId, 7U, "a", static_cast<ui::NoteSearchField>(999)));
  region->notes[1].phoneticHint = "invalid-phone";
  CHECK(!ui::NoteSearchModel::search(project, regionId, 7U, "a", ui::NoteSearchField::GeneratedPhoneme));
  CHECK(!ui::NoteSearchModel::search(project, regionId, 7U, "warning", ui::NoteSearchField::PronunciationDiagnostic));
  // Literal fields remain independently searchable when pronunciation cannot resolve.
  CHECK(ui::NoteSearchModel::search(project, regionId, 7U, a.id.toString(), ui::NoteSearchField::NoteId));
}

TEST_CASE("note search resolves generated phones through the region language service") {
  using namespace seam;
  application::ProjectFactory factory{202000U}; auto project = factory.createProject("English derived search");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"a", domain::Language::English);
  auto* region = project.findRegion(regionId); region->lyrics = {lyric}; region->notes = {note};

  const auto phones = ui::NoteSearchModel::search(project, regionId, 3U, "ah1",
      ui::NoteSearchField::GeneratedPhoneme);
  CHECK(phones);
  if (!phones) return;
  CHECK(phones.value().hits.size() == 1U);
  if (phones.value().hits.empty()) return;
  CHECK(phones.value().hits.front().noteId == note.id);
  CHECK(phones.value().hits.front().matchedText == U"ah1");
  CHECK(phones.value().hits.front().firstMatch == 0U);
}

TEST_CASE("note search separates Unicode lyrics and hints with stable musical ordering") {
  using namespace seam;
  application::ProjectFactory factory{21000U};
  auto project = factory.createProject("Search");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, a] = factory.makeNote(time::Tick{960}, time::Tick{480}, 60U, U"🌸歌", domain::Language::Japanese);
  auto [unused, b] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"unused", domain::Language::Japanese);
  b.lyricTokenId = lyric.id; a.phoneticHint = "ka";
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {a,b};
  const auto before = project;
  const auto found = ui::NoteSearchModel::search(project, regionId, 9U, "歌"); CHECK(found);
  CHECK(found.value().revision == 9U); CHECK(found.value().scannedNotes == 2U);
  CHECK(found.value().hits.size() == 2U); CHECK(found.value().hits[0].noteId == b.id);
  CHECK(found.value().hits[0].firstMatch == 1U); CHECK(found.value().hits[1].lyricId == lyric.id);
  CHECK(found.value().hits[0].matchedText == lyric.surface);
  const auto hint = ui::NoteSearchModel::search(project, regionId, 9U, "ka", ui::NoteSearchField::PronunciationHint); CHECK(hint);
  CHECK(hint.value().hits.size() == 1U); CHECK(hint.value().hits[0].noteId == a.id);
  const auto noMatch = ui::NoteSearchModel::search(project, regionId, 9U, "ka"); CHECK(noMatch); CHECK(noMatch.value().hits.empty());
  CHECK(project == before);
  CHECK(!ui::NoteSearchModel::search(project, regionId, 9U, ""));
  CHECK(!ui::NoteSearchModel::search(project, regionId, 9U, std::string(1, static_cast<char>(0xff))));
  CHECK(!ui::NoteSearchModel::search(project, regionId, 9U, std::string(257U, 'a')));
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!ui::NoteSearchModel::search(project, regionId, 9U, "歌", ui::NoteSearchField::Lyric, cancelled.get_token()));
}

TEST_CASE("note search admits 10000 notes and rejects oversized or malformed sources atomically") {
  using namespace seam;
  application::ProjectFactory factory{22000U}; auto project = factory.createProject("Bounded search");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{20000});
  auto* region = project.findRegion(regionId);
  for (int i = 0; i < 10000; ++i) {
    auto [lyric, note] = factory.makeNote(time::Tick{i}, time::Tick{1}, 60U, U"ababababac", domain::Language::English);
    region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note));
  }
  const auto found = ui::NoteSearchModel::search(project, regionId, 1U, "ababac"); CHECK(found);
  CHECK(found.value().hits.size() == 10000U); CHECK(found.value().hits.front().firstMatch == 4U);
  const auto ids = ui::NoteSearchModel::search(project, regionId, 1U, region->notes.back().id.toString(), ui::NoteSearchField::NoteId);
  CHECK(ids); CHECK(ids.value().scannedNotes == 10000U); CHECK(ids.value().hits.size() == 1U);
  // This fixture exceeds the resolver's aggregate referenced-text budget.
  // Do not misreport rejection as zero matches or split away phrase context.
  CHECK(!ui::NoteSearchModel::search(project, regionId, 1U, "a", ui::NoteSearchField::GeneratedPhoneme));
  CHECK(!ui::NoteSearchModel::search(project, regionId, 1U, "Unsupported", ui::NoteSearchField::PronunciationDiagnostic));
  {
    application::EditorSession session{project};
    const auto started = std::chrono::steady_clock::now();
    auto navigation = ui::NoteSearchNavigation::prepare(session, regionId, "ababac", ui::NoteSearchField::Lyric); CHECK(navigation);
    CHECK(navigation.value().result().hits.size() == 10000U);
    CHECK(navigation.value().select(session, regionId, 9999U).value() == region->notes.back().id);
    CHECK(navigation.value().step(session, regionId).value() == region->notes.front().id);
    CHECK(navigation.value().step(session, regionId, true).value() == region->notes.back().id);
    CHECK(session.project() == project); CHECK(session.revision() == 0U);
    std::cout << "large-find-capture-and-three-navigation-ms=" <<
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
  }
  region->notes.push_back(region->notes.back());
  CHECK(!ui::NoteSearchModel::search(project, regionId, 1U, "a")); region->notes.pop_back();
  region->lyrics.front().surface = std::u32string(ui::NoteSearchModel::maximumTextScalars + 1U, U'a');
  CHECK(!ui::NoteSearchModel::search(project, regionId, 1U, "a"));
  region->lyrics.front().surface = {U'a', static_cast<char32_t>(0xd800)};
  CHECK(!ui::NoteSearchModel::search(project, regionId, 1U, "a")); // Validate after an early hit too.
}
