#include "test_framework.hpp"

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/technical_edit_controller.hpp"
#include "seam/application/project_factory.hpp"

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
