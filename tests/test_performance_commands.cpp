#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using seam::application::EditPerformanceCommand;
using seam::application::NoteExpressionEdit;
using seam::application::RegionDynamicsEdit;
using seam::application::TrackStyleEdit;
using seam::application::RegionOwnershipEdit;
using seam::domain::NoteId;
using seam::domain::RegionId;
using seam::domain::TrackId;
using seam::time::Tick;

seam::domain::Project expressionProject() {
  seam::domain::Project project{seam::domain::ProjectId{1U}, "Expression edits"};
  seam::domain::VocalRegion region;
  region.id = RegionId{21U};
  region.name = "Verse";
  region.durationTick = Tick{1920};
  region.lyrics = {
      {seam::domain::LyricTokenId{41U}, U"か", seam::domain::Language::Japanese},
      {seam::domain::LyricTokenId{42U}, U"き", seam::domain::Language::Japanese},
  };
  seam::domain::Note first;
  first.id = NoteId{31U};
  first.durationTick = Tick{480};
  first.midiKey = 60U;
  first.lyricTokenId = region.lyrics[0].id;
  first.articulation = seam::domain::NoteArticulation::Legato;
  first.slurGroup = 71U;
  first.vibrato.enabled = true;
  first.vibrato.depthCents = 35.0F;
  first.phoneticHint = "k a";
  auto second = first;
  second.id = NoteId{32U};
  second.startTick = Tick{480};
  second.midiKey = 64U;
  second.lyricTokenId = region.lyrics[1].id;
  second.phoneticHint = "k i";
  region.notes = {first, second};
  CHECK(region.pitchAutomation.upsert({Tick{120}, 18.0F}));
  CHECK(region.dynamicsAutomation.upsert({Tick{0}, 0.8F}));

  seam::domain::VocalTrack track;
  track.id = TrackId{11U};
  track.name = "Lead";
  track.voicebank = {"singer", "1.0.0", std::string(64U, 'a')};
  track.character = {"character", "1.0.0"};
  track.gainDb = -2.0F;
  track.pan = 0.2F;
  track.regions = {region};
  track.styleSelection = {seam::domain::VoiceStyleOrigin::LegacyManifestFirst,
                          "original"};
  project.vocalTracks().push_back(track);

  track.id = TrackId{12U};
  track.name = "Harmony";
  track.regions.front().id = RegionId{22U};
  track.regions.front().notes[0].id = NoteId{33U};
  track.regions.front().notes[1].id = NoteId{34U};
  project.vocalTracks().push_back(std::move(track));
  CHECK(project.validate());
  return project;
}

seam::domain::NoteVibrato changedVibrato() {
  return {
      .enabled = true,
      .startFraction = 0.4F,
      .fadeInFraction = 0.2F,
      .fadeOutFraction = 0.3F,
      .depthCents = 90.0F,
      .periodMilliseconds = 160.0F,
      .phaseTurns = 0.25F,
  };
}

seam::domain::DynamicsAutomation changedDynamics() {
  seam::domain::DynamicsAutomation curve;
  CHECK(curve.replacePoints({{Tick{0}, 0.0F}, {Tick{960}, 1.5F}}));
  return curve;
}

void checkRejected(std::vector<NoteExpressionEdit> notes,
                   std::vector<RegionDynamicsEdit> regions = {},
                   std::vector<TrackStyleEdit> tracks = {}) {
  auto project = expressionProject();
  const auto before = project;
  EditPerformanceCommand direct{notes, regions, tracks};
  CHECK(!direct.apply(project));
  CHECK(project == before);

  seam::application::EditorSession session{before};
  CHECK(!session.execute(std::make_unique<EditPerformanceCommand>(
      std::move(notes), std::move(regions), std::move(tracks))));
  CHECK(session.project() == before);
  CHECK(session.revision() == 0U);
  CHECK(!session.canUndo());
  CHECK(!session.canRedo());
  CHECK(session.health() == seam::application::SessionHealth::Ready);
}

}

TEST_CASE("procedural selection relink and sample switching preserve exact undo state") {
  using namespace seam;
  const auto original = expressionProject();
  application::EditorSession session{original};
  const domain::ProceduralRecipeReference recipe{
      {domain::SingerResourceKind::Procedural, "draft", "1", std::string(64U, 'b')}, "recipes/draft.json", "neutral"};
  CHECK(session.execute(std::make_unique<application::SetTrackProceduralRecipeCommand>(TrackId{11U}, std::nullopt, recipe)));
  auto selected = original; selected.vocalTracks().front().proceduralRecipe = recipe;
  CHECK(session.project() == selected);
  CHECK(session.lastImpact().trackIds == std::vector<TrackId>{TrackId{11U}});
  CHECK(session.undo()); CHECK(session.project() == original);
  CHECK(session.redo()); CHECK(session.project() == selected);
  auto moved = recipe; moved.path = "relocated/draft.json";
  CHECK(session.execute(std::make_unique<application::SetTrackProceduralRecipeCommand>(TrackId{11U}, recipe, moved)));
  CHECK(session.project().vocalTracks().front().proceduralRecipe == moved);
  CHECK(session.undo()); CHECK(session.project() == selected);
  const auto revision = session.revision();
  CHECK(!session.execute(std::make_unique<application::SetTrackProceduralRecipeCommand>(TrackId{11U}, std::nullopt, moved)));
  CHECK(session.revision() == revision); CHECK(session.project() == selected);
  auto invalid = recipe; invalid.path.clear();
  CHECK(!session.execute(std::make_unique<application::SetTrackProceduralRecipeCommand>(TrackId{11U}, recipe, invalid)));
  CHECK(session.project() == selected);
  CHECK(session.execute(std::make_unique<application::SetTrackVoicebankCommand>(TrackId{11U},
      original.vocalTracks().front().voicebank)));
  CHECK(!session.project().vocalTracks().front().proceduralRecipe);
  CHECK(session.undo()); CHECK(session.project() == selected);
  CHECK(session.redo()); CHECK(session.project() == original);
  application::SetTrackProceduralRecipeCommand guarded{TrackId{11U}, std::nullopt, recipe};
  auto direct = original; CHECK(guarded.apply(direct));
  direct.vocalTracks().front().proceduralRecipe = moved;
  CHECK(!guarded.revert(direct)); CHECK(direct.vocalTracks().front().proceduralRecipe == moved);
}

TEST_CASE("neural singer selection is guarded, reversible and rejects a stale chooser result") {
  using namespace seam;
  const auto original = expressionProject();
  application::EditorSession session{original};
  const domain::NeuralResourceReference singer{
      {domain::SingerResourceKind::Neural, "seam-pilot-01", "1.0.0", std::string(64U, 'c')}};
  CHECK(session.execute(std::make_unique<application::SetTrackNeuralResourceCommand>(
      TrackId{11U}, std::nullopt, singer)));
  auto selected = original;
  selected.vocalTracks().front().neuralResource = singer;
  CHECK(session.project() == selected);
  CHECK(session.lastImpact().trackIds == std::vector<TrackId>{TrackId{11U}});
  CHECK(session.undo());
  CHECK(session.project() == original);
  CHECK(session.redo());
  CHECK(session.project() == selected);
  // A chooser result computed against a selection that has since changed is
  // refused rather than overwriting the newer choice.
  auto newer = singer;
  newer.resource.contentHash = std::string(64U, 'd');
  auto stale = session.revision();
  CHECK(!session.execute(std::make_unique<application::SetTrackNeuralResourceCommand>(
      TrackId{11U}, std::nullopt, newer)));
  CHECK(session.revision() == stale);
  CHECK(session.project() == selected);
  // A reference whose identity is not a neural resource is rejected outright.
  auto invalid = singer;
  invalid.resource.kind = domain::SingerResourceKind::Sample;
  CHECK(!session.execute(std::make_unique<application::SetTrackNeuralResourceCommand>(
      TrackId{11U}, singer, invalid)));
  CHECK(session.project() == selected);
  CHECK(session.execute(std::make_unique<application::SetTrackNeuralResourceCommand>(
      TrackId{11U}, singer, std::nullopt)));
  CHECK(!session.project().vocalTracks().front().neuralResource.has_value());
  CHECK(session.undo());
  CHECK(session.project() == selected);
  CHECK(!session.execute(std::make_unique<application::SetTrackNeuralResourceCommand>(
      TrackId{9999U}, std::nullopt, singer)));
}

TEST_CASE("performance take selection is atomic reversible and rejects stale proposals") {
  using namespace seam::domain;
  auto project = expressionProject();
  auto& region = *project.findRegion(RegionId{21U});
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(pronunciation);
  region.performance.ownership = {{PerformanceChannel::Attack, NoteId{31U}, ManualPerformanceMode::Replace, {}}};
  region.performance.takes = {{.id = "attack", .sourceRegionId = region.id,
      .capturedRevision = region.performance.revision,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity,
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Attack, {{Tick{0}, 100.0}}}}}};
  const auto initial = project;
  const auto expected = region.performance;
  const std::vector<AcceptedPerformanceSelection> selected{{"attack", PerformanceChannel::Attack, NoteId{31U}, Tick{0}}};
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetAcceptedPerformanceCommand>(region.id, expected, selected)));
  CHECK(session.revision() == 1U);
  const auto accepted = session.project();
  const auto& state = accepted.findRegion(region.id)->performance;
  CHECK(state.accepted == selected);
  CHECK(state.ownership == expected.ownership);
  CHECK(state.takes == expected.takes);
  CHECK(state.revision.ownership == expected.revision.ownership + 1U);
  CHECK(session.undo()); CHECK(session.project() == initial);
  CHECK(session.redo()); CHECK(session.project() == accepted);
  CHECK(session.execute(std::make_unique<seam::application::SetAcceptedPerformanceCommand>(region.id, state,
      std::vector<AcceptedPerformanceSelection>{})));
  CHECK(session.project().findRegion(region.id)->performance.accepted.empty());
  CHECK(session.project().findRegion(region.id)->performance.takes == expected.takes);
  CHECK(session.undo()); CHECK(session.project() == accepted);
  for (int invalid = 0; invalid < 6; ++invalid) {
    auto bad = initial;
    auto& badState = bad.findRegion(region.id)->performance;
    auto selection = selected;
    if (invalid == 0) ++badState.revision.musical;
    if (invalid == 1) badState.takes[0].pronunciation.sequenceHash = std::string(64U, 'f');
    if (invalid == 2) badState.takes[0].state = PerformanceProposalState::Rejected;
    if (invalid == 3) selection[0].takeId = "missing";
    if (invalid == 4) selection.push_back(selection.front());
    if (invalid == 5) selection[0].sourceTickOffset = Tick{1920};
    const auto before = bad;
    seam::application::SetAcceptedPerformanceCommand command{region.id, badState, selection};
    CHECK(!command.apply(bad)); CHECK(bad == before);
  }
  seam::application::SetAcceptedPerformanceCommand outdated{region.id, expected, selected};
  ++project.findRegion(region.id)->performance.revision.ownership;
  const auto changed = project;
  CHECK(!outdated.apply(project)); CHECK(project == changed);
}

TEST_CASE("combined performance edits commit as one revision and restore exact history") {
  const auto before = expressionProject();
  auto expected = before;
  seam::application::SetNoteHintsCommand expectedHints{{
      {NoteId{31U}, "k a", "ky a"}, {NoteId{32U}, "k i", std::nullopt}}};
  CHECK(expectedHints.apply(expected));
  expected.findNote(NoteId{31U})->vibrato = changedVibrato();
  expected.findNote(NoteId{31U})->phoneticHint = "ky a";
  expected.findNote(NoteId{32U})->vibrato = {};
  expected.findNote(NoteId{32U})->phoneticHint.reset();
  expected.findRegion(RegionId{21U})->dynamicsAutomation = changedDynamics();
  expected.findVocalTrack(TrackId{11U})->styleSelection = {
      seam::domain::VoiceStyleOrigin::Explicit, "soft"};

  seam::application::EditorSession session{before};
  CHECK(session.execute(std::make_unique<EditPerformanceCommand>(
      std::vector<NoteExpressionEdit>{
          {NoteId{31U}, changedVibrato(), "ky a"},
          {NoteId{32U}, {}, std::nullopt}},
      std::vector<RegionDynamicsEdit>{{RegionId{21U}, changedDynamics()}},
      std::vector<TrackStyleEdit>{{TrackId{11U},
                                  {seam::domain::VoiceStyleOrigin::Explicit,
                                   "soft"}}})));
  CHECK(session.revision() == 1U);
  CHECK(session.project() == expected);
  CHECK(session.project().findRegion(RegionId{21U})->performance.revision.pronunciation == 1U);
  CHECK(session.lastImpact().scope == seam::application::CommandAudioImpact::ProjectAudio);
  CHECK(!session.lastImpact().projectWide);
  CHECK((session.lastImpact().noteIds == std::vector<NoteId>{NoteId{31U}, NoteId{32U}}));
  CHECK((session.lastImpact().regionIds == std::vector<RegionId>{RegionId{21U}}));
  CHECK((session.lastImpact().trackIds == std::vector<TrackId>{TrackId{11U}}));

  CHECK(session.undo());
  CHECK(session.revision() == 2U);
  CHECK(session.project() == before);
  CHECK(!session.canUndo());
  CHECK(session.redo());
  CHECK(session.revision() == 3U);
  CHECK(session.project() == expected);
  CHECK(session.undo());
  CHECK(session.project() == before);
}

TEST_CASE("explicit manual ownership and expressions share one undo transaction") {
  const auto before = expressionProject();
  seam::application::EditorSession session{before};
  seam::domain::ManualPerformanceOwnership owner;
  owner.scope = NoteId{31U};
  CHECK(session.execute(std::make_unique<EditPerformanceCommand>(
      std::vector<NoteExpressionEdit>{{NoteId{31U}, {}, "a"}},
      std::vector<RegionDynamicsEdit>{{RegionId{21U}, changedDynamics()}},
      std::vector<TrackStyleEdit>{{TrackId{11U},
          {seam::domain::VoiceStyleOrigin::Explicit, "soft"}}},
      std::vector<RegionOwnershipEdit>{{RegionId{21U}, {}, {}, {owner}}})));
  const auto after = session.project();
  const auto& state = after.findRegion(RegionId{21U})->performance;
  CHECK(state.revision.ownership == 1U);
  CHECK(state.ownership.size() == 1U);
  CHECK(state.ownership.front().revision == state.revision);
  CHECK(!state.permitsGenerated(seam::domain::PerformanceChannel::Pitch,
                               NoteId{31U}, Tick{0}, false));
  CHECK(session.revision() == 1U);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
}

TEST_CASE("invalid or stale ownership cannot partially apply coupled expressions") {
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto project = expressionProject();
    auto& state = project.findRegion(RegionId{21U})->performance;
    if (scenario == 3U) state.revision.ownership = std::numeric_limits<std::uint64_t>::max();
    const auto before = project;
    seam::domain::ManualPerformanceOwnership owner;
    owner.scope = scenario == 0U ? NoteId{999U} : NoteId{31U};
    RegionOwnershipEdit edit{RegionId{21U}, state.revision, {}, {owner}};
    if (scenario == 1U) edit.expectedRevision.musical = 1U;
    if (scenario == 2U) edit.expectedOwnership.push_back(owner);
    EditPerformanceCommand command{
        {{NoteId{31U}, changedVibrato(), "changed"}}, {}, {}, {edit}};
    CHECK(!command.apply(project));
    CHECK(project == before);
  }
}

TEST_CASE("mixed hint and ownership edits retain combined revisions and reject invalid hints atomically") {
  auto project = expressionProject(); const auto before = project;
  seam::domain::ManualPerformanceOwnership owner; owner.scope = NoteId{31U};
  EditPerformanceCommand bad{{{NoteId{31U}, changedVibrato(), "not-a-phone"}}, {}, {},
      {{RegionId{21U}, {}, {}, {owner}}}};
  CHECK(!bad.apply(project)); CHECK(project == before); CHECK(!bad.revert(project));
  EditPerformanceCommand command{{{NoteId{31U}, changedVibrato(), "sh a"}}, {}, {},
      {{RegionId{21U}, {}, {}, {owner}}}};
  CHECK(command.apply(project));
  const auto& state = project.findRegion(RegionId{21U})->performance;
  CHECK(state.revision.pronunciation == 1U); CHECK(state.revision.ownership == 1U);
  CHECK(state.ownership.front().revision == state.revision);
  CHECK(project.findRegion(RegionId{21U})->lyrics == before.findRegion(RegionId{21U})->lyrics);
  const auto after = project;
  CHECK(command.revert(project)); CHECK(project == before);
  CHECK(command.apply(project)); CHECK(project == after);
}

TEST_CASE("ownership only edits publish region impact and reject duplicate targets") {
  auto project = expressionProject();
  seam::domain::ManualPerformanceOwnership owner;
  owner.scope = NoteId{31U};
  RegionOwnershipEdit edit{RegionId{21U}, {}, {}, {owner}};
  EditPerformanceCommand duplicate{{}, {}, {}, {edit, edit}};
  const auto before = project;
  CHECK(!duplicate.apply(project));
  CHECK(project == before);
  EditPerformanceCommand command{{}, {}, {}, {edit}};
  CHECK((command.impact().regionIds == std::vector<RegionId>{RegionId{21U}}));
  CHECK(command.apply(project));
  CHECK(command.revert(project));
  CHECK(project == before);
}

TEST_CASE("performance copy transfers complete unit spans and retains partial spans unresolved") {
  for (bool complete : {false, true}) {
    auto project = expressionProject();
    auto* region = project.findRegion(RegionId{21U});
    auto a = region->notes[0];
    auto b = region->notes[1];
    a.id = NoteId{33U}; a.startTick = Tick{960};
    b.id = NoteId{34U}; b.startTick = Tick{1440};
    region->notes.push_back(a); region->notes.push_back(b);
    region->unitSelectionOverrides = {{.startKey = {NoteId{31U}, 0U}, .tokenCount = 3U,
        .unitId = "cross-note", .renderer = seam::domain::UnitRendererKind::ClassicPsola, .loopPrint = 0.25F}};
    region->seamOverrides = {{.incomingStartKey = {NoteId{31U}, 1U}, .seamAmount = 0.2F},
        {.incomingStartKey = {NoteId{32U}, 0U}, .seamAmount = 0.7F}};
    const auto before = project;
    std::vector<seam::domain::PerformanceNoteRemap> mapping{{NoteId{31U}, NoteId{33U}}};
    if (complete) mapping.push_back({NoteId{32U}, NoteId{34U}});
    seam::application::CopyNotePerformanceCommand command{RegionId{21U}, mapping};
    CHECK(command.apply(project));
    region = project.findRegion(RegionId{21U});
    CHECK(region->unitSelectionOverrides.size() == 2U);
    auto expected = before.findRegion(RegionId{21U})->unitSelectionOverrides.front();
    expected.startKey.noteId = NoteId{33U}; expected.unresolved = !complete;
    CHECK(region->unitSelectionOverrides.back() == expected);
    CHECK(!region->findSeamOverride({NoteId{33U}, 1U})->unresolved);
    if (complete) CHECK(!region->findSeamOverride({NoteId{34U}, 0U})->unresolved);
    CHECK(region->unitSelectionOverrides.front() == before.findRegion(RegionId{21U})->unitSelectionOverrides.front());
    const auto after = project;
    CHECK(command.revert(project)); CHECK(project == before);
    CHECK(command.apply(project)); CHECK(project == after);
  }
}

TEST_CASE("performance copy rejects occupied unit and seam targets atomically") {
  for (bool unit : {false, true}) {
    auto project = expressionProject();
    auto* region = project.findRegion(RegionId{21U});
    if (unit) region->unitSelectionOverrides = {{.startKey = {NoteId{31U}, 0U}, .unitId = "source"},
        {.startKey = {NoteId{32U}, 0U}, .unitId = "occupied"}};
    else region->seamOverrides = {{.incomingStartKey = {NoteId{31U}, 0U}, .seamAmount = 0.2F},
        {.incomingStartKey = {NoteId{32U}, 0U}, .seamAmount = 0.7F}};
    const auto before = project;
    seam::application::CopyNotePerformanceCommand command{RegionId{21U}, {{NoteId{31U}, NoteId{32U}}}};
    CHECK(!command.apply(project)); CHECK(project == before);
  }
}

TEST_CASE("performance copy rejects ambiguous mappings without changing state") {
  const std::vector<std::vector<seam::domain::PerformanceNoteRemap>> mappings{
      {}, {{NoteId{31U}, NoteId{31U}}}, {{NoteId{31U}, NoteId{999U}}},
      {{NoteId{31U}, NoteId{32U}}, {NoteId{31U}, NoteId{32U}}},
      {{NoteId{31U}, NoteId{32U}}, {NoteId{32U}, NoteId{31U}}}};
  for (const auto& mapping : mappings) {
    auto project = expressionProject();
    const auto before = project;
    seam::application::CopyNotePerformanceCommand command{RegionId{21U}, mapping};
    CHECK(!command.apply(project));
    CHECK(project == before);
  }
}

TEST_CASE("performance copy retains fixed region scopes and rejects ownership collisions") {
  auto project = expressionProject();
  auto& state = project.findRegion(RegionId{21U})->performance;
  using namespace seam::domain;
  state.ownership = {
      {PerformanceChannel::Pitch, NoteId{31U}, ManualPerformanceMode::Replace, {}},
      {PerformanceChannel::Pitch, PerformanceTimeRange{Tick{480}, Tick{960}},
       ManualPerformanceMode::Replace, {}}};
  CHECK(project.validate());
  const auto before = project;
  seam::application::CopyNotePerformanceCommand command{
      RegionId{21U}, {{NoteId{31U}, NoteId{32U}}}};
  CHECK(!command.apply(project));
  CHECK(project == before);
}

TEST_CASE("performance copy rejects phoneme key collisions without partial ownership changes") {
  auto project = expressionProject();
  auto* region = project.findRegion(RegionId{21U});
  region->phonemeOverrides = {{.key = {NoteId{31U}, 0U}, .locked = true},
                             {.key = {NoteId{32U}, 0U}, .locked = true}};
  const auto before = project;
  seam::application::CopyNotePerformanceCommand copy{RegionId{21U}, {{NoteId{31U}, NoteId{32U}}}};
  CHECK(!copy.apply(project));
  CHECK(project == before);
}

TEST_CASE("expression-only edits publish phrase impact before application") {
  EditPerformanceCommand command{
      {{NoteId{31U}, changedVibrato(), std::nullopt}},
      {{RegionId{21U}, changedDynamics()}}};
  const auto impact = command.impact();
  CHECK(command.audioImpact() == seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(impact.scope == seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(!impact.projectWide);
  CHECK(impact.trackIds.empty());
  CHECK((impact.noteIds == std::vector<NoteId>{NoteId{31U}}));
  CHECK((impact.regionIds == std::vector<RegionId>{RegionId{21U}}));

  seam::application::EditorSession session{expressionProject()};
  CHECK(session.execute(std::make_unique<EditPerformanceCommand>(
      std::vector<NoteExpressionEdit>{{NoteId{31U}, changedVibrato(), std::nullopt}},
      std::vector<RegionDynamicsEdit>{{RegionId{21U}, changedDynamics()}})));
  CHECK(session.lastImpact().scope == seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(session.undo());
  CHECK(session.lastImpact().noteIds == impact.noteIds);
  CHECK(session.lastImpact().regionIds == impact.regionIds);
}

TEST_CASE("bank replacement resets unrelated style provenance and restores it on undo") {
  auto project = expressionProject();
  project.findVocalTrack(TrackId{11U})->styleSelection = {
      seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution, {}};
  const auto before = project;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetTrackVoicebankCommand>(
      TrackId{11U}, seam::domain::VoicebankReference{"other-singer", "2.0.0", std::string(64U, 'b')})));
  CHECK(session.project().findVocalTrack(TrackId{11U})->styleSelection ==
        seam::domain::VoiceStyleSelection{});
  const auto changed = session.project();
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == changed);
}

TEST_CASE("same singer updates preserve a known style choice without substitution") {
  auto project = expressionProject();
  project.findVocalTrack(TrackId{11U})->styleSelection = {
      seam::domain::VoiceStyleOrigin::Explicit, "soft"};
  const auto selection = project.findVocalTrack(TrackId{11U})->styleSelection;
  seam::application::EditorSession session{project};
  CHECK(session.execute(std::make_unique<seam::application::SetTrackVoicebankCommand>(
      TrackId{11U}, seam::domain::VoicebankReference{"singer", "2.0.0", std::string(64U, 'b')})));
  CHECK(session.project().findVocalTrack(TrackId{11U})->styleSelection == selection);
}

TEST_CASE("performance validation rejects malformed fields before touching valid targets") {
  const NoteExpressionEdit valid{NoteId{31U}, changedVibrato(), "a"};
  auto invalidVibrato = changedVibrato();
  invalidVibrato.depthCents = std::numeric_limits<float>::quiet_NaN();
  checkRejected({valid, {NoteId{32U}, invalidVibrato, "i"}});
  checkRejected({valid, {NoteId{32U}, {}, std::string{}}});
  checkRejected({valid, {NoteId{32U}, {}, std::string(4097U, 'a')}});
  checkRejected({valid, {NoteId{32U}, {}, std::string(1U, '\xff')}});
  checkRejected({valid}, {}, {{TrackId{11U},
                              {seam::domain::VoiceStyleOrigin::Explicit, {}}}});

  seam::domain::DynamicsAutomation outOfRegion;
  CHECK(outOfRegion.upsert({Tick{1921}, 1.0F}));
  checkRejected({valid}, {{RegionId{21U}, outOfRegion}});
}

TEST_CASE("missing or duplicate performance targets reject the whole batch") {
  const NoteExpressionEdit valid{NoteId{31U}, changedVibrato(), "a"};
  checkRejected({valid, {NoteId{999U}, {}, std::nullopt}});
  checkRejected({valid, valid});
  checkRejected({valid}, {{RegionId{999U}, changedDynamics()}});
  checkRejected({valid}, {{RegionId{21U}, changedDynamics()},
                          {RegionId{21U}, {}}});
  const seam::domain::VoiceStyleSelection style{
      seam::domain::VoiceStyleOrigin::Explicit, "soft"};
  checkRejected({valid}, {}, {{TrackId{999U}, style}});
  checkRejected({valid}, {}, {{TrackId{11U}, style}, {TrackId{11U}, style}});
  checkRejected({});
}

TEST_CASE("performance undo restores touched fields without replacing unrelated state") {
  auto project = expressionProject();
  const auto initialVibrato = project.findNote(NoteId{31U})->vibrato;
  const auto initialHint = project.findNote(NoteId{31U})->phoneticHint;
  EditPerformanceCommand command{
      {{NoteId{31U}, changedVibrato(), "a"}}};
  CHECK(!command.revert(project));
  CHECK(command.apply(project));
  project.setName("Renamed after edit");
  project.findNote(NoteId{31U})->midiKey = 67U;
  project.findVocalTrack(TrackId{12U})->gainDb = -9.0F;
  CHECK(command.revert(project));
  CHECK(project.findNote(NoteId{31U})->vibrato == initialVibrato);
  CHECK(project.findNote(NoteId{31U})->phoneticHint == initialHint);
  CHECK(project.name() == "Renamed after edit");
  CHECK(project.findNote(NoteId{31U})->midiKey == 67U);
  CHECK(project.findVocalTrack(TrackId{12U})->gainDb == -9.0F);
  CHECK(project.validate());
}
