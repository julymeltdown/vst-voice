#include "test_framework.hpp"
#include "seam/application/tempo_commands.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include <limits>

TEST_CASE("tempo commands preserve sustained score geometry and invalidate captured jobs through undo") {
  using namespace seam;
  application::ProjectFactory factory{19000U};
  auto project = factory.createProject("Tempo changes");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Sustain", time::Tick{0}, time::Tick{3840});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 60U, U"あ", domain::Language::Japanese);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::EditorSession session{project};
  const auto job = session.capturePerformanceJob(); CHECK(job);
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, 60.0)));
  CHECK(session.project().tempoMap().sampleFrameAt(note.endTick(), 48000.0) == 72000);
  CHECK(session.project().tempoMap().tickAtSampleFrame(72000, 48000.0) == note.endTick());
  CHECK(*session.project().findNote(note.id) == note);
  CHECK(session.lastImpact().scope == application::CommandAudioImpact::ProjectAudio);
  CHECK(session.lastImpact().projectWide); CHECK(!session.validatePerformanceJob(job.value()));
  const auto inserted = session.project();
  CHECK(session.undo()); CHECK(session.project() == project);
  CHECK(!session.validatePerformanceJob(job.value())); // Undo cannot revive an old worker receipt.
  CHECK(session.redo()); CHECK(session.project() == inserted);
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, 240.0)));
  CHECK(session.project().tempoMap().sampleFrameAt(note.endTick(), 48000.0) == 36000);
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, std::nullopt)));
  CHECK(session.project().tempoMap().sampleFrameAt(note.endTick(), 48000.0) == 48000);
  CHECK(session.undo()); CHECK(session.project().tempoMap().bpmAt(time::Tick{960}) == 240.0);
}

TEST_CASE("invalid tempo and meter edits preserve state and history") {
  using namespace seam;
  application::EditorSession session{domain::Project{domain::ProjectId{19001U}, "Invalid maps"}};
  const auto before = session.project();
  for (const double bpm : {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    CHECK(!session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, bpm)));
    CHECK(session.project() == before);
  }
  CHECK(!session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{-1}, 120.0)));
  CHECK(!session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, std::nullopt)));
  CHECK(!session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, std::nullopt)));
  for (const auto signature : {application::EditMeterCommand::Signature{0, 4}, {33, 4}, {4, 3}})
    CHECK(!session.execute(std::make_unique<application::EditMeterCommand>(time::Tick{960}, signature)));
  CHECK(!session.execute(std::make_unique<application::EditMeterCommand>(time::Tick{0}, std::nullopt)));
  CHECK(!session.execute(std::make_unique<application::EditMeterCommand>(time::Tick{960}, std::nullopt)));
  CHECK(session.project() == before); CHECK(!session.undo());
}

TEST_CASE("meter insert replace remove preserves tempo and exact history") {
  using namespace seam;
  application::EditorSession session{domain::Project{domain::ProjectId{19002U}, "Meter history"}};
  const auto before = session.project();
  const auto job = session.capturePerformanceJob(); CHECK(job);
  CHECK(session.execute(std::make_unique<application::EditMeterCommand>(time::Tick{3840}, application::EditMeterCommand::Signature{3, 4})));
  CHECK(session.project().meterMap().barBeatAt(time::Tick{6720}).bar == 3);
  CHECK(session.project().tempoMap() == before.tempoMap());
  CHECK(!session.validatePerformanceJob(job.value())); CHECK(session.lastImpact().projectWide);
  const auto inserted = session.project();
  CHECK(session.execute(std::make_unique<application::EditMeterCommand>(time::Tick{3840}, application::EditMeterCommand::Signature{7, 8})));
  CHECK(session.project().meterMap().meterAt(time::Tick{3840}).numerator == 7);
  CHECK(session.undo()); CHECK(session.project() == inserted);
  CHECK(session.execute(std::make_unique<application::EditMeterCommand>(time::Tick{3840}, std::nullopt)));
  CHECK(session.project().meterMap() == before.meterMap());
  CHECK(session.undo()); CHECK(session.project() == inserted);
  CHECK(session.undo()); CHECK(session.project() == before);
}

TEST_CASE("time map command history rejects out of band replacement") {
  using namespace seam;
  domain::Project project{domain::ProjectId{19003U}, "Map conflict"};
  application::EditTempoCommand tempo{time::Tick{960}, 90.0};
  CHECK(!tempo.revert(project)); CHECK(tempo.apply(project));
  CHECK(project.tempoMap().addOrReplace(time::Tick{1920}, 70.0));
  const auto changed = project; CHECK(!tempo.revert(project)); CHECK(project == changed);
  application::EditMeterCommand meter{time::Tick{3840}, application::EditMeterCommand::Signature{3, 4}};
  CHECK(meter.apply(project)); CHECK(meter.revert(project));
  CHECK(project.meterMap().addOrReplace(time::Tick{0}, 6, 8));
  const auto changedMeter = project; CHECK(!meter.apply(project)); CHECK(project == changedMeter);
}
