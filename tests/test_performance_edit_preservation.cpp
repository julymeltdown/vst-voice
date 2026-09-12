#include "test_framework.hpp"

#include "seam/application/arrangement_commands.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/ui/piano_roll_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

using seam::domain::DynamicsAutomationPoint;
using seam::time::Tick;

seam::domain::Project splitExpressionProject(
    std::vector<DynamicsAutomationPoint> points) {
  seam::application::ProjectFactory factory{100U};
  auto project = factory.createProject("Preserve split expression");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(
      project, trackId, "Phrase", Tick{960}, Tick{960});
  auto [leftLyric, leftNote] = factory.makeNote(Tick{0}, Tick{240}, 60U, U"あ");
  auto [rightLyric, rightNote] = factory.makeNote(Tick{720}, Tick{240}, 64U, U"い");
  leftNote.vibrato.enabled = true;
  leftNote.vibrato.depthCents = 65.0F;
  leftNote.phoneticHint = "a";
  rightNote.vibrato.periodMilliseconds = 130.0F;
  rightNote.phoneticHint = "i";
  auto* region = project.findRegion(regionId);
  region->lyrics = {leftLyric, rightLyric};
  region->notes = {leftNote, rightNote};
  CHECK(region->dynamicsAutomation.replacePoints(std::move(points)));
  CHECK(project.validate());
  return project;
}

seam::domain::RegionId executeSplit(seam::application::EditorSession& session,
                                    Tick splitTick) {
  const auto& track = session.project().vocalTracks().front();
  auto command = std::make_unique<seam::application::SplitVocalRegionCommand>(
      track.id, track.regions.front().id, splitTick);
  const auto* observed = command.get();
  CHECK(session.execute(std::move(command)));
  return observed->splitRegionId();
}

}

TEST_CASE("piano roll duplication preserves note expressions through undo and redo") {
  seam::application::ProjectFactory factory{1000U};
  auto project = factory.createProject("Duplicate expression");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(
      project, trackId, "Phrase", Tick{0}, Tick{3840});
  auto [firstLyric, firstNote] = factory.makeNote(Tick{0}, Tick{240}, 60U, U"あ");
  firstNote.articulation = seam::domain::NoteArticulation::Legato;
  firstNote.slurGroup = 8U;
  firstNote.vibrato = {
      .enabled = true,
      .startFraction = 0.3F,
      .fadeInFraction = 0.15F,
      .fadeOutFraction = 0.25F,
      .depthCents = 75.0F,
      .periodMilliseconds = 145.0F,
      .phaseTurns = 0.35F,
  };
  firstNote.phoneticHint = "a";
  auto [secondLyric, secondNote] = factory.makeNote(Tick{960}, Tick{240}, 64U, U"い");
  secondNote.vibrato.depthCents = 27.0F;
  secondNote.vibrato.phaseTurns = 0.6F;
  auto* region = project.findRegion(regionId);
  region->lyrics = {firstLyric, secondLyric};
  region->notes = {firstNote, secondNote};
  using namespace seam::domain;
  const PronunciationIdentity identity{Language::Japanese, "test-ja", "1",
      std::string(64U, 'a'), std::string(64U, 'b'), std::string(64U, 'c')};
  region->performance.pronunciation = identity;
  region->performance.ownership = {
      {PerformanceChannel::Pitch, firstNote.id, ManualPerformanceMode::Replace, {}},
      {PerformanceChannel::Tension, PerformanceTimeRange{Tick{0}, Tick{240}},
       ManualPerformanceMode::Replace, {}}};
  region->performance.takes = {{.id = "diagnostic-take", .sourceRegionId = regionId,
      .resource = {SingerResourceKind::Procedural, "fixture", "1", std::string(64U, 'd')},
      .pronunciation = identity, .generatorId = "fixture", .generatorVersion = "1",
      .range = {Tick{0}, Tick{240}},
      .lanes = {{PerformanceChannel::Dynamics, {{Tick{0}, 0.5}, {Tick{240}, 1.0}}}}}};
  region->performance.accepted = {
      {"diagnostic-take", PerformanceChannel::Dynamics, firstNote.id, Tick{0}}};
  seam::application::UpsertPhonemeOverrideCommand phoneme{regionId,
      PhonemeOverride{.key = {firstNote.id, 0U}, .timing = {.startOffset = -1400}, .locked = true}};
  CHECK(phoneme.apply(project));
  region->unitSelectionOverrides = {{.startKey = {firstNote.id, 0U}, .tokenCount = 2U, .unitId = "copied-span"}};
  region->seamOverrides = {{.incomingStartKey = {secondNote.id, 0U}, .seamAmount = 0.4F}};
  CHECK(project.validate());
  const auto before = project;
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  session.selection().replace({firstNote.id, secondNote.id});

  const auto duplicatedFirst = model.duplicateSelection();
  CHECK(duplicatedFirst);
  CHECK(session.revision() == 1U);
  CHECK(session.project().noteCount() == 4U);
  const auto copiedIds = session.selection().noteIds();
  CHECK(copiedIds.size() == 2U);
  CHECK(session.selection().contains(duplicatedFirst.value()));
  const auto secondId = copiedIds[0] == duplicatedFirst.value()
                            ? copiedIds[1] : copiedIds[0];
  const auto* firstCopy = session.project().findNote(duplicatedFirst.value());
  const auto* secondCopy = session.project().findNote(secondId);
  CHECK(firstCopy != nullptr);
  CHECK(secondCopy != nullptr);
  CHECK(firstCopy->id != firstNote.id);
  CHECK(firstCopy->lyricTokenId != firstNote.lyricTokenId);
  CHECK(firstCopy->midiKey == firstNote.midiKey);
  const auto* copiedRegion = session.project().findRegion(regionId);
  const auto* copiedUnit = copiedRegion->findUnitSelectionOverride({firstCopy->id, 0U});
  CHECK(copiedUnit != nullptr);
  CHECK(copiedUnit->unitId == "copied-span");
  // Common phrase translation preserves the full copied cross-note span.
  // Rebinding must retain it as usable rather than silently dropping tuning.
  CHECK(!copiedUnit->unresolved);
  CHECK(copiedUnit->tokenCount == 2U);
  const auto* copiedSeam = copiedRegion->findSeamOverride({secondId, 0U});
  CHECK(copiedSeam != nullptr);
  CHECK(copiedSeam->seamAmount == 0.4F);
  CHECK(!copiedSeam->unresolved);
  const auto* copiedEdit = copiedRegion->findPhonemeOverride({firstCopy->id, 0U});
  CHECK(copiedEdit != nullptr);
  CHECK(copiedEdit->timing.startOffset == -1400);
  CHECK(copiedEdit->sourceContextId.has_value());
  CHECK(copiedEdit->sourceContextId != before.findRegion(regionId)->phonemeOverrides.front().sourceContextId);
  CHECK(!copiedEdit->unresolved);
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*copiedRegion);
  CHECK(resolved);
  CHECK(resolved.value().pronunciation.tokensForNote(firstCopy->id).front().locked);
  const auto& copiedPerformance = session.project().findRegion(regionId)->performance;
  CHECK(copiedPerformance.ownership.size() == 3U);
  CHECK(copiedPerformance.ownership[1] == before.findRegion(regionId)->performance.ownership[1]);
  CHECK(std::get<NoteId>(copiedPerformance.ownership.back().scope) == firstCopy->id);
  CHECK(copiedPerformance.accepted.size() == 2U);
  CHECK(std::get<NoteId>(copiedPerformance.accepted.back().scope) == firstCopy->id);
  CHECK(copiedPerformance.accepted.back().sourceTickOffset ==
        firstNote.startTick - firstCopy->startTick);
  CHECK(copiedPerformance.takes == before.findRegion(regionId)->performance.takes);
  CHECK(!copiedPerformance.pronunciation.has_value());
  CHECK(secondCopy->midiKey == secondNote.midiKey);
  CHECK(firstCopy->vibrato == firstNote.vibrato);
  CHECK(firstCopy->phoneticHint == firstNote.phoneticHint);
  CHECK(firstCopy->articulation == firstNote.articulation);
  CHECK(firstCopy->slurGroup.has_value());
  CHECK(firstCopy->slurGroup != firstNote.slurGroup);
  CHECK(secondCopy->startTick - firstCopy->startTick == secondNote.startTick - firstNote.startTick);
  CHECK(firstCopy->startTick > secondNote.endTick());
  CHECK(secondCopy->vibrato == secondNote.vibrato);
  CHECK(secondCopy->phoneticHint == secondNote.phoneticHint);
  CHECK(*session.project().findNote(firstNote.id) == firstNote);
  CHECK(*session.project().findNote(secondNote.id) == secondNote);
  const auto duplicated = session.project();
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == duplicated);
}

TEST_CASE("region split preserves interpolated dynamics and shifts the right curve") {
  const auto before = splitExpressionProject(
      {{Tick{0}, 0.2F}, {Tick{240}, 0.4F}, {Tick{720}, 1.2F}, {Tick{960}, 0.8F}});
  const auto leftId = before.vocalTracks().front().regions.front().id;
  seam::application::EditorSession session{before};
  const auto rightId = executeSplit(session, Tick{480});
  const auto* left = session.project().findRegion(leftId);
  const auto* right = session.project().findRegion(rightId);
  CHECK(left != nullptr);
  CHECK(right != nullptr);
  CHECK(left->durationTick == Tick{480});
  CHECK(right->startTick == Tick{1440});
  CHECK(right->durationTick == Tick{480});
  CHECK(left->dynamicsAutomation.points().back().tick == Tick{480});
  CHECK(right->dynamicsAutomation.points().front().tick == Tick{0});
  CHECK(right->dynamicsAutomation.points().back().tick == Tick{480});
  CHECK_NEAR(left->dynamicsAutomation.valueAt(Tick{0}), 0.2, 1.0e-6);
  CHECK_NEAR(left->dynamicsAutomation.valueAt(Tick{120}), 0.3, 1.0e-6);
  CHECK_NEAR(left->dynamicsAutomation.valueAt(Tick{360}), 0.6, 1.0e-6);
  CHECK_NEAR(left->dynamicsAutomation.valueAt(Tick{480}), 0.8, 1.0e-6);
  CHECK_NEAR(right->dynamicsAutomation.valueAt(Tick{0}), 0.8, 1.0e-6);
  CHECK_NEAR(right->dynamicsAutomation.valueAt(Tick{120}), 1.0, 1.0e-6);
  CHECK_NEAR(right->dynamicsAutomation.valueAt(Tick{240}), 1.2, 1.0e-6);
  CHECK_NEAR(right->dynamicsAutomation.valueAt(Tick{360}), 1.0, 1.0e-6);
  CHECK_NEAR(right->dynamicsAutomation.valueAt(Tick{480}), 0.8, 1.0e-6);
  CHECK(left->notes.front().vibrato == before.findRegion(leftId)->notes.front().vibrato);
  CHECK(right->notes.front().phoneticHint == before.findRegion(leftId)->notes.back().phoneticHint);
  CHECK(session.project().validate());
  const auto split = session.project();
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == split);
}

TEST_CASE("region split preserves empty curves and constant endpoint extrapolation") {
  struct Case final {
    std::vector<DynamicsAutomationPoint> points;
    float gain;
  };
  const std::array cases{
      Case{{}, 1.0F},
      Case{{{Tick{0}, 0.35F}}, 0.35F},
      Case{{{Tick{480}, 0.75F}}, 0.75F},
      Case{{{Tick{960}, 1.1F}}, 1.1F},
  };
  for (const auto& example : cases) {
    const auto before = splitExpressionProject(example.points);
    const auto leftId = before.vocalTracks().front().regions.front().id;
    seam::application::EditorSession session{before};
    const auto rightId = executeSplit(session, Tick{480});
    const auto* left = session.project().findRegion(leftId);
    const auto* right = session.project().findRegion(rightId);
    CHECK(left != nullptr);
    CHECK(right != nullptr);
    for (const auto tick : {Tick{0}, Tick{240}, Tick{480}}) {
      CHECK_NEAR(left->dynamicsAutomation.valueAt(tick), example.gain, 1.0e-6);
      CHECK_NEAR(right->dynamicsAutomation.valueAt(tick), example.gain, 1.0e-6);
    }
    if (example.points.empty()) {
      CHECK(left->dynamicsAutomation.points().empty());
      CHECK(right->dynamicsAutomation.points().empty());
    }
    CHECK(session.project().validate());
    const auto split = session.project();
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == split);
  }
}

TEST_CASE("invalid region splits leave note and dynamics expression unchanged") {
  const auto before = splitExpressionProject({{Tick{0}, 0.4F}, {Tick{960}, 1.2F}});
  for (const auto splitTick : {Tick{0}, Tick{120}, Tick{800}, Tick{960}}) {
    seam::application::EditorSession session{before};
    const auto& track = session.project().vocalTracks().front();
    CHECK(!session.execute(std::make_unique<seam::application::SplitVocalRegionCommand>(
        track.id, track.regions.front().id, splitTick)));
    CHECK(session.project() == before);
    CHECK(session.revision() == 0U);
    CHECK(!session.canUndo());
  }
}

TEST_CASE("splitting a full one-sided dynamics curve keeps every point within its limit") {
  for (const bool pointsOnRight : {false, true}) {
    auto project = splitExpressionProject({});
    auto& region = project.vocalTracks().front().regions.front();
    region.durationTick = Tick{40000};
    const auto leftId = region.id;
    std::vector<DynamicsAutomationPoint> points;
    points.reserve(seam::domain::kMaximumDynamicsPoints);
    for (std::size_t index = 0U; index < seam::domain::kMaximumDynamicsPoints; ++index) {
      const auto offset = static_cast<std::int64_t>(index);
      points.push_back({Tick{pointsOnRight ? 20001 + offset : offset},
                        index % 2U == 0U ? 0.25F : 0.75F});
    }
    CHECK(region.dynamicsAutomation.replacePoints(std::move(points)));
    CHECK(project.validate());
    const auto before = project;
    seam::application::EditorSession session{std::move(project)};
    const auto rightId = executeSplit(session, Tick{20000});
    const auto* left = session.project().findRegion(leftId);
    const auto* right = session.project().findRegion(rightId);
    CHECK(left != nullptr);
    CHECK(right != nullptr);
    const auto& retained = pointsOnRight ? right->dynamicsAutomation.points()
                                         : left->dynamicsAutomation.points();
    CHECK(retained.size() == seam::domain::kMaximumDynamicsPoints);
    for (std::size_t index = 0U; index < retained.size(); ++index) {
      const auto offset = static_cast<std::int64_t>(index);
      CHECK(retained[index].tick == Tick{pointsOnRight ? offset + 1 : offset});
      CHECK_NEAR(retained[index].linearGain, index % 2U == 0U ? 0.25F : 0.75F, 1.0e-6);
    }
    const auto boundaryGain = pointsOnRight ? 0.25F : 0.75F;
    CHECK_NEAR(left->dynamicsAutomation.valueAt(Tick{20000}), boundaryGain, 1.0e-6);
    CHECK_NEAR(right->dynamicsAutomation.valueAt(Tick{0}), boundaryGain, 1.0e-6);
    CHECK(session.project().validate());
    CHECK(session.undo());
    CHECK(session.project() == before);
  }
}
