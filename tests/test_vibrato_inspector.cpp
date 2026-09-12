#include "test_framework.hpp"
#include "seam/native_ui/vibrato_inspector.hpp"
#include "seam/application/project_factory.hpp"

namespace {
struct Fixture final {
  seam::application::ProjectFactory factory{311000U};
  seam::domain::RegionId region;
  std::vector<seam::domain::NoteId> notes;
  seam::application::EditorSession session{makeProject()};
  seam::domain::Project makeProject() {
    auto project = factory.createProject("Vibrato inspector"); const auto track = factory.addVocalTrack(project, "Lead");
    region = factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{9600});
    for (int i = 0; i < 2; ++i) {
      auto [lyric, note] = factory.makeNote(seam::time::Tick{i * 960}, seam::time::Tick{480}, 60U, U"la", seam::domain::Language::English);
      note.vibrato.enabled = i == 1; note.vibrato.phaseTurns = i == 0 ? 0.12345679F : 0.75F;
      note.vibrato.fadeOutFraction = i == 0 ? 0.1F : 0.5F;
      notes.push_back(note.id); project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
    }
    return project;
  }
};
}

TEST_CASE("vibrato inspector retains invalid drafts and resolves coupled fades before one Apply") {
  using namespace seam; using F = native_ui::VibratoField;
  Fixture fixture; fixture.session.selection().replace(fixture.notes); const auto before = fixture.session.project();
  auto draft = native_ui::VibratoInspectorDraft::prepare(fixture.session, fixture.region); CHECK(draft);
  CHECK(draft.value().text(F::Start) == "0.65");
  CHECK(draft.value().text(F::Enabled) == "Mixed"); CHECK(draft.value().text(F::Phase) == "Mixed");
  CHECK(draft.value().mixed(F::Phase)); CHECK(!draft.value().set(fixture.session, fixture.region, F::Phase, "Mixed"));
  CHECK(!draft.value().mixed(F::Phase)); CHECK(draft.value().reset(fixture.session, fixture.region, F::Phase)); CHECK(draft.value().mixed(F::Phase));
  CHECK(draft.value().text(F::Period) == "180"); CHECK(!draft.value().canApply(fixture.session, fixture.region));
  CHECK(!draft.value().set(fixture.session, fixture.region, F::FadeIn, "0.6"));
  CHECK(draft.value().text(F::FadeIn) == "0.6"); CHECK(!draft.value().error().empty()); CHECK(!draft.value().canApply(fixture.session, fixture.region));
  CHECK(!draft.value().apply(fixture.session, fixture.region)); CHECK(fixture.session.project() == before);
  CHECK(draft.value().set(fixture.session, fixture.region, F::FadeOut, "0.3")); CHECK(draft.value().error().empty());
  CHECK(draft.value().set(fixture.session, fixture.region, F::Enabled, "On")); CHECK(draft.value().set(fixture.session, fixture.region, F::Depth, " 75 \t"));
  CHECK(draft.value().changedCount() == 2U); CHECK(draft.value().canApply(fixture.session, fixture.region)); CHECK(fixture.session.project() == before);
  CHECK(!draft.value().canApply(fixture.session, {})); CHECK(!draft.value().apply(fixture.session, {}));
  CHECK(!draft.value().set(fixture.session, {}, F::Depth, "1")); CHECK(!draft.value().reset(fixture.session, {}, F::Depth));
  CHECK(draft.value().apply(fixture.session, fixture.region)); auto expected = before;
  for (const auto id : fixture.notes) {
    auto& vibrato = expected.findNote(id)->vibrato;
    vibrato.enabled = true; vibrato.depthCents = 75.0F; vibrato.fadeInFraction = 0.6F; vibrato.fadeOutFraction = 0.3F;
  }
  CHECK(fixture.session.project() == expected); CHECK(!draft.value().canApply(fixture.session, fixture.region)); CHECK(!draft.value().apply(fixture.session, fixture.region));
  CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before); CHECK(fixture.session.redo()); CHECK(fixture.session.project() == expected);
}

TEST_CASE("vibrato inspector round trips canonical text and rejects malformed stale or cancelled drafts") {
  using namespace seam; using F = native_ui::VibratoField;
  Fixture fixture; fixture.session.selection().selectOnly(fixture.notes[0]); const auto before = fixture.session.project();
  auto draft = native_ui::VibratoInspectorDraft::prepare(fixture.session, fixture.region); CHECK(draft);
  for (std::size_t i = 0U; i < static_cast<std::size_t>(F::Count); ++i) {
    const auto field = static_cast<F>(i); CHECK(draft.value().set(fixture.session, fixture.region, field, draft.value().text(field)));
  }
  CHECK(draft.value().changedCount() == 0U); CHECK(!draft.value().canApply(fixture.session, fixture.region));
  for (const auto* invalid : {"", "NaN", "inf", "50 cents", "1.2.3"}) {
    CHECK(!draft.value().set(fixture.session, fixture.region, F::Depth, invalid)); CHECK(!draft.value().canApply(fixture.session, fixture.region));
    CHECK(draft.value().text(F::Depth) == invalid);
  }
  CHECK(draft.value().reset(fixture.session, fixture.region, F::Depth)); CHECK(draft.value().text(F::Depth) == "50");
  CHECK(!draft.value().set(fixture.session, fixture.region, F::Phase, "1")); CHECK(draft.value().reset(fixture.session, fixture.region, F::Phase));
  CHECK(!draft.value().set(fixture.session, fixture.region, F::Period, "0")); CHECK(draft.value().reset(fixture.session, fixture.region, F::Period));
  CHECK(!draft.value().set(fixture.session, fixture.region, F::Depth, std::string(65U, '1')));
  CHECK(!draft.value().set(fixture.session, fixture.region, static_cast<F>(99), "1"));
  CHECK(draft.value().set(fixture.session, fixture.region, F::Depth, "60"));
  fixture.session.selection().selectOnly(fixture.notes[1]); CHECK(!draft.value().canApply(fixture.session, fixture.region)); CHECK(!draft.value().apply(fixture.session, fixture.region));
  fixture.session.selection().selectOnly(fixture.notes[0]);
  CHECK(fixture.session.replaceProject(before)); fixture.session.selection().selectOnly(fixture.notes[0]);
  CHECK(!draft.value().apply(fixture.session, fixture.region));
  auto cancelled = native_ui::VibratoInspectorDraft::prepare(fixture.session, fixture.region); CHECK(cancelled);
  CHECK(cancelled.value().set(fixture.session, fixture.region, F::Depth, "60")); cancelled.value().cancel(); CHECK(!cancelled.value().apply(fixture.session, fixture.region));
  CHECK(fixture.session.project() == before);
}
