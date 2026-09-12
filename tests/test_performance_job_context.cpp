#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/application/view_commands.hpp"
#include "seam/authoring/project_document.hpp"
#include "seam/authoring/authoring_runtime.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "test_support.hpp"

#include <chrono>
#include <thread>

namespace {

using namespace seam;

domain::Project project() {
  domain::Project value{domain::ProjectId{1U}, "Job source"};
  domain::VocalRegion region{.id = domain::RegionId{3U}, .name = "Phrase",
      .durationTick = time::Tick{1920},
      .lyrics = {{domain::LyricTokenId{4U}, U"あ", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{5U}, .durationTick = time::Tick{480},
                 .lyricTokenId = domain::LyricTokenId{4U}}}};
  value.vocalTracks().push_back({.id = domain::TrackId{2U}, .name = "Singer", .regions = {region}});
  return value;
}

std::unique_ptr<application::ICommand> pitch(float cents = 20.0F) {
  return std::make_unique<application::UpsertPitchAutomationPointCommand>(
      domain::RegionId{3U}, domain::PitchAutomationPoint{time::Tick{0}, cents});
}

}

TEST_CASE("generated proposals are inert undoable and delivered through one live job receipt") {
  application::EditorSession session{project()};
  const auto source = session.project();
  const auto& region = *source.findRegion(domain::RegionId{3U});
  const auto pronunciation = phonemizer::resolveJapanesePronunciation(region);
  CHECK(pronunciation);
  domain::PerformanceTake take{.id = "proposal", .sourceRegionId = region.id,
      .capturedRevision = region.performance.revision,
      .resource = {domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity,
      .generatorId = "fixture", .generatorVersion = "1", .range = {time::Tick{0}, time::Tick{1920}},
      .lanes = {{domain::PerformanceChannel::Attack, {{time::Tick{0}, 100.0}}}}};
  const auto command = [&](const auto& state, const auto& proposal) {
    return std::make_unique<application::AddPerformanceProposalCommand>(region.id, state, proposal);
  };
  const auto job = session.capturePerformanceJob();
  const auto independent = session.capturePerformanceJob();
  CHECK(job); CHECK(independent);
  CHECK(session.executePerformanceResult(job.value(), command(region.performance, take)));
  const auto stored = session.project();
  const auto& state = stored.findRegion(region.id)->performance;
  CHECK(state.takes == std::vector{take});
  CHECK(state.accepted.empty()); CHECK(state.ownership == region.performance.ownership);
  CHECK(state.revision == region.performance.revision);
  CHECK(session.lastImpact().scope == application::CommandAudioImpact::MetadataOnly);
  CHECK(session.validatePerformanceJob(independent.value()));
  CHECK(!session.executePerformanceResult(job.value(), command(state, take)));
  CHECK(session.project() == stored);
  CHECK(session.undo()); CHECK(session.project() == source);
  CHECK(session.redo()); CHECK(session.project() == stored);
  auto duplicate = command(state, take);
  auto copy = stored;
  CHECK(!duplicate->apply(copy)); CHECK(copy == stored);
  for (int invalid = 0; invalid < 5; ++invalid) {
    auto bad = take;
    if (invalid == 0) ++bad.capturedRevision.musical;
    if (invalid == 1) bad.pronunciation.sequenceHash = std::string(64U, 'f');
    if (invalid == 2) bad.state = domain::PerformanceProposalState::Rejected;
    if (invalid == 3) bad.sourceRegionId = domain::RegionId{99U};
    if (invalid == 4) bad.range.endTick = time::Tick{1921};
    copy = source;
    CHECK(!command(region.performance, bad)->apply(copy)); CHECK(copy == source);
  }
  application::EditorSession edited{source};
  const auto stale = edited.capturePerformanceJob();
  CHECK(stale); CHECK(edited.execute(pitch()));
  const auto editedBefore = edited.project();
  CHECK(!edited.executePerformanceResult(stale.value(), command(region.performance, take)));
  CHECK(edited.project() == editedBefore);
}

TEST_CASE("recipe selection invalidates pending performance jobs even after undo") {
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob(); CHECK(captured);
  const domain::ProceduralRecipeReference reference{
      {domain::SingerResourceKind::Procedural, "draft", "1", std::string(64U, 'a')}, "singer.json", "neutral"};
  CHECK(session.execute(std::make_unique<application::SetTrackProceduralRecipeCommand>(domain::TrackId{2U},
      std::nullopt, reference)));
  CHECK(!session.validatePerformanceJob(captured.value()));
  const auto selectedJob = session.capturePerformanceJob(); CHECK(selectedJob);
  CHECK(session.undo());
  CHECK(session.project() == captured.value().sourceProject());
  CHECK(!session.validatePerformanceJob(captured.value()));
  CHECK(!session.validatePerformanceJob(selectedJob.value()));
  const auto fresh = session.capturePerformanceJob(); CHECK(fresh);
  CHECK(session.executePerformanceResult(fresh.value(), pitch()));
}

TEST_CASE("performance results commit only into the captured live musical generation") {
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  CHECK(session.validatePerformanceJob(captured.value()));
  CHECK(session.executePerformanceResult(captured.value(), pitch()));
  CHECK(session.revision() == 1U);
  const auto after = session.project();
  CHECK(!session.executePerformanceResult(captured.value(), pitch(40.0F)));
  CHECK(session.project() == after);
  CHECK(session.revision() == 1U);
  CHECK(captured.value().sourceProject().findRegion(domain::RegionId{3U})->pitchAutomation.points().empty());
}

TEST_CASE("undo restoring identical music cannot revive an old performance job") {
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  CHECK(session.execute(pitch()));
  CHECK(session.undo());
  CHECK(session.project() == captured.value().sourceProject());
  CHECK(!session.validatePerformanceJob(captured.value()));
  const auto restored = session.capturePerformanceJob();
  CHECK(restored);
  CHECK(session.redo());
  CHECK(!session.validatePerformanceJob(restored.value()));
}

TEST_CASE("view-only edits and failed transactions do not discard performance jobs") {
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  CHECK(session.execute(std::make_unique<application::SetTechnicalLanePresentationCommand>(
      domain::TechnicalLane::Pitch, domain::TechnicalLanePresentation{domain::TechnicalLaneMode::Expanded, 180.0})));
  CHECK(session.validatePerformanceJob(captured.value()));
  CHECK(session.undo());
  CHECK(session.validatePerformanceJob(captured.value()));
  CHECK(!session.execute(std::make_unique<application::UpsertPitchAutomationPointCommand>(
      domain::RegionId{99U}, domain::PitchAutomationPoint{time::Tick{0}, 10.0F})));
  CHECK(session.validatePerformanceJob(captured.value()));
  CHECK(!session.executePerformanceResult(captured.value(),
      std::make_unique<application::UpsertPitchAutomationPointCommand>(
          domain::RegionId{99U}, domain::PitchAutomationPoint{time::Tick{0}, 10.0F})));
  CHECK(session.validatePerformanceJob(captured.value()));
}

TEST_CASE("replacement and another session reject identical-source job captures") {
  application::EditorSession first{project()};
  application::EditorSession second{project()};
  const auto captured = first.capturePerformanceJob();
  CHECK(captured);
  CHECK(!second.validatePerformanceJob(captured.value()));
  CHECK(first.replaceProject(project()));
  CHECK(!first.validatePerformanceJob(captured.value()));
}

TEST_CASE("a successful result consumes only its own completion context") {
  application::EditorSession session{project()};
  const auto first = session.capturePerformanceJob();
  const auto second = session.capturePerformanceJob();
  CHECK(first);
  CHECK(second);
  const auto copy = first.value();
  CHECK(session.executePerformanceResult(first.value(),
      std::make_unique<application::SetTechnicalLanePresentationCommand>(domain::TechnicalLane::Pitch,
          domain::TechnicalLanePresentation{domain::TechnicalLaneMode::Expanded, 180.0})));
  CHECK(!session.validatePerformanceJob(copy));
  CHECK(session.validatePerformanceJob(second.value()));
}

TEST_CASE("performance validation detects source mutations outside command bookkeeping") {
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  session.project().findNote(domain::NoteId{5U})->midiKey = 65U;
  CHECK(!session.validatePerformanceJob(captured.value()));
  CHECK(!session.executePerformanceResult(captured.value(), pitch()));
  CHECK(session.revision() == 0U);
}

TEST_CASE("performance generation follows real musical changes rather than impact labels") {
  class MisclassifiedEdit final : public application::ICommand {
  public:
    std::string_view name() const noexcept override { return "Misclassified fixture edit"; }
    application::CommandAudioImpact audioImpact() const noexcept override {
      return application::CommandAudioImpact::MetadataOnly;
    }
    core::Result<void> apply(domain::Project& value) override {
      value.findNote(domain::NoteId{5U})->midiKey = 65U;
      return core::success();
    }
    core::Result<void> revert(domain::Project& value) override {
      value.findNote(domain::NoteId{5U})->midiKey = 60U;
      return core::success();
    }
  };
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  CHECK(session.execute(std::make_unique<MisclassifiedEdit>()));
  CHECK(session.undo());
  CHECK(session.project() == captured.value().sourceProject());
  CHECK(!session.validatePerformanceJob(captured.value()));
}

TEST_CASE("resource changes invalidate a captured performance context") {
  application::EditorSession session{project()};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  CHECK(session.execute(std::make_unique<application::SetTrackVoicebankCommand>(domain::TrackId{2U},
      domain::VoicebankReference{"other-singer", "2", std::string(64U, 'a')})));
  CHECK(!session.validatePerformanceJob(captured.value()));
  CHECK(session.undo());
  CHECK(!session.validatePerformanceJob(captured.value()));
}

TEST_CASE("tempo style and ownership changes are performance inputs but cosmetic metadata is not") {
  for (const auto field : {"tempo", "style", "ownership", "sample-rate"}) {
    application::EditorSession session{project()};
    const auto captured = session.capturePerformanceJob();
    CHECK(captured);
    session.project().setName("Renamed song");
    session.project().vocalTracks().front().name = "Renamed singer";
    session.project().vocalTracks().front().gainDb = -3.0F;
    CHECK(session.validatePerformanceJob(captured.value()));
    const std::string_view selected{field};
    if (selected == "tempo") CHECK(session.project().tempoMap().addOrReplace(time::Tick{0}, 150.0));
    if (selected == "style") session.project().vocalTracks().front().styleSelection =
        {domain::VoiceStyleOrigin::Explicit, "soft"};
    if (selected == "ownership") session.project().findRegion(domain::RegionId{3U})->performance.ownership =
        {{domain::PerformanceChannel::Pitch, domain::NoteId{5U}, domain::ManualPerformanceMode::Replace, {}}};
    if (selected == "sample-rate") session.project().settings().sampleRate = 96000.0;
    CHECK(!session.validatePerformanceJob(captured.value()));
  }
}

TEST_CASE("unused proposals do not invalidate jobs but accepted payload changes do") {
  auto source = project();
  source.findRegion(domain::RegionId{3U})->performance.takes.push_back({
      .id = "fixture-take", .sourceRegionId = domain::RegionId{3U},
      .resource = {domain::SingerResourceKind::Sample, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {domain::Language::Japanese, "fixture", "1", std::string(64U, 'b'),
                         std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {time::Tick{0}, time::Tick{1920}},
      .lanes = {{domain::PerformanceChannel::Pitch, {{time::Tick{0}, 6000.0}}}}});
  application::EditorSession session{source};
  const auto captured = session.capturePerformanceJob();
  CHECK(captured);
  auto& state = session.project().findRegion(domain::RegionId{3U})->performance;
  state.takes.front().lanes.front().points.front().value = 6100.0;
  CHECK(session.validatePerformanceJob(captured.value()));
  state.accepted.push_back({"fixture-take", domain::PerformanceChannel::Pitch, domain::NoteId{5U}, time::Tick{0}});
  CHECK(!session.validatePerformanceJob(captured.value()));
  const auto selected = session.capturePerformanceJob();
  CHECK(selected);
  state.takes.front().lanes.front().points.front().value = 6200.0;
  CHECK(!session.validatePerformanceJob(selected.value()));
}

TEST_CASE("a context outliving its original document cannot publish into a reopened session") {
  const auto captured = [] {
    application::EditorSession original{project()};
    return original.capturePerformanceJob();
  }();
  CHECK(captured);
  application::EditorSession reopened{captured.value().sourceProject()};
  CHECK(!reopened.executePerformanceResult(captured.value(), pitch()));
  CHECK(reopened.project() == captured.value().sourceProject());
}

TEST_CASE("document result publication synchronizes dirty state and preserves durable identity") {
  authoring::ProjectDocument document{project(), application::ProjectFactory{100U}};
  document.markSaved("song.seam", "saved-source-digest");
  const auto captured = document.session().capturePerformanceJob();
  CHECK(captured);
  CHECK(document.executePerformanceResult(captured.value(), pitch()));
  CHECK(document.dirty());
  CHECK(document.identity().baseProjectHash == "saved-source-digest");
  const auto after = document.session().project();
  const auto revision = document.session().revision();
  CHECK(!document.executePerformanceResult(captured.value(), pitch(40.0F)));
  CHECK(document.session().project() == after);
  CHECK(document.session().revision() == revision);
  CHECK(document.undo());
  CHECK(document.session().project() == captured.value().sourceProject());
  CHECK(!document.session().validatePerformanceJob(captured.value()));
}

TEST_CASE("runtime result publication triggers real preview and stale completion submits nothing") {
  const auto root = test::support::temporaryDirectory("job-context-runtime");
  const auto bankRoot = root / "bank";
  std::filesystem::create_directories(bankRoot / "audio");
  const auto samples = test::support::sineWave(48000U, 261.6256, 0.2);
  CHECK(voicebank::writePcm16Wav(bankRoot / "audio/a.wav", 48000U, 1U, samples));
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 60, voicebank::UnitKind::Sustain, samples.size())});
  CHECK(voicebank::ManifestJsonCodec{}.save(manifest, bankRoot / "manifest.json"));
  const std::vector roots{voicebank::VoicebankSearchRoot{bankRoot, voicebank::VoicebankRootKind::Development}};
  const auto scanned = voicebank::VoicebankCatalog{}.scan(roots);
  CHECK(scanned);
  CHECK(scanned.value().size() == 1U);
  auto source = project();
  const auto& bank = scanned.value().front();
  source.vocalTracks().front().voicebank = {bank.manifest.id, bank.manifest.version, bank.contentHash};
  auto document = std::unique_ptr<authoring::ProjectDocument>{
      new authoring::ProjectDocument(source, application::ProjectFactory{100U})};
  authoring::AuthoringRuntime runtime{std::move(document), {
      .cacheRoot = root / "cache", .voicebankRoots = roots,
      .allowDevelopmentVoicebanks = true, .enableTransport = false}};
  CHECK(runtime.initialize());
  const auto waitReady = [&](std::uint64_t revision) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
      const auto progress = runtime.renderer().progress();
      if (progress.state == authoring::RenderState::Ready && progress.publishedRevision == revision) return true;
      if (progress.state == authoring::RenderState::Failed) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
  };
  CHECK(waitReady(runtime.document().session().revision()));
  const auto captured = runtime.document().session().capturePerformanceJob();
  CHECK(captured);
  const auto before = runtime.renderer().stats().submitted;
  CHECK(runtime.executePerformanceResult(captured.value(), pitch()));
  CHECK(runtime.document().dirty());
  CHECK(waitReady(runtime.document().session().revision()));
  CHECK(runtime.renderer().stats().submitted > before);
  const auto after = runtime.renderer().stats().submitted;
  const auto current = runtime.document().session().project();
  CHECK(!runtime.executePerformanceResult(captured.value(), pitch(40.0F)));
  CHECK(runtime.renderer().stats().submitted == after);
  CHECK(runtime.document().session().project() == current);
  runtime.shutdown();
}
