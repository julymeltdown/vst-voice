#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/command.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_scheduler.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/synthesis/seam_composer.hpp"
#include "seam/synthesis/spectral_classic.hpp"
#include "seam/synthesis/stretch_renderer.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <numbers>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::shared_ptr<seam::rendering::PlaybackTimeline> constantTimeline(
    float value, std::size_t frames = 2048U) {
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = 48000U,
          .startFrame = 0,
          .samples = std::vector<float>(frames, value),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(48000U);
  const auto added = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "constant",
      .pcm = std::move(pcm),
  });
  if (!added) throw std::runtime_error(added.error().message);
  return timeline;
}

template <typename T>
void writeLittle(std::ostream& stream, T value) {
  using Unsigned = std::make_unsigned_t<T>;
  const auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0U; index < sizeof(T); ++index) {
    stream.put(static_cast<char>((bits >> (index * 8U)) &
                                 static_cast<Unsigned>(0xffU)));
  }
}

struct SnapshotFixture final {
  seam::application::ProjectFactory factory{4100};
  seam::domain::Project project{factory.createProject("Identity")};
  seam::domain::TrackId trackId{factory.addVocalTrack(project, "Voice")};
  seam::domain::RegionId regionId{factory.addRegion(
      project, trackId, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840})};

  SnapshotFixture() {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{0}, seam::time::Tick{960}, 69, U"あ",
        seam::domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    region->sortNotes();
  }
};

class SetNameCommand final : public seam::application::ICommand {
public:
  explicit SetNameCommand(std::string value) : value_(std::move(value)) {}
  std::string_view name() const noexcept override { return "Set name"; }
  seam::core::Result<void> apply(seam::domain::Project& project) override {
    previous_ = project.name();
    project.setName(value_);
    return seam::core::success();
  }
  seam::core::Result<void> revert(seam::domain::Project& project) override {
    project.setName(previous_);
    return seam::core::success();
  }
private:
  std::string value_;
  std::string previous_;
};

class MutatingApplyFailure final : public seam::application::ICommand {
public:
  std::string_view name() const noexcept override { return "Fail apply"; }
  seam::core::Result<void> apply(seam::domain::Project& project) override {
    project.setName("partially-mutated");
    return seam::core::failure(seam::core::ErrorCode::Internal,
                               "intentional apply failure");
  }
  seam::core::Result<void> revert(seam::domain::Project&) override {
    return seam::core::success();
  }
};

class MutatingRevertFailure final : public seam::application::ICommand {
public:
  std::string_view name() const noexcept override { return "Fail revert"; }
  seam::core::Result<void> apply(seam::domain::Project& project) override {
    project.setName("applied");
    return seam::core::success();
  }
  seam::core::Result<void> revert(seam::domain::Project& project) override {
    project.setName("partially-reverted");
    return seam::core::failure(seam::core::ErrorCode::Internal,
                               "intentional revert failure");
  }
};

}  // namespace

TEST_CASE("SHA-256 implementation matches published vectors") {
  CHECK(seam::core::sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(seam::core::sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("generated build identity owns the Phase 4.1 render ABI") {
  CHECK(seam::build::kApplicationVersion == "0.4.1");
  CHECK(!seam::build::kRenderAbiId.empty());
  CHECK(seam::build::kRenderAbiId.find("4.1") != std::string_view::npos);
  CHECK(seam::build::kPcmCacheFormatRevision >= 3U);
}

TEST_CASE("JSON preserves int64 surrogate pairs and parser budgets") {
  const auto exact = seam::formats::parseJson("9223372036854775807");
  CHECK(exact);
  CHECK(exact.value().isInteger());
  CHECK(exact.value().asInt64() == INT64_MAX);
  CHECK(seam::formats::stringifyJson(exact.value(), false) ==
        "9223372036854775807");

  const auto emoji = seam::formats::parseJson("\"\\uD83D\\uDE00\"");
  CHECK(emoji);
  CHECK(emoji.value().asString() == "\xF0\x9F\x98\x80");
  CHECK(!seam::formats::parseJson("\"\\uD83D\""));

  CHECK(!seam::formats::parseJson(
      "[[[0]]]", seam::formats::JsonParseLimits{
          .maximumInputBytes = 32U,
          .maximumDepth = 1U,
          .maximumNodes = 32U,
          .maximumStringBytes = 16U,
          .maximumCollectionEntries = 8U,
      }));
  CHECK(!seam::formats::parseJson(
      "[[]]", seam::formats::JsonParseLimits{
          .maximumInputBytes = 32U,
          .maximumDepth = 1U,
          .maximumNodes = 32U,
          .maximumStringBytes = 16U,
          .maximumCollectionEntries = 8U,
      }));
  bool rejectedUpperBound = false;
  try {
    static_cast<void>(seam::formats::JsonValue{9223372036854775808.0}.asInt64());
  } catch (const std::bad_variant_access&) {
    rejectedUpperBound = true;
  }
  CHECK(rejectedUpperBound);

  CHECK(!seam::formats::parseJson(
      "[0,1,2]", seam::formats::JsonParseLimits{
          .maximumInputBytes = 32U,
          .maximumDepth = 8U,
          .maximumNodes = 32U,
          .maximumStringBytes = 16U,
          .maximumCollectionEntries = 2U,
      }));
}

TEST_CASE("durable atomic write preserves old data across injected faults") {
  const auto directory = seam::test::support::temporaryDirectory("atomic-write");
  const auto target = directory / "project.json";
  const auto backup = directory / "project.json.bak";
  CHECK(seam::core::durableAtomicWriteText(target, "old"));

  const auto beforeReplace = seam::core::durableAtomicWriteText(
      target, "new-before", seam::core::AtomicWriteOptions{
          .backupPath = backup,
          .maximumBackupBytes = 1024U,
          .faultInjector = [](seam::core::AtomicWriteStage stage) {
            if (stage == seam::core::AtomicWriteStage::BeforeReplace) {
              return seam::core::failure(
                  seam::core::ErrorCode::IoError, "injected before replace");
            }
            return seam::core::success();
          },
      });
  CHECK(!beforeReplace);
  const auto oldAfterFault = seam::core::readTextFileLimited(target, 1024U);
  const auto backupAfterFault = seam::core::readTextFileLimited(backup, 1024U);
  CHECK(oldAfterFault && oldAfterFault.value() == "old");
  CHECK(backupAfterFault && backupAfterFault.value() == "old");

  const auto afterReplace = seam::core::durableAtomicWriteText(
      target, "new-after", seam::core::AtomicWriteOptions{
          .backupPath = backup,
          .maximumBackupBytes = 1024U,
          .faultInjector = [](seam::core::AtomicWriteStage stage) {
            if (stage == seam::core::AtomicWriteStage::Replaced) {
              return seam::core::failure(
                  seam::core::ErrorCode::IoError, "injected after replace");
            }
            return seam::core::success();
          },
      });
  CHECK(!afterReplace);
  const auto newAfterFault = seam::core::readTextFileLimited(target, 1024U);
  const auto oldBackup = seam::core::readTextFileLimited(backup, 1024U);
  CHECK(newAfterFault && newAfterFault.value() == "new-after");
  CHECK(oldBackup && oldBackup.value() == "old");
}

TEST_CASE("render identity binds selected WAV bytes and effective render options") {
  SnapshotFixture fixture;
  const auto root = seam::test::support::temporaryDirectory("render-identity-v3");
  std::filesystem::create_directories(root / "audio");
  auto selected = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000U);
  auto unrelated = seam::test::support::makeUnit(
      "i", {"i"}, "audio/i.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000U);
  auto bank = seam::test::support::makeManifest({selected, unrelated});
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      root / "audio/a.wav", 48000U,
      seam::test::support::sineWave(48000U, 440.0, 0.5)));
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      root / "audio/i.wav", 48000U,
      seam::test::support::sineWave(48000U, 330.0, 0.5)));

  const auto segments = seam::rendering::PhraseSegmenter{}.segment(
      *fixture.project.findRegion(fixture.regionId));
  CHECK(segments && segments.value().size() == 1U);
  seam::rendering::RenderSnapshotFactory factory;
  const auto baseline = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 1U,
      seam::rendering::RenderQuality::Preview, root);
  CHECK(baseline);
  CHECK(baseline.value().contentHash.size() == 64U);
  CHECK(baseline.value().renderAbiId == seam::build::kRenderAbiId);
  CHECK(baseline.value().frozenAudio.size() == 1U);
  seam::rendering::PhraseRenderPipeline pipeline;
  const auto frozenBeforeMutation = pipeline.render(baseline.value());
  CHECK(frozenBeforeMutation);

  bank.findUnit("i")->priority = 999;
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      root / "audio/i.wav", 48000U,
      seam::test::support::sineWave(48000U, 660.0, 0.5)));
  const auto unrelatedEdit = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 2U,
      seam::rendering::RenderQuality::Preview, root);
  CHECK(unrelatedEdit);
  CHECK(unrelatedEdit.value().contentHash == baseline.value().contentHash);

  seam::synthesis::PhraseRenderOptions options;
  options.renderer.spectral.fftSize = 2048U;
  const auto optionEdit = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 3U,
      seam::rendering::RenderQuality::Preview, root, 48000U, "original", options);
  CHECK(optionEdit);
  CHECK(optionEdit.value().contentHash != baseline.value().contentHash);

  CHECK(seam::voicebank::writeMonoPcm16Wav(
      root / "audio/a.wav", 48000U,
      seam::test::support::sineWave(48000U, 220.0, 0.5)));
  const auto audioEdit = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 4U,
      seam::rendering::RenderQuality::Preview, root);
  CHECK(audioEdit);
  CHECK(audioEdit.value().contentHash != baseline.value().contentHash);

  // A background snapshot must render the exact decoded bytes that produced
  // its identity even when the on-disk voicebank is replaced afterward.
  const auto frozenAfterMutation = pipeline.render(baseline.value());
  CHECK(frozenAfterMutation);
  CHECK(frozenAfterMutation.value().rendered.audio.samples ==
        frozenBeforeMutation.value().rendered.audio.samples);
  const auto refreshedAfterMutation = pipeline.render(audioEdit.value());
  CHECK(refreshedAfterMutation);
  CHECK(refreshedAfterMutation.value().rendered.audio.samples !=
        frozenBeforeMutation.value().rendered.audio.samples);
}

TEST_CASE("voicebank asset resolver rejects symbolic-link escapes") {
  const auto root = seam::test::support::temporaryDirectory("bank-symlink");
  const auto outside = seam::test::support::temporaryDirectory("bank-outside") /
                       "outside.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      outside, 48000U, seam::test::support::sineWave(48000U, 440.0, 0.02)));
  std::filesystem::create_directories(root / "audio");
  std::error_code error;
  std::filesystem::create_symlink(outside, root / "audio/escape.wav", error);
  if (error) return;  // Some restricted Windows test environments disallow symlinks.
  const auto resolved = seam::voicebank::resolveBankAsset(root, "audio/escape.wav");
  CHECK(!resolved);
}

TEST_CASE("PCM cache validates declared payload before allocating samples") {
  const auto root = seam::test::support::temporaryDirectory("pcm-size-guard");
  std::filesystem::create_directories(root);
  const auto path = root / "huge.spcm";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write("SEAMPCM4", 8);
  writeLittle(stream, seam::build::kPcmCacheFormatRevision);
  writeLittle(stream, std::uint32_t{48000U});
  writeLittle(stream, std::int64_t{0});
  writeLittle(stream, std::uint64_t{200'000'000ULL});
  writeLittle(stream, std::uint64_t{0ULL});
  stream.close();

  seam::rendering::PcmCache cache{
      root, seam::rendering::PcmCacheLimits{
          .maximumMemoryBytes = 1024U,
          .maximumDiskBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL,
          .maximumEntryBytes = 1024ULL * 1024ULL * 1024ULL,
          .maximumDiskEntries = 16U,
      }};
  const auto loaded = cache.load("huge");
  CHECK(!loaded);
  CHECK(cache.stats().corruptEntries == 1U);

  seam::rendering::PcmCache tinyEntryCache{
      root / "tiny", seam::rendering::PcmCacheLimits{
          .maximumMemoryBytes = 1024U,
          .maximumDiskBytes = 4096U,
          .maximumEntryBytes = 64U,
          .maximumDiskEntries = 16U,
      }};
  CHECK(!tinyEntryCache.store("oversized", seam::rendering::CachedPcm{
      .sampleRate = 48000U,
      .startFrame = 0,
      .samples = std::vector<float>(100U, 0.1F),
  }));
}

TEST_CASE("spectral and stretch renderers preserve the recorded vowel transition") {
  constexpr std::size_t frames = 2048U;
  std::vector<float> source(frames, 0.0F);
  for (std::size_t index = 0U; index < 400U; ++index) {
    source[index] = 0.35F * static_cast<float>(std::sin(
        2.0 * std::numbers::pi * 173.0 * static_cast<double>(index) / 48000.0));
  }
  for (std::size_t index = 400U; index < frames; ++index) {
    source[index] = 0.25F * static_cast<float>(std::sin(
        2.0 * std::numbers::pi * 440.0 * static_cast<double>(index) / 48000.0));
  }
  seam::voicebank::AudioBuffer audio{
      .sampleRate = 48000U,
      .channels = 1U,
      .interleaved = source,
  };
  auto unit = seam::test::support::makeUnit(
      "transition", {"a"}, "unused.wav", 69,
      seam::voicebank::UnitKind::Sustain, frames);
  unit.markers = seam::voicebank::UnitMarkers{
      .audioOffset = 0,
      .consonantEnd = 80,
      .vowelOnset = 120,
      .stableStart = 400,
      .loopStart = 512,
      .loopEnd = 1536,
      .releaseStart = 1792,
      .audioEnd = static_cast<seam::time::SampleFrame>(frames),
  };

  seam::synthesis::SpectralClassicRenderer spectral;
  const auto spectralResult = spectral.render(
      unit, audio, 48000U, static_cast<seam::time::SampleFrame>(frames), 69,
      seam::synthesis::SpectralRenderParameters{
          .fftSize = 128U,
          .hopSize = 32U,
          .formantFollow = 0.0F,
          .phaseReset = 1.0F,
          .additionalGainDb = 0.0F,
          .pitchCurve = {},
      });
  CHECK(spectralResult);
  CHECK(spectralResult.value().vowelOnsetOffset == 120);
  CHECK_NEAR(
      spectralResult.value().samples[160] - spectralResult.value().samples[300],
      source[160] - source[300], 2.0e-5);

  seam::synthesis::StretchUnitRenderer stretch;
  const auto stretchResult = stretch.render(
      unit, audio, 48000U, static_cast<seam::time::SampleFrame>(frames), 69,
      seam::synthesis::StretchRenderParameters{
          .grainSize = 128U,
          .hopSize = 32U,
          .transientPreservation = 0.75F,
          .sourceDrift = 0.25F,
          .additionalGainDb = 0.0F,
          .pitchCurve = {},
      });
  CHECK(stretchResult);
  CHECK(stretchResult.value().vowelOnsetOffset == 120);
  CHECK_NEAR(
      stretchResult.value().samples[160] - stretchResult.value().samples[300],
      source[160] - source[300], 2.0e-5);
}

TEST_CASE("phase-aligned seam remains continuous after overlap ends") {
  std::vector<float> outgoing(256U, 0.0F);
  std::vector<float> incoming(256U, 0.0F);
  for (std::size_t index = 0U; index < outgoing.size(); ++index) {
    outgoing[index] = 0.5F * static_cast<float>(std::sin(
        2.0 * std::numbers::pi * static_cast<double>(index) / 32.0));
    incoming[index] = 0.5F * static_cast<float>(std::sin(
        2.0 * std::numbers::pi * static_cast<double>(index + 7U) / 32.0));
  }
  std::vector<seam::synthesis::PlacedRenderedUnit> units;
  units.push_back(seam::synthesis::PlacedRenderedUnit{
      .destinationStart = 0,
      .unit = seam::synthesis::RenderedUnit{
          .unitId = "left", .samples = outgoing, .vowelOnsetOffset = 0},
      .incomingBoundary = std::nullopt,
  });
  units.push_back(seam::synthesis::PlacedRenderedUnit{
      .destinationStart = 128,
      .unit = seam::synthesis::RenderedUnit{
          .unitId = "right", .samples = incoming, .vowelOnsetOffset = 0},
      .incomingBoundary = seam::synthesis::BoundarySeamSettings{
          .seamAmount = 0.0F,
          .curve = seam::domain::SeamCurve::Smooth,
          .maxOverlapFrames = 128,
          .phaseReset = 0.0F,
          .envelopeBlend = 0.0F,
      },
  });
  const auto rendered = seam::synthesis::SeamComposer{}.compose(
      units, seam::synthesis::SeamSettings{
          .seamAmount = 0.0F,
          .curve = seam::domain::SeamCurve::Smooth,
          .sampleRate = 48000U,
      });
  CHECK(rendered);
  CHECK(rendered.value().samples.size() == 384U);
  const auto derivative = std::abs(
      rendered.value().samples[256] - rendered.value().samples[255]);
  CHECK(derivative < 0.18F);
}

TEST_CASE("scheduler applies a final revision gate after cache publication") {
  const auto root = seam::test::support::temporaryDirectory("scheduler-final-gate");
  seam::rendering::PcmCache cache{root};
  std::mutex mutex;
  std::condition_variable condition;
  bool firstHookEntered = false;
  bool releaseFirst = false;
  std::atomic<int> hookCount{0};
  seam::rendering::BackgroundRenderScheduler scheduler{
      cache, 1U, seam::rendering::RenderSchedulerHooks{
          .beforeFinalPublish = [&] {
            if (hookCount.fetch_add(1) != 0) return;
            std::unique_lock lock{mutex};
            firstHookEntered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return releaseFirst; });
          },
      }};

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase",
      .cacheKey = "revision-one",
      .revision = 1U,
      .sampleRate = 48000U,
      .priority = seam::rendering::RenderPriority::Background,
      .task = [](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
        return seam::synthesis::PhraseAudio{
            .startFrame = 0,
            .samples = std::vector<float>(64U, 0.1F),
        };
      },
  }));
  {
    std::unique_lock lock{mutex};
    CHECK(condition.wait_for(lock, 2s, [&] { return firstHookEntered; }));
  }
  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase",
      .cacheKey = "revision-two",
      .revision = 2U,
      .sampleRate = 48000U,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = [](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
        return seam::synthesis::PhraseAudio{
            .startFrame = 0,
            .samples = std::vector<float>(64U, 0.2F),
        };
      },
  }));
  {
    std::scoped_lock lock{mutex};
    releaseFirst = true;
  }
  condition.notify_all();
  CHECK(scheduler.waitIdle(3s));
  const auto completions = scheduler.drainCompleted();
  bool oldPublished = false;
  bool latestCompleted = false;
  for (const auto& completion : completions) {
    if (completion.revision == 1U) {
      oldPublished = completion.status == seam::rendering::RenderCompletionStatus::Completed ||
                     completion.status == seam::rendering::RenderCompletionStatus::CacheHit;
      CHECK(completion.pcm == nullptr);
    }
    if (completion.revision == 2U) {
      latestCompleted = completion.status ==
                        seam::rendering::RenderCompletionStatus::Completed;
      CHECK(completion.pcm != nullptr);
    }
  }
  CHECK(!oldPublished);
  CHECK(latestCompleted);
}

TEST_CASE("playback transport uses command queue and consumer-owned reset epoch") {
  seam::rendering::SpscAudioRingBuffer ring{64U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 16U, 32U};
  CHECK(feeder.setTimeline(constantTimeline(0.1F)));
  CHECK(feeder.seek(0));
  CHECK(feeder.setPlaying(true));
  CHECK(feeder.feedToWatermark(32U) >= 32U);
  std::array<float, 8> oldSamples{};
  CHECK(ring.read(oldSamples) == oldSamples.size());
  CHECK_NEAR(oldSamples.front(), 0.1, 1.0e-6);

  CHECK(feeder.setTimeline(constantTimeline(0.8F)));
  CHECK(feeder.seek(0));
  CHECK(feeder.setPlaying(true));
  CHECK(feeder.feedOnce() == 0U);  // Waits for the callback-owned reset.
  std::array<float, 8> resetBuffer{};
  CHECK(ring.read(resetBuffer) == 0U);
  for (const auto sample : resetBuffer) CHECK(sample == 0.0F);
  CHECK(feeder.feedToWatermark(16U) >= 16U);
  std::array<float, 8> newSamples{};
  CHECK(ring.read(newSamples) == newSamples.size());
  for (const auto sample : newSamples) CHECK_NEAR(sample, 0.8, 1.0e-6);
  CHECK(feeder.stats().resetRequests >= 1U);
  CHECK(feeder.stats().resetWaits >= 1U);
}

TEST_CASE("loop and play-state changes discard already buffered future audio") {
  seam::rendering::SpscAudioRingBuffer ring{64U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 16U};
  CHECK(feeder.setTimeline(constantTimeline(0.25F, 256U)));
  CHECK(feeder.setPlaying(true));
  CHECK(feeder.feedToWatermark(32U) >= 32U);

  CHECK(feeder.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 4, .endFrame = 12}));
  CHECK(feeder.feedOnce() == 0U);
  std::array<float, 16> resetBlock{};
  CHECK(ring.read(resetBlock) == 0U);
  CHECK(std::all_of(resetBlock.begin(), resetBlock.end(),
                    [](float value) { return value == 0.0F; }));
  CHECK(feeder.feedToWatermark(16U) >= 16U);

  CHECK(feeder.setPlaying(false));
  CHECK(feeder.feedOnce() == 0U);
  CHECK(ring.read(resetBlock) == 0U);
  CHECK(feeder.feedOnce() == 0U);
  CHECK(ring.availableRead() == 0U);

  CHECK(feeder.setPlaying(true));
  CHECK(feeder.feedToWatermark(16U) >= 16U);
}

TEST_CASE("playback feeder remains race-free under threaded transport changes") {
  seam::rendering::SpscAudioRingBuffer ring{512U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 64U, 512U};
  CHECK(feeder.setTimeline(constantTimeline(0.25F, 8192U)));
  CHECK(feeder.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true, .startFrame = 0, .endFrame = 4096}));
  CHECK(feeder.setPlaying(true));

  std::atomic<bool> stop{false};
  std::atomic<bool> finite{true};
  std::jthread producer([&](std::stop_token token) {
    while (!token.stop_requested() && !stop.load(std::memory_order_acquire)) {
      static_cast<void>(feeder.feedToWatermark(384U));
      std::this_thread::yield();
    }
  });
  std::jthread consumer([&](std::stop_token token) {
    std::array<float, 64> block{};
    while (!token.stop_requested() && !stop.load(std::memory_order_acquire)) {
      static_cast<void>(ring.read(block));
      for (const auto sample : block) {
        if (!std::isfinite(sample)) finite.store(false, std::memory_order_release);
      }
      std::this_thread::yield();
    }
  });

  for (int index = 0; index < 100; ++index) {
    seam::core::Result<void> queued = feeder.seek((index * 37) % 4000);
    while (!queued) {
      std::this_thread::yield();
      queued = feeder.seek((index * 37) % 4000);
    }
    if ((index % 10) == 0) {
      CHECK(feeder.setPlaying(false));
      CHECK(feeder.setPlaying(true));
    }
  }
  std::this_thread::sleep_for(20ms);
  stop.store(true, std::memory_order_release);
  producer.request_stop();
  consumer.request_stop();
  CHECK(finite.load(std::memory_order_acquire));
  CHECK(feeder.stats().controlCommands >= 100U);
}

TEST_CASE("command transactions restore project state on apply and undo failure") {
  seam::application::ProjectFactory factory{5100};
  auto project = factory.createProject("original");
  seam::application::EditorSession session{project};
  CHECK(!session.execute(std::make_unique<MutatingApplyFailure>()));
  CHECK(session.project().name() == "original");
  CHECK(!session.canUndo());

  seam::application::CompositeCommand composite{"atomic composite"};
  composite.add(std::make_unique<SetNameCommand>("first"));
  composite.add(std::make_unique<MutatingApplyFailure>());
  CHECK(!composite.apply(project));
  CHECK(project.name() == "original");

  CHECK(session.execute(std::make_unique<MutatingRevertFailure>()));
  CHECK(session.project().name() == "applied");
  CHECK(!session.undo());
  CHECK(session.project().name() == "applied");
  CHECK(!session.canUndo());
  CHECK(!session.canRedo());
  CHECK(session.health() == seam::application::SessionHealth::RecoveryRequired);
  CHECK(!session.execute(std::make_unique<SetNameCommand>("blocked")));
  session.clearHistory();
  CHECK(session.health() == seam::application::SessionHealth::Ready);
  CHECK(session.execute(std::make_unique<SetNameCommand>("recovered")));
}
