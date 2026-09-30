#include "test_framework.hpp"

#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/audio_measurement_capture.hpp"
#include "seam/authoring/audio_measurement_job.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <limits>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for render coordinator tests
#endif

namespace {

struct RenderFixture final {
  seam::domain::Project project;
  seam::domain::TrackId trackId;
  seam::domain::RegionId regionId;
  seam::rendering::TrackVoicebankSource source;
};

std::filesystem::path uniqueTempRoot(std::string_view label) {
  static std::atomic<std::uint64_t> counter{0U};
  const auto value = counter.fetch_add(1U, std::memory_order_relaxed);
  return std::filesystem::temp_directory_path() /
         ("project-seam-" + std::string{label} + "-" +
          std::to_string(std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count()) +
          "-" + std::to_string(value));
}

RenderFixture makeRenderFixture() {
  seam::voicebank::VoicebankCatalog catalog;
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }};
  const auto scanned = catalog.scan(roots);
  if (!scanned || scanned.value().size() != 1U) {
    throw seam::test::Failure{"production Voicebank fixture is unavailable"};
  }
  const auto candidate = scanned.value().front();

  seam::application::ProjectFactory factory{1000U};
  auto project = factory.createProject("Authoring Render Coordinator Test");
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 154.0));
  const auto trackId = factory.addVocalTrack(project, "VOICE");
  const auto regionId = factory.addRegion(
      project, trackId, "PHRASE", seam::time::Tick{0}, seam::time::Tick{15360});
  auto* track = project.findVocalTrack(trackId);
  auto* region = project.findRegion(regionId);
  if (track == nullptr || region == nullptr) {
    throw seam::test::Failure{"render fixture track or region is missing"};
  }
  track->voicebank = seam::domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };

  const std::array<std::tuple<std::int64_t, std::int64_t, std::uint8_t,
                              const char32_t*>, 8>
      notes{{
          {0, 720, 64U, U"こ"},
          {720, 480, 67U, U"え"},
          {1200, 960, 69U, U"を"},
          {2400, 480, 67U, U"つ"},
          {2880, 720, 64U, U"な"},
          {3600, 960, 62U, U"ぐ"},
          {4800, 720, 64U, U"ま"},
          {5520, 1440, 67U, U"で"},
      }};
  for (const auto& [start, duration, key, lyricText] : notes) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{start}, seam::time::Tick{duration}, key,
        std::u32string{lyricText}, seam::domain::Language::Japanese);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();

  const std::array<std::tuple<std::uint16_t, const char*,
                              seam::domain::UnitRendererKind>, 8>
      overrides{{
          {2U, "demo.ja.g4.k-o.01", seam::domain::UnitRendererKind::ClassicPsola},
          {1U, "demo.ja.g4.e.01", seam::domain::UnitRendererKind::Raw},
          {1U, "demo.ja.g4.o.01", seam::domain::UnitRendererKind::SpectralClassic},
          {2U, "demo.ja.g4.ts-u.01", seam::domain::UnitRendererKind::Stretch},
          {2U, "demo.ja.g4.n-a.01", seam::domain::UnitRendererKind::ClassicPsola},
          {2U, "demo.ja.g4.g-u.01", seam::domain::UnitRendererKind::Raw},
          {2U, "demo.ja.g4.m-a.01", seam::domain::UnitRendererKind::SpectralClassic},
          {2U, "demo.ja.g4.d-e.01", seam::domain::UnitRendererKind::Stretch},
      }};
  for (std::size_t index = 0U; index < region->notes.size(); ++index) {
    const auto& [tokenCount, unitId, renderer] = overrides[index];
    region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
        .startKey = seam::domain::PhonemeKey{
            .noteId = region->notes[index].id, .ordinal = 0U},
        .tokenCount = tokenCount,
        .unitId = unitId,
        .renderer = renderer,
        .locked = true,
    });
  }

  return RenderFixture{
      .project = std::move(project),
      .trackId = trackId,
      .regionId = regionId,
      .source = seam::rendering::TrackVoicebankSource{
          .trackId = trackId,
          .manifest = candidate.manifest,
          .bankRoot = candidate.bankRoot,
          .contentHash = candidate.contentHash,
          .trust = candidate.trust,
      },
  };
}

seam::authoring::RenderProgress waitForTerminal(
    seam::authoring::AuthoringRenderCoordinator& coordinator,
    std::uint64_t requestedRevision,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{8000}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = coordinator.progress();
    if (progress.requestedRevision == requestedRevision &&
        (progress.state == seam::authoring::RenderState::Ready ||
         progress.state == seam::authoring::RenderState::Cancelled ||
         progress.state == seam::authoring::RenderState::Failed)) {
      return progress;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return coordinator.progress();
}

struct DebounceObservation final {
  std::mutex mutex;
  std::condition_variable condition;
  std::uint64_t startedRevision{0U};
  std::uint64_t finishedRevision{0U};
  std::vector<std::uint64_t> renderedRevisions;

  seam::authoring::RenderCoordinatorHooks hooks() {
    seam::authoring::RenderCoordinatorHooks result;
    // Longer than every observation deadline below. The real timed wait must
    // wake from cancellation/replacement, not expire before the assertion.
    result.debounceInterval = std::chrono::seconds{30};
    result.beforeDebounceWait = [this](std::uint64_t revision) {
      std::lock_guard lock(mutex);
      startedRevision = revision;
      condition.notify_all();
    };
    result.afterDebounceWait = [this](std::uint64_t revision) {
      std::lock_guard lock(mutex);
      finishedRevision = revision;
      condition.notify_all();
    };
    result.beforeRender = [this](std::uint64_t revision, std::stop_token) {
      std::lock_guard lock(mutex);
      renderedRevisions.push_back(revision);
    };
    return result;
  }

  bool waitFor(std::uint64_t revision, bool finished) {
    std::unique_lock lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds{2}, [&] {
      return (finished ? finishedRevision : startedRevision) == revision;
    });
  }
};

template <typename Predicate>
bool waitForCondition(Predicate&& predicate,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds{8000}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return predicate();
}

// Holds a coordinator thread at one of its hooks until the test lets it go. It ignores stop
// requests on purpose: a test that acts while the thread is held decides, and only it, when the
// thread continues. The wait is bounded so a failed check cannot hang the runner.
struct HookBarrier final {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered{false};
  bool released{false};

  void pass() {
    std::unique_lock lock(mutex);
    entered = true;
    condition.notify_all();
    static_cast<void>(condition.wait_for(lock, std::chrono::seconds{20},
                                         [this] { return released; }));
  }
  [[nodiscard]] bool waitEntered() {
    std::unique_lock lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds{10}, [this] { return entered; });
  }
  void release() {
    std::lock_guard lock(mutex);
    released = true;
    condition.notify_all();
  }
};

// Lets a held thread go when a case ends, whether it finished or stopped on a failed check.
struct ReleaseOnExit final {
  HookBarrier& barrier;
  ~ReleaseOnExit() { barrier.release(); }
};

}  // namespace

TEST_CASE("coordinator publishes typed procedural previews without sample bank approval") {
  seam::application::ProjectFactory factory{8000U};
  auto project = factory.createProject("Procedural preview");
  const auto trackId = factory.addVocalTrack(project, "Draft singer");
  const auto regionId = factory.addRegion(project, trackId, "Vowel", seam::time::Tick{960}, seam::time::Tick{960});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 69U,
      U"あ", seam::domain::Language::Japanese);
  project.findRegion(regionId)->lyrics.push_back(std::move(lyric));
  project.findRegion(regionId)->notes.push_back(std::move(note));
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "coordinator-draft";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto resource = seam::voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  std::vector<seam::rendering::TrackSingerSource> sources{
      seam::rendering::TrackProceduralSource{trackId, resource.value(), "neutral"}};
  const auto expected = seam::rendering::ProductionProjectRenderer{}.renderWithSources(
      project, sources, trackId, regionId, 1U, 48000U); CHECK(expected);
  seam::authoring::AuthoringRenderCoordinator coordinator{uniqueTempRoot("procedural-preview")};
  coordinator.submitWithSources(project, sources, trackId, regionId, 1U, 48000U,
      seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 1U).state == seam::authoring::RenderState::Ready);
  CHECK(coordinator.progress().totalPhrases == 1U);
  CHECK(coordinator.progress().completedPhrases == 1U);
  const auto published = coordinator.latest(); CHECK(published);
  CHECK(published->result.interleaved == expected.value().interleaved);
  CHECK(published->activeRenderer == "seam.source-filter.v1");
  CHECK(published->activeVoicebankId.empty()); CHECK(published->result.activeUnitPlan.empty());
  CHECK(project.findVocalTrack(trackId)->voicebank.id.empty());
  auto invalid = sources;
  std::get<seam::rendering::TrackProceduralSource>(invalid.front()).resource.identity.contentHash = std::string(64U, 'f');
  coordinator.submitWithSources(project, invalid, trackId, regionId, 2U, 48000U,
      seam::rendering::RenderQuality::Preview, true);
  const auto failed = waitForTerminal(coordinator, 2U);
  CHECK(failed.state == seam::authoring::RenderState::Failed);
  CHECK(failed.failure == seam::authoring::RenderFailureKind::RenderFailed);
  CHECK(coordinator.latest()->projectRevision == 1U);
  CHECK(coordinator.latest()->result.interleaved == expected.value().interleaved);
  coordinator.submitWithSources(project, sources, trackId, regionId, 3U, 48000U,
      seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 3U).state == seam::authoring::RenderState::Ready);
  coordinator.submitWithSources(project, invalid, trackId, regionId, 2U, 48000U,
      seam::rendering::RenderQuality::Preview, true);
  CHECK(coordinator.progress().requestedRevision == 3U);
  CHECK(coordinator.latest()->projectRevision == 3U);
  CHECK(coordinator.latest()->result.interleaved == expected.value().interleaved);
}

TEST_CASE("authoring_render_coordinator_matches_direct_production_renderer") {
  auto fixture = makeRenderFixture();
  const auto coordinatorCache = uniqueTempRoot("render-coordinator-parity");
  const auto directCache = uniqueTempRoot("render-direct-parity");
  seam::authoring::AuthoringRenderCoordinator coordinator{coordinatorCache};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 42U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  const auto progress = waitForTerminal(coordinator, 42U);
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.publishedRevision == 42U);
  CHECK_NEAR(progress.fraction, 1.0, 0.000001);

  auto published = coordinator.acquire();
  CHECK(published);
  CHECK(published->state == seam::authoring::RenderState::Ready);
  CHECK(published->projectRevision == 42U);
  CHECK(published->projectId == fixture.project.id()); CHECK(published->requestId != 0U); CHECK(published->sourceIdentity);
  CHECK(coordinator.matchesCurrent(*published)); CHECK(coordinator.acquireCurrent());
  CHECK(!published->result.interleaved.empty());

  seam::rendering::PcmCache cache{directCache};
  seam::rendering::ProductionProjectRenderer renderer;
  const std::array sources{fixture.source};
  const auto direct = renderer.render(
      fixture.project, sources, fixture.trackId, fixture.regionId, 42U, 48000U,
      seam::rendering::RenderQuality::Preview,
      seam::synthesis::PhraseRenderOptions{}, &cache);
  CHECK(direct);
  CHECK(published->result.interleaved == direct.value().interleaved);
  CHECK(published->result.phraseContentHashes ==
        direct.value().phraseContentHashes);
  CHECK(published->result.activeUnitPlan == direct.value().activeUnitPlan);
}

TEST_CASE("successful preview carries a classical edge-mark warning to render status") {
  auto fixture = makeRenderFixture();
  const auto unit = std::find_if(fixture.source.manifest.units.begin(),
      fixture.source.manifest.units.end(), [](const auto& candidate) {
        return candidate.id == "demo.ja.g4.o.01";
      });
  CHECK(unit != fixture.source.manifest.units.end());
  unit->pitchMarks.clear();
  const auto contentHash = seam::voicebank::computeVoicebankContentHash(
      fixture.source.manifest, fixture.source.bankRoot);
  CHECK(contentHash);
  fixture.source.contentHash = contentHash.value();
  fixture.project.findVocalTrack(fixture.trackId)->voicebank.contentHash =
      contentHash.value();

  const std::array sources{fixture.source};
  seam::rendering::PcmCache cache{uniqueTempRoot("edge-notice-cache")};
  const seam::rendering::ProductionProjectRenderer renderer;
  const auto direct = renderer.render(
      fixture.project, sources, fixture.trackId, fixture.regionId, 7U,
      48000U, seam::rendering::RenderQuality::Final, {}, &cache);
  CHECK(direct);
  CHECK(direct.value().renderNotice.has_value());
  CHECK(direct.value().renderNotice->message.find(
      "Voiced-edge pitch retargeting unavailable") != std::string::npos);
  CHECK(seam::rendering::validateCompleteProjectRender(direct.value()));
  const auto cached = renderer.render(
      fixture.project, sources, fixture.trackId, fixture.regionId, 7U,
      48000U, seam::rendering::RenderQuality::Final, {}, &cache);
  CHECK(cached);
  CHECK(cached.value().cacheHits > 0U);
  CHECK(cached.value().renderNotice.has_value());
  CHECK(cached.value().renderNotice->message.find(
      "Voiced-edge pitch retargeting unavailable") != std::string::npos);
  CHECK(seam::rendering::validateCompleteProjectRender(cached.value()));

  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-edge-notice")};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId,
      7U, 48000U, seam::rendering::RenderQuality::Preview);
  const auto progress = waitForTerminal(coordinator, 7U);
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.diagnostic.find("non-blocking notice") != std::string::npos);
  CHECK(progress.diagnostic.find(
      "Voiced-edge pitch retargeting unavailable") != std::string::npos);
  const auto published = coordinator.latest();
  CHECK(published);
  CHECK(published->diagnostic == progress.diagnostic);
}

TEST_CASE("render source identity rejects same revision resubmissions cancellation and unrelated coordinators") {
  using namespace seam;
  auto fixture = makeRenderFixture();
  authoring::AuthoringRenderCoordinator coordinator{uniqueTempRoot("measurement-source")};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 42U, 48000U, rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 42U).state == authoring::RenderState::Ready);
  const auto first = coordinator.latest(); CHECK(first); CHECK(coordinator.matchesCurrent(*first));
  auto altered = *first; altered.result.interleaved[0] = 0.25F;
  CHECK(!coordinator.matchesCurrent(altered));
  altered = *first; altered.result.sampleRate = 96000U; CHECK(!coordinator.matchesCurrent(altered));
  altered = *first; altered.quality = rendering::RenderQuality::Final; CHECK(!coordinator.matchesCurrent(altered));
  authoring::AuthoringRenderCoordinator other{uniqueTempRoot("measurement-other")};
  other.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 42U, 48000U, rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(other, 42U).state == authoring::RenderState::Ready);
  CHECK(other.latest()->requestId == first->requestId); CHECK(other.latest()->projectId == first->projectId);
  CHECK(!other.matchesCurrent(*first));
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 42U, 48000U, rendering::RenderQuality::Preview, true);
  CHECK(!coordinator.matchesCurrent(*first)); // Revision and content can be identical; the request is not.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  bool renewed = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto current = coordinator.acquireCurrent();
    if (current && current->requestId != first->requestId) { renewed = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(renewed); const auto second = coordinator.latest(); CHECK(second);
  CHECK(second->projectId == first->projectId); CHECK(second->projectRevision == first->projectRevision);
  CHECK(second->requestId != first->requestId); CHECK(coordinator.matchesCurrent(*second));
  coordinator.cancel(); CHECK(!coordinator.acquireCurrent()); CHECK(!coordinator.matchesCurrent(*second));
  CHECK(coordinator.acquire()->state == authoring::RenderState::Ready); // Retained playback is not current measurement evidence.
  auto invalid = fixture.source; invalid.contentHash = std::string(64U, '0');
  coordinator.submit(fixture.project, {invalid}, fixture.trackId, fixture.regionId, 43U, 48000U, rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 43U).state == authoring::RenderState::Failed);
  CHECK(!coordinator.acquireCurrent()); CHECK(!coordinator.matchesCurrent(*second));
  CHECK(coordinator.acquire()->state == authoring::RenderState::Ready);
}

TEST_CASE("audio measurement capture binds actual render input and rejects same content document replacement") {
  using namespace seam; auto fixture = makeRenderFixture();
  application::EditorSession session{fixture.project};
  authoring::AuthoringRenderCoordinator coordinator{uniqueTempRoot("measurement-session")};
  CHECK(!authoring::AudioMeasurementCapture::prepare(session, coordinator));
  coordinator.submit(session.project(), {fixture.source}, fixture.trackId, fixture.regionId, session.revision(),
      48000U, rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, session.revision()).state == authoring::RenderState::Ready);
  auto capture = authoring::AudioMeasurementCapture::prepare(session, coordinator); CHECK(capture);
  CHECK(capture.value().matches(session, coordinator)); CHECK(capture.value().source().sourceProject);
  const auto count = capture.value().source().result.interleaved.size() / capture.value().source().result.channelCount;
  const auto measured = capture.value().measure(0U, count, 64U); CHECK(measured); CHECK(measured.value().bins.size() == 64U);
  const auto before = session.project(); application::EditorSession reopened{before};
  CHECK(reopened.revision() == session.revision());
  CHECK(!capture.value().matches(reopened, coordinator)); // Same ID, revision and contents; different session generation.
  CHECK(capture.value().measure(0U, count, 64U)); // Retained immutable data remains readable, never current publication authority.
  auto fresh = authoring::AudioMeasurementCapture::prepare(reopened, coordinator); CHECK(fresh);
  CHECK(fresh.value().matches(reopened, coordinator)); fresh.value().close(); CHECK(!fresh.value().matches(reopened, coordinator));
  reopened.project().findVocalTrack(fixture.trackId)->muted = true; // Also reject changes bypassing revision counters.
  CHECK(!authoring::AudioMeasurementCapture::prepare(reopened, coordinator));
  reopened.project().findVocalTrack(fixture.trackId)->muted = before.findVocalTrack(fixture.trackId)->muted;
  CHECK(session.replaceProject(before)); CHECK(!capture.value().matches(session, coordinator));
  CHECK(!authoring::AudioMeasurementCapture::prepare(session, coordinator)); // New revision needs a matching render.
  auto active = authoring::AudioMeasurementCapture::prepare(reopened, coordinator); CHECK(active);
  coordinator.cancel(); CHECK(!active.value().matches(reopened, coordinator));
  CHECK(!authoring::AudioMeasurementCapture::prepare(reopened, coordinator)); CHECK(reopened.project() == before);
}

TEST_CASE("audio measurement worker retires rejects stale results and never publishes cancelled work") {
  using namespace seam; auto fixture = makeRenderFixture();
  auto session = std::make_unique<application::EditorSession>(fixture.project);
  authoring::AuthoringRenderCoordinator coordinator{uniqueTempRoot("measurement-worker")};
  coordinator.submit(session->project(), {fixture.source}, fixture.trackId, fixture.regionId, 0U, 48000U, rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 0U).state == authoring::RenderState::Ready);
  authoring::AudioMeasurementJob job;
  const auto wait = [&]() -> core::Result<bool> {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    while (std::chrono::steady_clock::now() < deadline) {
      auto result = job.poll(*session, coordinator); if (!result || result.value()) return result;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return core::failure<bool>(core::ErrorCode::Internal, "Measurement did not retire");
  };
  CHECK(job.start(*session, coordinator, 0U, 4096U, 32U)); CHECK(job.preparing());
  CHECK(!job.start(*session, coordinator, 0U, 4096U, 32U)); CHECK(wait());
  CHECK(!job.preparing()); CHECK(job.current(*session, coordinator)); CHECK(job.current(*session, coordinator)->bins.size() == 32U);
  const auto before = session->project(); session = std::make_unique<application::EditorSession>(before); CHECK(!job.current(*session, coordinator));
  CHECK(job.start(*session, coordinator, 0U, 4096U, 32U)); session = std::make_unique<application::EditorSession>(before); CHECK(!wait());
  CHECK(job.state() == authoring::AudioMeasurementJob::State::Failed); CHECK(!job.current(*session, coordinator));
  CHECK(job.start(*session, coordinator, 0U, 4096U, 32U)); job.cancel(); CHECK(wait());
  CHECK(job.state() == authoring::AudioMeasurementJob::State::Cancelled); CHECK(!job.current(*session, coordinator));
  CHECK(job.start(*session, coordinator, 0U, std::numeric_limits<std::size_t>::max())); CHECK(!wait());
  CHECK(!job.current(*session, coordinator)); CHECK(session->project() == before); CHECK(!session->canUndo());
}

TEST_CASE("authoring_render_coordinator_publishes_command_impact") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-impact")};
  seam::application::CommandImpact impact{
      .scope = seam::application::CommandAudioImpact::PhraseAudio,
      .regionIds = {fixture.regionId},
  };

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 43U, 48000U,
                     seam::rendering::RenderQuality::Preview, false, impact);
  const auto progress = waitForTerminal(coordinator, 43U);
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.activeVoicebankId == fixture.source.manifest.id);
  CHECK(progress.activeVoicebankVersion == fixture.source.manifest.version);
  const auto published = coordinator.acquire();
  CHECK(published);
  CHECK(published->impact.scope ==
        seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(published->impact.regionIds.size() == 1U);
  CHECK(published->impact.regionIds.front() == fixture.regionId);
}

TEST_CASE("authoring_render_coordinator_newer_revision_prevents_old_publication") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool firstEntered = false;

  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t revision, std::stop_token token) {
    if (revision != 101U) return;
    std::unique_lock lock(gateMutex);
    firstEntered = true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock, token, [] { return false; }));
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-stale"), std::move(hooks)};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 101U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return firstEntered; }));
  }
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 102U, 48000U,
                     seam::rendering::RenderQuality::Preview);

  const auto progress = waitForTerminal(coordinator, 102U);
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.publishedRevision == 102U);
  auto published = coordinator.acquire();
  CHECK(published);
  CHECK(published->projectRevision == 102U);
  const auto stats = coordinator.stats();
  CHECK(stats.submitted == 2U);
  CHECK(stats.completed == 1U);
  CHECK(stats.cancelled + stats.stale >= 1U);
}

TEST_CASE("authoring_render_coordinator_keeps_stale_audibility_while_rendering") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered = false;
  bool release = false;
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t revision, std::stop_token token) {
    if (revision != 91U) return;
    std::unique_lock lock(gateMutex);
    entered = true;
    gateCondition.notify_all();
    static_cast<void>(
        gateCondition.wait(lock, token, [&] { return release; }));
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-stale-audibility"), std::move(hooks)};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 90U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 90U).state ==
        seam::authoring::RenderState::Ready);
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 91U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return entered; }));
  }
  const auto rendering = coordinator.progress();
  CHECK(rendering.state == seam::authoring::RenderState::Rendering);
  CHECK(rendering.audibleAudioStale);
  CHECK(rendering.requestedRevision == 91U);
  CHECK(rendering.publishedRevision == 90U);
  {
    std::lock_guard lock(gateMutex);
    release = true;
    gateCondition.notify_all();
  }
  const auto ready = waitForTerminal(coordinator, 91U);
  CHECK(ready.state == seam::authoring::RenderState::Ready);
  CHECK(!ready.audibleAudioStale);
}

TEST_CASE("five_minute_stereo_pcm_copies_share_one_allocation") {
  constexpr std::size_t sampleCount = 5U * 60U * 48000U * 2U;
  seam::rendering::SharedPcmBuffer pcm;
  pcm.assign(sampleCount, 0.0F);
  const auto first = pcm;
  const auto second = first;
  CHECK(pcm.size() == sampleCount);
  CHECK(pcm.allocatedBytes() == sampleCount * sizeof(float));
  CHECK(first.storageIdentity() == pcm.storageIdentity());
  CHECK(second.storageIdentity() == pcm.storageIdentity());
}

TEST_CASE("authoring_render_coordinator_orders_same_revision_publications") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool firstEntered = false;
  bool release = false;
  std::size_t publicationCalls = 0U;

  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforePublication = [&](std::uint64_t revision, std::stop_token stopToken) {
    if (revision != 301U) return;
    std::unique_lock lock(gateMutex);
    ++publicationCalls;
    gateCondition.notify_all();
    if (publicationCalls != 1U) return;
    firstEntered = true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock, stopToken, [&] { return release; }));
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-same-revision"), std::move(hooks)};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 301U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return firstEntered; }));
  }
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 301U, 48000U,
                     seam::rendering::RenderQuality::Final, true);
  const auto replacementProgress = coordinator.progress();
  CHECK(replacementProgress.requestedQuality ==
        seam::rendering::RenderQuality::Final);
  CHECK(replacementProgress.publishedQuality ==
        seam::rendering::RenderQuality::Preview);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{120};
  {
    std::unique_lock lock(gateMutex);
    release = true;
    gateCondition.notify_all();
    // Release the hook mutex while waiting. Sleeping with it held can starve
    // either publication hook for the entire timeout on an unfair mutex.
    CHECK(gateCondition.wait_until(lock, deadline,
                                  [&] { return publicationCalls >= 2U; }));
  }

  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = coordinator.progress();
    if (progress.state == seam::authoring::RenderState::Ready &&
        progress.publishedQuality == seam::rendering::RenderQuality::Final &&
        coordinator.stats().completed >= 1U) {
      break;
    }
    if (progress.state == seam::authoring::RenderState::Failed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  const auto published = coordinator.acquire();
  CHECK(published);
  if (!published || published->quality != seam::rendering::RenderQuality::Final) {
    const auto prog = coordinator.progress();
    const auto st = coordinator.stats();
    std::lock_guard lock(gateMutex);
    std::string diag = "DIAG: pubQuality=" +
        std::to_string(published ? static_cast<int>(published->quality) : -1) +
        ", progState=" + std::to_string(static_cast<int>(prog.state)) +
        ", progQuality=" + std::to_string(static_cast<int>(prog.publishedQuality)) +
        ", progDiag=" + prog.diagnostic +
        ", pubCalls=" + std::to_string(publicationCalls) +
        ", completed=" + std::to_string(st.completed) +
        ", stale=" + std::to_string(st.stale) +
        ", failed=" + std::to_string(st.failed);
    throw seam::test::Failure(diag);
  }
  CHECK(published->quality == seam::rendering::RenderQuality::Final);
  const auto stats = coordinator.stats();
  CHECK(stats.completed == 1U);
  CHECK(stats.stale >= 1U);
}

TEST_CASE("coordinator cancellation wakes an admitted debounce without rendering and permits a fresh request") {
  auto fixture = makeRenderFixture();
  DebounceObservation observation;
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel-debounce"), observation.hooks()};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 301U, 48000U,
                     seam::rendering::RenderQuality::Preview, false);
  CHECK(observation.waitFor(301U, false));
  // The entry callback holds the coordinator mutex. cancel() cannot clear the
  // pending request until the actual inner condition-variable wait releases it.
  coordinator.cancel();
  CHECK(observation.waitFor(301U, true));
  {
    std::lock_guard lock(observation.mutex);
    CHECK(observation.renderedRevisions.empty());
  }
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Cancelled);
  CHECK(coordinator.stats().cancelled == 1U);
  CHECK(coordinator.stats().completed == 0U);
  CHECK(coordinator.stats().failed == 0U);
  CHECK(!coordinator.acquireCurrent());
  CHECK(coordinator.latest()->state == seam::authoring::RenderState::Idle);

  // The finished callback also holds the coordinator mutex. This submission
  // cannot hide an empty pending_ before the worker rechecks it under that lock.
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 302U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 302U).state ==
        seam::authoring::RenderState::Ready);
  const auto current = coordinator.acquireCurrent();
  CHECK(current);
  CHECK(current->projectRevision == 302U);
  CHECK(!current->result.interleaved.empty());
  CHECK(coordinator.stats().submitted == 2U);
  CHECK(coordinator.stats().completed == 1U);
  CHECK(coordinator.stats().failed == 0U);
  {
    std::lock_guard lock(observation.mutex);
    CHECK(observation.renderedRevisions == std::vector<std::uint64_t>{302U});
  }
}

TEST_CASE("coordinator immediate replacement wakes an admitted debounce and renders only the replacement") {
  auto fixture = makeRenderFixture();
  DebounceObservation observation;
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-replace-debounce"), observation.hooks()};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 401U, 48000U,
                     seam::rendering::RenderQuality::Preview, false);
  CHECK(observation.waitFor(401U, false));
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 402U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(observation.waitFor(401U, true));
  CHECK(waitForTerminal(coordinator, 402U).state ==
        seam::authoring::RenderState::Ready);
  const auto current = coordinator.acquireCurrent();
  CHECK(current);
  CHECK(current->projectRevision == 402U);
  CHECK(coordinator.stats().cancelled == 1U);
  CHECK(coordinator.stats().completed == 1U);
  CHECK(coordinator.stats().failed == 0U);
  {
    std::lock_guard lock(observation.mutex);
    CHECK(observation.renderedRevisions == std::vector<std::uint64_t>{402U});
  }
}

TEST_CASE("authoring_render_coordinator_cancel_invalidates_unpublished_audio") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered = false;
  bool release = false;
  bool exited = false;

  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforePublication = [&](std::uint64_t revision, std::stop_token) {
    if (revision != 302U) return;
    std::unique_lock lock(gateMutex);
    entered = true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock, [&] { return release; }));
    exited = true;
    gateCondition.notify_all();
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel-publication"),
      std::move(hooks)};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 302U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return entered; }));
  }
  coordinator.cancel();
  {
    std::lock_guard lock(gateMutex);
    release = true;
    gateCondition.notify_all();
  }
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{20};
  while (std::chrono::steady_clock::now() < deadline) {
    bool finished = false;
    {
      std::lock_guard lock(gateMutex);
      finished = exited;
    }
    if (finished) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  const auto published = coordinator.acquire();
  CHECK(published);
  CHECK(published->state == seam::authoring::RenderState::Idle);
  CHECK(coordinator.stats().completed == 0U);
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Cancelled);
}

TEST_CASE("authoring_render_coordinator_ignores_lower_revision_submission") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered = false;
  bool release = false;

  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t revision, std::stop_token token) {
    if (revision != 202U) return;
    std::unique_lock lock(gateMutex);
    entered = true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock, token, [&] { return release; }));
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-revision-order"), std::move(hooks)};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 202U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return entered; }));
  }
  const auto submitted = coordinator.stats().submitted;
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 201U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(coordinator.stats().submitted == submitted);
  {
    std::lock_guard lock(gateMutex);
    release = true;
    gateCondition.notify_all();
  }

  const auto progress = waitForTerminal(coordinator, 202U);
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.publishedRevision == 202U);
  const auto published = coordinator.acquire();
  CHECK(published);
  CHECK(published->projectRevision == 202U);
}

TEST_CASE("authoring_render_coordinator_cancellation_is_not_failure") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered = false;

  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t, std::stop_token token) {
    std::unique_lock lock(gateMutex);
    entered = true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock, token, [] { return false; }));
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel"), std::move(hooks)};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 77U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return entered; }));
  }
  coordinator.cancel();
  const auto progress = waitForTerminal(coordinator, 77U);
  CHECK(progress.state == seam::authoring::RenderState::Cancelled);
  CHECK(coordinator.stats().failed == 0U);
}

TEST_CASE("cancelling a coordinator that has nothing in flight reports nothing as cancelled") {
  // A fresh project has nothing to render, and the application still revokes the old sound's
  // authority when it refreshes its voicebanks. There is no request to cancel, so the status
  // must stay idle instead of announcing a cancellation the creator never asked for.
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel-idle")};
  std::atomic<int> notifications{0};
  coordinator.setCompletionCallback([&notifications] { notifications.fetch_add(1); });
  coordinator.cancel();
  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Idle);
  CHECK(progress.diagnostic.empty());
  CHECK(progress.failure == seam::authoring::RenderFailureKind::None);
  CHECK(coordinator.stats().cancelled == 0U);
  CHECK(notifications.load() == 0);
  CHECK(!coordinator.acquireCurrent());
  // The coordinator is still usable: the next request queues and renders as usual.
  auto fixture = makeRenderFixture();
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 78U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 78U).state == seam::authoring::RenderState::Ready);
}

TEST_CASE("cancelling after a render has finished does not turn the finished render into a cancellation") {
  // A cancel that arrives just after the render ended used to overwrite Ready with Cancelled,
  // so the status said the phrase was cancelled while the published audio was complete.
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel-finished")};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 79U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 79U).state == seam::authoring::RenderState::Ready);
  const auto cancelledBefore = coordinator.stats().cancelled;
  coordinator.cancel();
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Ready);
  CHECK(coordinator.stats().cancelled == cancelledBefore);
  // Revoking the current-audio authority is unchanged: the retained audio is not current.
  CHECK(!coordinator.acquireCurrent());
  const auto retained = coordinator.acquire();
  CHECK(retained);
  CHECK(retained->state == seam::authoring::RenderState::Ready);
}

TEST_CASE("the progress callback reports a queued request and a started render, and only the completion callback the end") {
  // A window that draws on demand paints "queued" and "rendering" only if something asks for
  // those frames, and the debounced submission happens after the edit's own frame. The
  // progress callback is that request; the completion callback keeps meaning "the attempt is over".
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-progress-callback")};
  std::mutex mutex;
  std::vector<seam::authoring::RenderState> progressStates;
  std::vector<seam::authoring::RenderState> completionStates;
  coordinator.setProgressCallback([&] {
    std::lock_guard lock(mutex);
    progressStates.push_back(coordinator.progress().state);
  });
  coordinator.setCompletionCallback([&] {
    std::lock_guard lock(mutex);
    completionStates.push_back(coordinator.progress().state);
  });
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 80U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 80U).state == seam::authoring::RenderState::Ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (std::chrono::steady_clock::now() < deadline) {
    {
      std::lock_guard lock(mutex);
      if (!completionStates.empty()) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  std::lock_guard lock(mutex);
  CHECK(progressStates.size() == 2U);
  if (progressStates.size() == 2U) {
    CHECK(progressStates[0] == seam::authoring::RenderState::Queued);
    CHECK(progressStates[1] == seam::authoring::RenderState::Rendering);
  }
  CHECK(completionStates.size() == 1U);
  if (completionStates.size() == 1U) {
    CHECK(completionStates.front() == seam::authoring::RenderState::Ready);
  }
}

TEST_CASE("resetting a coordinator that has nothing in flight reports and notifies nothing") {
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-reset-nothing")};
  std::atomic<int> notifications{0};
  coordinator.setCompletionCallback([&notifications] { notifications.fetch_add(1); });
  coordinator.resetToIdle();
  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Idle);
  CHECK(progress.diagnostic.empty());
  CHECK(coordinator.stats().cancelled == 0U);
  CHECK(notifications.load() == 0);
  CHECK(!coordinator.acquireCurrent());
  // The coordinator is still usable: the next request renders as usual.
  auto fixture = makeRenderFixture();
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 501U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 501U).state == seam::authoring::RenderState::Ready);
}

TEST_CASE("resetting a coordinator whose request is still queued drops it quietly and reports idle") {
  auto fixture = makeRenderFixture();
  DebounceObservation observation;
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-reset-queued"), observation.hooks()};
  std::atomic<int> completions{0};
  coordinator.setCompletionCallback([&completions] { completions.fetch_add(1); });
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 502U, 48000U,
                     seam::rendering::RenderQuality::Preview, false);
  CHECK(observation.waitFor(502U, false));
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Queued);
  coordinator.resetToIdle();
  CHECK(observation.waitFor(502U, true));
  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Idle);
  CHECK(progress.requestedRevision == 0U);
  CHECK(progress.diagnostic.empty());
  CHECK(completions.load() == 1);
  CHECK(!coordinator.acquireCurrent());
  {
    std::lock_guard lock(observation.mutex);
    CHECK(observation.renderedRevisions.empty());
  }
  // The dropped request is counted as dropped; it neither completed nor failed.
  CHECK(coordinator.stats().cancelled == 1U);
  CHECK(coordinator.stats().completed == 0U);
  CHECK(coordinator.stats().failed == 0U);
  // Nothing is newer any more, so a revision lower than the dropped request's is accepted: a score
  // that was emptied and then replaced by one with a lower revision still renders.
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 501U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 501U).state == seam::authoring::RenderState::Ready);
}

TEST_CASE("resetting a coordinator while it renders reports idle at once and the abandoned render never speaks again") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered = false;
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t, std::stop_token token) {
    std::unique_lock lock(gateMutex);
    entered = true;
    gateCondition.notify_all();
    // Until the coordinator asks the render to stop.
    static_cast<void>(gateCondition.wait(lock, token, [] { return false; }));
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-reset-rendering"), std::move(hooks)};
  std::atomic<int> completions{0};
  coordinator.setCompletionCallback([&completions] { completions.fetch_add(1); });
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 504U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{10}, [&] { return entered; }));
  }
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Rendering);
  coordinator.resetToIdle();
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Idle);
  CHECK(completions.load() == 1);
  // The abandoned render winds down on its own: it is counted as cancelled, publishes nothing and
  // leaves the state alone.
  CHECK(waitForCondition([&] { return coordinator.stats().cancelled == 1U; }));
  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Idle);
  CHECK(progress.requestedRevision == 0U);
  CHECK(!coordinator.acquireCurrent());
  CHECK(coordinator.stats().completed == 0U);
  CHECK(coordinator.stats().failed == 0U);
  CHECK(completions.load() == 1);
}

TEST_CASE("a render the worker had taken but not yet reported is not reported as rendering after a reset") {
  // Between taking a request and saying so, the worker counts the phrases of the whole project.
  // A reset that lands in that interval must not be overwritten by a state that no longer holds.
  auto fixture = makeRenderFixture();
  HookBarrier admitted;
  ReleaseOnExit release{admitted};
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.afterAdmission = [&admitted](std::uint64_t, std::stop_token) { admitted.pass(); };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-reset-admitted"), std::move(hooks)};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 505U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(admitted.waitEntered());
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Queued);
  coordinator.resetToIdle();
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Idle);
  admitted.release();
  CHECK(waitForCondition([&] { return coordinator.stats().cancelled == 1U; }));
  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Idle);
  CHECK(progress.requestedRevision == 0U);
  CHECK(!coordinator.acquireCurrent());
}

TEST_CASE("a request cancelled while its submitting thread has yet to finish is left cancelled") {
  // The request has been admitted and reported as queued; its submitting thread is held before it
  // notifies and wakes the worker. A cancel in that interval finds the request pending and reports
  // the cancellation; the thread carrying on afterwards must not put the request back on screen
  // with nothing left to render it.
  auto fixture = makeRenderFixture();
  HookBarrier admitted;
  ReleaseOnExit release{admitted};
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.afterSubmitAdmission = [&admitted](std::uint64_t) { admitted.pass(); };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel-admitted"), std::move(hooks)};
  std::jthread submitter{[&] {
    coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                       fixture.regionId, 506U, 48000U,
                       seam::rendering::RenderQuality::Preview, false);
  }};
  CHECK(admitted.waitEntered());
  coordinator.cancel();
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Cancelled);
  // The report is for the request that was cancelled, which is the one the cancel found.
  CHECK(coordinator.progress().requestedRevision == 506U);
  admitted.release();
  submitter.join();
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Cancelled);
  CHECK(coordinator.stats().cancelled == 1U);
}

TEST_CASE("resetting a coordinator after a failure or a finished render returns it to idle and keeps the publication as history") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-reset-terminal")};
  std::atomic<int> completions{0};
  coordinator.setCompletionCallback([&completions] { completions.fetch_add(1); });
  // A failure: the coordinator was given no voicebank.
  coordinator.submit(fixture.project, {}, fixture.trackId, fixture.regionId, 507U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 507U).state == seam::authoring::RenderState::Failed);
  CHECK(waitForCondition([&] { return completions.load() >= 1; }));
  const auto beforeReset = completions.load();
  coordinator.resetToIdle();
  {
    const auto progress = coordinator.progress();
    CHECK(progress.state == seam::authoring::RenderState::Idle);
    CHECK(progress.failure == seam::authoring::RenderFailureKind::None);
    CHECK(progress.diagnostic.empty());
  }
  CHECK(completions.load() == beforeReset + 1);
  // A render that finished.
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId,
                     508U, 48000U, seam::rendering::RenderQuality::Preview, true);
  CHECK(waitForTerminal(coordinator, 508U).state == seam::authoring::RenderState::Ready);
  const auto cancelledBefore = coordinator.stats().cancelled;
  coordinator.resetToIdle();
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Idle);
  CHECK(coordinator.stats().cancelled == cancelledBefore);
  // The audio that was current no longer is; the publication itself stays readable as history.
  CHECK(!coordinator.acquireCurrent());
  const auto retained = coordinator.acquire();
  CHECK(retained);
  CHECK(retained->state == seam::authoring::RenderState::Ready);
  CHECK(retained->projectRevision == 508U);
}

TEST_CASE("authoring_render_coordinator_publishes_voicebank_failures_as_silence") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-bank-failure")};

  coordinator.submit(fixture.project, {}, fixture.trackId, fixture.regionId,
                     81U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  auto progress = waitForTerminal(coordinator, 81U);
  CHECK(progress.state == seam::authoring::RenderState::Failed);
  auto missing = coordinator.acquire();
  CHECK(missing);
  CHECK(missing->state == seam::authoring::RenderState::Failed);
  CHECK(missing->failure ==
        seam::authoring::RenderFailureKind::VoicebankMissing);
  CHECK(missing->result.interleaved.empty());
  CHECK(missing->diagnostic.find("Voicebank") != std::string::npos);

  auto mismatch = fixture.source;
  mismatch.contentHash.assign(64U, 'f');
  coordinator.submit(fixture.project, {mismatch}, fixture.trackId,
                     fixture.regionId, 82U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  progress = waitForTerminal(coordinator, 82U);
  CHECK(progress.state == seam::authoring::RenderState::Failed);
  auto mismatched = coordinator.acquire();
  CHECK(mismatched);
  CHECK(mismatched->failure ==
        seam::authoring::RenderFailureKind::VoicebankContentMismatch);
  CHECK(mismatched->result.interleaved.empty());
}

TEST_CASE("authoring_render_coordinator_render_failure_names_the_sound_the_voicebank_cannot_sing") {
  // The bundled voice has no stand-alone /a/, so the default lyric あ cannot be rendered. The reason the
  // creator reads has to say which sound, whose lyric and where, not only that the voicebank cannot cover
  // the phoneme sequence.
  auto fixture = makeRenderFixture();
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->unitSelectionOverrides.clear();
  auto* lyric = region->findLyric(region->notes.front().lyricTokenId);
  CHECK(lyric != nullptr);
  if (lyric == nullptr) return;
  lyric->surface = U"あ";
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-uncovered-sound")};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId,
                     91U, 48000U, seam::rendering::RenderQuality::Preview);
  const auto progress = waitForTerminal(coordinator, 91U);
  CHECK(progress.state == seam::authoring::RenderState::Failed);
  CHECK(progress.failure == seam::authoring::RenderFailureKind::RenderFailed);
  CHECK(progress.diagnostic.find(
            "No voicebank unit covers the sound \"a\" of the lyric \"あ\" at bar 1, beat 1") !=
        std::string::npos);
}

TEST_CASE("authoring_render_coordinator_failed_render_preserves_previous_audio") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-failed-previous")};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 85U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  CHECK(waitForTerminal(coordinator, 85U).state ==
        seam::authoring::RenderState::Ready);
  auto previous = coordinator.acquire();
  CHECK(previous);
  const auto previousRevision = previous->projectRevision;
  const auto previousPcm = previous->result.interleaved;
  previous = {};

  coordinator.submit(fixture.project, {}, fixture.trackId, fixture.regionId,
                     86U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  CHECK(waitForTerminal(coordinator, 86U).state ==
        seam::authoring::RenderState::Failed);
  const auto current = coordinator.acquire();
  CHECK(current);
  CHECK(current->state == seam::authoring::RenderState::Ready);
  CHECK(current->projectRevision == previousRevision);
  CHECK(current->result.interleaved == previousPcm);
}

TEST_CASE("authoring_render_coordinator_publication_busy_keeps_context") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-publication-busy")};
  std::vector<seam::authoring::RealtimeProjectAudioPublication::ReadHandle>
      heldReaders;
  heldReaders.push_back(coordinator.acquire());
  CHECK(heldReaders.front());

  for (const auto revision : {131U, 132U}) {
    coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                       fixture.regionId, revision, 48000U,
                       seam::rendering::RenderQuality::Preview, true);
    CHECK(waitForTerminal(coordinator, revision).state ==
          seam::authoring::RenderState::Ready);
    heldReaders.push_back(coordinator.acquire());
    CHECK(heldReaders.back());
  }

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 133U, 48000U,
                     seam::rendering::RenderQuality::Preview, true);
  const auto progress = waitForTerminal(coordinator, 133U);
  CHECK(progress.state == seam::authoring::RenderState::Failed);
  CHECK(progress.failure == seam::authoring::RenderFailureKind::PublicationBusy);
  CHECK(progress.activeVoicebankId == fixture.source.manifest.id);
  CHECK(progress.activeVoicebankVersion == fixture.source.manifest.version);
}

TEST_CASE("authoring_render_coordinator_quality_changes_cache_identity") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-quality")};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 91U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  CHECK(waitForTerminal(coordinator, 91U).state ==
        seam::authoring::RenderState::Ready);
  auto preview = coordinator.acquire();
  CHECK(preview);
  const auto previewHashes = preview->result.phraseContentHashes;

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 92U, 48000U,
                     seam::rendering::RenderQuality::Final);
  CHECK(waitForTerminal(coordinator, 92U).state ==
        seam::authoring::RenderState::Ready);
  auto final = coordinator.acquire();
  CHECK(final);
  CHECK(final->quality == seam::rendering::RenderQuality::Final);
  CHECK(final->result.phraseContentHashes != previewHashes);
}

TEST_CASE("authoring_render_coordinator_character_display_does_not_change_audio") {
  auto fixture = makeRenderFixture();
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-character")};
  fixture.project.settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Minimal;
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 111U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  CHECK(waitForTerminal(coordinator, 111U).state ==
        seam::authoring::RenderState::Ready);
  auto visible = coordinator.acquire();
  CHECK(visible);
  const auto pcm = visible->result.interleaved;
  const auto hashes = visible->result.phraseContentHashes;
  visible = {};

  fixture.project.settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId,
                     fixture.regionId, 112U, 48000U,
                     seam::rendering::RenderQuality::Preview);
  CHECK(waitForTerminal(coordinator, 112U).state ==
        seam::authoring::RenderState::Ready);
  auto hidden = coordinator.acquire();
  CHECK(hidden);
  CHECK(hidden->result.interleaved == pcm);
  CHECK(hidden->result.phraseContentHashes == hashes);
  CHECK(hidden->result.cacheHits == hidden->result.phraseCount);
}

TEST_CASE("authoring_render_coordinator_shutdown_joins_active_worker") {
  auto fixture = makeRenderFixture();
  std::mutex gateMutex;
  std::condition_variable_any gateCondition;
  bool entered = false;
  bool exited = false;

  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t, std::stop_token token) {
    std::unique_lock lock(gateMutex);
    entered = true;
    gateCondition.notify_all();
    static_cast<void>(gateCondition.wait(lock, token, [] { return false; }));
    exited = true;
    gateCondition.notify_all();
  };

  auto coordinator = std::make_unique<seam::authoring::AuthoringRenderCoordinator>(
      uniqueTempRoot("render-coordinator-shutdown"), std::move(hooks));
  coordinator->submit(fixture.project, {fixture.source}, fixture.trackId,
                      fixture.regionId, 121U, 48000U,
                      seam::rendering::RenderQuality::Preview);
  {
    std::unique_lock lock(gateMutex);
    CHECK(gateCondition.wait_for(lock, std::chrono::seconds{2},
                                 [&] { return entered; }));
  }

  coordinator->shutdown();
  {
    std::lock_guard lock(gateMutex);
    CHECK(exited);
  }
  CHECK(coordinator->progress().state ==
        seam::authoring::RenderState::Cancelled);
  coordinator.reset();
}

TEST_CASE("a queued replacement request keeps what the screen knew about the audio it replaces") {
  auto fixture = makeRenderFixture();
  DebounceObservation observation;
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-queued-content"), observation.hooks()};
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 700U,
                     48000U, seam::rendering::RenderQuality::Preview, true);
  const auto first = waitForTerminal(coordinator, 700U);
  CHECK(first.state == seam::authoring::RenderState::Ready);
  CHECK(!first.activeRenderer.empty());
  // A debounced request stays queued until the debounce ends, which is far away here.
  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 701U,
                     48000U, seam::rendering::RenderQuality::Preview, false);
  CHECK(observation.waitFor(701U, false));
  const auto queued = coordinator.progress();
  CHECK(queued.state == seam::authoring::RenderState::Queued);
  CHECK(queued.requestedRevision == 701U);
  CHECK(queued.publishedRevision == 700U);
  CHECK(queued.publishedQuality == seam::rendering::RenderQuality::Preview);
  CHECK(queued.audibleAudioStale);
  CHECK(!queued.activeVoicebankId.empty());
  CHECK(queued.activeVoicebankId == first.activeVoicebankId);
  CHECK(queued.activeVoicebankVersion == first.activeVoicebankVersion);
  CHECK(queued.activeRenderer == first.activeRenderer);
}


TEST_CASE("a replacement request is reported as queued before the worker can take it, and never again once it has finished") {
  // The worker is busy with request 600. Request 601 is admitted, and its submitting thread is held
  // right after the admission, as a thread that has been preempted there would be. The worker then
  // finishes 600, takes 601 and renders it to the end before that thread runs again. The screen
  // must already say that 601 is queued at the moment the worker takes it, and must not go back to
  // "queued" over a render that has finished.
  auto fixture = makeRenderFixture();
  HookBarrier busy;
  HookBarrier submitterHeld;
  seam::authoring::AuthoringRenderCoordinator* coordinatorPtr = nullptr;
  std::atomic<int> stateWhenTaken{-1};
  std::atomic<std::uint64_t> revisionWhenTaken{0U};
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.beforeRender = [&](std::uint64_t revision, std::stop_token) {
    if (revision == 600U) busy.pass();
  };
  hooks.afterSubmitAdmission = [&](std::uint64_t revision) {
    if (revision == 601U) submitterHeld.pass();
  };
  hooks.afterAdmission = [&](std::uint64_t revision, std::stop_token) {
    if (revision != 601U || coordinatorPtr == nullptr) return;
    const auto progress = coordinatorPtr->progress();
    stateWhenTaken.store(static_cast<int>(progress.state));
    revisionWhenTaken.store(progress.requestedRevision);
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-queued-order"), std::move(hooks)};
  coordinatorPtr = &coordinator;

  std::mutex logMutex;
  std::vector<std::pair<seam::authoring::RenderState, std::uint64_t>> observations;
  const auto observe = [&] {
    const auto progress = coordinator.progress();
    std::lock_guard lock(logMutex);
    observations.emplace_back(progress.state, progress.requestedRevision);
  };
  coordinator.setProgressCallback(observe);
  coordinator.setCompletionCallback(observe);

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 600U,
                     48000U, seam::rendering::RenderQuality::Preview, true);
  CHECK(busy.waitEntered());
  std::jthread submitter{[&] {
    coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 601U,
                       48000U, seam::rendering::RenderQuality::Preview, true);
  }};
  ReleaseOnExit releaseBusy{busy};
  ReleaseOnExit releaseSubmitter{submitterHeld};
  CHECK(submitterHeld.waitEntered());
  busy.release();
  // The worker finishes 600 as a stale render, takes 601 and renders it, with the submitter held.
  CHECK(waitForCondition([&] {
    const auto progress = coordinator.progress();
    return progress.requestedRevision == 601U &&
           progress.state == seam::authoring::RenderState::Ready;
  }));
  submitterHeld.release();
  submitter.join();

  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.requestedRevision == 601U);
  CHECK(progress.publishedRevision == 601U);
  CHECK(stateWhenTaken.load() == static_cast<int>(seam::authoring::RenderState::Queued));
  CHECK(revisionWhenTaken.load() == 601U);
  std::lock_guard lock(logMutex);
  bool readySeen = false;
  bool regressed = false;
  for (const auto& [state, revision] : observations) {
    if (revision != 601U) continue;
    if (state == seam::authoring::RenderState::Ready) readySeen = true;
    else if (readySeen && (state == seam::authoring::RenderState::Queued ||
                           state == seam::authoring::RenderState::Rendering)) regressed = true;
  }
  CHECK(readySeen);
  CHECK(!regressed);
}

TEST_CASE("a request submitted while a cancel is being reported is queued after it and stays on screen") {
  // cancel() drops the queued request, revokes the current one and reports the cancellation. A
  // submission that landed between the revocation and the report was reported as queued, and then
  // the report for the request before it was written over that: a render that was about to run sat
  // on screen as "cancelled". The steps are one step under the admission lock, so a submission
  // made while cancel() is in progress waits for it.
  auto fixture = makeRenderFixture();
  HookBarrier cancelling;
  HookBarrier taken;
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.debounceInterval = std::chrono::seconds{30};  // request 800 stays queued, never taken
  hooks.duringCancel = [&cancelling] { cancelling.pass(); };
  hooks.afterAdmission = [&taken](std::uint64_t revision, std::stop_token) {
    if (revision == 801U) taken.pass();
  };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-cancel-step"), std::move(hooks)};
  std::jthread canceller;
  std::jthread submitter;
  ReleaseOnExit releaseCancelling{cancelling};
  ReleaseOnExit releaseTaken{taken};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 800U,
                     48000U, seam::rendering::RenderQuality::Preview, false);
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Queued);

  canceller = std::jthread{[&] { coordinator.cancel(); }};
  CHECK(cancelling.waitEntered());
  submitter = std::jthread{[&] {
    coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 801U,
                       48000U, seam::rendering::RenderQuality::Preview, true);
  }};
  // Long enough for a submission that can get in to do so; one that has to wait is still waiting.
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  cancelling.release();
  canceller.join();

  // The worker has taken request 801 and not yet reported it as rendering: what is on screen is
  // what the last report said, and that must be the queued request, not the cancelled one.
  CHECK(taken.waitEntered());
  const auto progress = coordinator.progress();
  CHECK(progress.state == seam::authoring::RenderState::Queued);
  CHECK(progress.requestedRevision == 801U);
  taken.release();
  submitter.join();
  CHECK(waitForTerminal(coordinator, 801U).state == seam::authoring::RenderState::Ready);
  CHECK(coordinator.stats().cancelled == 1U);
}

TEST_CASE("a request submitted while a reset is settling is admitted after it and renders") {
  // resetToIdle() drops the queued request, revokes the current one and publishes idle. A
  // submission that landed between the first steps and the last was admitted, and the last step
  // then revoked it: the worker rendered it for nothing and discarded it as stale, with the screen
  // on "idle". It is one step under the admission lock, so a submission made while it is in
  // progress waits for it and then renders.
  auto fixture = makeRenderFixture();
  HookBarrier settling;
  seam::authoring::RenderCoordinatorHooks hooks;
  hooks.debounceInterval = std::chrono::seconds{30};  // request 802 stays queued, never taken
  hooks.duringReset = [&settling] { settling.pass(); };
  seam::authoring::AuthoringRenderCoordinator coordinator{
      uniqueTempRoot("render-coordinator-reset-step"), std::move(hooks)};
  std::jthread resetter;
  std::jthread submitter;
  ReleaseOnExit releaseSettling{settling};

  coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 802U,
                     48000U, seam::rendering::RenderQuality::Preview, false);
  CHECK(coordinator.progress().state == seam::authoring::RenderState::Queued);

  resetter = std::jthread{[&] { coordinator.resetToIdle(); }};
  CHECK(settling.waitEntered());
  submitter = std::jthread{[&] {
    coordinator.submit(fixture.project, {fixture.source}, fixture.trackId, fixture.regionId, 803U,
                       48000U, seam::rendering::RenderQuality::Preview, true);
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  settling.release();
  resetter.join();
  submitter.join();

  const auto progress = waitForTerminal(coordinator, 803U);
  CHECK(progress.state == seam::authoring::RenderState::Ready);
  CHECK(progress.requestedRevision == 803U);
  CHECK(static_cast<bool>(coordinator.acquireCurrent()));
  const auto stats = coordinator.stats();
  CHECK(stats.completed == 1U);
  CHECK(stats.stale == 0U);
  CHECK(stats.cancelled == 1U);  // request 802, dropped by the reset
}
