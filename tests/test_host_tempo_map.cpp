#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

using seam::clap_editor::HostTempoMap;
using seam::clap_editor::HostTempoObservation;

HostTempoMap observed(std::initializer_list<HostTempoObservation> entries) {
  HostTempoMap map;
  for (const auto& entry : entries) {
    CHECK(map.observe(entry.beats, entry.bpm));
  }
  return map;
}

std::shared_ptr<const seam::clap_editor::RenderedPreview> waitForPreview(
    seam::clap_editor::EditorRuntime& runtime) {
  for (int attempt = 0; attempt < 2000; ++attempt) {
    auto preview = runtime.renderedPreview();
    if (preview != nullptr && preview->status != seam::clap_editor::PreviewStatus::Empty) return preview;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return runtime.renderedPreview();
}

seam::domain::Project voicebankProject(seam::application::ProjectFactory& factory,
                                       const seam::domain::VoicebankReference& reference) {
  auto project = factory.createProject("Host tempo");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, track, "Phrase", seam::time::Tick{0},
                                          seam::time::Tick{1920});
  auto* region = project.findRegion(regionId);
  seam::domain::VocalTrack* vocal = project.findVocalTrack(track);
  if (region == nullptr || vocal == nullptr)
    throw seam::test::Failure{"host tempo fixture project is incomplete"};
  vocal->voicebank = reference;
  // The production demo bank covers these syllables; an uncovered phone sequence would
  // fail the render for a reason unrelated to host timing.
  const std::array<const char32_t*, 2> lyrics{U"こ", U"え"};
  for (std::size_t index = 0U; index < lyrics.size(); ++index) {
    auto note = factory.makeNote(seam::time::Tick{960 * static_cast<std::int64_t>(index)},
                                 seam::time::Tick{960}, static_cast<std::uint8_t>(60U + index),
                                 std::u32string{lyrics[index]}, seam::domain::Language::Japanese);
    region->lyrics.push_back(std::move(note.first));
    region->notes.push_back(std::move(note.second));
  }
  region->sortNotes();
  return project;
}

}  // namespace

TEST_CASE("host tempo map records a versioned history of what the host reported") {
  HostTempoMap map;
  CHECK(map.empty());
  CHECK(map.revision() == 0U);
  CHECK(map.observe(4.0, 120.0));
  CHECK(map.observe(0.0, 100.0));
  CHECK(map.observe(2.0, 110.0));
  CHECK(map.size() == 3U);
  // Position order is the map's own order, not the host's reporting order.
  CHECK(map.observations()[0].beats == 0.0);
  CHECK(map.observations()[1].beats == 2.0);
  CHECK(map.observations()[2].beats == 4.0);
  const auto revision = map.revision();
  CHECK(map.observe(2.0, 110.0));
  CHECK(map.revision() == revision);
  // A host that changed its mind must invalidate an older timing claim.
  CHECK(map.observe(2.0, 90.0));
  CHECK(map.revision() == revision + 1U);
  CHECK(map.observations()[1].bpm == 90.0);
  CHECK(map.size() == 3U);
  for (const auto& invalid : {std::pair<double, double>{-1.0, 120.0}, {1.0, 0.0}, {1.0, 1001.0},
                              {std::nan(""), 120.0}, {1.0, std::nan("")}}) {
    CHECK(!map.observe(invalid.first, invalid.second));
  }
  CHECK(map.size() == 3U);
}

TEST_CASE("host tempo map coverage needs both ends and a bounded gap") {
  const HostTempoMap map = observed({{0.0, 120.0}, {2.0, 120.0}, {4.0, 120.0}});
  CHECK(map.covers(0.0, 4.0, 2.0));
  CHECK(map.covers(1.0, 3.0, 2.0));
  // A later start is still covered by the observation before it.
  CHECK(map.covers(1.0, 4.0, 2.0));
  CHECK(!map.covers(0.0, 5.0, 2.0));
  CHECK(!map.covers(0.0, 4.0, 1.0));
  CHECK(!map.covers(4.0, 4.0, 2.0));
  CHECK(!HostTempoMap{}.covers(0.0, 1.0, 1.0));
  CHECK(HostTempoMap{}.uncoveredSpan(0.0, 4.0, 1.0) == std::string{"0.000000..4.000000"});
  CHECK(map.uncoveredSpan(0.0, 4.0, 2.0).empty());
  CHECK(map.uncoveredSpan(0.0, 6.0, 2.0) == std::string{"4.000000..6.000000"});
  CHECK(map.uncoveredSpan(0.0, 4.0, 1.0) == std::string{"0.000000..2.000000"});
}

TEST_CASE("host tempo map identity follows the history and ignores revision churn") {
  const HostTempoMap first = observed({{0.0, 120.0}, {2.0, 60.0}});
  const HostTempoMap same = observed({{2.0, 60.0}, {0.0, 120.0}});
  HostTempoMap changed = observed({{0.0, 120.0}, {2.0, 60.0}});
  CHECK(first.contentHash() == same.contentHash());
  CHECK(first.contentHash() == changed.contentHash());
  // A changed report changes the identity, and re-observing the old history restores it:
  // the hash follows what the host said, not how many times it said it.
  CHECK(changed.observe(2.0, 61.0));
  CHECK(changed.contentHash() != first.contentHash());
  CHECK(changed.observe(2.0, 60.0));
  CHECK(changed.contentHash() == first.contentHash());
  changed.clear();
  CHECK(changed.empty());
  CHECK(changed.contentHash() != first.contentHash());
}

TEST_CASE("follow host rendering refuses a map that cannot cover the score") {
  const auto fixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  seam::voicebank::VoicebankCatalog catalog;
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = fixture, .kind = seam::voicebank::VoicebankRootKind::Development}};
  const auto scanned = catalog.scan(roots);
  CHECK(scanned);
  if (!scanned || scanned.value().empty()) return;
  const auto& candidate = scanned.value().front();
  seam::application::ProjectFactory factory{8100U};
  auto project = voicebankProject(factory,
      seam::domain::VoicebankReference{.id = candidate.manifest.id,
                                       .version = candidate.manifest.version,
                                       .contentHash = candidate.contentHash});
  seam::clap_editor::EditorRuntime runtime{std::nullopt, {}, roots};
  CHECK(runtime.replaceProject(std::move(project)));
  CHECK(waitForPreview(runtime) != nullptr);
  runtime.setOfflineTimingAuthority(seam::clap_editor::OfflineTimingAuthority::FollowHost);

  // The score spans two beats, and this host has reported nothing yet.
  const auto refused = runtime.prepareOfflineRender(std::chrono::seconds{10});
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("uncovered span") != std::string::npos);
  CHECK(runtime.offlineRenderView().state == seam::clap_editor::OfflineRenderState::Failed);

  // One instantaneous value is not a map: a single report cannot certify later events.
  runtime.setHostTimelineState(seam::clap_editor::HostTimelineState{
      .playing = true, .hasBeats = true, .beats = 0.0, .hasTempo = true, .tempo = 120.0});
  const auto single = runtime.prepareOfflineRender(std::chrono::seconds{10});
  CHECK(!single);
  CHECK(single.error().code == seam::core::ErrorCode::Unsupported);
  CHECK(single.error().message.find("1 tempo observation") != std::string::npos);
  CHECK(runtime.hostTempoMap().size() == 1U);
}

TEST_CASE("follow host rendering times the score with the acquired host tempo") {
  const auto fixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  seam::voicebank::VoicebankCatalog catalog;
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = fixture, .kind = seam::voicebank::VoicebankRootKind::Development}};
  const auto scanned = catalog.scan(roots);
  CHECK(scanned);
  if (!scanned || scanned.value().empty()) return;
  const auto& candidate = scanned.value().front();
  seam::application::ProjectFactory factory{8200U};
  auto project = voicebankProject(factory,
      seam::domain::VoicebankReference{.id = candidate.manifest.id,
                                       .version = candidate.manifest.version,
                                       .contentHash = candidate.contentHash});
  seam::clap_editor::EditorRuntime runtime{std::nullopt, {}, roots};
  CHECK(runtime.replaceProject(std::move(project)));
  CHECK(waitForPreview(runtime) != nullptr);
  runtime.setOfflineTimingAuthority(seam::clap_editor::OfflineTimingAuthority::FollowHost);

  const auto observe = [&runtime](double bpm) {
    for (const double beats : {0.0, 1.0, 2.0}) {
      runtime.setHostTimelineState(seam::clap_editor::HostTimelineState{
          .playing = true, .hasBeats = true, .beats = beats, .hasTempo = true, .tempo = bpm});
    }
  };
  observe(120.0);
  CHECK(runtime.hostTempoMap().size() == 3U);
  const auto prepared = runtime.prepareOfflineRender(std::chrono::seconds{30});
  if (!prepared) {
    throw seam::test::Failure{"follow-host prepare failed: " + prepared.error().message + " | " +
                              runtime.offlineRenderView().diagnostic};
  }
  const auto fast = runtime.acquireOfflineRenderedPreview();
  CHECK(fast);
  if (!fast) return;
  const auto fastFrames = fast->interleaved.size() / fast->channelCount;
  // Two beats at 120 BPM is one second at 48 kHz.
  CHECK(std::llabs(static_cast<long long>(fastFrames) - 48000LL) < 4800LL);

  // The same score under a host that half-timed must render about twice as long, and the
  // two renders cannot share a timing identity.
  const auto fastIdentity = runtime.offlineRenderView().identity.timingMapHash;
  observe(60.0);
  CHECK(runtime.hostTempoMap().revision() > 0U);
  CHECK(runtime.prepareOfflineRender(std::chrono::seconds{30}));
  const auto slow = runtime.acquireOfflineRenderedPreview();
  CHECK(slow);
  if (!slow) return;
  const auto slowFrames = slow->interleaved.size() / slow->channelCount;
  CHECK(slowFrames > fastFrames * 2U - fastFrames / 4U);
  CHECK(runtime.offlineRenderView().identity.timingMapHash != fastIdentity);
}
