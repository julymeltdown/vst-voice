#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/voicebank/catalog.hpp"

#include <chrono>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

using seam::clap_editor::HostMeterSegment;
using seam::clap_editor::HostTimelineCapture;
using seam::clap_editor::HostTimelineCaptureRequest;
using seam::clap_editor::HostTimelineState;
using seam::clap_editor::PreparedHostTimeline;

HostTimelineState report(double beats, double tempo) {
  return HostTimelineState{.playing = true,
                           .hasSeconds = true,
                           .seconds = beats * 60.0 / tempo,
                           .hasBeats = true,
                           .beats = beats,
                           .hasTempo = true,
                           .tempo = tempo};
}

HostTimelineCaptureRequest request(double endBeats) {
  return HostTimelineCaptureRequest{
      .projectId = seam::domain::ProjectId{7U},
      .projectRevision = 3U,
      .sampleRate = 48000U,
      .ppq = seam::time::Ppq{960U},
      .projectOffsetSeconds = 0.0,
      .requestedStartBeats = 0.0,
      .requestedEndBeats = endBeats,
      .maximumGapBeats = 1.0,
      .hostId = "fixture-host",
  };
}

HostTimelineCapture constantCapture(double bpm, double lastBeats) {
  HostTimelineCapture capture;
  for (double beats = 0.0; beats <= lastBeats + 1e-9; beats += 1.0) {
    capture.observe(report(beats, bpm), 48000U);
  }
  return capture;
}

std::shared_ptr<const seam::clap_editor::RenderedPreview> waitForPreview(
    seam::clap_editor::EditorRuntime& runtime) {
  for (int attempt = 0; attempt < 2000; ++attempt) {
    auto preview = runtime.renderedPreview();
    if (preview != nullptr && preview->status != seam::clap_editor::PreviewStatus::Empty) {
      return preview;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return runtime.renderedPreview();
}

}  // namespace

TEST_CASE("a prepared host timeline freezes the range the host actually covered") {
  const auto capture = constantCapture(120.0, 4.0);
  const auto frozen = capture.freeze(request(4.0));
  CHECK(frozen);
  if (!frozen) return;
  const PreparedHostTimeline& prepared = frozen.value();
  CHECK(prepared.requestedStartBeats() == 0.0);
  CHECK(prepared.requestedEndBeats() == 4.0);
  CHECK(prepared.observedStartBeats() == 0.0);
  CHECK(prepared.observedEndBeats() == 4.0);
  CHECK(prepared.sampleRate() == 48000U);
  CHECK(prepared.tempoSegments().size() == 5U);
  CHECK(prepared.tempoMap().events().size() == 5U);
  // Four beats at 120 BPM is two seconds, derived through the frozen map itself.
  const auto range = prepared.observedSampleRange();
  CHECK(std::abs(range.startSeconds) < 1e-9);
  CHECK(std::abs(range.endSeconds - 2.0) < 1e-6);
  CHECK(prepared.covers(1.0, 3.0, 1.0));
  CHECK(prepared.captureRevision() > 0U);
  CHECK(prepared.reportCount() == 5U);

  // Re-reporting the same map while the transport advances is the same authority.
  auto repeated = capture;
  repeated.observe(report(5.0, 120.0), 48000U);
  CHECK(repeated.contentHash() != capture.contentHash());
  auto same = constantCapture(120.0, 4.0);
  CHECK(same.contentHash() == capture.contentHash());
  const auto sameFrozen = same.freeze(request(4.0));
  CHECK(sameFrozen);
  if (sameFrozen) CHECK(sameFrozen.value().contentHash() == prepared.contentHash());

  // A different map is a different authority.
  auto slower = constantCapture(60.0, 4.0);
  const auto slowerFrozen = slower.freeze(request(4.0));
  CHECK(slowerFrozen);
  if (slowerFrozen) CHECK(slowerFrozen.value().contentHash() != prepared.contentHash());
}

TEST_CASE("a prepared host timeline refuses a range the host never reported") {
  auto capture = constantCapture(120.0, 2.0);
  const auto missing = capture.freeze(request(4.0));
  CHECK(!missing);
  if (!missing) {
    CHECK(missing.error().code == seam::core::ErrorCode::Unsupported);
    CHECK(missing.error().message.find("2.000000..4.000000") != std::string::npos);
    CHECK(missing.error().message.find("Recapture") != std::string::npos);
  }

  // A gap the caller is not willing to accept is not coverage either.
  HostTimelineCapture sparse;
  sparse.observe(report(0.0, 120.0), 48000U);
  sparse.observe(report(4.0, 120.0), 48000U);
  auto tight = request(4.0);
  tight.maximumGapBeats = 2.0;
  const auto gapped = sparse.freeze(tight);
  CHECK(!gapped);
  if (!gapped) CHECK(gapped.error().message.find("0.000000..4.000000") != std::string::npos);
  // The same two reports are a valid authority when the caller does declare that it will
  // accept an unobserved four-beat gap.
  auto accepting = request(4.0);
  accepting.maximumGapBeats = 4.0;
  CHECK(sparse.freeze(accepting));

  const auto empty = HostTimelineCapture{}.freeze(request(1.0));
  CHECK(!empty);
  if (!empty) CHECK(empty.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("an active host loop must state its musical boundaries") {
  auto capture = constantCapture(120.0, 4.0);
  auto looping = seam::clap_editor::HostTimelineState{.playing = true,
                                                      .hasBeats = true,
                                                      .beats = 1.0,
                                                      .hasTempo = true,
                                                      .tempo = 120.0,
                                                      .loopActive = true};
  capture.observe(looping, 48000U);
  const auto refused = capture.freeze(request(4.0));
  CHECK(!refused);
  if (!refused) {
    CHECK(refused.error().code == seam::core::ErrorCode::Unsupported);
    CHECK(refused.error().message.find("loop") != std::string::npos);
  }

  looping.loopHasBeats = true;
  looping.loopStartBeats = 1.0;
  looping.loopEndBeats = 3.0;
  capture.observe(looping, 48000U);
  const auto frozen = capture.freeze(request(4.0));
  CHECK(frozen);
  if (frozen) {
    CHECK(frozen.value().loopActive());
    CHECK(frozen.value().loopStartBeats() == 1.0);
    CHECK(frozen.value().loopEndBeats() == 3.0);
    // Loop semantics are part of the authority a bounce rests on.
    CHECK(frozen.value().contentHash() != constantCapture(120.0, 4.0).freeze(request(4.0)).value().contentHash());
  }
}

TEST_CASE("a host meter change changes the prepared identity") {
  auto plain = constantCapture(120.0, 4.0);
  const auto plainFrozen = plain.freeze(request(4.0));
  CHECK(plainFrozen);
  if (!plainFrozen) return;
  CHECK(!plainFrozen.value().hasHostMeter());
  CHECK(plainFrozen.value().meterSegments().empty());

  auto changing = constantCapture(120.0, 4.0);
  auto meter = report(2.0, 120.0);
  meter.hasTimeSignature = true;
  meter.numerator = 3U;
  meter.denominator = 4U;
  changing.observe(meter, 48000U);
  const auto changingFrozen = changing.freeze(request(4.0));
  CHECK(changingFrozen);
  if (!changingFrozen) return;
  CHECK(changingFrozen.value().hasHostMeter());
  CHECK(changingFrozen.value().meterSegments().size() == 1U);
  const HostMeterSegment expectedMeter{2.0, 3U, 4U};
  CHECK(changingFrozen.value().meterSegments().front() == expectedMeter);
  // A meter change is a different musical authority even at the same tempo.
  CHECK(changingFrozen.value().contentHash() != plainFrozen.value().contentHash());

  // 300+ distinct meter positions exceed the segment capacity, which is refused.
  auto crowded = constantCapture(120.0, 4.0);
  for (std::size_t index = 0U; index < 300U; ++index) {
    auto extra = report(static_cast<double>(index) * 0.01, 120.0);
    extra.hasTimeSignature = true;
    extra.numerator = static_cast<std::uint16_t>(2U + (index % 5U));
    extra.denominator = 4U;
    crowded.observe(extra, 48000U);
  }
  const auto capped = crowded.freeze(request(4.0));
  CHECK(!capped);
  if (!capped) CHECK(capped.error().code == seam::core::ErrorCode::InvalidArgument);
}

TEST_CASE("a seek keeps the host's map and a tempo change rewrites it") {
  HostTimelineCapture capture;
  for (double beats = 0.0; beats <= 4.0; beats += 1.0) {
    capture.observe(report(beats, 120.0), 48000U);
  }
  capture.observe(report(1.0, 120.0), 48000U);
  capture.observe(report(2.0, 120.0), 48000U);
  CHECK(capture.seekCount() == 1U);
  const auto afterSeek = capture.freeze(request(4.0));
  CHECK(afterSeek);
  if (!afterSeek) return;
  // The tempo map is a function of musical position, so a seek does not remove coverage...
  CHECK(afterSeek.value().covers(0.0, 4.0, 1.0));
  // ...but the sample-domain relation is no longer monotonic, and that is part of identity.
  const auto noSeek = constantCapture(120.0, 4.0).freeze(request(4.0));
  CHECK(noSeek);
  if (noSeek) CHECK(afterSeek.value().contentHash() != noSeek.value().contentHash());

  // A tempo change after preparation produces a different map and identity.
  auto preparation = capture;
  preparation.observe(report(3.0, 90.0), 48000U);
  auto later = preparation.freeze(request(4.0));
  CHECK(later);
  if (!later) return;
  CHECK(later.value().contentHash() != afterSeek.value().contentHash());
  CHECK(later.value().tempoMap().bpmAt(seam::time::Tick{3 * 960}) == 90.0);
  // The map is not flattened to the tempo it started with.
  CHECK(later.value().tempoMap().bpmAt(seam::time::Tick{0}) == 120.0);
  const auto range = later.value().observedSampleRange();
  CHECK(range.endSeconds > afterSeek.value().observedSampleRange().endSeconds);
}

TEST_CASE("a stopped host and a rate change both refuse preparation") {
  auto stopped = constantCapture(120.0, 2.0);
  auto halted = report(2.0, 120.0);
  halted.playing = false;
  stopped.observe(halted, 48000U);
  const auto stoppedFrozen = stopped.freeze(request(4.0));
  CHECK(!stoppedFrozen);
  if (!stoppedFrozen) {
    CHECK(stoppedFrozen.error().code == seam::core::ErrorCode::Unsupported);
  }

  auto mixed = constantCapture(120.0, 2.0);
  mixed.observe(report(3.0, 120.0), 44100U);
  CHECK(mixed.sampleRateChanged());
  const auto mixedFrozen = mixed.freeze(request(4.0));
  CHECK(!mixedFrozen);
  if (!mixedFrozen) {
    CHECK(mixedFrozen.error().code == seam::core::ErrorCode::Conflict);
    CHECK(mixedFrozen.error().message.find("sample rate changed") != std::string::npos);
  }

  const auto inverted = constantCapture(120.0, 4.0).freeze(request(0.0));
  CHECK(!inverted);
  if (!inverted) CHECK(inverted.error().code == seam::core::ErrorCode::InvalidArgument);
}

TEST_CASE("follow host preparation freezes an authority fixed audio does not inherit") {
  const auto fixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  seam::voicebank::VoicebankCatalog catalog;
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = fixture, .kind = seam::voicebank::VoicebankRootKind::Development}};
  const auto scanned = catalog.scan(roots);
  CHECK(scanned);
  if (!scanned || scanned.value().empty()) return;
  const auto& candidate = scanned.value().front();

  seam::application::ProjectFactory factory{9400U};
  auto project = factory.createProject("Prepared timeline");
  // The document's own map is deliberately not the host's tempo, so inheriting one for the
  // other would be audible rather than a bookkeeping difference.
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 60.0));
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, track, "Phrase", seam::time::Tick{0},
                                          seam::time::Tick{1920});
  auto* region = project.findRegion(regionId);
  seam::domain::VocalTrack* vocal = project.findVocalTrack(track);
  CHECK(region != nullptr);
  CHECK(vocal != nullptr);
  if (region == nullptr || vocal == nullptr) return;
  vocal->voicebank = seam::domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash};
  const std::array<const char32_t*, 2> lyrics{U"\u3053", U"\u3048"};
  for (std::size_t index = 0U; index < lyrics.size(); ++index) {
    auto note = factory.makeNote(seam::time::Tick{960 * static_cast<std::int64_t>(index)},
                                 seam::time::Tick{960},
                                 static_cast<std::uint8_t>(60U + index),
                                 std::u32string{lyrics[index]},
                                 seam::domain::Language::Japanese);
    region->lyrics.push_back(std::move(note.first));
    region->notes.push_back(std::move(note.second));
  }
  region->sortNotes();

  seam::clap_editor::EditorRuntime runtime{std::nullopt, {}, roots};
  CHECK(runtime.replaceProject(std::move(project)));
  CHECK(waitForPreview(runtime) != nullptr);
  CHECK(!runtime.preparedHostTimeline().has_value());

  runtime.setOfflineTimingAuthority(seam::clap_editor::OfflineTimingAuthority::FollowHost);
  for (const double beats : {0.0, 1.0, 2.0}) {
    runtime.setHostTimelineState(report(beats, 120.0));
  }
  CHECK(runtime.prepareOfflineRender(std::chrono::seconds{30}));
  const auto prepared = runtime.preparedHostTimeline();
  CHECK(prepared.has_value());
  if (prepared.has_value()) {
    CHECK(prepared->projectRevision() > 0U);
    CHECK(prepared->requestedEndBeats() == 2.0);
    CHECK(prepared->sampleRate() == 48000U);
    CHECK(prepared->tempoSegments().size() == 3U);
  }
  const auto follow = runtime.acquireOfflineRenderedPreview();
  CHECK(follow);
  if (!follow) return;
  // Two beats at the host's 120 BPM is one second; the document says 60 BPM.
  const auto followFrames = follow->interleaved.size() / follow->channelCount;
  CHECK(std::llabs(static_cast<long long>(followFrames) - 48000LL) < 4800LL);

  // Advancing the transport is not a change of authority, so the bounce stays usable.
  runtime.setHostTimelineState(report(3.0, 120.0));
  CHECK(runtime.preparedHostTimeline().has_value());
  CHECK(runtime.acquireOfflineRenderedPreview());
  // Neither is a tempo the host states beyond the range the bounce covers.
  runtime.setHostTimelineState(report(3.0, 90.0));
  CHECK(runtime.preparedHostTimeline().has_value());
  // A tempo change inside the rendered range is, and it drops the frozen authority with
  // the audio that was prepared from it.
  runtime.setHostTimelineState(report(0.5, 90.0));
  CHECK(!runtime.preparedHostTimeline().has_value());
  CHECK(!runtime.acquireOfflineRenderedPreview());
  CHECK(runtime.offlineRenderView().state == seam::clap_editor::OfflineRenderState::Stale);

  // Fixed Audio remains an explicit alternative that never inherits host timing.
  runtime.setOfflineTimingAuthority(seam::clap_editor::OfflineTimingAuthority::FixedAudio);
  CHECK(!runtime.preparedHostTimeline().has_value());
  CHECK(runtime.prepareOfflineRender(std::chrono::seconds{30}));
  const auto fixed = runtime.acquireOfflineRenderedPreview();
  CHECK(fixed);
  if (!fixed) return;
  const auto fixedFrames = fixed->interleaved.size() / fixed->channelCount;
  CHECK(fixedFrames > followFrames * 2U - followFrames / 4U);
}
