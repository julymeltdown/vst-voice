#include "test_framework.hpp"

#include "seam/domain/performance_intent.hpp"
#include "seam/domain/project.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/application/arrangement_commands.hpp"
#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/core/file_io.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "test_support.hpp"

#include <limits>

namespace {

using namespace seam::domain;
using seam::time::Tick;

PronunciationIdentity pronunciation() {
  return {Language::Japanese, "seam-ja", "1", std::string(64U, 'a'),
          std::string(64U, 'b'), std::string(64U, 'c')};
}

PerformanceTake take(std::string id = "take-1") {
  return {.id = std::move(id), .sourceRegionId = RegionId{10U},
          .capturedRevision = {1U, 2U, 3U},
          .resource = {SingerResourceKind::Neural, "test-singer", "1", std::string(64U, 'd')},
          .pronunciation = pronunciation(), .generatorId = "test-generator",
          .generatorVersion = "1", .seed = std::numeric_limits<std::uint64_t>::max(),
          .range = {Tick{0}, Tick{1920}},
          .lanes = {{PerformanceChannel::Pitch, {{Tick{0}, 6000.0}, {Tick{960}, std::nullopt}}},
                    {PerformanceChannel::Dynamics, {{Tick{0}, 0.5}, {Tick{1920}, 1.0}}}}};
}

std::vector<Note> notes() {
  return {{.id = NoteId{1U}, .startTick = Tick{0}, .durationTick = Tick{480},
           .lyricTokenId = LyricTokenId{1U}},
          {.id = NoteId{2U}, .startTick = Tick{960}, .durationTick = Tick{480},
           .lyricTokenId = LyricTokenId{2U}}};
}

Project projectWithPerformance() {
  Project project{ProjectId{100U}, "Persisted take state"};
  VocalRegion region{.id = RegionId{10U}, .name = "Phrase", .durationTick = Tick{1920},
      .lyrics = {{LyricTokenId{1U}, U"あ", Language::Japanese},
                 {LyricTokenId{2U}, U"い", Language::Japanese}}, .notes = notes()};
  region.performance = {.revision = {9U, 9U, 9U}, .pronunciation = pronunciation(),
      .ownership = {{PerformanceChannel::Pitch, NoteId{1U}, ManualPerformanceMode::Replace, {1U, 2U, 3U}}},
      .takes = {take()}, .accepted = {{"take-1", PerformanceChannel::Dynamics, NoteId{1U}, Tick{0}}}};
  project.vocalTracks().push_back({.id = TrackId{20U}, .name = "Singer", .regions = {region}});
  return project;
}

}

TEST_CASE("stale proposals remain saveable without becoming accepted performance") {
  RegionPerformanceState state{.revision = {9U, 9U, 9U}, .pronunciation = pronunciation(),
                               .takes = {take()}};
  CHECK(state.validate(notes(), Tick{1920}));
  CHECK(state.accepted.empty());
  CHECK(!validatePerformanceAcceptanceRevision(state.takes.front().capturedRevision, state.revision));
  state.accepted.push_back({"take-1", PerformanceChannel::Dynamics, NoteId{1U}, Tick{0}});
  CHECK(state.validate(notes(), Tick{1920}));
  state.takes.front().state = PerformanceProposalState::Rejected;
  CHECK(!state.validate(notes(), Tick{1920}));
}

TEST_CASE("U3 coupled ownership hint style and expression roundtrip through project and plugin") {
  const auto before = projectWithPerformance();
  const auto& oldState = before.findRegion(RegionId{10U})->performance;
  auto ownership = oldState.ownership;
  ownership.push_back({PerformanceChannel::Dynamics, NoteId{2U}, ManualPerformanceMode::Replace, {}});
  DynamicsAutomation dynamics;
  CHECK(dynamics.replacePoints({{Tick{0}, 0.75F}, {Tick{1920}, 1.2F}}));
  NoteVibrato vibrato;
  vibrato.enabled = true;
  vibrato.depthCents = 71.0F;
  seam::application::EditorSession session{before};
  using namespace seam::application;
  CHECK(session.execute(std::make_unique<EditPerformanceCommand>(
      std::vector<NoteExpressionEdit>{{NoteId{2U}, vibrato, "i"}},
      std::vector<RegionDynamicsEdit>{{RegionId{10U}, dynamics}},
      std::vector<TrackStyleEdit>{{TrackId{20U}, {VoiceStyleOrigin::Explicit, "soft"}}},
      std::vector<RegionOwnershipEdit>{{RegionId{10U}, oldState.revision, oldState.ownership, ownership}})));
  const auto after = session.project();
  CHECK(session.revision() == 1U);
  CHECK(after.findNote(NoteId{2U})->phoneticHint == "i");
  CHECK(after.findNote(NoteId{2U})->vibrato == vibrato);
  CHECK(after.findRegion(RegionId{10U})->dynamicsAutomation == dynamics);
  CHECK(after.findRegion(RegionId{10U})->performance.revision.ownership == 10U);
  CHECK(after.findVocalTrack(TrackId{20U})->styleSelection.styleId == "soft");
  const auto root = seam::test::support::temporaryDirectory("u3-coupled-roundtrip");
  const auto roundtrip = [&](const Project& expected) {
    seam::formats::ProjectJsonCodec codec;
    CHECK(codec.save(expected, root / "coupled.seam"));
    const auto loaded = codec.load(root / "coupled.seam");
    CHECK(loaded);
    CHECK(loaded.value() == expected);
    const auto encoded = seam::clap_editor::encodeEditorState(expected);
    CHECK(encoded);
    const auto decoded = seam::clap_editor::decodeEditorState(encoded.value());
    CHECK(decoded);
    CHECK(decoded.value() == expected);
    seam::clap_editor::EditorRuntime runtime(expected, {},
        {{root / "absent-banks", seam::voicebank::VoicebankRootKind::Installed}});
    CHECK(runtime.replaceProject(decoded.value()));
    CHECK(runtime.projectCopy() == expected);
  };
  roundtrip(after);
  CHECK(session.undo());
  CHECK(session.project() == before);
  roundtrip(session.project());
  CHECK(session.redo());
  CHECK(session.project() == after);
  roundtrip(session.project());
}

TEST_CASE("U3 rejected opens preserve source bytes and the current document") {
  const auto before = projectWithPerformance();
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(before);
  CHECK(encoded);
  const auto root = seam::test::support::temporaryDirectory("u3-rejected-source");
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    auto tree = seam::formats::parseJson(encoded.value());
    CHECK(tree);
    if (scenario == 0U) *tree.value().find("schemaVersion") = seam::formats::JsonValue{std::int64_t{99}};
    if (scenario == 1U) {
      *tree.value().find("vocalTracks")->asArray().front().find("regions")->asArray().front()
          .find("notes")->asArray().front().find("vibrato")->find("depthCents") = seam::formats::JsonValue{-1.0};
    }
    auto bytes = seam::formats::stringifyJson(tree.value());
    if (scenario == 2U) {
      const auto key = bytes.find("\"depthCents\"");
      CHECK(key != std::string::npos);
      const auto start = bytes.find(':', key);
      const auto end = bytes.find_first_of(",}", start);
      CHECK(end != std::string::npos);
      bytes.replace(start + 1U, end - start - 1U, " 1e999");
    }
    const auto path = root / ("invalid-" + std::to_string(scenario) + ".seam");
    CHECK(seam::core::durableAtomicWriteText(path, bytes));
    seam::authoring::ProjectDocument document{before, seam::application::ProjectFactory{200U}};
    seam::authoring::ProjectLifecycleService lifecycle;
    const auto identity = document.identity().baseProjectHash;
    CHECK(!lifecycle.open(document, path));
    CHECK(document.session().project() == before);
    CHECK(document.session().revision() == 0U);
    CHECK(document.identity().baseProjectHash == identity);
    const auto retained = seam::core::readTextFileLimited(path, 1U << 20U);
    CHECK(retained);
    CHECK(retained.value() == bytes);
  }
}

TEST_CASE("live lyric command rebinds timing to the vowel and restores exact undo") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->phonemeOverrides = {{.key = {NoteId{1U}, 0U},
      .timing = {.startOffset = -1200, .endOffset = 2000}, .locked = true}};
  const auto before = project;
  seam::application::EditorSession session{project};
  const auto job = session.capturePerformanceJob();
  CHECK(job);
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"か", Language::Japanese)));
  const auto after = session.project();
  const auto* changed = after.findRegion(RegionId{10U});
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*changed);
  CHECK(resolved);
  CHECK(changed->performance.pronunciation == resolved.value().identity);
  CHECK(changed->performance.revision.pronunciation ==
        before.findRegion(RegionId{10U})->performance.revision.pronunciation + 1U);
  CHECK(changed->performance.revision.musical == before.findRegion(RegionId{10U})->performance.revision.musical);
  CHECK(changed->performance.revision.ownership == before.findRegion(RegionId{10U})->performance.revision.ownership);
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(after);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == after);
  CHECK(changed->phonemeOverrides.front().key.ordinal == 1U);
  CHECK(!changed->phonemeOverrides.front().unresolved);
  seam::phonemizer::JapaneseKanaPhonemizer adapter;
  const auto heard = adapter.phonemize(*changed).tokensForNote(NoteId{1U});
  CHECK(heard.size() == 2U);
  CHECK(!heard[0].locked);
  CHECK(heard[1].locked);
  CHECK(heard[1].timing.startOffset == -1200);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(!session.validatePerformanceJob(job.value()));
  CHECK(session.redo());
  CHECK(session.project() == after);
}

TEST_CASE("lyric identity exhaustion rejects atomically and unsupported language clears identity") {
  auto project = projectWithPerformance();
  project.findRegion(RegionId{10U})->performance.revision.pronunciation =
      std::numeric_limits<std::uint64_t>::max();
  const auto before = project;
  seam::application::SetLyricCommand exhausted{LyricTokenId{1U}, U"か", Language::Japanese};
  CHECK(!exhausted.apply(project));
  CHECK(project == before);
  project = projectWithPerformance();
  const auto supportedBefore = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"hello", Language::English)));
  CHECK(!session.project().findRegion(RegionId{10U})->performance.pronunciation);
  CHECK(session.project().findRegion(RegionId{10U})->performance.revision.pronunciation == 10U);
  CHECK(session.undo());
  CHECK(session.project() == supportedBefore);
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, std::u32string(4097U, U'あ'), Language::Japanese)));
  CHECK(!session.project().findRegion(RegionId{10U})->performance.pronunciation);
  CHECK(session.undo());
  CHECK(session.project() == supportedBefore);
}

TEST_CASE("phoneme upsert and reset maintain saved identity with exact dependency history") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->phonemeOverrides = {{.key = {NoteId{2U}, 0U}, .locked = true},
                             {.key = {NoteId{1U}, 0U}, .locked = true}};
  const auto before = project;
  seam::application::EditorSession session{project};
  auto changed = region->phonemeOverrides.back();
  changed.symbol = "i";
  changed.timing.startOffset = -2000;
  CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(RegionId{10U}, changed)));
  const auto edited = session.project();
  const auto checkCurrent = [](const Project& value, std::uint64_t revision) {
    const auto* current = value.findRegion(RegionId{10U});
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*current);
    CHECK(resolved);
    CHECK(current->performance.pronunciation == resolved.value().identity);
    CHECK(current->performance.revision.pronunciation == revision);
    CHECK(current->performance.revision.musical == 9U);
    CHECK(current->performance.revision.ownership == 9U);
  };
  checkCurrent(edited, 10U);
  CHECK(session.execute(std::make_unique<seam::application::RemovePhonemeOverrideCommand>(
      RegionId{10U}, changed.key)));
  const auto reset = session.project();
  checkCurrent(reset, 11U);
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(reset);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == reset);
  CHECK(session.undo());
  CHECK(session.project() == edited);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == edited);
  CHECK(session.redo());
  CHECK(session.project() == reset);
}

TEST_CASE("phoneme revision exhaustion and invalid targets cannot partially mutate edits") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->performance.revision.pronunciation = std::numeric_limits<std::uint64_t>::max();
  const PhonemeOverride original{.key = {NoteId{1U}, 0U}, .locked = true};
  region->phonemeOverrides = {original};
  const auto before = project;
  auto changed = original;
  changed.symbol = "i";
  seam::application::UpsertPhonemeOverrideCommand upsert{RegionId{10U}, changed};
  CHECK(!upsert.apply(project));
  CHECK(project == before);
  seam::application::RemovePhonemeOverrideCommand remove{RegionId{10U}, original.key};
  CHECK(!remove.apply(project));
  CHECK(project == before);
  seam::application::UpsertPhonemeOverrideCommand unchanged{RegionId{10U}, original};
  CHECK(unchanged.apply(project));
  CHECK(project == before);
  CHECK(unchanged.revert(project));
  CHECK(project == before);
}

TEST_CASE("direct phoneme symbol edits and reset invalidate dependent sounds atomically") {
  for (const bool reset : {false, true}) {
    auto project = projectWithPerformance();
    auto* region = project.findRegion(RegionId{10U});
    const PhonemeOverride replacement{.key = {NoteId{1U}, 0U}, .symbol = "u", .locked = true};
    if (reset) region->phonemeOverrides = {replacement};
    region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 0U},
        .tokenCount = 2U, .unitId = reset ? "cross-u-i" : "cross-a-i", .loopPrint = 0.6F}};
    region->seamOverrides = {{.incomingStartKey = {NoteId{2U}, 0U}, .overlap = 1300}};
    const auto before = project;
    seam::application::EditorSession session{project};
    if (reset) {
      CHECK(session.execute(std::make_unique<seam::application::RemovePhonemeOverrideCommand>(
          RegionId{10U}, replacement.key)));
    } else {
      CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(
          RegionId{10U}, replacement)));
    }
    const auto after = session.project();
    const auto* changed = after.findRegion(RegionId{10U});
    CHECK(changed->unitSelectionOverrides.front().unresolved);
    CHECK(changed->seamOverrides.front().unresolved);
    CHECK(changed->unitSelectionOverrides.front().unitId == before.findRegion(RegionId{10U})->unitSelectionOverrides.front().unitId);
    CHECK(changed->unitSelectionOverrides.front().loopPrint == 0.6F);
    CHECK(changed->seamOverrides.front().overlap == 1300);
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*changed);
    CHECK(resolved);
    CHECK(changed->performance.pronunciation == resolved.value().identity);
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("bound phoneme edits survive save and reject changed source context") {
  auto project = projectWithPerformance();
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(
      RegionId{10U}, PhonemeOverride{.key = {NoteId{1U}, 0U},
          .timing = {.startOffset = -1700}, .locked = true})));
  const auto bound = session.project();
  const auto& edit = bound.findRegion(RegionId{10U})->phonemeOverrides.front();
  CHECK(edit.sourceContextId);
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(bound);
  CHECK(encoded);
  auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == bound);
  auto* changed = decoded.value().findRegion(RegionId{10U});
  changed->lyrics.front().surface = U"か";
  const auto stale = seam::phonemizer::resolveJapanesePronunciation(*changed);
  CHECK(stale);
  CHECK(!stale.value().pronunciation.warnings.empty());
  for (const auto& token : stale.value().pronunciation.tokensForNote(NoteId{1U})) CHECK(!token.locked);
  const auto beforeRejected = decoded.value();
  auto staleEdit = edit;
  staleEdit.timing.startOffset = -1900;
  seam::application::UpsertPhonemeOverrideCommand rejected{RegionId{10U}, staleEdit};
  CHECK(!rejected.apply(decoded.value()));
  CHECK(decoded.value() == beforeRejected);
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"か", Language::Japanese)));
  const auto* rebound = session.project().findRegion(RegionId{10U});
  CHECK(rebound->phonemeOverrides.front().key.ordinal == 1U);
  CHECK(rebound->phonemeOverrides.front().sourceContextId != edit.sourceContextId);
  CHECK(!rebound->phonemeOverrides.front().unresolved);
  const auto current = seam::phonemizer::resolveJapanesePronunciation(*rebound);
  CHECK(current);
  CHECK(current.value().pronunciation.tokensForNote(NoteId{1U})[1].locked);
  CHECK(session.undo());
  CHECK(session.project() == bound);
}

TEST_CASE("appended phoneme bindings cannot become a different base token after lyric changes") {
  auto project = projectWithPerformance();
  seam::application::UpsertPhonemeOverrideCommand append{RegionId{10U},
      PhonemeOverride{.key = {NoteId{1U}, 1U}, .symbol = "u", .locked = true}};
  CHECK(append.apply(project));
  const auto* region = project.findRegion(RegionId{10U});
  CHECK(region->phonemeOverrides.front().sourceContextId);
  const auto appended = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(appended);
  const auto notes = appended.value().pronunciation.tokensForNote(NoteId{1U});
  CHECK(notes.size() == 2U);
  CHECK(notes[1].symbol == "u");
  CHECK(notes[1].locked);
  auto changed = project;
  changed.findRegion(RegionId{10U})->lyrics.front().surface = U"か";
  const auto stale = seam::phonemizer::resolveJapanesePronunciation(*changed.findRegion(RegionId{10U}));
  CHECK(stale);
  const auto replacement = stale.value().pronunciation.tokensForNote(NoteId{1U});
  CHECK(replacement.size() == 2U);
  CHECK(replacement[1].symbol == "a");
  CHECK(!replacement[1].locked);
  auto malformed = region->phonemeOverrides.front();
  malformed.sourceContextId = "";
  CHECK(!malformed.validate());
  malformed.sourceContextId = std::string(64U, 'G');
  CHECK(!malformed.validate());
}

TEST_CASE("language service changes retain phoneme unit and seam edits unresolved with exact undo") {
  for (const auto originalLanguage : {Language::Japanese, Language::Unspecified}) {
    auto project = projectWithPerformance();
    auto* region = project.findRegion(RegionId{10U});
    region->performance = {};
    region->notes.resize(1U);
    region->lyrics.resize(1U);
    region->lyrics.front().language = originalLanguage;
    seam::application::UpsertPhonemeOverrideCommand bind{RegionId{10U},
        PhonemeOverride{.key = {NoteId{1U}, 0U}, .timing = {.startOffset = -1500}, .locked = true}};
    CHECK(bind.apply(project));
    region = project.findRegion(RegionId{10U});
    region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 0U}, .unitId = "japanese-a",
        .loopPrint = 0.4F, .locked = true}};
    region->seamOverrides = {{.incomingStartKey = {NoteId{1U}, 0U}, .seamAmount = 0.6F,
        .overlap = 1200, .locked = true}};
    const auto before = project;
    const auto previousContext = region->phonemeOverrides.front().sourceContextId;
    CHECK(previousContext);
    seam::application::EditorSession session{project};
    CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
        LyricTokenId{1U}, U"아", Language::Korean)));
    const auto after = session.project();
    const auto* changed = after.findRegion(RegionId{10U});
    auto phoneme = before.findRegion(RegionId{10U})->phonemeOverrides.front();
    auto unit = before.findRegion(RegionId{10U})->unitSelectionOverrides.front();
    auto seam = before.findRegion(RegionId{10U})->seamOverrides.front();
    phoneme.unresolved = true;
    unit.unresolved = true;
    seam.unresolved = true;
    CHECK(changed->phonemeOverrides == std::vector<PhonemeOverride>{phoneme});
    CHECK(changed->unitSelectionOverrides == std::vector<UnitSelectionOverride>{unit});
    CHECK(changed->seamOverrides == std::vector<SeamOverride>{seam});
    CHECK(changed->phonemeOverrides.front().sourceContextId == previousContext);
    const auto resolved = seam::phonemizer::resolvePronunciation(*changed);
    CHECK(resolved);
    CHECK(resolved.value().identity.language == Language::Korean);
    CHECK(!resolved.value().pronunciation.tokens.front().locked);
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("region duplication readdresses valid bindings without promoting stale ones") {
  for (const bool stale : {false, true}) {
    auto project = projectWithPerformance();
    seam::application::UpsertPhonemeOverrideCommand edit{RegionId{10U},
        PhonemeOverride{.key = {NoteId{1U}, 0U}, .timing = {.startOffset = -1600}, .locked = true}};
    CHECK(edit.apply(project));
    if (stale) project.findRegion(RegionId{10U})->phonemeOverrides.front().sourceContextId = std::string(64U, 'b');
    const auto before = project;
    const auto originalContext = project.findRegion(RegionId{10U})->phonemeOverrides.front().sourceContextId;
    seam::application::EditorSession session{project};
    auto duplicate = std::make_unique<seam::application::DuplicateVocalRegionCommand>(TrackId{20U}, RegionId{10U});
    const auto* observed = duplicate.get();
    CHECK(session.execute(std::move(duplicate)));
    const auto after = session.project();
    const auto* copied = after.findRegion(observed->duplicatedRegionId());
    CHECK(copied);
    CHECK(copied->phonemeOverrides.front().unresolved == stale);
    CHECK(copied->phonemeOverrides.front().timing.startOffset == -1600);
    if (!stale) {
      CHECK(copied->phonemeOverrides.front().sourceContextId != originalContext);
      const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*copied);
      CHECK(resolved);
      CHECK(resolved.value().pronunciation.tokens.front().locked);
    } else CHECK(copied->phonemeOverrides.front().sourceContextId == originalContext);
    CHECK(after.findRegion(RegionId{10U})->phonemeOverrides == before.findRegion(RegionId{10U})->phonemeOverrides);
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("split render edits retain broken spans and boundary seams unresolved") {
  for (const bool continuation : {false, true}) {
    auto project = projectWithPerformance();
    auto* region = project.findRegion(RegionId{10U});
    region->lyrics.back().surface = continuation ? U"ー" : U"こ";
    region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 0U}, .tokenCount = 2U, .unitId = "cross-split"},
        {.startKey = {NoteId{2U}, 0U}, .unitId = "right-unit"}};
    region->seamOverrides = {{.incomingStartKey = {NoteId{1U}, 0U}, .seamAmount = 0.2F},
        {.incomingStartKey = {NoteId{2U}, 0U}, .seamAmount = 0.6F}};
    if (!continuation) region->seamOverrides.push_back({.incomingStartKey = {NoteId{2U}, 1U}, .seamAmount = 0.3F});
    const auto before = project;
    seam::application::EditorSession session{project};
    auto split = std::make_unique<seam::application::SplitVocalRegionCommand>(TrackId{20U}, RegionId{10U}, Tick{960});
    const auto* observed = split.get();
    CHECK(session.execute(std::move(split)));
    const auto after = session.project();
    const auto* left = after.findRegion(RegionId{10U});
    const auto* right = after.findRegion(observed->splitRegionId());
    CHECK(left && right);
    CHECK(left->unitSelectionOverrides.front().unresolved);
    CHECK(left->unitSelectionOverrides.front().unitId == "cross-split");
    CHECK(left->unitSelectionOverrides.front().tokenCount == 2U);
    CHECK(!left->seamOverrides.front().unresolved);
    CHECK(right->unitSelectionOverrides.front().unresolved == continuation);
    CHECK(right->seamOverrides.front().unresolved);
    CHECK(right->seamOverrides.front().seamAmount == 0.6F);
    if (!continuation) CHECK(!right->seamOverrides.back().unresolved);
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("split bindings preserve unchanged sounds and leave lost continuation context unresolved") {
  for (const bool continuation : {false, true}) {
    auto project = projectWithPerformance();
    if (continuation) project.findRegion(RegionId{10U})->lyrics.back().surface = U"ー";
    seam::application::UpsertPhonemeOverrideCommand edit{RegionId{10U},
        PhonemeOverride{.key = {NoteId{2U}, 0U}, .timing = {.startOffset = -1500}, .locked = true}};
    CHECK(edit.apply(project));
    const auto before = project;
    seam::application::EditorSession session{project};
    auto split = std::make_unique<seam::application::SplitVocalRegionCommand>(TrackId{20U}, RegionId{10U}, Tick{960});
    const auto* observed = split.get();
    CHECK(session.execute(std::move(split)));
    const auto after = session.project();
    const auto* right = after.findRegion(observed->splitRegionId());
    CHECK(right);
    CHECK(right->phonemeOverrides.front().unresolved == continuation);
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*right);
    CHECK(resolved);
    CHECK(resolved.value().pronunciation.tokens.front().locked == !continuation);
    CHECK(right->phonemeOverrides.front().timing.startOffset == -1500);
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("timing only phoneme edits retain unit and seam correspondence") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 0U},
      .tokenCount = 2U, .unitId = "cross-a-i"}};
  region->seamOverrides = {{.incomingStartKey = {NoteId{2U}, 0U}, .phaseReset = 0.7F}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(
      RegionId{10U}, PhonemeOverride{.key = {NoteId{1U}, 0U},
                                    .timing = {.startOffset = -2500}, .locked = true})));
  const auto after = session.project();
  CHECK(after.findRegion(RegionId{10U})->unitSelectionOverrides == region->unitSelectionOverrides);
  CHECK(after.findRegion(RegionId{10U})->seamOverrides == region->seamOverrides);
  CHECK(session.execute(std::make_unique<seam::application::RemovePhonemeOverrideCommand>(
      RegionId{10U}, PhonemeKey{NoteId{1U}, 0U})));
  CHECK(session.project().findRegion(RegionId{10U})->unitSelectionOverrides == region->unitSelectionOverrides);
  CHECK(session.undo());
  CHECK(session.project() == after);
  CHECK(session.undo());
  CHECK(session.project() == before);
}

TEST_CASE("ambiguous lyric edits persist unresolved without applying or reviving locks") {
  auto project = projectWithPerformance();
  project.findRegion(RegionId{10U})->phonemeOverrides = {
      {.key = {NoteId{1U}, 0U}, .symbol = "custom",
       .timing = {.startOffset = -1200}, .locked = true}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"ああ", Language::Japanese)));
  const auto after = session.project();
  const auto& edit = after.findRegion(RegionId{10U})->phonemeOverrides.front();
  CHECK(edit.unresolved);
  CHECK(edit.key == before.findRegion(RegionId{10U})->phonemeOverrides.front().key);
  CHECK(edit.symbol == "custom");
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(after);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == after);
  auto tree = seam::formats::parseJson(encoded.value());
  CHECK(tree);
  auto& overrideJson = tree.value().find("vocalTracks")->asArray().front()
      .find("regions")->asArray().front().find("phonemeOverrides")->asArray().front();
  *overrideJson.find("unresolved") = seam::formats::JsonValue{std::int64_t{1}};
  CHECK(!codec.decode(seam::formats::stringifyJson(tree.value())));
  seam::phonemizer::JapaneseKanaPhonemizer adapter;
  const auto result = adapter.phonemize(*decoded.value().findRegion(RegionId{10U}));
  CHECK(!result.warnings.empty());
  for (const auto& token : result.tokensForNote(NoteId{1U})) {
    CHECK(!token.locked);
    CHECK(token.symbol == "a");
  }
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"あ", Language::Japanese)));
  CHECK(session.project().findRegion(RegionId{10U})->phonemeOverrides.front().unresolved);
  CHECK(session.undo());
  CHECK(session.project() == after);
  CHECK(session.undo());
  CHECK(session.project() == before);
}

TEST_CASE("batch lyric reconciliation retains colliding edits and restores all lyrics") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->lyrics.front().surface = U"あか";
  region->phonemeOverrides = {
      {.key = {NoteId{1U}, 0U}, .symbol = "first", .locked = true},
      {.key = {NoteId{1U}, 1U}, .symbol = "second", .locked = true}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::BatchSetLyricsCommand>(
      std::vector<seam::application::BatchLyricEdit>{
          {LyricTokenId{1U}, U"あか", U"か", Language::Japanese, Language::Japanese},
          {LyricTokenId{2U}, U"い", U"き", Language::Japanese, Language::Japanese}})));
  const auto after = session.project();
  const auto& edits = after.findRegion(RegionId{10U})->phonemeOverrides;
  CHECK(edits.size() == 2U);
  for (std::size_t i = 0U; i < edits.size(); ++i) {
    CHECK(edits[i].unresolved);
    CHECK(edits[i].key == before.findRegion(RegionId{10U})->phonemeOverrides[i].key);
    CHECK(edits[i].symbol == before.findRegion(RegionId{10U})->phonemeOverrides[i].symbol);
  }
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
}

TEST_CASE("lyric edits move complete unit spans and preserve seam neighborhoods") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->lyrics.front().surface = U"か";
  region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 0U},
      .tokenCount = 2U, .unitId = "original-ka"}};
  region->seamOverrides = {{.incomingStartKey = {NoteId{1U}, 1U}, .seamAmount = 0.7F}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"すか", Language::Japanese)));
  const auto after = session.project();
  const auto* changed = after.findRegion(RegionId{10U});
  CHECK(changed->unitSelectionOverrides.front().startKey.ordinal == 2U);
  CHECK(!changed->unitSelectionOverrides.front().unresolved);
  CHECK(changed->unitSelectionOverrides.front().unitId == "original-ka");
  CHECK(changed->seamOverrides.front().incomingStartKey.ordinal == 3U);
  CHECK(!changed->seamOverrides.front().unresolved);
  CHECK(changed->seamOverrides.front().seamAmount == 0.7F);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
}

TEST_CASE("changed unit and seam sounds remain saved unresolved without revival") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->lyrics.front().surface = U"か";
  region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 0U},
      .tokenCount = 2U, .unitId = "original-ka"}};
  region->seamOverrides = {{.incomingStartKey = {NoteId{1U}, 1U}, .overlap = 1200}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"き", Language::Japanese)));
  const auto after = session.project();
  CHECK(after.findRegion(RegionId{10U})->unitSelectionOverrides.front().unresolved);
  CHECK(after.findRegion(RegionId{10U})->seamOverrides.front().unresolved);
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(after);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == after);
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"か", Language::Japanese)));
  CHECK(session.project().findRegion(RegionId{10U})->unitSelectionOverrides.front().unresolved);
  CHECK(session.project().findRegion(RegionId{10U})->seamOverrides.front().unresolved);
  CHECK(session.undo());
  CHECK(session.project() == after);
  CHECK(session.undo());
  CHECK(session.project() == before);
}

TEST_CASE("lyric edits preserve verified cross note units and joins") {
  auto project = projectWithPerformance();
  auto* region = project.findRegion(RegionId{10U});
  region->lyrics.front().surface = U"か";
  region->unitSelectionOverrides = {{.startKey = {NoteId{1U}, 1U},
      .tokenCount = 2U, .unitId = "cross-a-i"}};
  region->seamOverrides = {{.incomingStartKey = {NoteId{2U}, 0U}, .seamAmount = 0.4F}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"すか", Language::Japanese)));
  const auto after = session.project();
  const auto* changed = after.findRegion(RegionId{10U});
  CHECK(changed->unitSelectionOverrides.front().startKey.ordinal == 3U);
  CHECK(!changed->unitSelectionOverrides.front().unresolved);
  CHECK(changed->seamOverrides == before.findRegion(RegionId{10U})->seamOverrides);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      LyricTokenId{1U}, U"すき", Language::Japanese)));
  CHECK(session.project().findRegion(RegionId{10U})->unitSelectionOverrides.front().unresolved);
  CHECK(session.project().findRegion(RegionId{10U})->seamOverrides.front().unresolved);
  CHECK(session.undo());
  CHECK(session.project() == after);
}

TEST_CASE("moving notes translates accepted source offsets and preserves exact undo") {
  auto project = projectWithPerformance();
  project.findRegion(RegionId{10U})->performance.accepted.push_back(
      {"take-1", PerformanceChannel::Pitch,
       PerformanceTimeRange{Tick{1440}, Tick{1920}}, Tick{0}});
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{{NoteId{1U}, Tick{0}, Tick{480}, 60U, 62U}})));
  const auto after = session.project();
  const auto& state = after.findRegion(RegionId{10U})->performance;
  CHECK(state.accepted.front().sourceTickOffset == Tick{-480});
  CHECK(state.accepted.back() == before.findRegion(RegionId{10U})->performance.accepted.back());
  CHECK(state.takes == before.findRegion(RegionId{10U})->performance.takes);
  CHECK(state.ownership == before.findRegion(RegionId{10U})->performance.ownership);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
}

TEST_CASE("resizing trims accepted material without slipping the source timeline") {
  const auto before = projectWithPerformance();
  seam::application::EditorSession session{before};
  CHECK(session.execute(std::make_unique<seam::application::ResizeNotesCommand>(
      std::vector<seam::application::NoteResize>{
          {NoteId{1U}, Tick{0}, Tick{480}, Tick{120}, Tick{240}}})));
  const auto after = session.project();
  auto expected = before.findRegion(RegionId{10U})->performance;
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*after.findRegion(RegionId{10U}));
  CHECK(resolved);
  expected.pronunciation = resolved.value().identity;
  ++expected.revision.pronunciation;
  CHECK(after.findRegion(RegionId{10U})->performance == expected);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
}

TEST_CASE("geometry edits reject missing source coverage and overlap atomically") {
  auto project = projectWithPerformance();
  auto& state = project.findRegion(RegionId{10U})->performance;
  state.takes.front().range.endTick = Tick{480};
  state.takes.front().lanes = {{PerformanceChannel::Dynamics,
      {{Tick{0}, 0.5}, {Tick{480}, 1.0}}}};
  CHECK(project.validate());
  const auto before = project;
  seam::application::ResizeNotesCommand resize{{
      {NoteId{2U}, Tick{960}, Tick{480}, Tick{1000}, Tick{480}},
      {NoteId{1U}, Tick{0}, Tick{480}, Tick{0}, Tick{600}}}};
  CHECK(!resize.apply(project));
  CHECK(project == before);

  state.ownership.push_back({PerformanceChannel::Pitch,
      PerformanceTimeRange{Tick{480}, Tick{960}}, ManualPerformanceMode::Replace, {}});
  CHECK(project.validate());
  const auto withRange = project;
  seam::application::MoveNotesCommand move{{
      {NoteId{2U}, Tick{960}, Tick{1000}, 60U, 64U},
      {NoteId{1U}, Tick{0}, Tick{480}, 60U, 62U}}};
  CHECK(!move.apply(project));
  CHECK(project == withRange);
}

TEST_CASE("manual replacement and vibrato exclude generated pitch but pitch offsets do not") {
  RegionPerformanceState state;
  state.ownership = {{PerformanceChannel::Pitch, NoteId{1U}, ManualPerformanceMode::PitchOffset, {}}};
  CHECK(state.validate(notes(), Tick{1920}));
  CHECK(state.permitsGenerated(PerformanceChannel::Pitch, NoteId{1U}, Tick{100}, false));
  CHECK(!state.permitsGenerated(PerformanceChannel::Pitch, NoteId{1U}, Tick{100}, true));
  CHECK(state.permitsGenerated(PerformanceChannel::Dynamics, NoteId{1U}, Tick{100}, true));
  state.ownership.front().mode = ManualPerformanceMode::Replace;
  CHECK(!state.permitsGenerated(PerformanceChannel::Pitch, NoteId{1U}, Tick{100}, false));
  CHECK(state.permitsGenerated(PerformanceChannel::Pitch, NoteId{2U}, Tick{1000}, false));
  state.ownership = {{PerformanceChannel::Dynamics, PerformanceTimeRange{Tick{0}, Tick{480}},
                      ManualPerformanceMode::Replace, {}}};
  CHECK(!state.permitsGenerated(PerformanceChannel::Dynamics, NoteId{1U}, Tick{479}, false));
  CHECK(state.permitsGenerated(PerformanceChannel::Dynamics, NoteId{1U}, Tick{480}, false));
}

TEST_CASE("current performance references reject dangling and ambiguous ownership") {
  RegionPerformanceState state;
  state.ownership = {{PerformanceChannel::Pitch, NoteId{99U}, ManualPerformanceMode::Replace, {}}};
  CHECK(!state.validate(notes(), Tick{1920}));
  state.ownership.front().scope = PerformanceTimeRange{Tick{0}, Tick{1921}};
  CHECK(!state.validate(notes(), Tick{1920}));
  state.ownership.front().scope = NoteId{1U};
  state.ownership.push_back({PerformanceChannel::Pitch, PerformanceTimeRange{Tick{240}, Tick{960}},
                             ManualPerformanceMode::Replace, {}});
  CHECK(!state.validate(notes(), Tick{1920}));
  state.ownership.back().mode = ManualPerformanceMode::PitchOffset;
  CHECK(state.validate(notes(), Tick{1920}));
}

TEST_CASE("distinct overlapping notes retain independent performance ownership and selections") {
  auto overlapping = notes();
  overlapping.back().startTick = Tick{240};
  RegionPerformanceState state{.takes = {take()}};
  state.ownership = {{PerformanceChannel::Pitch, NoteId{1U}, ManualPerformanceMode::Replace, {}},
                     {PerformanceChannel::Pitch, NoteId{2U}, ManualPerformanceMode::Replace, {}}};
  state.accepted = {{"take-1", PerformanceChannel::Pitch, NoteId{1U}, Tick{0}},
                    {"take-1", PerformanceChannel::Pitch, NoteId{2U}, Tick{0}}};
  CHECK(state.validate(overlapping, Tick{1920}));
  state.ownership.back().scope = NoteId{1U};
  CHECK(!state.validate(overlapping, Tick{1920}));
  state.ownership.back().scope = NoteId{2U};
  state.accepted.back().scope = NoteId{1U};
  CHECK(!state.validate(overlapping, Tick{1920}));
  state.accepted.back().scope = NoteId{2U};
  state.ownership.push_back({PerformanceChannel::Pitch, PerformanceTimeRange{Tick{480}, Tick{800}},
                             ManualPerformanceMode::Replace, {}});
  CHECK(!state.validate(overlapping, Tick{1920}));
}

TEST_CASE("accepted source mapping allows signed offsets without integer overflow") {
  RegionPerformanceState state{.takes = {take()},
      .accepted = {{"take-1", PerformanceChannel::Pitch, NoteId{2U}, Tick{-960}}}};
  CHECK(state.validate(notes(), Tick{1920}));
  state.accepted.front().sourceTickOffset = Tick{-961};
  CHECK(!state.validate(notes(), Tick{1920}));
  state.accepted.front().sourceTickOffset = Tick{std::numeric_limits<std::int64_t>::min()};
  CHECK(!state.validate(notes(), Tick{1920}));
  state.accepted.front().sourceTickOffset = Tick{std::numeric_limits<std::int64_t>::max()};
  CHECK(!state.validate(notes(), Tick{1920}));
}

TEST_CASE("accepted selections require one existing lane and unambiguous live scope") {
  RegionPerformanceState state{.takes = {take()}};
  state.accepted = {{"missing", PerformanceChannel::Pitch, NoteId{1U}, Tick{0}}};
  CHECK(!state.validate(notes(), Tick{1920}));
  state.accepted.front().takeId = "take-1";
  state.accepted.front().channel = PerformanceChannel::Growl;
  CHECK(!state.validate(notes(), Tick{1920}));
  state.accepted.front().channel = PerformanceChannel::Pitch;
  state.accepted.front().sourceTickOffset = Tick{1900};
  CHECK(!state.validate(notes(), Tick{1920}));
  state.accepted.front().sourceTickOffset = Tick{0};
  CHECK(state.validate(notes(), Tick{1920}));
  state.accepted.push_back({"take-1", PerformanceChannel::Pitch,
                            PerformanceTimeRange{Tick{240}, Tick{960}}, Tick{0}});
  CHECK(!state.validate(notes(), Tick{1920}));
}

TEST_CASE("take values validate declared units and explicit unvoiced pitch") {
  auto value = take();
  CHECK(value.validate());
  CHECK(performanceChannelUnit(PerformanceChannel::Pitch) == "midi-cents");
  CHECK(performanceChannelUnit(PerformanceChannel::Timing) == "microseconds-offset");
  value.lanes.back().points.front().value.reset();
  CHECK(!value.validate());
  value = take();
  value.lanes.front().points.front().value = std::numeric_limits<double>::infinity();
  CHECK(!value.validate());
  value = take();
  value.lanes.front().points.front().value = -0.001;
  CHECK(!value.validate());
  value = take();
  value.lanes.back().points.front().value = 4.0;
  CHECK(!value.validate());
  value = take();
  value.lanes.push_back(value.lanes.front());
  CHECK(!value.validate());
  value = take();
  value.pronunciation.sequenceHash = "not-a-digest";
  CHECK(!value.validate());
}

TEST_CASE("project schema eight preserves ownership and proposed versus accepted take state") {
  const auto project = projectWithPerformance();
  CHECK(project.validate());
  const auto encoded = seam::formats::ProjectJsonCodec{}.encode(project);
  CHECK(encoded);
  const auto decoded = seam::formats::ProjectJsonCodec{}.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
  CHECK(decoded.value().vocalTracks().front().regions.front().performance.takes.front().seed ==
        std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("canonical project validation rejects invalid live performance references") {
  auto project = projectWithPerformance();
  project.vocalTracks().front().regions.front().performance.accepted.front().takeId = "absent";
  CHECK(!project.validate());
  CHECK(!seam::formats::ProjectJsonCodec{}.encode(project));
}

TEST_CASE("performance serialization rejects ambiguous counters discriminants and units") {
  const auto encoded = seam::formats::ProjectJsonCodec{}.encode(projectWithPerformance());
  CHECK(encoded);
  const auto original = seam::formats::parseJson(encoded.value());
  CHECK(original);
  for (const auto corruption : {"missing", "counter-number", "counter-leading-zero", "counter-overflow",
                                "scope", "state", "unit", "null-gain", "unknown"}) {
    auto tree = original.value();
    auto& region = tree.asObject().at("vocalTracks").asArray().front().asObject()
                       .at("regions").asArray().front().asObject();
    const std::string_view selected{corruption};
    if (selected == "missing") {
      region.erase("performance");
    } else {
      auto& performance = region.at("performance").asObject();
      auto& captured = performance.at("takes").asArray().front().asObject();
      if (selected == "counter-number") captured.at("seed") = seam::formats::JsonValue{1.0};
      if (selected == "counter-leading-zero") captured.at("seed") = seam::formats::JsonValue{std::string{"01"}};
      if (selected == "counter-overflow") captured.at("seed") = seam::formats::JsonValue{std::string(17U, 'f')};
      if (selected == "scope") performance.at("ownership").asArray().front().asObject().at("scope")
          .asObject().at("kind") = seam::formats::JsonValue{std::string{"future"}};
      if (selected == "state") captured.at("state") = seam::formats::JsonValue{std::string{"accepted"}};
      if (selected == "unit") captured.at("lanes").asArray().front().asObject().at("unit") =
          seam::formats::JsonValue{std::string{"Hz"}};
      if (selected == "null-gain") captured.at("lanes").asArray().back().asObject().at("points")
          .asArray().front().asObject().at("value") = seam::formats::JsonValue{nullptr};
      if (selected == "unknown") performance.emplace("implicitlyAccept", seam::formats::JsonValue{true});
    }
    CHECK(!seam::formats::ProjectJsonCodec{}.decode(seam::formats::stringifyJson(tree)));
  }
}

TEST_CASE("region performance applies an aggregate budget across valid take payloads") {
  RegionPerformanceState state;
  for (std::size_t index = 0U; index < 4U; ++index) {
    auto proposal = take("take-" + std::to_string(index));
    proposal.range.endTick = Tick{20000};
    proposal.lanes = {{PerformanceChannel::Pitch, {}}};
    for (std::size_t point = 0U; point < kMaximumPerformanceLanePoints; ++point) {
      proposal.lanes.front().points.push_back({Tick{static_cast<std::int64_t>(point)}, 6000.0});
    }
    state.takes.push_back(std::move(proposal));
  }
  CHECK(state.validate(notes(), Tick{1920}));
  auto excess = take("excess");
  excess.lanes = {{PerformanceChannel::Dynamics, {{Tick{0}, 1.0}}}};
  state.takes.push_back(std::move(excess));
  CHECK(!state.validate(notes(), Tick{1920}));
  state.takes.clear();
  for (std::size_t index = 0U; index < kMaximumPerformanceTakes; ++index) {
    state.takes.push_back(take(std::to_string(index)));
  }
  CHECK(state.validate(notes(), Tick{1920}));
  state.takes.push_back(take("extra"));
  CHECK(!state.validate(notes(), Tick{1920}));
}

TEST_CASE("deleting notes removes live bindings but retains proposals and restores exact undo") {
  auto project = projectWithPerformance();
  auto& state = project.findRegion(RegionId{10U})->performance;
  state.ownership.push_back({PerformanceChannel::Dynamics, PerformanceTimeRange{Tick{480}, Tick{960}},
                             ManualPerformanceMode::Replace, {}});
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      std::vector<NoteId>{NoteId{1U}})));
  const auto& changed = session.project().findRegion(RegionId{10U})->performance;
  CHECK(changed.ownership.size() == 1U);
  CHECK(changed.ownership.front().channel == PerformanceChannel::Dynamics);
  CHECK(changed.accepted.empty());
  CHECK(changed.takes == state.takes);
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*session.project().findRegion(RegionId{10U}));
  CHECK(resolved);
  CHECK(changed.pronunciation == resolved.value().identity);
  CHECK(changed.revision.pronunciation == state.revision.pronunciation + 1U);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project().findRegion(RegionId{10U})->performance.accepted.empty());
}

TEST_CASE("region split partitions performance ownership and maps immutable take source time") {
  auto project = projectWithPerformance();
  auto& state = project.findRegion(RegionId{10U})->performance;
  state.ownership.push_back({PerformanceChannel::Pitch, NoteId{2U}, ManualPerformanceMode::Replace, {}});
  state.ownership.push_back({PerformanceChannel::Dynamics, PerformanceTimeRange{Tick{480}, Tick{1440}},
                             ManualPerformanceMode::Replace, {}});
  state.accepted.push_back({"take-1", PerformanceChannel::Pitch,
                            PerformanceTimeRange{Tick{480}, Tick{1440}}, Tick{0}});
  const auto before = project;
  seam::application::EditorSession session{project};
  auto command = std::make_unique<seam::application::SplitVocalRegionCommand>(
      TrackId{20U}, RegionId{10U}, Tick{960});
  const auto* observed = command.get();
  CHECK(session.execute(std::move(command)));
  const auto* left = session.project().findRegion(RegionId{10U});
  const auto* right = session.project().findRegion(observed->splitRegionId());
  CHECK(right != nullptr);
  CHECK(left->performance.ownership.size() == 2U);
  CHECK(right->performance.ownership.size() == 2U);
  CHECK(std::get<NoteId>(right->performance.ownership.front().scope) == right->notes.front().id);
  const auto rightRange = std::get<PerformanceTimeRange>(right->performance.ownership.back().scope);
  CHECK(rightRange.startTick == Tick{0});
  CHECK(rightRange.endTick == Tick{480});
  CHECK(right->performance.accepted.size() == 1U);
  CHECK(right->performance.accepted.front().sourceTickOffset == Tick{960});
  CHECK(right->performance.takes == state.takes);
  CHECK(!left->performance.pronunciation.has_value());
  CHECK(!right->performance.pronunciation.has_value());
  const auto divided = session.project();
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == divided);
}

TEST_CASE("duplicating a region remaps live scopes while preserving captured take provenance") {
  const auto before = projectWithPerformance();
  seam::application::EditorSession session{before};
  auto command = std::make_unique<seam::application::DuplicateVocalRegionCommand>(TrackId{20U}, RegionId{10U});
  const auto* observed = command.get();
  CHECK(session.execute(std::move(command)));
  const auto* duplicate = session.project().findRegion(observed->duplicatedRegionId());
  CHECK(duplicate != nullptr);
  const auto newNoteId = duplicate->notes.front().id;
  CHECK(newNoteId != NoteId{1U});
  CHECK(std::get<NoteId>(duplicate->performance.ownership.front().scope) == newNoteId);
  CHECK(std::get<NoteId>(duplicate->performance.accepted.front().scope) == newNoteId);
  CHECK(duplicate->performance.takes.front().sourceRegionId == RegionId{10U});
  CHECK(!duplicate->performance.pronunciation.has_value());
  CHECK(session.undo());
  CHECK(session.project() == before);
}

TEST_CASE("plugin replacement and autosave recovery preserve proposed and selected state without accepting more") {
  const auto root = seam::test::support::temporaryDirectory("region-performance-roundtrip");
  auto project = projectWithPerformance();
  seam::application::SetLyricCommand edit{LyricTokenId{1U}, U"か", Language::Japanese};
  CHECK(edit.apply(project));
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*project.findRegion(RegionId{10U}));
  CHECK(resolved);
  CHECK(project.findRegion(RegionId{10U})->performance.pronunciation == resolved.value().identity);
  const auto pluginState = seam::clap_editor::encodeEditorState(project);
  CHECK(pluginState);
  const auto decoded = seam::clap_editor::decodeEditorState(pluginState.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
  seam::clap_editor::EditorRuntime runtime(project, {},
      {{root / "absent-banks", seam::voicebank::VoicebankRootKind::Installed}});
  CHECK(runtime.replaceProject(decoded.value()));
  CHECK(runtime.projectCopy() == project);
  seam::authoring::ProjectDocument source{project, seam::application::ProjectFactory{200U}};
  CHECK(source.replaceProject(project));
  seam::authoring::AutosaveService autosave({.root = root / "recovery"});
  CHECK(autosave.request(source));
  CHECK(autosave.flush());
  const auto recovered = autosave.discover();
  CHECK(recovered);
  CHECK(recovered.value().size() == 1U);
  seam::authoring::ProjectDocument destination{Project{}, seam::application::ProjectFactory{300U}};
  CHECK(autosave.recover(destination, recovered.value().front()));
  CHECK(destination.session().project() == project);
  CHECK(destination.dirty());
  CHECK(destination.session().project().findRegion(RegionId{10U})->performance.accepted.size() == 1U);
}
