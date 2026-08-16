#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_scheduler.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/stale_audio_store.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>

namespace {

struct RenderFixture final {
  seam::application::ProjectFactory factory{900};
  seam::domain::Project project{factory.createProject("Render fixture")};
  seam::domain::TrackId trackId{factory.addVocalTrack(project, "Voice")};
  seam::domain::RegionId regionId{
      factory.addRegion(project, trackId, "Phrase", seam::time::Tick{0},
                        seam::time::Tick{30720})};

  seam::domain::NoteId add(seam::time::Tick start, std::u32string lyric = U"あ") {
    auto [token, note] = factory.makeNote(
        start, seam::time::Tick{960}, 69, std::move(lyric),
        seam::domain::Language::Japanese);
    const auto id = note.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(token));
    region->notes.push_back(std::move(note));
    region->sortNotes();
    return id;
  }
};

}  // namespace

TEST_CASE("phrase segmentation is stable and dirty invalidation includes neighbors") {
  RenderFixture fixture;
  fixture.add(seam::time::Tick{0});
  fixture.add(seam::time::Tick{960});
  fixture.add(seam::time::Tick{4800});
  fixture.add(seam::time::Tick{5760});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  seam::rendering::PhraseSegmenter segmenter;
  const auto segments = segmenter.segment(
      *region, seam::rendering::PhraseSegmentationConfig{
                   .splitRest = seam::time::Tick{1920},
                   .maximumDuration = seam::time::Tick{15360},
               });
  CHECK(segments);
  CHECK(segments.value().size() == 2);
  const auto repeated = segmenter.segment(
      *region, seam::rendering::PhraseSegmentationConfig{
                   .splitRest = seam::time::Tick{1920},
                   .maximumDuration = seam::time::Tick{15360},
               });
  CHECK(repeated);
  CHECK(repeated.value() == segments.value());

  seam::rendering::DirtyPhraseInvalidator invalidator;
  const auto affected = invalidator.affected(
      segments.value(), seam::time::Tick{0}, seam::time::Tick{480}, true);
  CHECK(affected.size() == 2);
  CHECK(affected.front() == segments.value().front().id);
  CHECK(affected.back() == segments.value().back().id);
}

TEST_CASE("render snapshot content hash changes with canonical edit") {
  RenderFixture fixture;
  const auto noteId = fixture.add(seam::time::Tick{0});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  seam::rendering::PhraseSegmenter segmenter;
  const auto segments = segmenter.segment(*region);
  CHECK(segments);
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  const auto bank = seam::test::support::makeManifest({unit});
  seam::rendering::RenderSnapshotFactory factory;
  const auto first = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 1,
      seam::rendering::RenderQuality::Preview);
  CHECK(first);
  CHECK(first.value().project != nullptr);
  CHECK(first.value().voicebank != nullptr);

  fixture.project.findNote(noteId)->midiKey = 72;
  const auto second = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 2,
      seam::rendering::RenderQuality::Preview);
  CHECK(second);
  CHECK(first.value().contentHash != second.value().contentHash);
  CHECK(first.value().project->findNote(noteId)->midiKey == 69);
}


TEST_CASE("phrase snapshot cache key ignores unrelated phrase edits") {
  RenderFixture fixture;
  const auto firstNote = fixture.add(seam::time::Tick{0});
  const auto secondNote = fixture.add(seam::time::Tick{4800});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  seam::rendering::PhraseSegmenter segmenter;
  const auto segments = segmenter.segment(
      *region, seam::rendering::PhraseSegmentationConfig{
                   .splitRest = seam::time::Tick{1920},
                   .maximumDuration = seam::time::Tick{15360},
               });
  CHECK(segments);
  CHECK(segments.value().size() == 2);
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  const auto bank = seam::test::support::makeManifest({unit});
  seam::rendering::RenderSnapshotFactory factory;
  const auto before = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 1,
      seam::rendering::RenderQuality::Preview);
  CHECK(before);

  fixture.project.findNote(secondNote)->midiKey = 76;
  const auto unrelated = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 2,
      seam::rendering::RenderQuality::Preview);
  CHECK(unrelated);
  CHECK(before.value().contentHash == unrelated.value().contentHash);

  fixture.project.findNote(firstNote)->midiKey = 72;
  const auto related = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 3,
      seam::rendering::RenderQuality::Preview);
  CHECK(related);
  CHECK(before.value().contentHash != related.value().contentHash);
  CHECK(related.value().project->noteCount() == 1);
}



TEST_CASE("phrase snapshot ignores presentation changes and future tempo events") {
  RenderFixture fixture;
  fixture.add(seam::time::Tick{0});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  seam::rendering::PhraseSegmenter segmenter;
  const auto segments = segmenter.segment(*region);
  CHECK(segments);
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  const auto bank = seam::test::support::makeManifest({unit});
  seam::rendering::RenderSnapshotFactory factory;
  const auto baseline = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 1,
      seam::rendering::RenderQuality::Preview);
  CHECK(baseline);

  fixture.project.setName("Renamed project");
  fixture.project.settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Full;
  fixture.project.settings().snapEnabled = false;
  fixture.project.settings().snapGrid = seam::time::Tick{120};
  fixture.project.findVocalTrack(fixture.trackId)->name = "Renamed track";
  fixture.project.findRegion(fixture.regionId)->name = "Renamed region";
  CHECK(fixture.project.tempoMap().addOrReplace(seam::time::Tick{9600}, 173.0));
  CHECK(fixture.project.meterMap().addOrReplace(seam::time::Tick{9600}, 7, 8));

  const auto presentationOnly = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 2,
      seam::rendering::RenderQuality::Preview);
  CHECK(presentationOnly);
  CHECK(baseline.value().contentHash == presentationOnly.value().contentHash);

  CHECK(fixture.project.tempoMap().addOrReplace(seam::time::Tick{480}, 90.0));
  const auto tempoInsidePhrase = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 3,
      seam::rendering::RenderQuality::Preview);
  CHECK(tempoInsidePhrase);
  CHECK(baseline.value().contentHash != tempoInsidePhrase.value().contentHash);
}

TEST_CASE("immutable snapshot executes the complete phrase render pipeline") {
  RenderFixture fixture;
  fixture.add(seam::time::Tick{1920}, U"か");
  fixture.add(seam::time::Tick{2880}, U"ー");
  const auto directory = seam::test::support::temporaryDirectory("snapshot-pipeline");
  std::filesystem::create_directories(directory / "audio");
  const auto samples = seam::test::support::sineWave(48000, 440.0, 0.5);
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "k-a.wav", 48000, samples));
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "a.wav", 48000, samples));
  auto cv = seam::test::support::makeUnit(
      "k-a", {"k", "a"}, "audio/k-a.wav", 69,
      seam::voicebank::UnitKind::Cv, samples.size());
  auto sustain = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  const auto bank = seam::test::support::makeManifest({cv, sustain});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  seam::rendering::PhraseSegmenter segmenter;
  const auto segments = segmenter.segment(*region);
  CHECK(segments);
  CHECK(segments.value().size() == 1);
  seam::rendering::RenderSnapshotFactory factory;
  const auto snapshot = factory.create(
      fixture.project, bank, fixture.trackId, segments.value().front(), 7,
      seam::rendering::RenderQuality::Preview, directory, 48000, "original");
  CHECK(snapshot);
  seam::rendering::PhraseRenderPipeline pipeline;
  const auto rendered = pipeline.render(snapshot.value());
  CHECK(rendered);
  CHECK(rendered.value().phonemes.tokens.size() == 3);
  CHECK(rendered.value().unitPlan.entries.size() == 2);
  CHECK(rendered.value().timing.placements.size() == 2);
  CHECK(!rendered.value().rendered.audio.samples.empty());
}

TEST_CASE("content addressed PCM cache survives memory eviction") {
  const auto directory = seam::test::support::temporaryDirectory("pcm-cache");
  seam::rendering::PcmCache cache{directory};
  seam::rendering::CachedPcm source{
      .sampleRate = 48000,
      .startFrame = -120,
      .samples = seam::test::support::sineWave(48000, 330.0, 0.05),
  };
  CHECK(cache.store("abc123", source));
  const auto memory = cache.load("abc123");
  CHECK(memory);
  CHECK(*memory.value() == source);
  cache.clearMemory();
  const auto disk = cache.load("abc123");
  CHECK(disk);
  CHECK(*disk.value() == source);
  CHECK(cache.stats().writes == 1);
  CHECK(cache.stats().memoryHits >= 1);
  CHECK(cache.stats().diskHits == 1);
  CHECK(!cache.load("../escape"));
}



TEST_CASE("PCM cache rejects non-finite and corrupted payloads") {
  const auto directory = seam::test::support::temporaryDirectory("pcm-cache-corruption");
  seam::rendering::PcmCache cache{directory};
  const seam::rendering::CachedPcm invalid{
      .sampleRate = 48000,
      .startFrame = 0,
      .samples = {0.0F, std::numeric_limits<float>::quiet_NaN()},
  };
  CHECK(!cache.store("nan-entry", invalid));

  const seam::rendering::CachedPcm valid{
      .sampleRate = 48000,
      .startFrame = -32,
      .samples = std::vector<float>(64, 0.125F),
  };
  CHECK(cache.store("corrupted", valid));
  cache.clearMemory();
  {
    std::fstream stream(directory / "corrupted.spcm",
                        std::ios::binary | std::ios::in | std::ios::out);
    CHECK(static_cast<bool>(stream));
    stream.seekp(-1, std::ios::end);
    const char replacement = static_cast<char>(0x7f);
    stream.write(&replacement, 1);
  }
  const auto checksumFailure = cache.load("corrupted");
  CHECK(!checksumFailure);
  CHECK(checksumFailure.error().code == seam::core::ErrorCode::ParseError);

  CHECK(cache.store("truncated", valid));
  cache.clearMemory();
  std::filesystem::resize_file(directory / "truncated.spcm", 12);
  const auto truncated = cache.load("truncated");
  CHECK(!truncated);
  CHECK(truncated.error().code == seam::core::ErrorCode::ParseError);
  CHECK(cache.stats().corruptEntries >= 2);
}

TEST_CASE("background scheduler cancels obsolete revision and reuses cache") {
  const auto directory = seam::test::support::temporaryDirectory("scheduler");
  seam::rendering::PcmCache cache{directory};
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 1};

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-a",
      .cacheKey = "revision-one",
      .revision = 1,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Background,
      .task = [](std::stop_token token)
          -> seam::core::Result<seam::synthesis::PhraseAudio> {
        for (int index = 0; index < 100; ++index) {
          if (token.stop_requested()) {
            return seam::core::failure<seam::synthesis::PhraseAudio>(
                seam::core::ErrorCode::Conflict, "cancelled");
          }
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return seam::synthesis::PhraseAudio{.startFrame = 0,
                                             .samples = std::vector<float>(64, 0.1F)};
      },
  }));
  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-a",
      .cacheKey = "revision-two",
      .revision = 2,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = [](std::stop_token token)
          -> seam::core::Result<seam::synthesis::PhraseAudio> {
        if (token.stop_requested()) {
          return seam::core::failure<seam::synthesis::PhraseAudio>(
              seam::core::ErrorCode::Conflict, "cancelled");
        }
        return seam::synthesis::PhraseAudio{.startFrame = -32,
                                             .samples = std::vector<float>(128, 0.25F)};
      },
  }));
  CHECK(scheduler.waitIdle(std::chrono::seconds{3}));
  auto completions = scheduler.drainCompleted();
  CHECK(completions.size() == 2);
  CHECK(std::any_of(completions.begin(), completions.end(), [](const auto& value) {
    return value.revision == 2 &&
           value.status == seam::rendering::RenderCompletionStatus::Completed &&
           value.pcm != nullptr;
  }));
  CHECK(std::any_of(completions.begin(), completions.end(), [](const auto& value) {
    return value.revision == 1 &&
           (value.status == seam::rendering::RenderCompletionStatus::Cancelled ||
            value.status == seam::rendering::RenderCompletionStatus::Stale);
  }));

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-a",
      .cacheKey = "revision-two",
      .revision = 2,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = [](std::stop_token)
          -> seam::core::Result<seam::synthesis::PhraseAudio> {
        return seam::core::failure<seam::synthesis::PhraseAudio>(
            seam::core::ErrorCode::Internal, "cache miss would be a test failure");
      },
  }));
  const auto cached = scheduler.drainCompleted();
  CHECK(cached.size() == 1);
  CHECK(cached.front().status == seam::rendering::RenderCompletionStatus::CacheHit);
  CHECK(cached.front().pcm->samples.size() == 128);
}

TEST_CASE("stale audio remains readable until a newer revision is published") {
  seam::rendering::StaleWhileRenderStore store;
  auto oldPcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{.sampleRate = 48000,
                                 .startFrame = 0,
                                 .samples = std::vector<float>(8, 0.1F)});
  CHECK(store.publish("phrase", 1, oldPcm).accepted);
  CHECK(store.markDirty("phrase"));
  CHECK(store.isDirty("phrase"));
  CHECK(store.current("phrase")->revision == 1);
  CHECK(store.current("phrase")->dirty);

  auto newPcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{.sampleRate = 48000,
                                 .startFrame = 0,
                                 .samples = std::vector<float>(8, 0.2F)});
  const auto published = store.publish("phrase", 2, newPcm);
  CHECK(published.accepted);
  CHECK(published.replacedExisting);
  CHECK(!store.isDirty("phrase"));
  CHECK(store.current("phrase")->revision == 2);
  CHECK(!store.publish("phrase", 1, oldPcm).accepted);
}

TEST_CASE("SPSC audio ring buffer wraps without allocation or data reordering") {
  seam::rendering::SpscAudioRingBuffer buffer{8};
  const std::array<float, 6> first{1, 2, 3, 4, 5, 6};
  CHECK(buffer.write(first) == first.size());
  std::array<float, 4> readA{};
  CHECK(buffer.read(readA) == readA.size());
  CHECK(readA == (std::array<float, 4>{1, 2, 3, 4}));
  const std::array<float, 5> second{7, 8, 9, 10, 11};
  CHECK(buffer.write(second) == second.size());
  std::array<float, 7> readB{};
  CHECK(buffer.read(readB) == readB.size());
  CHECK(readB == (std::array<float, 7>{5, 6, 7, 8, 9, 10, 11}));
  CHECK(buffer.availableRead() == 0);
}



TEST_CASE("scheduler rejects an immediately stale request without running its task") {
  const auto directory = seam::test::support::temporaryDirectory("scheduler-immediate-stale");
  seam::rendering::PcmCache cache{directory};
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 1};
  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-stale",
      .cacheKey = "revision-two-render",
      .revision = 2,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = [](std::stop_token)
          -> seam::core::Result<seam::synthesis::PhraseAudio> {
        return seam::synthesis::PhraseAudio{
            .startFrame = 0,
            .samples = std::vector<float>(32, 0.2F),
        };
      },
  }));
  CHECK(scheduler.waitIdle(std::chrono::seconds{2}));
  static_cast<void>(scheduler.drainCompleted());

  std::atomic<int> executions{0};
  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-stale",
      .cacheKey = "revision-one-render",
      .revision = 1,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = [&executions](std::stop_token)
          -> seam::core::Result<seam::synthesis::PhraseAudio> {
        ++executions;
        return seam::synthesis::PhraseAudio{
            .startFrame = 0,
            .samples = std::vector<float>(16, 0.1F),
        };
      },
  }));
  const auto completion = scheduler.drainCompleted();
  CHECK(completion.size() == 1);
  CHECK(completion.front().status ==
        seam::rendering::RenderCompletionStatus::Stale);
  CHECK(completion.front().pcm == nullptr);
  CHECK(executions.load() == 0);
}

TEST_CASE("cached audio content can be reused by a newer revision") {
  const auto directory = seam::test::support::temporaryDirectory("scheduler-revision-cache");
  seam::rendering::PcmCache cache{directory};
  CHECK(cache.store("shared-content", seam::rendering::CachedPcm{
      .sampleRate = 48000,
      .startFrame = -48,
      .samples = std::vector<float>(48, 0.3F),
  }));
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 1};
  std::atomic<int> executions{0};
  const auto task = [&executions](std::stop_token)
      -> seam::core::Result<seam::synthesis::PhraseAudio> {
    ++executions;
    return seam::core::failure<seam::synthesis::PhraseAudio>(
        seam::core::ErrorCode::Internal, "cache reuse failed");
  };

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-shared",
      .cacheKey = "shared-content",
      .revision = 3,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Selected,
      .task = task,
  }));
  auto first = scheduler.drainCompleted();
  CHECK(first.size() == 1);
  CHECK(first.front().status == seam::rendering::RenderCompletionStatus::CacheHit);

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-shared",
      .cacheKey = "shared-content",
      .revision = 4,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = task,
  }));
  auto second = scheduler.drainCompleted();
  CHECK(second.size() == 1);
  CHECK(second.front().revision == 4);
  CHECK(second.front().status == seam::rendering::RenderCompletionStatus::CacheHit);
  CHECK(second.front().pcm != nullptr);
  CHECK(executions.load() == 0);

  seam::rendering::StaleWhileRenderStore store;
  CHECK(store.publish("phrase-shared", second.front().revision,
                      second.front().pcm).accepted);
  const auto current = store.current("phrase-shared");
  CHECK(current.has_value());
  CHECK(current->revision == 4);
  CHECK(current->pcm->startFrame == -48);
}

TEST_CASE("render scheduler executes queued jobs by priority") {
  const auto directory = seam::test::support::temporaryDirectory("scheduler-priority");
  seam::rendering::PcmCache cache{directory};
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 1};
  std::mutex gateMutex;
  std::condition_variable gateCondition;
  bool releaseBlocker = false;
  std::mutex orderMutex;
  std::vector<int> order;

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "blocker",
      .cacheKey = "blocker-key",
      .revision = 1,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = [&](std::stop_token token)
          -> seam::core::Result<seam::synthesis::PhraseAudio> {
        std::unique_lock lock{gateMutex};
        gateCondition.wait(lock, [&] { return releaseBlocker || token.stop_requested(); });
        return seam::synthesis::PhraseAudio{
            .startFrame = 0,
            .samples = std::vector<float>(8, 0.0F),
        };
      },
  }));

  // Let the single worker enter the blocker before the jobs under test are queued.
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  const auto makeTask = [&](int marker) {
    return [&, marker](std::stop_token)
        -> seam::core::Result<seam::synthesis::PhraseAudio> {
      {
        std::scoped_lock lock{orderMutex};
        order.push_back(marker);
      }
      return seam::synthesis::PhraseAudio{
          .startFrame = 0,
          .samples = std::vector<float>(8, static_cast<float>(marker) * 0.1F),
      };
    };
  };
  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "background",
      .cacheKey = "background-key",
      .revision = 1,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Background,
      .task = makeTask(1),
  }));
  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "playhead",
      .cacheKey = "playhead-key",
      .revision = 1,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = makeTask(2),
  }));
  {
    std::scoped_lock lock{gateMutex};
    releaseBlocker = true;
  }
  gateCondition.notify_one();
  CHECK(scheduler.waitIdle(std::chrono::seconds{2}));
  CHECK(order.size() == 2);
  CHECK(order[0] == 2);
  CHECK(order[1] == 1);
}

TEST_CASE("scheduler rejects an older cached revision without replacing the latest") {
  const auto directory = seam::test::support::temporaryDirectory("scheduler-stale-cache");
  seam::rendering::PcmCache cache{directory};
  CHECK(cache.store("newer-cache", seam::rendering::CachedPcm{
      .sampleRate = 48000,
      .startFrame = 0,
      .samples = std::vector<float>(32, 0.4F),
  }));
  CHECK(cache.store("older-cache", seam::rendering::CachedPcm{
      .sampleRate = 48000,
      .startFrame = 0,
      .samples = std::vector<float>(16, 0.2F),
  }));

  seam::rendering::BackgroundRenderScheduler scheduler{cache, 1};
  const auto unreachableTask = [](std::stop_token)
      -> seam::core::Result<seam::synthesis::PhraseAudio> {
    return seam::core::failure<seam::synthesis::PhraseAudio>(
        seam::core::ErrorCode::Internal,
        "A populated cache entry must not execute the render task");
  };

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-cache-order",
      .cacheKey = "newer-cache",
      .revision = 5,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = unreachableTask,
  }));
  auto newer = scheduler.drainCompleted();
  CHECK(newer.size() == 1);
  CHECK(newer.front().status == seam::rendering::RenderCompletionStatus::CacheHit);
  CHECK(newer.front().revision == 5);
  CHECK(newer.front().pcm != nullptr);
  CHECK(newer.front().pcm->samples.size() == 32);

  CHECK(scheduler.submit(seam::rendering::ScheduledRenderRequest{
      .phraseId = "phrase-cache-order",
      .cacheKey = "older-cache",
      .revision = 4,
      .sampleRate = 48000,
      .priority = seam::rendering::RenderPriority::Playhead,
      .task = unreachableTask,
  }));
  auto older = scheduler.drainCompleted();
  CHECK(older.size() == 1);
  CHECK(older.front().status == seam::rendering::RenderCompletionStatus::Stale);
  CHECK(older.front().revision == 4);
  CHECK(older.front().pcm == nullptr);
  CHECK(scheduler.stats().cacheHits == 1);
  CHECK(scheduler.stats().stale == 1);
}
