#include "test_framework.hpp"

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/technical_edit_controller.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TechnicalFixture final {
  seam::domain::RegionId regionId{};
  seam::authoring::ProjectDocument document;
  seam::domain::PhonemeKey firstKey{};
  std::uint64_t renderRequests{0U};
  seam::authoring::TechnicalEditController controller;

  TechnicalFixture()
      : document(makeDocument(regionId)),
        firstKey{.noteId = document.session().project()
                               .findRegion(regionId)
                               ->notes.front()
                               .id,
                 .ordinal = 0U},
        controller(document, regionId,
                   [] {
                     seam::authoring::TechnicalRenderView view;
                     view.units.push_back(seam::authoring::TechnicalUnitView{
                         .entry = seam::synthesis::UnitPlanEntry{
                             .unitId = "unit-a",
                             .tokenStart = 0U,
                             .tokenCount = 2U,
                             .score = 0.0,
                             .targetMidi = 60,
                             .forced = false,
                             .renderer = seam::domain::UnitRendererKind::Raw,
                             .alternatives = {"unit-b"},
                         },
                         .usedFallback = true,
                         .diagnostic = "spectral fallback",
                     });
                     return view;
                   },
                   [this] { ++renderRequests; }) {}

  static seam::authoring::ProjectDocument makeDocument(
      seam::domain::RegionId& regionId) {
    seam::application::ProjectFactory factory{1000U};
    auto project = makeProject(factory, regionId);
    return seam::authoring::ProjectDocument(
        std::move(project),
        seam::application::ProjectFactory{factory.nextIdValue()});
  }

  static seam::domain::Project makeProject(
      seam::application::ProjectFactory& factory,
      seam::domain::RegionId& regionId) {
    auto project = factory.createProject("Technical edit test");
    const auto trackId = factory.addVocalTrack(project, "VOICE");
    regionId = factory.addRegion(project, trackId, "REGION",
                                 seam::time::Tick{0},
                                 seam::time::Tick{3840});
    auto* region = project.findRegion(regionId);
    auto [lyricA, noteA] = factory.makeNote(
        seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"こ");
    auto [lyricB, noteB] = factory.makeNote(
        seam::time::Tick{960}, seam::time::Tick{960}, 62U, U"え");
    region->lyrics.push_back(std::move(lyricA));
    region->notes.push_back(std::move(noteA));
    region->lyrics.push_back(std::move(lyricB));
    region->notes.push_back(std::move(noteB));
    region->sortNotes();
    return project;
  }
};

}  // namespace

TEST_CASE("editing inferred procedural timing preserves dependent bounds in one undo step") {
  TechnicalFixture fixture;
  auto& project = fixture.document.session().project();
  seam::voice_design::VoiceRecipe recipe; recipe.id = "timing-test";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.poses.push_back(recipe.poses.front()); recipe.poses.back().phone = "e";
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  const auto resource = seam::voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  project.vocalTracks().front().proceduralRecipe = seam::domain::ProceduralRecipeReference{
      resource.value().identity,
      "recipe.json", "neutral"};
  auto* region = project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"ささ";
  const auto before = project;
  CHECK(!fixture.controller.movePhonemeBoundary(fixture.firstKey, false, 100000));
  CHECK(project == before); CHECK(fixture.renderRequests == 0U);
  CHECK(fixture.controller.movePhonemeBoundary(fixture.firstKey, false, 50000));
  CHECK(fixture.renderRequests == 1U); CHECK(fixture.document.session().revision() == 1U);
  region = project.findRegion(fixture.regionId);
  CHECK(region->phonemeOverrides.size() == 4U);
  CHECK(region->findPhonemeOverride(fixture.firstKey)->timing.startOffset == 0);
  CHECK(region->findPhonemeOverride(fixture.firstKey)->timing.endOffset == 50000);
  CHECK(region->findPhonemeOverride({fixture.firstKey.noteId, 1U})->timing.startOffset == 60000);
  CHECK(region->findPhonemeOverride({fixture.firstKey.noteId, 1U})->timing.endOffset == 250000);
  CHECK(region->findPhonemeOverride({fixture.firstKey.noteId, 2U})->timing.startOffset == 250000);
  const auto snapshot = seam::rendering::RenderSnapshotFactory{}.createProcedural(project, resource.value(),
      project.vocalTracks().front().id, fixture.regionId, fixture.document.session().revision(),
      seam::rendering::RenderQuality::Preview, 48000U); CHECK(snapshot);
  CHECK(seam::rendering::PhraseRenderPipeline{}.render(snapshot.value()));
  const auto after = project;
  CHECK(fixture.controller.undo()); CHECK(project == before);
  CHECK(fixture.controller.redo()); CHECK(project == after);
  const auto revision = fixture.document.session().revision();
  const auto requests = fixture.renderRequests;
  CHECK(fixture.controller.movePhonemeBoundary(fixture.firstKey, false, 50000));
  CHECK(fixture.document.session().revision() == revision); CHECK(fixture.renderRequests == requests);
  CHECK(!fixture.controller.movePhonemeBoundary(fixture.firstKey, false, 100000));
  CHECK(!fixture.controller.movePhonemeBoundary({fixture.firstKey.noteId, 1U}, true, 40000));
  CHECK(!fixture.controller.movePhonemeBoundary({fixture.firstKey.noteId, 2U}, true, 240000));
  CHECK(!fixture.controller.movePhonemeBoundary({fixture.firstKey.noteId, 3U}, false, 510000));
  CHECK(project == after); CHECK(fixture.document.session().revision() == revision); CHECK(fixture.renderRequests == requests);
  CHECK(fixture.controller.movePhonemeBoundary(fixture.firstKey, false, 55000));
  CHECK(fixture.document.session().revision() == revision + 1U); CHECK(fixture.renderRequests == requests + 1U);
  const auto explicitSnapshot = seam::rendering::RenderSnapshotFactory{}.createProcedural(project, resource.value(),
      project.vocalTracks().front().id, fixture.regionId, fixture.document.session().revision(),
      seam::rendering::RenderQuality::Preview, 48000U); CHECK(explicitSnapshot);
  CHECK(seam::rendering::PhraseRenderPipeline{}.render(explicitSnapshot.value()));
  CHECK(fixture.controller.undo()); CHECK(project == after);
}

TEST_CASE("technical_edit_controller_commits_one_revision_and_one_render_request") {
  TechnicalFixture fixture;
  const auto before = fixture.document.session().revision();
  const auto result = fixture.controller.movePhonemeBoundary(
      fixture.firstKey, false, seam::time::Microseconds{42000});
  CHECK(result);
  CHECK(fixture.document.session().revision() == before + 1U);
  CHECK(fixture.renderRequests == 1U);

  CHECK(fixture.controller.undo());
  CHECK(fixture.renderRequests == 2U);
  CHECK(fixture.controller.redo());
  CHECK(fixture.renderRequests == 3U);
}

TEST_CASE("retained phoneme review is read only and explicit rebinding has one undo step") {
  TechnicalFixture fixture;
  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  region->phonemeOverrides = {{.key = fixture.firstKey, .timing = {.startOffset = -2100},
                               .locked = true, .unresolved = true}};
  const auto before = fixture.document.session().project();
  const auto review = fixture.controller.reviewPhonemeBindings();
  CHECK(review);
  CHECK(review.value().retainedEdits.size() == 1U);
  CHECK(review.value().targets.size() == 3U);
  CHECK(review.value().warnings.empty());
  CHECK(fixture.document.session().project() == before);
  CHECK(fixture.renderRequests == 0U);
  const auto target = review.value().targets[1];
  CHECK(fixture.controller.rebindPhonemeOverride(review.value().retainedEdits.front(),
                                                target.key, target.contextId));
  const auto after = fixture.document.session().project();
  CHECK(fixture.document.session().revision() == 1U);
  CHECK(fixture.renderRequests == 1U);
  CHECK(after.findRegion(fixture.regionId)->findPhonemeOverride(fixture.firstKey) == nullptr);
  const auto* rebound = after.findRegion(fixture.regionId)->findPhonemeOverride(target.key);
  CHECK(rebound != nullptr);
  CHECK(!rebound->unresolved);
  CHECK(rebound->sourceContextId == target.contextId);
  CHECK(rebound->timing.startOffset == -2100);
  CHECK(fixture.controller.phonemes().tokens[1].locked);
  CHECK(fixture.controller.reviewPhonemeBindings().value().retainedEdits.empty());
  CHECK(fixture.controller.undo());
  CHECK(fixture.document.session().project() == before);
  CHECK(fixture.controller.redo());
  CHECK(fixture.document.session().project() == after);
}

TEST_CASE("phoneme rebinding rejects stale review payload context and occupied targets") {
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    TechnicalFixture fixture;
    auto* region = fixture.document.session().project().findRegion(fixture.regionId);
    region->phonemeOverrides = {{.key = fixture.firstKey, .locked = true, .unresolved = true}};
    const auto review = fixture.controller.reviewPhonemeBindings();
    CHECK(review);
    const auto source = review.value().retainedEdits.front();
    const auto target = review.value().targets[1];
    if (scenario == 0U) region->phonemeOverrides.front().timing.startOffset = -3000;
    if (scenario == 1U) region->lyrics.front().surface = U"か";
    if (scenario == 2U) region->phonemeOverrides.push_back({.key = target.key, .locked = true});
    const auto before = fixture.document.session().project();
    CHECK(!fixture.controller.rebindPhonemeOverride(source, target.key, target.contextId));
    CHECK(fixture.document.session().project() == before);
    CHECK(fixture.document.session().revision() == 0U);
    CHECK(fixture.renderRequests == 0U);
  }
}

TEST_CASE("phoneme review excludes fallback sounds but preserves unrelated valid targets") {
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    TechnicalFixture fixture;
    auto* region = fixture.document.session().project().findRegion(fixture.regionId);
    region->phonemeOverrides = {{.key = fixture.firstKey, .locked = true, .unresolved = true}};
    const auto initial = fixture.controller.reviewPhonemeBindings();
    CHECK(initial);
    const auto oldTarget = initial.value().targets.front();
    const auto retained = initial.value().retainedEdits.front();
    if (scenario == 0U) region->lyrics.front().language = seam::domain::Language::English;
    if (scenario == 1U) region->lyrics.front().surface.clear();
    if (scenario == 2U) region->lyrics.front().surface = U"漢";
    const auto before = fixture.document.session().project();
    const auto review = fixture.controller.reviewPhonemeBindings();
    CHECK(review);
    CHECK(review.value().retainedEdits.size() == 1U);
    CHECK(review.value().retainedEdits.front() == retained);
    CHECK(!review.value().warnings.empty());
    if (scenario == 0U) CHECK(review.value().targets.empty());
    else {
      CHECK(review.value().targets.size() == 1U);
      CHECK(review.value().targets.front().key.noteId != fixture.firstKey.noteId);
    }
    CHECK(!fixture.controller.rebindPhonemeOverride(retained, oldTarget.key, oldTarget.contextId));
    CHECK(fixture.document.session().project() == before);
    CHECK(fixture.document.session().revision() == 0U);
    CHECK(fixture.renderRequests == 0U);
  }
}

TEST_CASE("mixed pronunciation review remains inspectable but cannot authorize any rebinding") {
  TechnicalFixture fixture;
  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  region->phonemeOverrides = {{.key = fixture.firstKey, .locked = true, .unresolved = true}};
  region->unitSelectionOverrides = {{.startKey = fixture.firstKey, .unitId = "retained-unit", .unresolved = true}};
  region->seamOverrides = {{.incomingStartKey = fixture.firstKey, .seamAmount = 0.3F, .unresolved = true}};
  const auto oldPhonemes = fixture.controller.reviewPhonemeBindings();
  const auto oldRender = fixture.controller.reviewRetainedRenderEdits();
  CHECK(oldPhonemes);
  CHECK(oldRender);
  const auto japaneseTarget = oldPhonemes.value().targets.back();
  region->lyrics.front().language = seam::domain::Language::English;
  const auto before = fixture.document.session().project();
  const auto phonemes = fixture.controller.reviewPhonemeBindings();
  const auto render = fixture.controller.reviewRetainedRenderEdits();
  CHECK(phonemes);
  CHECK(render);
  CHECK(phonemes.value().targets.empty());
  CHECK(phonemes.value().retainedEdits == region->phonemeOverrides);
  CHECK(!render.value().tokens.empty());
  CHECK(!fixture.controller.rebindPhonemeOverride(region->phonemeOverrides.front(),
      japaneseTarget.key, japaneseTarget.contextId));
  CHECK(!fixture.controller.rebindUnitOverride(render.value(), region->unitSelectionOverrides.front(),
      japaneseTarget.key));
  CHECK(!fixture.controller.rebindSeamOverride(render.value(), region->seamOverrides.front(),
      japaneseTarget.key));
  CHECK(!fixture.controller.rebindUnitOverride(oldRender.value(), region->unitSelectionOverrides.front(),
      japaneseTarget.key));
  CHECK(fixture.document.session().project() == before);
  CHECK(fixture.document.session().revision() == 0U);
  CHECK(fixture.renderRequests == 0U);
}

TEST_CASE("retained unit rebinding preserves settings ordering and exact undo") {
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    TechnicalFixture fixture;
    auto* region = fixture.document.session().project().findRegion(fixture.regionId);
    seam::domain::UnitSelectionOverride retained{.startKey = fixture.firstKey,
        .tokenCount = static_cast<std::uint16_t>(scenario == 2U ? 2U : 1U),
        .unitId = "retained-unit", .renderer = seam::domain::UnitRendererKind::ClassicPsola,
        .loopPrint = 0.25F, .sourcePitchResidual = 0.5F, .locked = false, .unresolved = true};
    if (scenario != 2U) {
      region->unitSelectionOverrides.push_back({.startKey = {
          .noteId = region->notes.back().id, .ordinal = 0U}, .unitId = "other-unit"});
    }
    region->unitSelectionOverrides.push_back(retained);
    const auto before = fixture.document.session().project();
    CHECK(before.validate());
    const auto review = fixture.controller.reviewRetainedRenderEdits();
    CHECK(review);
    const auto target = review.value().tokens[scenario == 0U ? 0U : 1U].key;
    CHECK(fixture.controller.rebindUnitOverride(review.value(), retained, target));
    region = fixture.document.session().project().findRegion(fixture.regionId);
    auto expected = retained;
    expected.startKey = target;
    expected.unresolved = false;
    CHECK(*region->findUnitSelectionOverride(target) == expected);
    CHECK(fixture.renderRequests == 1U);
    CHECK(fixture.document.session().revision() == 1U);
    const auto after = fixture.document.session().project();
    CHECK(fixture.controller.undo());
    CHECK(fixture.document.session().project() == before);
    CHECK(fixture.controller.redo());
    CHECK(fixture.document.session().project() == after);
  }
}

TEST_CASE("retained unit rebinding rejects stale incomplete warned and overlapping spans") {
  for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
    TechnicalFixture fixture;
    auto* region = fixture.document.session().project().findRegion(fixture.regionId);
    seam::domain::UnitSelectionOverride retained{.startKey = fixture.firstKey,
        .tokenCount = 2U, .unitId = "retained-unit", .unresolved = true};
    region->unitSelectionOverrides.push_back(retained);
    if (scenario == 3U) region->lyrics.back().language = seam::domain::Language::English;
    const auto review = fixture.controller.reviewRetainedRenderEdits();
    CHECK(review);
    auto target = review.value().tokens[1].key;
    if (scenario == 0U) target = review.value().tokens.back().key;
    if (scenario == 1U) region->lyrics.front().surface = U"さ";
    if (scenario == 2U) region->unitSelectionOverrides.front().unitId = "changed";
    if (scenario >= 4U) region->unitSelectionOverrides.push_back({
        .startKey = review.value().tokens.back().key, .unitId = "occupied",
        .unresolved = scenario == 5U});
    const auto before = fixture.document.session().project();
    CHECK(before.validate());
    CHECK(!fixture.controller.rebindUnitOverride(review.value(), retained, target));
    CHECK(fixture.document.session().project() == before);
    CHECK(fixture.document.session().revision() == 0U);
    CHECK(fixture.renderRequests == 0U);
  }
}

TEST_CASE("retained seam review rebinds explicitly with exact undo and rejects stale targets") {
  TechnicalFixture fixture;
  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  seam::domain::SeamOverride retained{.incomingStartKey = fixture.firstKey,
      .seamAmount = 0.7F, .overlap = seam::time::Microseconds{12000},
      .locked = true, .unresolved = true};
  // Valid persisted vectors need not be key-sorted; undo must preserve order.
  region->seamOverrides.push_back({.incomingStartKey = {
      .noteId = region->notes.back().id, .ordinal = 0U}, .seamAmount = 0.2F});
  region->seamOverrides.push_back(retained);
  region->unitSelectionOverrides.push_back({.startKey = fixture.firstKey,
      .tokenCount = 2U, .unitId = "retained-unit", .unresolved = true});
  const auto before = fixture.document.session().project();
  const auto review = fixture.controller.reviewRetainedRenderEdits();
  CHECK(review);
  CHECK(review.value().seams == std::vector<seam::domain::SeamOverride>{retained});
  CHECK(review.value().units.size() == 1U);
  CHECK(review.value().tokens.size() == 3U);
  CHECK(fixture.document.session().project() == before);
  CHECK(fixture.renderRequests == 0U);
  const auto target = review.value().tokens[1].key;
  CHECK(fixture.controller.rebindSeamOverride(review.value(), retained, target));
  region = fixture.document.session().project().findRegion(fixture.regionId);
  auto expected = retained;
  expected.incomingStartKey = target;
  expected.unresolved = false;
  CHECK(*region->findSeamOverride(target) == expected);
  CHECK(region->findSeamOverride(fixture.firstKey) == nullptr);
  CHECK(fixture.renderRequests == 1U);
  CHECK(fixture.document.session().revision() == 1U);
  const auto after = fixture.document.session().project();
  CHECK(fixture.controller.undo());
  CHECK(fixture.document.session().project() == before);
  CHECK(fixture.controller.redo());
  CHECK(fixture.document.session().project() == after);
  CHECK(fixture.controller.undo());
  region = fixture.document.session().project().findRegion(fixture.regionId);
  region->lyrics.front().surface = U"さ";
  const auto changed = fixture.document.session().project();
  const auto revision = fixture.document.session().revision();
  const auto requests = fixture.renderRequests;
  CHECK(!fixture.controller.rebindSeamOverride(review.value(), retained, target));
  CHECK(fixture.document.session().project() == changed);
  CHECK(fixture.document.session().revision() == revision);
  CHECK(fixture.renderRequests == requests);
}

TEST_CASE("retained seam review rejects occupied targets stale payloads and warning predecessors") {
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    TechnicalFixture fixture;
    auto* region = fixture.document.session().project().findRegion(fixture.regionId);
    seam::domain::SeamOverride retained{.incomingStartKey = fixture.firstKey, .seamAmount = 0.3F, .unresolved = true};
    region->seamOverrides.push_back(retained);
    if (scenario == 2U) region->lyrics.front().language = seam::domain::Language::English;
    const auto review = fixture.controller.reviewRetainedRenderEdits();
    CHECK(review);
    const auto target = review.value().tokens.back().key;
    if (scenario == 0U) region->seamOverrides.push_back({.incomingStartKey = target, .seamAmount = 0.2F});
    if (scenario == 1U) region->seamOverrides.front().locked = false;
    const auto before = fixture.document.session().project();
    CHECK(!fixture.controller.rebindSeamOverride(review.value(), retained, target));
    CHECK(fixture.document.session().project() == before);
    CHECK(fixture.document.session().revision() == 0U);
    CHECK(fixture.renderRequests == 0U);
  }
}

TEST_CASE("technical_edit_controller_rejects_invalid_targets_without_render") {
  TechnicalFixture fixture;
  const auto revision = fixture.document.session().revision();
  const seam::domain::PhonemeKey missing{
      .noteId = seam::domain::NoteId{999999U}, .ordinal = 0U};
  CHECK(!fixture.controller.movePhonemeBoundary(
      missing, true, seam::time::Microseconds{-1000}));
  CHECK(!fixture.controller.selectUnitVariant(
      fixture.firstKey, "not-an-alternative",
      seam::domain::UnitRendererKind::Raw));
  CHECK(fixture.document.session().revision() == revision);
  CHECK(fixture.renderRequests == 0U);
}

TEST_CASE("technical_edit_controller locks and resets phoneme overrides") {
  TechnicalFixture fixture;
  CHECK(fixture.controller.movePhonemeBoundary(
      fixture.firstKey, true, seam::time::Microseconds{12000}));
  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  const auto* overrideValue = region->findPhonemeOverride(fixture.firstKey);
  CHECK(overrideValue != nullptr);
  CHECK(overrideValue->locked);
  CHECK(fixture.controller.setPhonemeLocked(fixture.firstKey, false));
  overrideValue = region->findPhonemeOverride(fixture.firstKey);
  CHECK(overrideValue != nullptr);
  CHECK(!overrideValue->locked);
  CHECK(fixture.controller.resetPhonemeOverride(fixture.firstKey));
  CHECK(region->findPhonemeOverride(fixture.firstKey) == nullptr);
  CHECK(fixture.controller.resetPhonemeOverride(fixture.firstKey));
}

TEST_CASE("technical_edit_controller_cycles_unit_variant_and_renderer") {
  TechnicalFixture fixture;
  CHECK(fixture.controller.cycleUnitVariant(fixture.firstKey));
  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  const auto* selected = region->findUnitSelectionOverride(fixture.firstKey);
  CHECK(selected != nullptr);
  CHECK(selected->unitId == "unit-b");
  CHECK(selected->renderer == seam::domain::UnitRendererKind::Raw);

  CHECK(fixture.controller.cycleUnitRenderer(fixture.firstKey));
  selected = region->findUnitSelectionOverride(fixture.firstKey);
  CHECK(selected != nullptr);
  CHECK(selected->renderer == seam::domain::UnitRendererKind::ClassicPsola);
  CHECK(fixture.renderRequests == 2U);

  const auto diagnostic = fixture.controller.unitDiagnostic(fixture.firstKey);
  CHECK(diagnostic.has_value());
  CHECK(diagnostic->usedFallback);
  CHECK(diagnostic->diagnostic == "spectral fallback");
}

TEST_CASE("technical_edit_controller_maps_unit edits from covered nonstart phonemes") {
  TechnicalFixture fixture;
  const seam::domain::PhonemeKey coveredKey{
      .noteId = fixture.firstKey.noteId, .ordinal = 1U};

  CHECK(fixture.controller.cycleUnitVariant(coveredKey));
  const auto* region = fixture.document.session().project().findRegion(
      fixture.regionId);
  CHECK(region != nullptr);
  const auto* selected = region->findUnitSelectionOverride(fixture.firstKey);
  CHECK(selected != nullptr);
  CHECK(selected->unitId == "unit-b");
  CHECK(fixture.renderRequests == 1U);

  CHECK(fixture.controller.cycleUnitRenderer(coveredKey));
  selected = region->findUnitSelectionOverride(fixture.firstKey);
  CHECK(selected != nullptr);
  CHECK(selected->renderer == seam::domain::UnitRendererKind::ClassicPsola);
  CHECK(fixture.renderRequests == 2U);
}

TEST_CASE("technical_edit_controller resets a selected voice unit") {
  TechnicalFixture fixture;
  CHECK(fixture.controller.cycleUnitVariant(fixture.firstKey));
  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->findUnitSelectionOverride(fixture.firstKey) != nullptr);
  CHECK(fixture.controller.resetUnitSelection(fixture.firstKey));
  CHECK(region->findUnitSelectionOverride(fixture.firstKey) == nullptr);
  CHECK(fixture.controller.resetUnitSelection(fixture.firstKey));
}

TEST_CASE("technical_edit_controller_edits_pitch_and_seam_with_undo") {
  TechnicalFixture fixture;
  seam::domain::PitchAutomationPoint point{
      .tick = seam::time::Tick{480},
      .cents = 25.0F,
      .interpolation = seam::domain::CurveInterpolation::Linear,
  };
  CHECK(fixture.controller.upsertPitchPoint(point));
  point.tick = seam::time::Tick{720};
  point.cents = -12.0F;
  CHECK(fixture.controller.movePitchPoint(seam::time::Tick{480}, point));
  CHECK(fixture.controller.cyclePitchInterpolation(seam::time::Tick{720}));

  seam::domain::SeamOverride seam{
      .incomingStartKey = fixture.firstKey,
      .seamAmount = 0.82F,
      .overlap = seam::time::Microseconds{12000},
      .phaseReset = 0.5F,
      .envelopeBlend = 0.3F,
      .curve = seam::domain::SeamCurve::EqualPower,
      .locked = true,
  };
  CHECK(fixture.controller.upsertSeam(seam));

  auto* region = fixture.document.session().project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->pitchAutomation.points().size() == 1U);
  CHECK(region->pitchAutomation.points().front().tick == seam::time::Tick{720});
  CHECK(region->pitchAutomation.points().front().interpolation ==
        seam::domain::CurveInterpolation::Smooth);
  const auto* storedSeam = region->findSeamOverride(fixture.firstKey);
  CHECK(storedSeam != nullptr);
  CHECK(storedSeam->seamAmount.has_value());
  CHECK_NEAR(*storedSeam->seamAmount, 0.82, 0.0001);

  CHECK(fixture.controller.removePitchPoint(seam::time::Tick{720}));
  CHECK(region->pitchAutomation.points().empty());
  CHECK(fixture.renderRequests == 5U);
}

TEST_CASE("technical_edit_controller resets selected technical ranges as one edit") {
  TechnicalFixture fixture;
  CHECK(fixture.controller.movePhonemeBoundary(
      fixture.firstKey, true, seam::time::Microseconds{12000}));
  CHECK(fixture.controller.cycleUnitVariant(fixture.firstKey));
  CHECK(fixture.controller.upsertPitchPoint(
      seam::domain::PitchAutomationPoint{
          .tick = seam::time::Tick{480},
          .cents = 12.0F,
          .interpolation = seam::domain::CurveInterpolation::Linear,
      }));
  CHECK(fixture.controller.resetPhonemeRegion());
  CHECK(fixture.controller.resetUnitRegion());
  CHECK(fixture.controller.resetPitchSegment(seam::time::Tick{0},
                                             seam::time::Tick{960}));
  const auto* region = fixture.document.session().project().findRegion(
      fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->phonemeOverrides.empty());
  CHECK(region->unitSelectionOverrides.empty());
  CHECK(region->pitchAutomation.points().empty());
}

TEST_CASE("technical_edit_controller clamps pitch points") {
  TechnicalFixture fixture;
  CHECK(fixture.controller.upsertPitchPoint(
      seam::domain::PitchAutomationPoint{
          .tick = seam::time::Tick{101},
          .cents = 9000.0F,
          .interpolation = seam::domain::CurveInterpolation::Linear,
      }));
  const auto* region = fixture.document.session().project().findRegion(
      fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->pitchAutomation.points().size() == 1U);
  CHECK(region->pitchAutomation.points().front().tick == seam::time::Tick{101});
  CHECK_NEAR(region->pitchAutomation.points().front().cents, 4800.0, 1e-6);
}
