#include "test_framework.hpp"
#include "seam/synthesis/neural_bundle.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_scheduler.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/synthesis/phrase_backend.hpp"
#include "seam/voice_design/procedural_renderer.hpp"

#include <array>

namespace {

struct PerformanceSnapshotFixture final {
  seam::application::ProjectFactory factory{9100U};
  seam::domain::Project project{factory.createProject("Performance snapshot")};
  seam::domain::TrackId trackId{factory.addVocalTrack(project, "Singer")};
  seam::domain::RegionId regionId{factory.addRegion(project, trackId, "Phrase",
      seam::time::Tick{4800}, seam::time::Tick{9600})};
  seam::domain::NoteId noteId;
  seam::voicebank::Manifest bank{seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 69,
          seam::voicebank::UnitKind::Sustain, 24000)})};
  std::filesystem::path bankRoot{seam::test::support::temporaryDirectory("performance-snapshot")};

  PerformanceSnapshotFixture() {
    bank.styles.push_back("soft");
    auto soft = bank.units.front();
    soft.id = "soft-a";
    soft.style = "soft";
    bank.units.push_back(soft);
    auto [lyric, note] = factory.makeNote(seam::time::Tick{1920},
        seam::time::Tick{960}, 69, U"あ", seam::domain::Language::Japanese);
    note.vibrato.enabled = true;
    note.vibrato.phaseTurns = 0.25F;
    note.phoneticHint = "a";
    noteId = note.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    CHECK(region->dynamicsAutomation.replacePoints({
        {.tick = seam::time::Tick{0}, .linearGain = 0.1F},
        {.tick = seam::time::Tick{960}, .linearGain = 0.2F},
        {.tick = seam::time::Tick{2160}, .linearGain = 0.4F},
        {.tick = seam::time::Tick{2640}, .linearGain = 0.8F},
        {.tick = seam::time::Tick{3840}, .linearGain = 0.6F},
        {.tick = seam::time::Tick{6000}, .linearGain = 1.0F}}));
    project.findVocalTrack(trackId)->styleSelection = {
        .origin = seam::domain::VoiceStyleOrigin::Explicit, .styleId = "original"};
    std::filesystem::create_directories(bankRoot / "audio");
    CHECK(seam::voicebank::writeMonoPcm16Wav(bankRoot / "audio/a.wav", 48000,
        seam::test::support::sineWave(48000, 440.0, 0.5)));
  }

  seam::rendering::RenderSnapshot snapshot() const {
    const auto segments = seam::rendering::PhraseSegmenter{}.segment(*project.findRegion(regionId));
    CHECK(segments);
    CHECK(segments.value().size() == 1U);
    auto result = seam::rendering::RenderSnapshotFactory{}.create(project, bank,
        trackId, segments.value().front(), 1U, seam::rendering::RenderQuality::Preview, bankRoot);
    if (!result) throw std::runtime_error(result.error().message + ": " + result.error().context);
    CHECK(result);
    return std::move(result).value();
  }
};

}

TEST_CASE("articulated snapshots preserve recipe timing PCM and markers through scheduler checkpoints") {
  using namespace seam;
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().phoneticHint.reset(); region->lyrics.front().surface = U"さ";
  region->phonemeOverrides = {
      {.key = {fixture.noteId, 0U}, .timing = {.startOffset = 0}, .locked = true},
      {.key = {fixture.noteId, 1U}, .timing = {.startOffset = 100000}, .locked = true}};
  voice_design::VoiceRecipe recipe; recipe.id = "articulated-snapshot";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto create = [&] {
    return rendering::RenderSnapshotFactory{}.createProcedural(fixture.project, resource.value(),
        fixture.trackId, fixture.regionId, 1U, rendering::RenderQuality::Preview, 48000U);
  };
  const auto snapshot = create(); CHECK(snapshot);
  const auto& performance = *snapshot.value().compiledPerformance;
  const synthesis::PhraseFrameRange context{performance.notes().front().startFrame, performance.notes().back().endFrame};
  CHECK(context.start > 0);
  const auto full = rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(full);
  auto direct = voice_design::ArticulatedStream::createFromRecipe(resource.value(), performance,
      snapshot.value().phonemes->tokens, "neutral"); CHECK(direct);
  CHECK(direct.value().renderOwned(context).value().samples == full.value().rendered.audio.samples);
  const auto& markers = full.value().proceduralMarkers; CHECK(markers.size() == 2U);
  CHECK(markers[0].phone == "s"); CHECK(markers[1].phone == "a");
  CHECK(markers[0].ownedSpan.start == context.start); CHECK(markers[0].ownedSpan.end == context.start + 4800);
  CHECK(markers[1].ownedSpan.start == context.start + 4800); CHECK(markers[1].ownedSpan.end == context.end);
  CHECK(!markers[0].startClipped && !markers[0].endClipped);
  const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(), context, 4701U); CHECK(chunks);
  auto stream = rendering::ProceduralSnapshotStream::create(snapshot.value()); CHECK(stream);
  auto first = stream.value().render(chunks.value().front()); CHECK(first);
  CHECK(first.value().markers.size() == 1U); CHECK(first.value().markers[0].endClipped);
  auto changed = chunks.value()[1]; ++changed.revision; CHECK(!stream.value().render(changed));
  std::stop_source stop; stop.request_stop();
  CHECK(!stream.value().render(chunks.value()[1], stop.get_token())); CHECK(stream.value().position() == context.start + 4701);
  auto checkpoint = stream.value();
  const auto second = stream.value().render(chunks.value()[1]); CHECK(second);
  CHECK(second.value().markers.size() == 2U); CHECK(second.value().markers[0].startClipped);
  CHECK(second.value().markers[1].endClipped);
  CHECK(checkpoint.render(chunks.value()[1]).value().audio.samples == second.value().audio.samples);
  rendering::PcmCache cache{fixture.bankRoot / "articulated-cache"};
  rendering::BackgroundRenderScheduler scheduler{cache, 2U};
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto invalid = markers.front();
    if (scenario == 0U) invalid.phone.clear();
    if (scenario == 1U) invalid.phone = "s\n";
    if (scenario == 2U) invalid.phone = std::string(129U, 's');
    if (scenario == 3U) invalid.key.ordinal = 16384U;
    CHECK(!scheduler.submit({.phraseId = "invalid-marker", .cacheKey = "invalid-marker", .revision = 1U,
        .sampleRate = 48000U, .task = [context](std::stop_token) -> core::Result<synthesis::PhraseAudio> {
          return synthesis::PhraseAudio{context.start, {}};
        }, .outputFrames = context, .proceduralMarkers = {invalid}}));
  }
  for (const auto& chunk : chunks.value()) { CHECK(scheduler.submitSnapshot(chunk)); CHECK(scheduler.waitIdle(std::chrono::seconds{20})); }
  const auto completed = scheduler.drainCompleted(); CHECK(completed.size() == chunks.value().size());
  const auto joined = scheduler.assembleSnapshotCompletions(chunks.value(), completed, context); CHECK(joined);
  CHECK(joined.value().samples == full.value().rendered.audio.samples);
  CHECK(scheduler.stats().proceduralFrames == static_cast<std::uint64_t>(context.end - context.start));
  CHECK(scheduler.submitSnapshot(chunks.value()[1]));
  const auto cached = scheduler.drainCompleted(); CHECK(cached.size() == 1U);
  CHECK(cached.front().status == rendering::RenderCompletionStatus::CacheHit);
  CHECK(cached.front().proceduralMarkers == second.value().markers);
  recipe.frications.front().source.seed = 43U;
  const auto changedResource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(changedResource);
  const auto changedSnapshot = rendering::RenderSnapshotFactory{}.createProcedural(fixture.project, changedResource.value(),
      fixture.trackId, fixture.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(changedSnapshot);
  CHECK(changedSnapshot.value().contentHash != snapshot.value().contentHash);
  CHECK(!stream.value().matchesContext(changedSnapshot.value()));
  region->phonemeOverrides.clear();
  const auto automatic = create(); CHECK(automatic);
  const auto anchors = automatic.value().compiledPerformance->phonemeTiming();
  CHECK(anchors[0].inferredStartFrame == std::optional{context.start});
  CHECK(!anchors[0].explicitStartFrame); CHECK(anchors[1].nucleusFrame == context.start + 2880);
  CHECK(!automatic.value().phonemes->tokens[0].timing.startOffset);
  CHECK(region->phonemeOverrides.empty());
  const auto autoAudio = rendering::PhraseRenderPipeline{}.render(automatic.value()); CHECK(autoAudio);
  CHECK(autoAudio.value().proceduralMarkers[0].ownedSpan.end == context.start + 2880);
  CHECK(automatic.value().contentHash != snapshot.value().contentHash);
}

TEST_CASE("typed project sources route procedural audio at absolute score time without sample banks") {
  PerformanceSnapshotFixture fixture;
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "project-procedural";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto resource = seam::voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  std::filesystem::rename(fixture.bankRoot / "audio/a.wav", fixture.bankRoot / "audio/unavailable.wav");
  auto* track = fixture.project.findVocalTrack(fixture.trackId); track->pan = -1.0F;
  std::vector<seam::rendering::TrackSingerSource> sources{
      seam::rendering::TrackProceduralSource{fixture.trackId, resource.value(), "neutral"}};
  const auto render = [&] {
    return seam::rendering::ProductionProjectRenderer{}.renderWithSources(fixture.project, sources,
        fixture.trackId, fixture.regionId, 1U, 48000U);
  };
  const auto snapshot = seam::rendering::RenderSnapshotFactory{}.createProcedural(fixture.project, resource.value(),
      fixture.trackId, fixture.regionId, 1U, seam::rendering::RenderQuality::Preview, 48000U); CHECK(snapshot);
  const auto direct = seam::rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(direct);
  const auto result = render(); CHECK(result);
  track->proceduralRecipe = seam::domain::ProceduralRecipeReference{resource.value().identity,
      "recipes/singer.json", "neutral"};
  CHECK(render());
  track->proceduralRecipe->resource.contentHash = std::string(64U, 'f'); CHECK(!render());
  track->proceduralRecipe.reset();
  CHECK(result.value().trackCount == 1U); CHECK(result.value().regionCount == 1U);
  CHECK(result.value().phraseCount == 1U); CHECK(result.value().unitCount == 0U);
  CHECK(result.value().activeUnitPlan.empty()); CHECK(result.value().diagnostics.empty());
  CHECK(result.value().phraseContentHashes == std::vector<std::string>{snapshot.value().contentHash});
  const auto& audio = direct.value().rendered.audio;
  const auto start = static_cast<std::size_t>(audio.startFrame); CHECK(start > 0U);
  CHECK(result.value().channelCount == 2U);
  CHECK(result.value().interleaved.size() == 2U * (start + audio.samples.size()));
  for (std::size_t frame = 0; frame < start + audio.samples.size(); ++frame) {
    const auto expected = frame < start ? 0.0F : audio.samples[frame - start];
    CHECK_NEAR(result.value().interleaved[frame * 2U], expected, 0.000001F);
    CHECK_NEAR(result.value().interleaved[frame * 2U + 1U], 0.0F, 0.000001F);
  }
  track->gainDb = -6.0F;
  const auto quiet = render(); CHECK(quiet);
  for (std::size_t i = 0; i < quiet.value().interleaved.size(); ++i) {
    CHECK_NEAR(quiet.value().interleaved[i], result.value().interleaved[i] * seam::domain::decibelsToLinear(-6.0F), 0.000001F);
  }
  sources.push_back(sources.front()); CHECK(!render()); sources.pop_back();
  std::get<seam::rendering::TrackProceduralSource>(sources.front()).style = "missing"; CHECK(!render());
  std::get<seam::rendering::TrackProceduralSource>(sources.front()).style = "neutral";
  track->muted = true; CHECK(!render()); track->muted = false;
  fixture.project.findRegion(fixture.regionId)->lyrics.front().surface = U"か";
  CHECK(render()); // The fixture's explicit "a" hint still owns pronunciation.
  fixture.project.findNote(fixture.noteId)->phoneticHint.reset();
  CHECK(!render());
}

TEST_CASE("procedural snapshots use the selected English and Korean language services") {
  using namespace seam;
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  auto* note = fixture.project.findNote(fixture.noteId);
  CHECK(region != nullptr && note != nullptr);

  const auto renderLanguage = [&](domain::Language language,
                                  std::u32string lyric,
                                  std::string hint,
                                  std::string phone) {
    const auto expectedPhone = phone;
    auto project = fixture.project;
    auto* localRegion = project.findRegion(fixture.regionId);
    auto* localNote = project.findNote(fixture.noteId);
    CHECK(localRegion != nullptr && localNote != nullptr);
    localRegion->findLyric(localNote->lyricTokenId)->language = language;
    localRegion->findLyric(localNote->lyricTokenId)->surface = std::move(lyric);
    if (hint.empty()) localNote->phoneticHint.reset();
    else localNote->phoneticHint = std::move(hint);
    voice_design::VoiceRecipe recipe;
    recipe.id = "language-procedural";
    recipe.poses = {{phone, "neutral", 0.0,
                     {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0},
                      {2600.0, 140.0, -6.0}}}};
    const auto resource = voice_design::freezeVoiceRecipeResource(recipe);
    CHECK(resource);
    const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(
        project, resource.value(), fixture.trackId, fixture.regionId, 1U,
        rendering::RenderQuality::Preview, 48000U);
    CHECK(snapshot);
    CHECK(snapshot.value().pronunciationIdentity.has_value());
    CHECK(snapshot.value().pronunciationIdentity->language == language);
    CHECK(snapshot.value().phonemes->tokens.size() == 1U);
    CHECK(snapshot.value().phonemes->tokens.front().symbol == expectedPhone);
    const auto rendered = rendering::PhraseRenderPipeline{}.render(snapshot.value());
    CHECK(rendered);
    CHECK(!rendered.value().rendered.audio.samples.empty());
    CHECK(std::any_of(rendered.value().rendered.audio.samples.begin(),
                      rendered.value().rendered.audio.samples.end(),
                      [](float sample) { return std::abs(sample) > 0.000001F; }));
  };

  renderLanguage(domain::Language::English, U"a", "ah1", "ah1");
  renderLanguage(domain::Language::Korean, U"아", {}, "a");
}

TEST_CASE("nasal consonant scheduler output preserves typed gestures across the vowel boundary") {
  using namespace seam;
  for (const bool plosive : {false,true}) {
  PerformanceSnapshotFixture f;
  f.project.findRegion(f.regionId)->lyrics.front().surface=plosive?U"か":U"ま";
  f.project.findNote(f.noteId)->phoneticHint.reset();
  voice_design::VoiceRecipe recipe; recipe.id="nasal-consonant-snapshot";
  recipe.poses={{"a","neutral",0.0,{{700.0,80.0,0.0},{1200.0,100.0,-3.0},{2600.0,140.0,-6.0}}},
      {"m","neutral",0.8,{{300.0,60.0,0.0},{2200.0,90.0,-3.0},{3000.0,120.0,-6.0}},voice_design::NasalResonance{}}};
  if (plosive) recipe.plosives={{"k","neutral",{.seed=42U},10.0}};
  const auto kind=plosive?voice_design::ProceduralGestureKind::Plosive:voice_design::ProceduralGestureKind::Nasal;
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto snapshot=rendering::RenderSnapshotFactory{}.createProcedural(f.project,resource.value(),f.trackId,f.regionId,1U,rendering::RenderQuality::Final,48000U); CHECK(snapshot);
  const auto full=rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(full);
  CHECK(full.value().proceduralMarkers.size()==2U);
  CHECK(full.value().proceduralMarkers.front().kind==kind);
  const auto& score=*snapshot.value().compiledPerformance;
  const synthesis::PhraseFrameRange context{score.notes().front().startFrame,score.notes().back().endFrame};
  const auto chunks=rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(),context,plosive?2500U:2980U); CHECK(chunks);
  rendering::PcmCache cache{f.bankRoot/"nasal-consonant-cache"}; rendering::BackgroundRenderScheduler scheduler{cache,2U};
  for (const auto& chunk:chunks.value()) { CHECK(scheduler.submitSnapshot(chunk)); CHECK(scheduler.waitIdle(std::chrono::seconds{20})); }
  const auto completed=scheduler.drainCompleted();
  const auto joined=scheduler.assembleSnapshotCompletions(chunks.value(),completed,context); CHECK(joined);
  CHECK(joined.value().samples==full.value().rendered.audio.samples);
  CHECK(completed.front().proceduralMarkers.front().kind==kind);
  CHECK(scheduler.submitSnapshot(chunks.value().front()));
  const auto cached=scheduler.drainCompleted(); CHECK(cached.size()==1U);
  CHECK(cached.front().status==rendering::RenderCompletionStatus::CacheHit);
  CHECK(cached.front().proceduralMarkers==completed.front().proceduralMarkers);
  }
}

TEST_CASE("nasal recipe snapshots reconstruct a transition and reject obsolete cache identity") {
  using namespace seam;
  PerformanceSnapshotFixture f;
  auto* region=f.project.findRegion(f.regionId);
  auto [lyric,note]=f.factory.makeNote(region->notes.front().startTick+region->notes.front().durationTick,
      time::Tick{960},69U,U"い",domain::Language::Japanese);
  note.phoneticHint="i"; region->lyrics.push_back(lyric); region->notes.push_back(note);
  voice_design::VoiceRecipe recipe; recipe.id="nasal-snapshot";
  recipe.poses={{"a","neutral",0.0,{{700.0,80.0,0.0},{1200.0,100.0,-3.0},{2600.0,140.0,-6.0}}},
      {"i","neutral",0.8,{{300.0,60.0,0.0},{2200.0,90.0,-3.0},{3000.0,120.0,-6.0}},voice_design::NasalResonance{}}};
  const auto resource=voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto snapshot=rendering::RenderSnapshotFactory{}.createProcedural(f.project,resource.value(),f.trackId,f.regionId,
      1U,rendering::RenderQuality::Final,48000U); CHECK(snapshot);
  const auto full=rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(full);
  const auto& score=*snapshot.value().compiledPerformance;
  const synthesis::PhraseFrameRange context{score.notes().front().startFrame,score.notes().back().endFrame};
  const auto split=score.notes()[1].startFrame+100;
  const auto chunks=rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(),context,
      static_cast<std::uint32_t>(split-context.start)); CHECK(chunks); CHECK(chunks.value().size()==2U);
  rendering::PcmCache cache{f.bankRoot/"nasal-cache"}; rendering::BackgroundRenderScheduler scheduler{cache,2U};
  for (const auto& chunk:chunks.value()) { CHECK(scheduler.submitSnapshot(chunk)); CHECK(scheduler.waitIdle(std::chrono::seconds{20})); }
  const auto completed=scheduler.drainCompleted();
  const auto joined=scheduler.assembleSnapshotCompletions(chunks.value(),completed,context); CHECK(joined);
  CHECK(joined.value().samples==full.value().rendered.audio.samples);
  const auto processed=scheduler.stats().proceduralFrames;
  for (const auto& chunk:chunks.value()) { CHECK(scheduler.submitSnapshot(chunk)); CHECK(scheduler.waitIdle(std::chrono::seconds{20})); }
  const auto cached=scheduler.assembleSnapshotCompletions(chunks.value(),scheduler.drainCompleted(),context); CHECK(cached);
  CHECK(cached.value().samples==joined.value().samples); CHECK(scheduler.stats().proceduralFrames==processed);
  recipe.poses.back().nasal->antiresonanceHz=1800.0;
  const auto edited=voice_design::freezeVoiceRecipeResource(recipe); CHECK(edited);
  const auto next=rendering::RenderSnapshotFactory{}.createProcedural(f.project,edited.value(),f.trackId,f.regionId,
      2U,rendering::RenderQuality::Final,48000U); CHECK(next);
  CHECK(next.value().contentHash!=snapshot.value().contentHash);
  const auto changed=rendering::PhraseRenderPipeline{}.render(next.value()); CHECK(changed);
  CHECK(changed.value().rendered.audio.samples!=full.value().rendered.audio.samples);
  CHECK(scheduler.submitSnapshot(next.value())); CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto newCompletion=scheduler.drainCompleted(); CHECK(newCompletion.size()==1U); CHECK(newCompletion.front().pcm);
  CHECK(newCompletion.front().pcm->samples==changed.value().rendered.audio.samples);
  CHECK(scheduler.stats().proceduralFrames>processed);
  const auto prefix=static_cast<std::size_t>(score.notes()[1].startFrame-context.start);
  CHECK(std::equal(full.value().rendered.audio.samples.begin(),full.value().rendered.audio.samples.begin()+static_cast<std::ptrdiff_t>(prefix),
      changed.value().rendered.audio.samples.begin()));
}

TEST_CASE("procedural vowel sequences transition poses across owned checkpoint boundaries") {
  using namespace seam;
  PerformanceSnapshotFixture f;
  auto* region = f.project.findRegion(f.regionId);
  auto [lyric, note] = f.factory.makeNote(region->notes.front().startTick + region->notes.front().durationTick,
      time::Tick{960}, 69U, U"い", domain::Language::Japanese);
  note.phoneticHint = "i";
  region->lyrics.push_back(lyric); region->notes.push_back(note);
  voice_design::VoiceRecipe recipe;
  recipe.id = "vowel-sequence";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}},
                 {"i", "neutral", 0.0, {{300.0, 60.0, 0.0}, {2200.0, 90.0, -3.0}, {3000.0, 120.0, -6.0}}}};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(f.project, resource.value(),
      f.trackId, f.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(snapshot);
  const auto full = rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(full);
  const auto& score = *snapshot.value().compiledPerformance;
  const synthesis::PhraseFrameRange context{score.notes().front().startFrame, score.notes().back().endFrame};
  const auto split = score.notes()[1].startFrame + 100;
  const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(), context,
      static_cast<std::uint32_t>(split - context.start)); CHECK(chunks); CHECK(chunks.value().size() == 2U);
  rendering::PcmCache cache{f.bankRoot / "vowel-sequence-cache"};
  rendering::BackgroundRenderScheduler scheduler{cache, 2U};
  CHECK(scheduler.submitSnapshot(chunks.value()[0])); CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  CHECK(scheduler.submitSnapshot(chunks.value()[1])); CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto completed = scheduler.drainCompleted();
  const auto joined = scheduler.assembleSnapshotCompletions(chunks.value(), completed, context); CHECK(joined);
  CHECK(joined.value().samples == full.value().rendered.audio.samples);
  CHECK(scheduler.stats().proceduralFrames == static_cast<std::uint64_t>(context.end - context.start));
  auto stream = voice_design::SustainedPoseStream::create(resource.value(), score, "a", "neutral", context, 127U); CHECK(stream);
  CHECK(stream.value().configureVowels(*snapshot.value().project->findRegion(f.regionId), snapshot.value().phonemes->tokens));
  CHECK(stream.value().renderOwned(context).value().audio.samples == full.value().rendered.audio.samples);
  stream.value().reset();
  CHECK(stream.value().renderOwned(context).value().audio.samples == full.value().rendered.audio.samples);
  auto baselineProject = f.project;
  baselineProject.findRegion(f.regionId)->lyrics.back().surface = U"あ";
  baselineProject.findRegion(f.regionId)->notes.back().phoneticHint = "a";
  const auto baseline = rendering::RenderSnapshotFactory{}.createProcedural(baselineProject, resource.value(),
      f.trackId, f.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(baseline);
  const auto baselineAudio = rendering::PhraseRenderPipeline{}.render(baseline.value()); CHECK(baselineAudio);
  const auto beforeTransition = static_cast<std::size_t>(score.notes()[1].startFrame - context.start);
  CHECK(std::equal(full.value().rendered.audio.samples.begin(), full.value().rendered.audio.samples.begin() +
      static_cast<std::ptrdiff_t>(beforeTransition), baselineAudio.value().rendered.audio.samples.begin()));
  CHECK(full.value().rendered.audio.samples != baselineAudio.value().rendered.audio.samples);
  auto syllables = f.project;
  auto* syllableRegion = syllables.findRegion(f.regionId);
  syllableRegion->notes.resize(1U); syllableRegion->lyrics.resize(1U);
  syllableRegion->notes.front().phoneticHint.reset();
  syllableRegion->lyrics.front().surface = U"あい";
  const auto inNote = rendering::RenderSnapshotFactory{}.createProcedural(syllables, resource.value(),
      f.trackId, f.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(inNote);
  CHECK(inNote.value().compiledPerformance->notes().size() == 1U);
  const auto anchors = inNote.value().compiledPerformance->phonemeTiming(); CHECK(anchors.size() == 2U);
  const auto& span = inNote.value().compiledPerformance->notes().front();
  CHECK(anchors[0].nucleusFrame == span.startFrame);
  CHECK(anchors[1].nucleusFrame == span.startFrame + (span.endFrame - span.startFrame) / 2);
  const auto inNoteAudio = rendering::PhraseRenderPipeline{}.render(inNote.value()); CHECK(inNoteAudio);
  syllableRegion->lyrics.front().surface = U"ああ";
  const auto same = rendering::RenderSnapshotFactory{}.createProcedural(syllables, resource.value(),
      f.trackId, f.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(same);
  const auto sameAudio = rendering::PhraseRenderPipeline{}.render(same.value()); CHECK(sameAudio);
  const auto nucleusOffset = static_cast<std::size_t>(anchors[1].nucleusFrame - span.startFrame);
  CHECK(std::equal(inNoteAudio.value().rendered.audio.samples.begin(), inNoteAudio.value().rendered.audio.samples.begin() +
      static_cast<std::ptrdiff_t>(nucleusOffset), sameAudio.value().rendered.audio.samples.begin()));
  CHECK(inNoteAudio.value().rendered.audio.samples != sameAudio.value().rendered.audio.samples);
  syllableRegion->lyrics.front().surface = U"あい";
  syllableRegion->phonemeOverrides = {
      {.key = {f.noteId, 0U}, .timing = {.startOffset = 100000, .endOffset = 200000}, .locked = true},
      {.key = {f.noteId, 1U}, .timing = {.startOffset = 300000, .endOffset = 450000}, .locked = true}};
  const auto retimed = rendering::RenderSnapshotFactory{}.createProcedural(syllables, resource.value(),
      f.trackId, f.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(retimed);
  const auto retimedAudio = rendering::PhraseRenderPipeline{}.render(retimed.value()); CHECK(retimedAudio);
  const auto& pcm = retimedAudio.value().rendered.audio.samples;
  const auto& markers = retimedAudio.value().proceduralMarkers;
  CHECK(markers.size() == 2U);
  CHECK(markers[0].phone == "a"); CHECK(markers[1].phone == "i");
  CHECK(markers[0].key == (domain::PhonemeKey{f.noteId, 0U}));
  CHECK(markers[1].key == (domain::PhonemeKey{f.noteId, 1U}));
  const auto origin = retimedAudio.value().rendered.audio.startFrame;
  CHECK(markers[0].ownedSpan == (synthesis::PhraseFrameRange{origin + 4800, origin + 9600}));
  CHECK(markers[1].ownedSpan == (synthesis::PhraseFrameRange{origin + 14400, origin + 21600}));
  CHECK(!markers[0].startClipped); CHECK(!markers[0].endClipped);
  const auto cropped = rendering::RenderSnapshotFactory{}.splitOwnedOutput(retimed.value(),
      {origin + 6000, origin + 9000}, 3000U); CHECK(cropped);
  const auto croppedAudio = rendering::PhraseRenderPipeline{}.render(cropped.value().front()); CHECK(croppedAudio);
  CHECK(croppedAudio.value().proceduralMarkers.size() == 1U);
  const auto& cropMarker = croppedAudio.value().proceduralMarkers.front();
  CHECK(cropMarker.startClipped); CHECK(cropMarker.endClipped);
  CHECK(cropMarker.key == markers.front().key);
  CHECK(cropMarker.ownedSpan == (synthesis::PhraseFrameRange{origin + 6000, origin + 9000}));
  const auto gap = rendering::RenderSnapshotFactory{}.splitOwnedOutput(retimed.value(),
      {origin + 10000, origin + 12000}, 2000U); CHECK(gap);
  const auto gapAudio = rendering::PhraseRenderPipeline{}.render(gap.value().front()); CHECK(gapAudio);
  CHECK(gapAudio.value().proceduralMarkers.empty());
  CHECK(pcm[4800] == 0.0F); CHECK(pcm[9599] == 0.0F);
  CHECK(pcm[14400] == 0.0F); CHECK(pcm[21599] == 0.0F);
  for (const auto [start, end] : {std::pair{0U, 4800U}, std::pair{9600U, 14400U}, std::pair{21600U, 24000U}})
    CHECK(std::all_of(pcm.begin() + start, pcm.begin() + end, [](float value) { return value == 0.0F; }));
  CHECK(std::any_of(pcm.begin() + 4800, pcm.begin() + 9600, [](float value) { return value != 0.0F; }));
  CHECK(std::any_of(pcm.begin() + 14400, pcm.begin() + 21600, [](float value) { return value != 0.0F; }));
  const synthesis::PhraseFrameRange retimedContext{retimedAudio.value().rendered.audio.startFrame,
      retimedAudio.value().rendered.audio.startFrame + static_cast<time::SampleFrame>(pcm.size())};
  const auto retimedChunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(retimed.value(), retimedContext, 12000U); CHECK(retimedChunks);
  std::vector<float> retimedJoined;
  for (const auto& chunk : retimedChunks.value()) {
    const auto part = rendering::PhraseRenderPipeline{}.render(chunk); CHECK(part);
    retimedJoined.insert(retimedJoined.end(), part.value().rendered.audio.samples.begin(), part.value().rendered.audio.samples.end());
  }
  CHECK(retimedJoined == pcm);
  auto checkpointed = rendering::ProceduralSnapshotStream::create(retimed.value()); CHECK(checkpointed);
  const auto tinyChunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(retimed.value(), retimedContext, 4837U); CHECK(tinyChunks);
  std::vector<float> rampJoined;
  for (const auto& chunk : tinyChunks.value()) {
    const auto part = checkpointed.value().render(chunk); CHECK(part);
    rampJoined.insert(rampJoined.end(), part.value().audio.samples.begin(), part.value().audio.samples.end());
  }
  CHECK(rampJoined == pcm);
  syllableRegion->phonemeOverrides[0].timing.startOffset = -1000;
  CHECK(!rendering::RenderSnapshotFactory{}.createProcedural(syllables, resource.value(), f.trackId, f.regionId,
      1U, rendering::RenderQuality::Preview, 48000U));
  syllableRegion->phonemeOverrides[0].timing.startOffset = 100000;
  syllableRegion->phonemeOverrides[1].timing.endOffset = 600000;
  CHECK(!rendering::RenderSnapshotFactory{}.createProcedural(syllables, resource.value(), f.trackId, f.regionId,
      1U, rendering::RenderQuality::Preview, 48000U));
  syllableRegion->phonemeOverrides.clear();
  syllableRegion->lyrics.front().surface = U"あいあ";
  syllableRegion->notes.front().durationTick = time::Tick{2};
  const auto shortPhrase = rendering::RenderSnapshotFactory{}.createProcedural(syllables, resource.value(),
      f.trackId, f.regionId, 1U, rendering::RenderQuality::Preview, 48000U); CHECK(shortPhrase);
  CHECK(shortPhrase.value().compiledPerformance->phonemeTiming().size() == 3U);
  CHECK(rendering::PhraseRenderPipeline{}.render(shortPhrase.value()));
  recipe.poses.pop_back();
  const auto incomplete = voice_design::freezeVoiceRecipeResource(recipe); CHECK(incomplete);
  CHECK(!rendering::RenderSnapshotFactory{}.createProcedural(f.project, incomplete.value(), f.trackId, f.regionId,
      1U, rendering::RenderQuality::Preview, 48000U));
}

TEST_CASE("scheduler preserves voiced frication marker identity through completion and cache hits") {
  using namespace seam;
  PerformanceSnapshotFixture fixture;
  rendering::PcmCache cache{fixture.bankRoot/"voiced-marker-cache"};
  rendering::BackgroundRenderScheduler scheduler{cache,1U};
  voice_design::ProceduralPhoneMarker marker{{domain::NoteId{1U},0U},"z",{0,32},false,false,
      voice_design::ArticulationGestureKind::VoicedFrication};
  const auto request=[&] {
    return rendering::ScheduledRenderRequest{.phraseId="voiced-marker",.cacheKey="voiced-marker",.revision=1U,.sampleRate=48000U,
        .task=[](std::stop_token)->core::Result<synthesis::PhraseAudio>{return synthesis::PhraseAudio{0,std::vector<float>(32U,0.1F)};},
        .outputFrames=synthesis::PhraseFrameRange{0,32},.proceduralMarkers={marker}};
  };
  CHECK(scheduler.submit(request())); CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto complete=scheduler.drainCompleted(); CHECK(complete.size()==1U);
  CHECK(complete[0].status==rendering::RenderCompletionStatus::Completed);
  CHECK(complete[0].proceduralMarkers==std::vector{marker});
  CHECK(scheduler.submit(request()));
  const auto hit=scheduler.drainCompleted(); CHECK(hit.size()==1U);
  CHECK(hit[0].status==rendering::RenderCompletionStatus::CacheHit);
  CHECK(hit[0].proceduralMarkers==std::vector{marker});
  marker.kind=static_cast<voice_design::ProceduralGestureKind>(255);
  CHECK(!scheduler.submit(request()));
}

TEST_CASE("procedural snapshots render frozen vowel resources without sample banks or fake units") {
  PerformanceSnapshotFixture fixture;
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "procedural-snapshot";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto resource = seam::voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  std::filesystem::rename(fixture.bankRoot / "audio/a.wav", fixture.bankRoot / "audio/unavailable.wav");
  const auto create = [&] {
    return seam::rendering::RenderSnapshotFactory{}.createProcedural(fixture.project, resource.value(),
        fixture.trackId, fixture.regionId, 1U, seam::rendering::RenderQuality::Preview, 48000U);
  };
  const auto originalProject = fixture.project;
  const auto snapshot = create(); CHECK(snapshot);
  CHECK(snapshot.value().sourceProjectId == fixture.project.id());
  CHECK(std::holds_alternative<seam::synthesis::ProceduralSingerResource>(snapshot.value().resource));
  const auto rendered = seam::rendering::PhraseRenderPipeline{}.render(snapshot.value()); CHECK(rendered);
  CHECK(rendered.value().resourceKind == seam::domain::SingerResourceKind::Procedural);
  CHECK(rendered.value().unitPlan.entries.empty()); CHECK(rendered.value().rendered.placements.empty());
  CHECK(!rendered.value().rendered.audio.samples.empty());
  const auto& performance = *snapshot.value().compiledPerformance;
  const seam::synthesis::PhraseFrameRange context{performance.notes().front().startFrame, performance.notes().back().endFrame};
  const auto direct = seam::voice_design::renderSustainedPose(resource.value(), performance, "a", "neutral",
      {48000U, context, context});
  CHECK(direct); CHECK(direct.value().audio.samples == rendered.value().rendered.audio.samples);
  seam::rendering::PcmCache proceduralCache{fixture.bankRoot / "procedural-cache"};
  seam::rendering::BackgroundRenderScheduler scheduler{proceduralCache, 2U};
  CHECK(scheduler.submitSnapshot(snapshot.value()));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto firstCompletion = scheduler.drainCompleted(); CHECK(firstCompletion.size() == 1U);
  CHECK(firstCompletion[0].status == seam::rendering::RenderCompletionStatus::Completed);
  CHECK(firstCompletion[0].pcm); CHECK(firstCompletion[0].pcm->samples == direct.value().audio.samples);
  CHECK(firstCompletion[0].proceduralMarkers == rendered.value().proceduralMarkers);
  CHECK(!firstCompletion[0].proceduralMarkers.empty());
  CHECK(scheduler.submitSnapshot(snapshot.value()));
  const auto cachedCompletion = scheduler.drainCompleted(); CHECK(cachedCompletion.size() == 1U);
  CHECK(cachedCompletion[0].status == seam::rendering::RenderCompletionStatus::CacheHit);
  CHECK(cachedCompletion[0].proceduralMarkers == firstCompletion[0].proceduralMarkers);
  {
    seam::rendering::BackgroundRenderScheduler obsolete{proceduralCache, 1U};
    CHECK(obsolete.submitSnapshot(snapshot.value()));
    CHECK(obsolete.invalidateGroup(obsolete.snapshotGroupId(snapshot.value()), 2U));
    const auto discarded = obsolete.drainCompleted(); CHECK(discarded.size() == 1U);
    CHECK(discarded[0].status == seam::rendering::RenderCompletionStatus::Stale);
    CHECK(!discarded[0].pcm); CHECK(discarded[0].proceduralMarkers.empty());
  }
  auto invalidCached = snapshot.value(); invalidCached.style = "missing-pose";
  CHECK(!scheduler.submitSnapshot(invalidCached));
  invalidCached = snapshot.value(); invalidCached.sampleRate = 44100U;
  CHECK(!scheduler.submitSnapshot(invalidCached));
  invalidCached = snapshot.value();
  std::get<seam::synthesis::ProceduralSingerResource>(invalidCached.resource).identity.contentHash = std::string(64U, 'f');
  CHECK(!scheduler.submitSnapshot(invalidCached));
  const auto midpoint = context.start + (context.end - context.start) / 2;
  const auto planned = seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(), context,
      static_cast<std::uint32_t>(midpoint - context.start));
  CHECK(planned); CHECK(planned.value().size() == 2U);
  auto boundStream = seam::rendering::ProceduralSnapshotStream::create(snapshot.value()); CHECK(boundStream);
  const auto boundFirst = boundStream.value().render(planned.value()[0]); CHECK(boundFirst);
  CHECK(boundFirst.value().processedFrames == static_cast<std::size_t>(midpoint - context.start));
  auto changedContext = planned.value()[1]; ++changedContext.revision;
  CHECK(!boundStream.value().render(changedContext));
  changedContext = planned.value()[1]; changedContext.style = "missing";
  CHECK(!boundStream.value().render(changedContext));
  changedContext = planned.value()[1]; changedContext.sourceProjectId = seam::domain::ProjectId{999U};
  CHECK(!boundStream.value().render(changedContext));
  const auto independentlyCompiled = create(); CHECK(independentlyCompiled);
  changedContext = independentlyCompiled.value(); changedContext.ownedFrames = planned.value()[1].ownedFrames;
  CHECK(!boundStream.value().render(changedContext));
  CHECK(boundStream.value().position() == midpoint);
  std::stop_source checkpointStop; checkpointStop.request_stop();
  CHECK(!boundStream.value().render(planned.value()[1], checkpointStop.get_token()));
  CHECK(boundStream.value().position() == midpoint);
  const auto boundSecond = boundStream.value().render(planned.value()[1]); CHECK(boundSecond);
  CHECK(boundSecond.value().processedFrames == static_cast<std::size_t>(context.end - midpoint));
  auto boundAudio = boundFirst.value().audio.samples;
  boundAudio.insert(boundAudio.end(), boundSecond.value().audio.samples.begin(), boundSecond.value().audio.samples.end());
  CHECK(boundAudio == direct.value().audio.samples);
  seam::rendering::PcmCache streamCache{fixture.bankRoot / "stream-cache"};
  seam::rendering::BackgroundRenderScheduler streamScheduler{streamCache, 2U};
  CHECK(streamScheduler.submitSnapshot(planned.value()[0]));
  CHECK(streamScheduler.waitIdle(std::chrono::seconds{20}));
  CHECK(streamScheduler.stats().proceduralFrames == static_cast<std::uint64_t>(midpoint - context.start));
  CHECK(streamScheduler.submitSnapshot(planned.value()[1]));
  CHECK(streamScheduler.waitIdle(std::chrono::seconds{20}));
  CHECK(streamScheduler.stats().proceduralFrames == static_cast<std::uint64_t>(context.end - context.start));
  const auto streamedCompletions = streamScheduler.drainCompleted();
  const auto streamedAudio = streamScheduler.assembleSnapshotCompletions(planned.value(), streamedCompletions, context);
  CHECK(streamedAudio); CHECK(streamedAudio.value().samples == direct.value().audio.samples);
  seam::rendering::PcmCache reverseCache{fixture.bankRoot / "reverse-stream-cache"};
  seam::rendering::BackgroundRenderScheduler reverseScheduler{reverseCache, 2U};
  CHECK(reverseScheduler.submitSnapshot(planned.value()[1]));
  CHECK(reverseScheduler.waitIdle(std::chrono::seconds{20}));
  CHECK(reverseScheduler.submitSnapshot(planned.value()[0]));
  CHECK(reverseScheduler.waitIdle(std::chrono::seconds{20}));
  const auto reversedCompletions = reverseScheduler.drainCompleted();
  const auto reversedAudio = reverseScheduler.assembleSnapshotCompletions(planned.value(), reversedCompletions, context);
  CHECK(reversedAudio); CHECK(reversedAudio.value().samples == direct.value().audio.samples);
  CHECK(reverseScheduler.stats().proceduralFrames == static_cast<std::uint64_t>(context.end + midpoint - 2 * context.start));
  seam::rendering::PcmCache resetCache{fixture.bankRoot / "reset-stream-cache"};
  seam::rendering::BackgroundRenderScheduler resetScheduler{resetCache, 2U};
  CHECK(resetScheduler.submitSnapshot(planned.value()[0]));
  CHECK(resetScheduler.waitIdle(std::chrono::seconds{20}));
  CHECK(resetScheduler.reset());
  CHECK(resetScheduler.submitSnapshot(planned.value()[1]));
  CHECK(resetScheduler.waitIdle(std::chrono::seconds{20}));
  CHECK(resetScheduler.stats().proceduralFrames == static_cast<std::uint64_t>(context.end + midpoint - 2 * context.start));
  const auto resetCompletion = resetScheduler.drainCompleted(); CHECK(resetCompletion.size() == 1U);
  CHECK(resetCompletion[0].pcm); CHECK(resetCompletion[0].pcm->samples == boundSecond.value().audio.samples);
  auto restrictedStream = seam::rendering::ProceduralSnapshotStream::create(planned.value()[0]); CHECK(restrictedStream);
  CHECK(!restrictedStream.value().render(snapshot.value()));
  CHECK(restrictedStream.value().position() == context.start);
  std::vector<seam::rendering::RenderSnapshot> manifest;
  for (const auto range : {seam::synthesis::PhraseFrameRange{context.start, midpoint},
                           seam::synthesis::PhraseFrameRange{midpoint, context.end}}) {
    const auto chunk = seam::rendering::RenderSnapshotFactory{}.createProcedural(fixture.project, resource.value(),
        fixture.trackId, fixture.regionId, 1U, seam::rendering::RenderQuality::Preview, 48000U, "neutral", range);
    CHECK(chunk);
    const auto& derived = planned.value()[manifest.size()];
    CHECK(derived.contentHash == chunk.value().contentHash);
    CHECK(derived.project.get() == snapshot.value().project.get());
    CHECK(derived.compiledPerformance.get() == snapshot.value().compiledPerformance.get());
    CHECK(std::get<seam::synthesis::ProceduralSingerResource>(derived.resource).patch.get() ==
        std::get<seam::synthesis::ProceduralSingerResource>(snapshot.value().resource).patch.get());
    manifest.push_back(derived);
    CHECK(scheduler.submitSnapshot(derived));
  }
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto completions = scheduler.drainCompleted(); CHECK(completions.size() == 2U);
  const auto assembled = scheduler.assembleSnapshotCompletions(manifest, completions, context);
  CHECK(assembled); CHECK(assembled.value().samples == direct.value().audio.samples);
  auto alteredMarkers = completions;
  CHECK(!alteredMarkers[0].proceduralMarkers.empty());
  alteredMarkers[0].proceduralMarkers.front().phone = "i";
  CHECK(!scheduler.assembleSnapshotCompletions(manifest, alteredMarkers, context));
  CHECK(!seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(planned.value()[0], context, 1000U));
  CHECK(!seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(),
      {context.start - 1, context.end}, 1000U));
  CHECK(!seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(), context, 1U));
  CHECK(create().value().contentHash == snapshot.value().contentHash);
  fixture.project.findNote(fixture.noteId)->midiKey = 72U;
  CHECK(create().value().contentHash != snapshot.value().contentHash);
  CHECK(seam::rendering::PhraseRenderPipeline{}.render(snapshot.value()).value().rendered.audio.samples == direct.value().audio.samples);
  fixture.project = originalProject;
  const auto owned = seam::rendering::RenderSnapshotFactory{}.createProcedural(fixture.project, resource.value(),
      fixture.trackId, fixture.regionId, 1U, seam::rendering::RenderQuality::Preview, 48000U, "neutral",
      seam::synthesis::PhraseFrameRange{context.start + 1000, context.start + 2000});
  CHECK(owned); CHECK(owned.value().contentHash != snapshot.value().contentHash);
  const auto slice = seam::rendering::PhraseRenderPipeline{}.render(owned.value()); CHECK(slice);
  CHECK(slice.value().rendered.audio.samples == std::vector<float>(direct.value().audio.samples.begin() + 1000,
      direct.value().audio.samples.begin() + 2000));
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"か"; CHECK(create()); // Explicit fixture hint remains a supported vowel.
  region->notes.front().phoneticHint.reset(); CHECK(!create());
  region->lyrics.front().surface = U"ああ"; CHECK(create());
  region->lyrics.front().surface = U"あ";
  region->phonemeOverrides = {{.key = {fixture.noteId, 0U}, .timing = {.startOffset = 1000}, .locked = true}};
  CHECK(create()); region->phonemeOverrides.clear();
  region->unitSelectionOverrides = {{.startKey = {fixture.noteId, 0U}, .unitId = "sample-only"}};
  CHECK(!create());
}

TEST_CASE("canonical audio identity does not merge scheduler revisions across source projects") {
  PerformanceSnapshotFixture fixture;
  const auto first = fixture.snapshot();
  {
    // A track that selected a neural bundle must not silently render from the
    // sample bank. The rule mirrors the saved procedural-recipe refusal.
    auto* track = fixture.project.findVocalTrack(fixture.trackId);
    track->neuralResource = seam::domain::NeuralResourceReference{
        {seam::domain::SingerResourceKind::Neural, "neural.bank", "1", std::string(64U, 'a')}};
    const auto segments = seam::rendering::PhraseSegmenter{}.segment(*fixture.project.findRegion(fixture.regionId));
    CHECK(segments);
    CHECK(!seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
        fixture.trackId, segments.value().front(), 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot));
    track->neuralResource.reset();
    CHECK(fixture.snapshot().contentHash == first.contentHash);
  }
  seam::domain::Project other{seam::domain::ProjectId{99999U}, "Independent project", fixture.project.ppq()};
  other.settings() = fixture.project.settings();
  other.tempoMap() = fixture.project.tempoMap();
  other.meterMap() = fixture.project.meterMap();
  other.routing() = fixture.project.routing();
  other.vocalTracks() = fixture.project.vocalTracks();
  const auto second = seam::rendering::RenderSnapshotFactory{}.create(other, fixture.bank,
      fixture.trackId, first.segment, 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot);
  CHECK(second);
  CHECK(first.project->id() == second.value().project->id()); // Deliberate audio canonicalization.
  CHECK(first.contentHash == second.value().contentHash);
  CHECK(first.sourceProjectId == fixture.project.id());
  CHECK(second.value().sourceProjectId == other.id());
  seam::rendering::PcmCache cache{fixture.bankRoot / "project-isolation"};
  seam::rendering::BackgroundRenderScheduler scheduler{cache};
  CHECK(scheduler.snapshotJobId(first) != scheduler.snapshotJobId(second.value()));
  CHECK(scheduler.snapshotGroupId(first) != scheduler.snapshotGroupId(second.value()));
  CHECK(scheduler.invalidateGroup(scheduler.snapshotGroupId(first), 100U));
  CHECK(scheduler.submitSnapshot(second.value()));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto completed = scheduler.drainCompleted(); CHECK(completed.size() == 1U);
  CHECK(completed[0].status == seam::rendering::RenderCompletionStatus::Completed);
  CHECK(scheduler.submitSnapshot(first));
  const auto stale = scheduler.drainCompleted(); CHECK(stale.size() == 1U);
  CHECK(stale[0].status == seam::rendering::RenderCompletionStatus::Stale);
  auto missingIdentity = second.value(); missingIdentity.sourceProjectId = {};
  CHECK(!scheduler.submitSnapshot(missingIdentity));
}

TEST_CASE("owned chunk assembly requires complete finite nonoverlapping output") {
  using namespace seam::synthesis;
  const std::vector<float> a{0.1F, 0.2F}, b{0.3F, 0.4F, 0.5F};
  std::vector<PhraseAudioView> chunks{{0, b}, {-2, a}};
  const auto complete = assemblePhraseOutput({-2, 3}, chunks);
  CHECK(complete); CHECK(complete.value().startFrame == -2);
  CHECK(complete.value().samples == (std::vector<float>{0.1F, 0.2F, 0.3F, 0.4F, 0.5F}));
  CHECK(!assemblePhraseOutput({-2, 3}, std::span<const PhraseAudioView>{chunks}.first(1U)));
  chunks[0].startFrame = 1; CHECK(!assemblePhraseOutput({-2, 3}, chunks));
  chunks[0].startFrame = -1; CHECK(!assemblePhraseOutput({-2, 3}, chunks));
  chunks[0].startFrame = 0;
  chunks.push_back(chunks[1]); CHECK(!assemblePhraseOutput({-2, 3}, chunks));
  chunks.pop_back();
  std::vector<float> invalid{std::numeric_limits<float>::infinity(), 0.4F, 0.5F};
  chunks[0].samples = invalid; CHECK(!assemblePhraseOutput({-2, 3}, chunks));
  chunks[0].samples = b;
  std::stop_source stop; stop.request_stop();
  CHECK(!assemblePhraseOutput({-2, 3}, chunks, stop.get_token()));
  CHECK(!assemblePhraseOutput({-2, 3}, {}));
  CHECK(!assemblePhraseOutput({3, -2}, chunks));
}

TEST_CASE("scheduler reset drops old session work and permits revision zero without late publication") {
  using namespace seam::rendering;
  std::atomic<bool> started{false}, release{false}, queuedRan{false};
  seam::rendering::PcmCache cache{seam::test::support::temporaryDirectory("chunk-session-reset")};
  BackgroundRenderScheduler scheduler{cache, 1U};
  struct ReleaseOnExit {
    std::atomic<bool>& flag;
    ~ReleaseOnExit() { flag.store(true); }
  } guard{release};
  const auto immediate = [](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
    return seam::synthesis::PhraseAudio{0, {0.25F}};
  };
  CHECK(scheduler.submit({.phraseId = "running", .cacheKey = "old-running-pcm", .revision = 90U,
      .sampleRate = 48000U, .priority = RenderPriority::Background,
      .task = [&](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
        started.store(true);
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
        return seam::synthesis::PhraseAudio{0, {0.5F}};
      }, .groupId = "region"}));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!started.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
  CHECK(started.load());
  CHECK(scheduler.submit({.phraseId = "queued", .cacheKey = "queued-pcm", .revision = 90U,
      .sampleRate = 48000U, .priority = RenderPriority::Background,
      .task = [&](std::stop_token token) { queuedRan.store(true); return immediate(token); }, .groupId = "region"}));
  CHECK(cache.store("retained-cache", seam::rendering::CachedPcm{
      .sampleRate = 48000U, .startFrame = 0, .samples = {0.75F}}));
  CHECK(scheduler.submit({.phraseId = "cached", .cacheKey = "retained-cache", .revision = 90U,
      .sampleRate = 48000U, .priority = RenderPriority::Background, .task = immediate, .groupId = "region"}));
  CHECK(scheduler.reset());
  CHECK(scheduler.drainCompleted().empty());
  CHECK(cache.load("retained-cache"));
  CHECK(scheduler.submit({.phraseId = "running", .cacheKey = "new-session-pcm", .revision = 0U,
      .sampleRate = 48000U, .priority = RenderPriority::Background, .task = immediate, .groupId = "region"}));
  release.store(true);
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto result = scheduler.drainCompleted(); CHECK(result.size() == 1U);
  CHECK(result[0].revision == 0U); CHECK(result[0].status == RenderCompletionStatus::Completed);
  CHECK(result[0].pcm); CHECK(result[0].pcm->samples == std::vector<float>{0.25F});
  CHECK(!queuedRan.load()); CHECK(!cache.load("old-running-pcm")); CHECK(!cache.load("queued-pcm"));
  CHECK(scheduler.stats().cancelled == 3U);
  CHECK(scheduler.reset()); CHECK(scheduler.drainCompleted().empty());
}

TEST_CASE("explicit revision invalidation prevents deleted groups from returning old audio") {
  using namespace seam::rendering;
  seam::rendering::PcmCache cache{seam::test::support::temporaryDirectory("deleted-chunk-group")};
  BackgroundRenderScheduler scheduler{cache};
  CHECK(cache.store("cached-window", seam::rendering::CachedPcm{
      .sampleRate = 48000U, .startFrame = 0, .samples = {0.25F}}));
  const auto submit = [&](std::string job, std::string group, std::uint64_t revision) {
    return scheduler.submit({.phraseId = std::move(job), .cacheKey = "cached-window", .revision = revision,
        .sampleRate = 48000U, .priority = RenderPriority::Background,
        .task = [](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
          return seam::synthesis::PhraseAudio{0, {0.25F}};
        }, .groupId = std::move(group)});
  };
  CHECK(submit("deleted-old", "deleted-region", 1U));
  CHECK(submit("unrelated", "other-region", 1U));
  CHECK(scheduler.invalidateGroup("deleted-region", 2U));
  auto ready = scheduler.drainCompleted(); CHECK(ready.size() == 2U);
  for (const auto& item : ready) {
    if (item.phraseId == "deleted-old") { CHECK(item.status == RenderCompletionStatus::Stale); CHECK(!item.pcm); }
    else { CHECK(item.phraseId == "unrelated"); CHECK(item.status == RenderCompletionStatus::CacheHit); CHECK(item.pcm); }
  }
  CHECK(scheduler.invalidateGroup("deleted-region", 1U)); // Never lowers the floor.
  CHECK(submit("late-deleted-window", "deleted-region", 1U));
  CHECK(scheduler.drainCompleted().front().status == RenderCompletionStatus::Stale);
  CHECK(scheduler.invalidateGroup("not-yet-seen", 9U));
  CHECK(submit("late-unseen-window", "not-yet-seen", 8U));
  CHECK(scheduler.drainCompleted().front().status == RenderCompletionStatus::Stale);
  CHECK(submit("restored-window", "deleted-region", 2U));
  CHECK(scheduler.drainCompleted().front().status == RenderCompletionStatus::CacheHit);
  CHECK(!scheduler.invalidateGroup("", 3U));
  CHECK(!scheduler.invalidateGroup(std::string(2049U, 'x'), 3U));
  CHECK(cache.load("cached-window")); // Invalidation does not destroy reusable content.
}

TEST_CASE("phrase revision groups invalidate queued cached and unseen obsolete chunk jobs") {
  seam::rendering::PcmCache cache{seam::test::support::temporaryDirectory("chunk-revision-groups")};
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 2U};
  using namespace seam::rendering;
  const auto task = [](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
    return seam::synthesis::PhraseAudio{0, {0.25F}};
  };
  CHECK(cache.store("old-cached", seam::rendering::CachedPcm{
      .sampleRate = 48000U, .startFrame = 0, .samples = {0.5F}}));
  CHECK(scheduler.submit({.phraseId = "old-window", .cacheKey = "old-cached", .revision = 1U,
      .sampleRate = 48000U, .priority = RenderPriority::Background, .task = task, .groupId = "phrase"}));
  CHECK(scheduler.submit({.phraseId = "new-window", .cacheKey = "new-pcm", .revision = 2U,
      .sampleRate = 48000U, .priority = RenderPriority::Background, .task = task, .groupId = "phrase"}));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto result = scheduler.drainCompleted(); CHECK(result.size() == 2U);
  for (const auto& item : result) {
    if (item.phraseId == "old-window") { CHECK(item.status == RenderCompletionStatus::Stale); CHECK(!item.pcm); }
    else { CHECK(item.phraseId == "new-window"); CHECK(item.status == RenderCompletionStatus::Completed); }
  }
  CHECK(scheduler.submit({.phraseId = "previously-unseen-old-window", .cacheKey = "unused", .revision = 1U,
      .sampleRate = 48000U, .priority = RenderPriority::Background, .task = task, .groupId = "phrase"}));
  const auto late = scheduler.drainCompleted(); CHECK(late.size() == 1U);
  CHECK(late[0].status == RenderCompletionStatus::Stale); CHECK(!cache.load("unused"));
  std::atomic<bool> started{false};
  CHECK(scheduler.submit({.phraseId = "running-old", .cacheKey = "running-old-pcm", .revision = 1U,
      .sampleRate = 48000U, .priority = RenderPriority::Background,
      .task = [&](std::stop_token token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
        started.store(true);
        while (!token.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
        return seam::synthesis::PhraseAudio{0, {0.5F}};
      }, .groupId = "another-phrase"}));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!started.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
  CHECK(started.load());
  CHECK(scheduler.submit({.phraseId = "replacement-window", .cacheKey = "replacement-pcm", .revision = 2U,
      .sampleRate = 48000U, .priority = RenderPriority::Background, .task = task, .groupId = "another-phrase"}));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto cancelled = scheduler.drainCompleted(); CHECK(cancelled.size() == 2U);
  for (const auto& item : cancelled) {
    if (item.phraseId == "running-old") { CHECK(item.status == RenderCompletionStatus::Cancelled); CHECK(!item.pcm); }
    else CHECK(item.status == RenderCompletionStatus::Completed);
  }
  CHECK(!cache.load("running-old-pcm"));
}

TEST_CASE("scheduled sibling snapshot windows coexist and validate cache extents") {
  PerformanceSnapshotFixture fixture;
  const auto full = fixture.snapshot();
  const auto fullAudio = seam::rendering::PhraseRenderPipeline{}.render(full);
  CHECK(fullAudio);
  const auto& audio = fullAudio.value().rendered.audio;
  const auto end = audio.startFrame + static_cast<seam::time::SampleFrame>(audio.samples.size());
  const auto middle = audio.startFrame + static_cast<seam::time::SampleFrame>(audio.samples.size() / 2U);
  const auto create = [&](seam::synthesis::PhraseFrameRange range) {
    return seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank, fixture.trackId,
        full.segment, 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot, 48000U, {}, {}, range);
  };
  const auto left = create({audio.startFrame, middle});
  const auto right = create({middle, end});
  CHECK(left); CHECK(right);
  seam::rendering::PcmCache cache{fixture.bankRoot / "scheduled-chunks"};
  seam::rendering::BackgroundRenderScheduler scheduler{cache, 2U};
  const auto leftId = scheduler.snapshotJobId(left.value());
  const auto rightId = scheduler.snapshotJobId(right.value());
  CHECK(leftId != rightId);
  CHECK(scheduler.submitSnapshot(left.value())); CHECK(scheduler.submitSnapshot(right.value()));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  auto complete = scheduler.drainCompleted();
  CHECK(complete.size() == 2U);
  for (const auto& completion : complete) {
    CHECK(completion.status == seam::rendering::RenderCompletionStatus::Completed);
    CHECK(completion.pcm);
  }
  std::sort(complete.begin(), complete.end(), [](const auto& a, const auto& b) {
    return a.pcm->startFrame < b.pcm->startFrame;
  });
  std::vector<float> joined;
  for (const auto& completion : complete) {
    CHECK(completion.status == seam::rendering::RenderCompletionStatus::Completed);
    CHECK(completion.pcm);
    joined.insert(joined.end(), completion.pcm->samples.begin(), completion.pcm->samples.end());
  }
  CHECK(joined == audio.samples);
  std::vector<seam::synthesis::PhraseAudioView> chunkViews;
  for (const auto& completion : complete) chunkViews.push_back({completion.pcm->startFrame, completion.pcm->samples});
  const auto assembled = seam::synthesis::assemblePhraseOutput({audio.startFrame, end}, chunkViews);
  CHECK(assembled); CHECK(assembled.value().samples == audio.samples);
  const std::vector<seam::rendering::RenderSnapshot> manifest{left.value(), right.value()};
  std::reverse(complete.begin(), complete.end());
  const auto verified = scheduler.assembleSnapshotCompletions(manifest, complete, {audio.startFrame, end});
  CHECK(verified); CHECK(verified.value().samples == audio.samples);
  for (int invalid = 0; invalid < 8; ++invalid) {
    auto altered = complete;
    if (invalid == 0) --altered[0].revision;
    if (invalid == 1) altered[0].cacheKey = "wrong-content";
    if (invalid == 2) altered[0].phraseId = "unknown-job";
    if (invalid == 3) altered[0].status = seam::rendering::RenderCompletionStatus::Stale;
    if (invalid == 4) altered[0].pcm.reset();
    if (invalid == 5) altered[0] = altered[1];
    if (invalid == 6) {
      auto pcm = std::make_shared<seam::rendering::CachedPcm>(*altered[0].pcm);
      pcm->sampleRate = 44100U;
      altered[0].pcm = pcm;
    }
    if (invalid == 7) altered.pop_back();
    CHECK(!scheduler.assembleSnapshotCompletions(manifest, altered, {audio.startFrame, end}));
  }
  auto duplicateManifest = manifest;
  duplicateManifest[1] = duplicateManifest[0];
  CHECK(!scheduler.assembleSnapshotCompletions(duplicateManifest, complete, {audio.startFrame, end}));
  auto mixedRevision = manifest; ++mixedRevision[1].revision;
  CHECK(!scheduler.assembleSnapshotCompletions(mixedRevision, complete, {audio.startFrame, end}));
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!scheduler.assembleSnapshotCompletions(manifest, complete, {audio.startFrame, end}, cancelled.get_token()));
  CHECK(scheduler.submitSnapshot(left.value())); CHECK(scheduler.submitSnapshot(right.value()));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto hits = scheduler.drainCompleted(); CHECK(hits.size() == 2U);
  for (const auto& hit : hits) CHECK(hit.status == seam::rendering::RenderCompletionStatus::CacheHit);
  CHECK(scheduler.assembleSnapshotCompletions(manifest, hits, {audio.startFrame, end}).value().samples == audio.samples);
  CHECK(cache.store(left.value().contentHash, seam::rendering::CachedPcm{
      .sampleRate = 48000U,
      .startFrame = audio.startFrame + 1,
      .samples = {0.0F}}));
  CHECK(!scheduler.submitSnapshot(left.value()));
  auto otherFamily = right.value();
  otherFamily.resource = seam::synthesis::NeuralSingerResource{};
  CHECK(!scheduler.submitSnapshot(otherFamily)); // A cached sample cannot bypass family dispatch.
  CHECK(scheduler.submit({.phraseId = "wrong-extent", .cacheKey = "wrong-extent", .revision = 1U,
      .sampleRate = 48000U, .priority = seam::rendering::RenderPriority::Background,
      .task = [](std::stop_token) -> seam::core::Result<seam::synthesis::PhraseAudio> {
        return seam::synthesis::PhraseAudio{0, {0.0F}};
      }, .outputFrames = seam::synthesis::PhraseFrameRange{0, 2}}));
  CHECK(scheduler.waitIdle(std::chrono::seconds{20}));
  const auto rejected = scheduler.drainCompleted();
  CHECK(rejected.size() == 1U); CHECK(rejected[0].status == seam::rendering::RenderCompletionStatus::Failed);
  CHECK(!cache.load("wrong-extent"));
}

TEST_CASE("owned output planner covers negative and partial intervals without gaps or truncation") {
  using seam::synthesis::PhraseFrameRange;
  const auto planned = seam::synthesis::planOwnedPhraseWindows({-3, 10}, 4U);
  CHECK(planned);
  CHECK(planned.value() == (std::vector<PhraseFrameRange>{{-3, 1}, {1, 5}, {5, 9}, {9, 10}}));
  CHECK(!seam::synthesis::planOwnedPhraseWindows({-3, 10}, 0U));
  CHECK(!seam::synthesis::planOwnedPhraseWindows({-3, 10}, 4U, 3U));
  CHECK(!seam::synthesis::planOwnedPhraseWindows({10, 10}, 4U));
  CHECK(!seam::synthesis::planOwnedPhraseWindows({0, 4097}, 1U));
  const auto single = seam::synthesis::planOwnedPhraseWindows({10, 11}, 100U);
  CHECK(single); CHECK(single.value() == (std::vector<PhraseFrameRange>{{10, 11}}));
}

TEST_CASE("snapshot owned windows retain full musical context and reconstruct complete audio") {
  for (const auto backend : {seam::voicebank::RendererHint::Raw, seam::voicebank::RendererHint::ClassicPsola,
      seam::voicebank::RendererHint::SpectralClassic, seam::voicebank::RendererHint::Stretch}) {
    PerformanceSnapshotFixture fixture;
    auto* region = fixture.project.findRegion(fixture.regionId);
    region->notes.front().articulation = seam::domain::NoteArticulation::Legato;
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{2880}, seam::time::Tick{960},
        76U, U"あ", seam::domain::Language::Japanese);
    note.lyricTokenId = region->notes.front().lyricTokenId;
    note.articulation = seam::domain::NoteArticulation::Legato;
    region->notes.push_back(note);
    auto& unit = fixture.bank.units.front();
    unit.renderer = backend;
    for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
      unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
    }
    const auto full = fixture.snapshot();
    CHECK(!full.compiledPerformance->notes()[1].reattack);
    const auto audio = seam::rendering::PhraseRenderPipeline{}.render(full);
    CHECK(audio);
    const auto& original = audio.value().rendered.audio;
    const auto end = original.startFrame + static_cast<seam::time::SampleFrame>(original.samples.size());
    const auto middle = original.startFrame + static_cast<seam::time::SampleFrame>(original.samples.size() / 2U);
    const auto create = [&](seam::synthesis::PhraseFrameRange range) {
      return seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank, fixture.trackId,
          full.segment, 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot, 48000U, {}, {}, range);
    };
    const auto left = create({original.startFrame, middle});
    const auto right = create({middle, end});
    CHECK(left); CHECK(right);
    CHECK(left.value().contentHash != right.value().contentHash);
    CHECK(left.value().contentHash != full.contentHash);
    CHECK(left.value().phonemes->tokens == full.phonemes->tokens);
    CHECK(right.value().phonemes->tokens == full.phonemes->tokens);
    CHECK(*left.value().project == *full.project);
    const auto leftAudio = seam::rendering::PhraseRenderPipeline{}.render(left.value());
    const auto rightAudio = seam::rendering::PhraseRenderPipeline{}.render(right.value());
    CHECK(leftAudio); CHECK(rightAudio);
    CHECK(leftAudio.value().rendered.audio.startFrame == original.startFrame);
    CHECK(rightAudio.value().rendered.audio.startFrame == middle);
    auto joined = leftAudio.value().rendered.audio.samples;
    const auto& tail = rightAudio.value().rendered.audio.samples;
    joined.insert(joined.end(), tail.begin(), tail.end());
    CHECK(joined == original.samples);
    CHECK(create({original.startFrame, middle}).value().contentHash == left.value().contentHash);
    CHECK(!create({middle, middle}));
    const auto outside = create({original.startFrame - 1, end});
    CHECK(outside); CHECK(!seam::rendering::PhraseRenderPipeline{}.render(outside.value()));
    const auto chunkSize = static_cast<std::uint32_t>((original.samples.size() + 1U) / 2U);
    const auto reference = create({original.startFrame, original.startFrame + chunkSize});
    CHECK(reference);
    std::filesystem::rename(fixture.bankRoot / "audio/a.wav", fixture.bankRoot / "audio/a-hidden.wav");
    const auto automatic = seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(full,
        {original.startFrame, end}, chunkSize);
    CHECK(automatic); CHECK(automatic.value().size() == 2U);
    CHECK(automatic.value()[0].contentHash == reference.value().contentHash);
    std::vector<float> reconstructed;
    for (const auto& chunk : automatic.value()) {
      CHECK(chunk.project.get() == full.project.get());
      CHECK(chunk.compiledPerformance.get() == full.compiledPerformance.get());
      CHECK(chunk.sample().frozenAudio[0].audio.get() == full.sample().frozenAudio[0].audio.get());
      const auto rendered = seam::rendering::PhraseRenderPipeline{}.render(chunk);
      CHECK(rendered);
      const auto& pcm = rendered.value().rendered.audio.samples;
      reconstructed.insert(reconstructed.end(), pcm.begin(), pcm.end());
    }
    CHECK(reconstructed == original.samples);
    CHECK(!seam::rendering::RenderSnapshotFactory{}.splitOwnedOutput(automatic.value()[0],
        {original.startFrame, end}, chunkSize));
  }
}

TEST_CASE("backend output ownership preserves absolute frames and rejects incomplete context") {
  using seam::synthesis::PhraseOutputContract;
  using seam::synthesis::PhraseAudio;
  const PhraseOutputContract contract{48000U, {-2, 6}, {0, 4}};
  const PhraseAudio context{-2, {-0.8F, -0.6F, -0.4F, -0.2F, 0.0F, 0.2F, 0.4F, 0.6F}};
  const auto owned = seam::synthesis::finalizePhraseBackendAudio(contract, context);
  CHECK(owned); CHECK(owned.value().startFrame == 0);
  CHECK(owned.value().samples == (std::vector<float>{-0.4F, -0.2F, 0.0F, 0.2F}));
  const auto full = seam::synthesis::finalizePhraseBackendAudio({48000U, {-2, 6}, {-2, 6}}, context);
  CHECK(full); CHECK(full.value().startFrame == context.startFrame); CHECK(full.value().samples == context.samples);
  auto shortAudio = context; shortAudio.samples.pop_back();
  CHECK(!seam::synthesis::finalizePhraseBackendAudio(contract, shortAudio));
  auto shifted = context; shifted.startFrame = -1;
  CHECK(!seam::synthesis::finalizePhraseBackendAudio(contract, shifted));
  auto invalid = context; invalid.samples.front() = std::numeric_limits<float>::quiet_NaN();
  CHECK(!seam::synthesis::finalizePhraseBackendAudio(contract, invalid)); // Even discarded context must be valid.
  CHECK(!(PhraseOutputContract{0U, {-2, 6}, {0, 4}}.validate()));
  CHECK(!(PhraseOutputContract{48000U, {-2, 6}, {-3, 4}}.validate()));
  CHECK(!(PhraseOutputContract{48000U, {-2, 6}, {4, 4}}.validate()));
  CHECK(!(PhraseOutputContract{48000U, {std::numeric_limits<seam::time::SampleFrame>::min(),
      std::numeric_limits<seam::time::SampleFrame>::max()}, {0, 4}}.validate()));
  std::stop_source stop; stop.request_stop();
  CHECK(!seam::synthesis::finalizePhraseBackendAudio(contract, context, stop.get_token()));
  // Adjacent chunks consume context but publish each sample only once.
  const auto left = seam::synthesis::finalizePhraseBackendAudio({48000U, {-2, 6}, {-2, 2}}, context);
  const auto right = seam::synthesis::finalizePhraseBackendAudio({48000U, {-2, 6}, {2, 6}}, context);
  CHECK(left); CHECK(right);
  auto joined = left.value().samples;
  joined.insert(joined.end(), right.value().samples.begin(), right.value().samples.end());
  CHECK(joined == context.samples);
}

TEST_CASE("non-sample resources freeze verified private bytes and reject mismatched identities") {
  std::vector<std::byte> source{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  const auto hash = seam::core::sha256Hex(std::span<const std::byte>{source});
  seam::domain::SingerResourceIdentity identity{seam::domain::SingerResourceKind::Procedural, "fixture", "1", hash};
  const auto patch = seam::synthesis::freezeProceduralResource(identity, source);
  CHECK(patch); CHECK(patch.value().validate());
  CHECK(patch.value().patch->sha256() == hash);
  identity.kind = seam::domain::SingerResourceKind::Neural;
  const auto model = seam::synthesis::freezeNeuralResource(identity, source);
  CHECK(model); CHECK(model.value().validate());
  CHECK(model.value().model->bytes().data() != source.data());
  source.front() = std::byte{99};
  CHECK(patch.value().patch->bytes().front() == std::byte{1});
  CHECK(model.value().model->bytes().front() == std::byte{1});
  CHECK(!seam::synthesis::freezeNeuralResource(identity, source));
  source.front() = std::byte{1};
  CHECK(!seam::synthesis::freezeProceduralResource(identity, source));
  auto wrong = model.value();
  wrong.identity.contentHash = std::string(64U, 'a');
  CHECK(!wrong.validate());
  wrong = model.value(); wrong.identity.kind = seam::domain::SingerResourceKind::Sample;
  CHECK(!wrong.validate());
  wrong = model.value(); wrong.model.reset(); CHECK(!wrong.validate());
  CHECK(!seam::synthesis::FrozenSingerData::freeze(source, hash, 3U));
  CHECK(!seam::synthesis::FrozenSingerData::freeze(source, "bad", source.size()));
  CHECK(!seam::synthesis::FrozenSingerData::freeze({}, hash, source.size()));
  CHECK(!seam::synthesis::FrozenSingerData::freeze(source, hash, 512U * 1024U * 1024U + 1U));
  std::stop_source stop; stop.request_stop();
  CHECK(!seam::synthesis::freezeNeuralResource(identity, source, stop.get_token()));
  PerformanceSnapshotFixture fixture;
  auto snapshot = fixture.snapshot();
  snapshot.resource = patch.value();
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(snapshot));
  snapshot.resource = model.value();
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(snapshot));
}

TEST_CASE("unconfigured resource variants reject without sample fallback") {
  PerformanceSnapshotFixture fixture;
  const auto original = fixture.snapshot();
  CHECK(std::holds_alternative<seam::synthesis::SampleSingerResource>(original.resource));
  CHECK(original.sample().voicebank);
  CHECK(original.sample().unitPlan);
  CHECK(!original.sample().frozenAudio.empty());
  for (const bool neural : {false, true}) {
    auto mismatched = original;
    if (neural) mismatched.resource = seam::synthesis::NeuralSingerResource{
        {seam::domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')}};
    else mismatched.resource = seam::synthesis::ProceduralSingerResource{
        {seam::domain::SingerResourceKind::Procedural, "fixture", "1", std::string(64U, 'a')}};
    const auto rendered = seam::rendering::PhraseRenderPipeline{}.render(mismatched);
    CHECK(!rendered); CHECK(rendered.error().code == seam::core::ErrorCode::Unsupported);
    CHECK(std::holds_alternative<seam::synthesis::SampleSingerResource>(original.resource));
  }
  CHECK(seam::rendering::PhraseRenderPipeline{}.render(original));
}

TEST_CASE("sample resource freezes renderer options and binds them to cache identity") {
  PerformanceSnapshotFixture fixture;
  const auto original = fixture.snapshot();
  const auto originalAudio = seam::rendering::PhraseRenderPipeline{}.render(original);
  CHECK(originalAudio);
  auto options = seam::synthesis::PhraseRenderOptions{};
  options.renderer.raw.additionalGainDb = -6.0F;
  const auto create = [&] {
    return seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
        fixture.trackId, original.segment, 1U, seam::rendering::RenderQuality::Preview,
        fixture.bankRoot, 48000U, {}, options);
  };
  const auto quiet = create();
  CHECK(quiet);
  CHECK(quiet.value().sample().renderOptions.renderer.raw.additionalGainDb == -6.0F);
  CHECK(quiet.value().contentHash != original.contentHash);
  options.renderer.raw.additionalGainDb = 0.0F;
  CHECK(create().value().contentHash == original.contentHash);
  const auto quietAudio = seam::rendering::PhraseRenderPipeline{}.render(quiet.value());
  CHECK(quietAudio);
  CHECK(quiet.value().sample().renderOptions.renderer.raw.additionalGainDb == -6.0F);
  const auto& baseline = originalAudio.value().rendered.audio.samples;
  const auto& samples = quietAudio.value().rendered.audio.samples;
  CHECK(samples.size() == baseline.size());
  CHECK(samples != baseline);
  const auto gain = std::pow(10.0, -6.0 / 20.0);
  for (std::size_t i = 0; i < samples.size(); ++i) CHECK_NEAR(samples[i], baseline[i] * gain, 0.000001);
}

TEST_CASE("production region sums independent overlapping voices and reuses voice caches") {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().vibrato.enabled = false;
  for (const auto pitch : {72U, 76U}) {
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{1920},
        seam::time::Tick{960}, static_cast<std::uint8_t>(pitch), U"あ", seam::domain::Language::Japanese);
    region->lyrics.push_back(lyric);
    region->notes.push_back(note);
  }
  const auto before = fixture.project;
  seam::rendering::ProductionRegionRenderer renderer;
  seam::rendering::PcmCache cache{fixture.bankRoot / "poly-cache"};
  const auto render = [&](const auto& project, seam::rendering::PcmCache* targetCache) {
    return renderer.render(project, fixture.bank, fixture.bankRoot, fixture.trackId,
        fixture.regionId, 1U, 48000U, seam::rendering::RenderQuality::Preview, {}, {}, targetCache);
  };
  const auto combined = render(fixture.project, &cache);
  if (!combined) throw std::runtime_error(combined.error().message + ": " + combined.error().context);
  CHECK(combined.value().phrases.size() == 3U);
  CHECK(combined.value().unitCount == 3U);
  CHECK(combined.value().cacheHits == 0U);
  std::vector<float> expected;
  for (const auto& note : region->notes) {
    auto solo = fixture.project;
    solo.findRegion(fixture.regionId)->notes = {note};
    const auto audio = render(solo, nullptr);
    CHECK(audio);
    expected.resize(std::max(expected.size(), audio.value().mono.size()), 0.0F);
    for (std::size_t i = 0; i < audio.value().mono.size(); ++i) expected[i] += audio.value().mono[i];
  }
  for (auto& sample : expected) sample = std::clamp(sample, -1.0F, 1.0F);
  CHECK(combined.value().mono == expected);
  const auto cached = render(fixture.project, &cache);
  CHECK(cached); CHECK(cached.value().cacheHits == 3U);
  CHECK(cached.value().mono == expected);
  CHECK(fixture.project == before);
  std::reverse(region->notes.begin(), region->notes.end());
  const auto reordered = render(fixture.project, &cache);
  CHECK(reordered); CHECK(reordered.value().mono == expected);
  CHECK(reordered.value().cacheHits == 3U);
  std::stop_source stop;
  stop.request_stop();
  CHECK(!renderer.render(fixture.project, fixture.bank, fixture.bankRoot, fixture.trackId,
      fixture.regionId, 1U, 48000U, seam::rendering::RenderQuality::Preview, {}, {}, &cache, stop.get_token()));
  region->unitSelectionOverrides = {{.startKey = {region->notes.back().id, 0U},
      .tokenCount = 2U, .unitId = "aa"}};
  CHECK(!render(fixture.project, nullptr));
}

TEST_CASE("overlapping singer voices resolve continuation from their own predecessor") {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().vibrato.enabled = false;
  auto [otherLyric, otherNote] = fixture.factory.makeNote(seam::time::Tick{2160},
      seam::time::Tick{1920}, 72U, U"お", seam::domain::Language::Japanese);
  region->lyrics.push_back(otherLyric); region->notes.push_back(otherNote);
  auto [heldLyric, heldNote] = fixture.factory.makeNote(seam::time::Tick{2880},
      seam::time::Tick{960}, 76U, U"-", seam::domain::Language::Japanese);
  region->lyrics.push_back(heldLyric); region->notes.push_back(heldNote);
  auto o = fixture.bank.units.front();
  o.id = "o"; o.phones = {"o"};
  fixture.bank.units.push_back(o);
  const auto original = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(original);
  CHECK(original.value().pronunciation.tokens.back().symbol == "o");
  const auto audio = seam::rendering::ProductionRegionRenderer{}.render(fixture.project,
      fixture.bank, fixture.bankRoot, fixture.trackId, fixture.regionId, 1U, 48000U);
  if (!audio) throw std::runtime_error(audio.error().message + ": " + audio.error().context);
  CHECK(audio.value().unitPlan.size() == 3U);
  CHECK(audio.value().unitPlan[0].unitId == "a");
  CHECK(audio.value().unitPlan[1].unitId == "a");
  CHECK(audio.value().unitPlan[2].unitId == "o");
}

TEST_CASE("production region clips preutterance at frame zero without shifting vowel audio") {
  for (const auto sampleRate : {44100U, 48000U}) {
    for (bool explicitOnset : {false, true}) {
      PerformanceSnapshotFixture fixture;
      auto* region = fixture.project.findRegion(fixture.regionId);
      region->startTick = seam::time::Tick{0};
      region->notes.front().startTick = seam::time::Tick{0};
      region->lyrics.front().surface = U"か";
      region->notes.front().phoneticHint.reset(); // Exercise CV preutterance, not the fixture's explicit vowel hint.
      fixture.bank.units.front().phones = {"k", "a"};
      fixture.bank.units.front().kind = seam::voicebank::UnitKind::Cv;
      if (explicitOnset) region->phonemeOverrides = {{.key = {fixture.noteId, 0U},
          .timing = {.startOffset = -45000}, .locked = true}};
      const auto before = fixture.project;
      const auto segments = seam::rendering::PhraseSegmenter{}.segment(*region);
      CHECK(segments); CHECK(segments.value().size() == 1U);
      const auto snapshot = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
          fixture.trackId, segments.value().front(), 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot, sampleRate);
      CHECK(snapshot);
      CHECK(snapshot.value().phonemes->tokens.size() == 2U);
      CHECK(snapshot.value().phonemes->tokens[0].symbol == "k"); CHECK(snapshot.value().phonemes->tokens[1].symbol == "a");
      const auto phrase = seam::rendering::PhraseRenderPipeline{}.render(snapshot.value());
      CHECK(phrase);
      const auto& audio = phrase.value().rendered.audio;
      CHECK(audio.startFrame < 0);
      CHECK(phrase.value().rendered.placements.front().vowelOnset == 0);
      const auto clipped = static_cast<std::size_t>(-audio.startFrame);
      CHECK(clipped < audio.samples.size());
      seam::rendering::PcmCache cache{fixture.bankRoot / "cache"};
      seam::rendering::ProductionRegionRenderer renderer;
      const auto render = [&] { return renderer.render(fixture.project, fixture.bank, fixture.bankRoot,
          fixture.trackId, fixture.regionId, 1U, sampleRate, seam::rendering::RenderQuality::Preview,
          {}, {}, &cache); };
      const auto fresh = render();
      CHECK(fresh); CHECK(fresh.value().cacheHits == 0U);
      CHECK(fresh.value().mono.size() == audio.samples.size() - clipped);
      for (std::size_t i = 0U; i < fresh.value().mono.size(); ++i) {
        CHECK(fresh.value().mono[i] == std::clamp(audio.samples[i + clipped], -1.0F, 1.0F));
      }
      cache.clearMemory();
      const auto cached = render();
      CHECK(cached); CHECK(cached.value().cacheHits == 1U);
      CHECK(cached.value().mono == fresh.value().mono);
      CHECK(fixture.project == before);
    }
  }
}

TEST_CASE("explicit release renders past the note but remains inside its region") {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->phonemeOverrides = {{.key = {fixture.noteId, 0U}, .timing = {.endOffset = 1000000}, .locked = true}};
  const auto before = fixture.project;
  const auto snapshot = fixture.snapshot();
  const auto audio = seam::rendering::PhraseRenderPipeline{}.render(snapshot);
  CHECK(audio);
  const auto noteStart = fixture.project.tempoMap().sampleFrameAt(region->startTick + region->notes.front().startTick, 48000.0);
  const auto& phrase = audio.value().rendered.audio;
  CHECK(phrase.startFrame + static_cast<seam::time::SampleFrame>(phrase.samples.size()) == noteStart + 48000);
  CHECK(fixture.project == before);
}

TEST_CASE("snapshot freezes audio-bound source alignment and includes it in cache identity") {
  PerformanceSnapshotFixture fixture;
  const auto baseline = fixture.snapshot();
  CHECK(!baseline.sample().frozenAudio.front().sourceAlignment);
  const auto bytes = seam::core::readFileBytesLimited(fixture.bankRoot / "audio/a.wav", 1024U * 1024U);
  CHECK(bytes);
  const auto hash = seam::core::sha256Hex(bytes.value());
  const auto& unit = fixture.bank.units.front();
  seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash, {{"a", 3600}}};
  const auto path = fixture.bankRoot / "alignments" / (seam::core::sha256Hex(unit.id) + ".json");
  std::filesystem::create_directories(path.parent_path());
  const auto write = [&](const auto& value, const auto& verifiedHash) {
    const auto json = seam::synthesis::encodeSourcePhonemeAlignment(value, unit, verifiedHash, 24000);
    CHECK(json); CHECK(seam::core::durableAtomicWriteText(path, json.value()));
  };
  write(alignment, hash);
  const auto frozen = fixture.snapshot();
  CHECK(frozen.sample().frozenAudio.front().sourceAlignment.has_value());
  CHECK(*frozen.sample().frozenAudio.front().sourceAlignment == alignment);
  CHECK(frozen.sample().frozenAudio.front().verifiedAudioSha256 == hash);
  CHECK(frozen.contentHash != baseline.contentHash);
  CHECK(!frozen.sample().selectedUnits.front().sourceAlignmentSha256.empty());
  alignment.landmarks.front().frame = 4000;
  write(alignment, hash);
  const auto updated = fixture.snapshot();
  CHECK(updated.contentHash != frozen.contentHash);
  CHECK(frozen.sample().frozenAudio.front().sourceAlignment->landmarks.front().frame == 3600);
  alignment.audioSha256 = std::string(64U, 'b');
  write(alignment, alignment.audioSha256);
  const auto segments = seam::rendering::PhraseSegmenter{}.segment(*fixture.project.findRegion(fixture.regionId));
  CHECK(segments);
  const auto create = [&] { return seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
      fixture.trackId, segments.value().front(), 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot); };
  CHECK(!create());
  CHECK(std::filesystem::remove(path));
  std::filesystem::create_symlink(fixture.bankRoot / "audio/a.wav", path);
  CHECK(!create());
}

TEST_CASE("frozen source alignment renders both nuclei and survives sidecar removal") {
  for (const bool crossNote : {false, true}) {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"ああ";
  region->notes.front().phoneticHint.reset();
  region->notes.front().vibrato.enabled = false;
  seam::domain::PhonemeKey editedKey{fixture.noteId, 1U};
  const auto editedOffset = crossNote ? 30000 : 280000;
  if (crossNote) {
    region->lyrics.front().surface = U"あ";
    region->notes.front().durationTick = seam::time::Tick{480};
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{2400},
        seam::time::Tick{480}, 69, U"あ", seam::domain::Language::Japanese);
    editedKey = {note.id, 0U};
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  CHECK(region->dynamicsAutomation.replacePoints({}));
  fixture.bank.units.front().phones = {"a", "a"};
  std::vector<float> samples(24000U, 0.0F);
  samples[3600] = 0.5F; samples[15000] = -0.75F;
  CHECK(seam::voicebank::writeMonoPcm16Wav(fixture.bankRoot / "audio/a.wav", 48000U, samples));
  const auto bytes = seam::core::readFileBytesLimited(fixture.bankRoot / "audio/a.wav", 1024U * 1024U);
  CHECK(bytes);
  const auto hash = seam::core::sha256Hex(bytes.value());
  const auto& unit = fixture.bank.units.front();
  const seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash, {{"a", 3600}, {"a", 15000}}};
  const auto json = seam::synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash, 24000);
  CHECK(json);
  const auto path = fixture.bankRoot / "alignments" / (seam::core::sha256Hex(unit.id) + ".json");
  std::filesystem::create_directories(path.parent_path());
  CHECK(seam::core::durableAtomicWriteText(path, json.value()));
  const auto snapshot = fixture.snapshot();
  seam::rendering::PcmCache editCache{fixture.bankRoot / "edit-cache"};
  const auto renderRegion = [&] { return seam::rendering::ProductionRegionRenderer{}.render(
      fixture.project, fixture.bank, fixture.bankRoot, fixture.trackId, fixture.regionId,
      1U, 48000U, seam::rendering::RenderQuality::Preview, {}, {}, &editCache); };
  const auto originalRegion = renderRegion();
  CHECK(originalRegion);
  seam::application::UpsertPhonemeOverrideCommand edit{fixture.regionId,
      {.key = editedKey, .timing = {.startOffset = editedOffset}, .locked = true}};
  CHECK(edit.apply(fixture.project));
  const auto editedSnapshot = fixture.snapshot();
  CHECK(editedSnapshot.sample().unitPlan->entries.size() == 1U);
  CHECK(editedSnapshot.sample().unitPlan->entries.front().unitId == unit.id);
  CHECK(editedSnapshot.contentHash != snapshot.contentHash);
  const auto editedRegion = renderRegion();
  CHECK(editedRegion);
  CHECK(editedRegion.value().cacheHits == 0U);
  CHECK(editedRegion.value().mono != originalRegion.value().mono);
  editCache.clearMemory();
  const auto replayedRegion = renderRegion();
  CHECK(replayedRegion);
  CHECK(replayedRegion.value().cacheHits == 1U);
  CHECK(replayedRegion.value().mono == editedRegion.value().mono);
  CHECK(edit.revert(fixture.project));
  CHECK(fixture.snapshot().contentHash == snapshot.contentHash);
  const auto undoneRegion = renderRegion();
  CHECK(undoneRegion);
  CHECK(undoneRegion.value().mono == originalRegion.value().mono);
  CHECK(edit.apply(fixture.project));
  CHECK(fixture.snapshot().contentHash == editedSnapshot.contentHash);
  const auto redoneRegion = renderRegion();
  CHECK(redoneRegion);
  CHECK(redoneRegion.value().mono == editedRegion.value().mono);
  CHECK(edit.revert(fixture.project));
  if (!crossNote) {
    seam::application::UpsertPhonemeOverrideCommand laterSecond{fixture.regionId,
        {.key = editedKey, .timing = {.startOffset = 400000}, .locked = true}};
    seam::application::UpsertPhonemeOverrideCommand laterFirst{fixture.regionId,
        {.key = {fixture.noteId, 0U}, .timing = {.startOffset = 300000}, .locked = true}};
    CHECK(laterSecond.apply(fixture.project));
    CHECK(laterFirst.apply(fixture.project));
    const auto delayedSnapshot = fixture.snapshot();
    const auto delayed = seam::rendering::PhraseRenderPipeline{}.render(delayedSnapshot);
    CHECK(delayed);
    const auto& targets = delayed.value().timing.placements.front().phonemeTargets;
    CHECK(targets[0].endFrame == targets[1].nucleusFrame);
    CHECK(targets[1].nucleusFrame - targets[0].nucleusFrame == 4800);
    const auto& delayedAudio = delayed.value().rendered.audio;
    const auto frozenSamples = delayedSnapshot.sample().frozenAudio.front().audio->monoMix();
    CHECK(delayedAudio.samples[static_cast<std::size_t>(targets[0].nucleusFrame - delayedAudio.startFrame)] == frozenSamples[3600]);
    CHECK(delayedAudio.samples[static_cast<std::size_t>(targets[1].nucleusFrame - delayedAudio.startFrame)] == frozenSamples[15000]);
    CHECK(laterFirst.revert(fixture.project));
    CHECK(laterSecond.revert(fixture.project));
    CHECK(fixture.snapshot().contentHash == snapshot.contentHash);
  }
  CHECK(std::filesystem::remove(path));
  const auto rendered = seam::rendering::PhraseRenderPipeline{}.render(snapshot);
  CHECK(rendered);
  CHECK(rendered.value().rendered.placements.size() == 1U);
  const auto& placement = rendered.value().timing.placements.front();
  const auto& output = rendered.value().rendered.audio;
  const auto source = snapshot.sample().frozenAudio.front().audio->monoMix();
  CHECK(output.samples[static_cast<std::size_t>(placement.phonemeTargets[0].nucleusFrame - output.startFrame)] == source[3600]);
  CHECK(output.samples[static_cast<std::size_t>(placement.phonemeTargets[1].nucleusFrame - output.startFrame)] == source[15000]);
  CHECK(rendered.value().rendered.placements.front().diagnostic.find("Source-aligned") != std::string::npos);
  const auto savedEditRender = seam::rendering::PhraseRenderPipeline{}.render(editedSnapshot);
  CHECK(savedEditRender);
  const auto& savedEditPlacement = savedEditRender.value().timing.placements.front();
  CHECK(savedEditPlacement.phonemeTargets[1].nucleusFrame == placement.phonemeTargets[1].nucleusFrame + 1440);
  const auto& savedEditAudio = savedEditRender.value().rendered.audio;
  CHECK(savedEditAudio.samples[static_cast<std::size_t>(savedEditPlacement.phonemeTargets[1].nucleusFrame - savedEditAudio.startFrame)] == source[15000]);
  auto edited = snapshot;
  auto editedPhones = std::make_shared<seam::phonemizer::Result>(*snapshot.phonemes);
  editedPhones->tokens[1].timing.startOffset = editedOffset;
  edited.phonemes = editedPhones;
  const auto& frozen = snapshot.sample().frozenAudio.front();
  const std::array evidence{seam::synthesis::SourceAlignmentEvidence{
      &*frozen.sourceAlignment, frozen.verifiedAudioSha256,
      static_cast<seam::time::SampleFrame>(frozen.audio->frameCount())}};
  const auto* frozenRegion = snapshot.project->findRegion(fixture.regionId);
  const auto selected = seam::synthesis::DeterministicUnitSelector{}.select(
      *snapshot.sample().voicebank, *frozenRegion, editedPhones->tokens, snapshot.style, {}, evidence);
  CHECK(selected);
  CHECK(selected.value().entries.size() == 1U);
  edited.sample().unitPlan = std::make_shared<const seam::synthesis::UnitPlan>(selected.value());
  const auto moved = seam::rendering::PhraseRenderPipeline{}.render(edited);
  CHECK(moved);
  const auto& movedPlacement = moved.value().timing.placements.front();
  CHECK(movedPlacement.phonemeTargets[0].nucleusFrame == placement.phonemeTargets[0].nucleusFrame);
  CHECK(movedPlacement.phonemeTargets[1].nucleusFrame == placement.phonemeTargets[1].nucleusFrame + 1440);
  const auto& movedAudio = moved.value().rendered.audio;
  CHECK(movedAudio.samples[static_cast<std::size_t>(movedPlacement.phonemeTargets[1].nucleusFrame - movedAudio.startFrame)] == source[15000]);
  auto staleEvidence = evidence;
  staleEvidence[0].verifiedAudioSha256 = std::string_view{};
  CHECK(!seam::synthesis::DeterministicUnitSelector{}.select(
      *snapshot.sample().voicebank, *frozenRegion, editedPhones->tokens, snapshot.style, {}, staleEvidence));
  edited.sample().frozenAudio.front().sourceAlignment.reset();
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(edited));
  auto unsupported = snapshot;
  unsupported.sample().renderOptions.renderer.policy = seam::synthesis::RenderPolicy::ForceClassicPsola;
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(unsupported));
  auto explicitRaw = std::make_shared<seam::synthesis::UnitPlan>(*snapshot.sample().unitPlan);
  explicitRaw->entries.front().renderer = seam::domain::UnitRendererKind::Raw;
  unsupported.sample().unitPlan = explicitRaw;
  CHECK(seam::rendering::PhraseRenderPipeline{}.render(unsupported));
  auto explicitPsola = std::make_shared<seam::synthesis::UnitPlan>(*snapshot.sample().unitPlan);
  explicitPsola->entries.front().renderer = seam::domain::UnitRendererKind::ClassicPsola;
  unsupported.sample().unitPlan = explicitPsola;
  unsupported.sample().renderOptions.renderer.policy = seam::synthesis::RenderPolicy::ForceRaw;
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(unsupported));
  unsupported = snapshot;
  unsupported.sample().renderOptions.renderer.raw.loopPrint = 0.5F;
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(unsupported));
  unsupported = snapshot;
  unsupported.sample().renderOptions.renderer.raw.additionalGainDb = 3.0F;
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(unsupported));
  if (crossNote) {
    auto changedPitch = snapshot;
    auto changedProject = std::make_shared<seam::domain::Project>(*snapshot.project);
    changedProject->findRegion(fixture.regionId)->findNote(editedKey.noteId)->midiKey = 72;
    changedPitch.project = changedProject;
    const auto rejected = seam::rendering::PhraseRenderPipeline{}.render(changedPitch);
    CHECK(!rejected);
    CHECK(rejected.error().message.find("pitch") != std::string::npos);
  }
  }
}

TEST_CASE("snapshot uses smaller units instead of unaligned multiple nuclei") {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"ああ";
  region->notes.front().phoneticHint.reset();
  auto longUnit = fixture.bank.units.front();
  longUnit.id = "long-aa";
  longUnit.phones = {"a", "a"};
  fixture.bank.units.push_back(longUnit);
  const auto snapshot = fixture.snapshot();
  CHECK(snapshot.phonemes->tokens.size() == 2U);
  CHECK(snapshot.sample().unitPlan->entries.size() == 2U);
  for (const auto& entry : snapshot.sample().unitPlan->entries) CHECK(entry.unitId == "a");
  CHECK(seam::rendering::PhraseRenderPipeline{}.render(snapshot));
  const auto segments = seam::rendering::PhraseSegmenter{}.segment(*region);
  CHECK(segments);
  const auto create = [&] { return seam::rendering::RenderSnapshotFactory{}.create(
      fixture.project, fixture.bank, fixture.trackId, segments.value().front(), 1U,
      seam::rendering::RenderQuality::Preview, fixture.bankRoot); };
  region->unitSelectionOverrides = {{.startKey = {fixture.noteId, 0U},
      .tokenCount = 2U, .unitId = longUnit.id}};
  const auto forced = create();
  CHECK(!forced);
  CHECK(forced.error().message.find("source alignment") != std::string::npos);
  region->unitSelectionOverrides.clear();
  fixture.bank.units.front().enabled = false;
  CHECK(!create());
  // A manually supplied legacy plan must not bypass the pipeline capability check.
  auto unsafe = snapshot;
  const auto legacy = seam::synthesis::DeterministicUnitSelector{}.select(
      *snapshot.sample().voicebank, *snapshot.project->findRegion(fixture.regionId), snapshot.phonemes->tokens, snapshot.style);
  CHECK(legacy);
  CHECK(legacy.value().entries.size() == 1U);
  unsafe.sample().unitPlan = std::make_shared<const seam::synthesis::UnitPlan>(legacy.value());
  unsafe.sample().frozenAudio.resize(1U);
  unsafe.sample().frozenAudio.front().unitId = longUnit.id;
  const auto rejected = seam::rendering::PhraseRenderPipeline{}.render(unsafe);
  CHECK(!rejected);
  CHECK(rejected.error().message.find("source alignment") != std::string::npos);
}

TEST_CASE("phrase boundaries preserve shared lyric continuation context") {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().articulation = seam::domain::NoteArticulation::Legato;
  auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{2880}, seam::time::Tick{960},
      69U, U"あ", seam::domain::Language::Japanese);
  note.lyricTokenId = region->notes.front().lyricTokenId;
  note.articulation = seam::domain::NoteArticulation::Legato;
  region->notes.push_back(note);
  const auto whole = seam::rendering::PhraseSegmenter{}.segment(*region);
  CHECK(whole); CHECK(whole.value().size() == 1U);
  CHECK(!seam::rendering::PhraseSegmenter{}.segment(*region, {.maximumDuration = seam::time::Tick{1200}}));
  auto cut = whole.value().front();
  cut.startTick = note.startTick;
  cut.noteIds = {note.id};
  const auto rejected = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
      fixture.trackId, cut, 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot);
  CHECK(!rejected);
  CHECK(rejected.error().message.find("vowel continuation") != std::string::npos);
}

TEST_CASE("snapshot rejects external performance that is absent from cache identity") {
  PerformanceSnapshotFixture fixture;
  const auto* region = fixture.project.findRegion(fixture.regionId);
  const auto score = seam::synthesis::compileScorePerformance(fixture.project, *region, 48000U);
  CHECK(score);
  seam::synthesis::PhraseRenderOptions options;
  options.renderer.psola.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(score.value());
  const auto segments = seam::rendering::PhraseSegmenter{}.segment(*region);
  CHECK(segments);
  const auto result = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
      fixture.trackId, segments.value().front(), 1U, seam::rendering::RenderQuality::Preview,
      fixture.bankRoot, 48000U, {}, options);
  CHECK(!result);
  CHECK(result.error().message.find("snapshot-owned") != std::string::npos);
}

TEST_CASE("normal classical snapshots own performance and apply manual pitch only once") {
  for (const auto backend : {seam::voicebank::RendererHint::ClassicPsola, seam::voicebank::RendererHint::SpectralClassic,
      seam::voicebank::RendererHint::Stretch, seam::voicebank::RendererHint::Raw}) {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().vibrato.enabled = false;
  CHECK(region->dynamicsAutomation.replacePoints({}));
  auto& unit = fixture.bank.units.front();
  unit.renderer = backend;
  for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
  }
  const auto baseline = fixture.snapshot();
  CHECK(baseline.compiledPerformance);
  CHECK(region->pitchAutomation.upsert({.tick = seam::time::Tick{0}, .cents = 100.0F}));
  const auto shifted = fixture.snapshot();
  CHECK(shifted.compiledPerformance);
  CHECK(shifted.contentHash != baseline.contentHash);
  CHECK(region->pitchAutomation.erase(seam::time::Tick{0}));
  const auto measure = [&](const auto& snapshot) {
    const auto rendered = seam::rendering::PhraseRenderPipeline{}.render(snapshot);
    CHECK(rendered);
    CHECK(!rendered.value().rendered.placements.front().usedFallback);
    const auto& samples = rendered.value().rendered.audio.samples;
    const auto pitch = seam::voicebank::analyzePitch(std::span<const float>{samples.data() + 8000U, 8000U}, 48000U);
    CHECK(pitch);
    return seam::voicebank::medianVoicedPitch(pitch.value());
  };
  CHECK_NEAR(measure(baseline), 440.0, 10.0);
  CHECK_NEAR(measure(shifted), 466.163762, 10.0);
  CHECK(fixture.snapshot().contentHash == baseline.contentHash);
  CHECK(region->dynamicsAutomation.replacePoints({{seam::time::Tick{0}, 0.5F}}));
  const auto quieter = fixture.snapshot();
  CHECK(quieter.contentHash != baseline.contentHash);
  const auto originalAudio = seam::rendering::PhraseRenderPipeline{}.render(baseline);
  const auto quieterAudio = seam::rendering::PhraseRenderPipeline{}.render(quieter);
  CHECK(originalAudio); CHECK(quieterAudio);
  const auto& originalPcm = originalAudio.value().rendered.audio;
  const auto& quieterPcm = quieterAudio.value().rendered.audio;
  CHECK(originalPcm.startFrame == quieterPcm.startFrame);
  CHECK(originalPcm.samples.size() == quieterPcm.samples.size());
  for (std::size_t i = 0; i < originalPcm.samples.size(); ++i) {
    const auto gain = quieter.compiledPerformance->at(originalPcm.startFrame + static_cast<seam::time::SampleFrame>(i)).dynamicsGain;
    CHECK(quieterPcm.samples[i] == static_cast<float>(static_cast<double>(originalPcm.samples[i]) * gain));
  }
  CHECK(region->dynamicsAutomation.replacePoints({}));
  CHECK(fixture.snapshot().contentHash == baseline.contentHash);
  // A missing PSOLA capability must not silently discard compiled pitch via raw fallback.
  if (backend == seam::voicebank::RendererHint::ClassicPsola) {
  unit.pitchMarks = {{.frame = 100, .confidence = 1.0F},
      {.frame = 200, .confidence = 1.0F}, {.frame = 300, .confidence = 1.0F}};
  const auto unsupported = fixture.snapshot();
  CHECK(!seam::rendering::PhraseRenderPipeline{}.render(unsupported));
  }
  }
}

TEST_CASE("single vowel continuation changes normal renderer audio with exact undo identity") {
  for (const auto backend : {seam::voicebank::RendererHint::ClassicPsola, seam::voicebank::RendererHint::SpectralClassic,
      seam::voicebank::RendererHint::Stretch, seam::voicebank::RendererHint::Raw}) {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().vibrato.enabled = false;
  CHECK(region->dynamicsAutomation.replacePoints({}));
  auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{2880}, seam::time::Tick{960},
      69U, U"あ", seam::domain::Language::Japanese);
  region->lyrics.push_back(lyric); region->notes.push_back(note);
  auto& unit = fixture.bank.units.front();
  unit.renderer = backend;
  for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
  }
  auto samples = seam::test::support::sineWave(48000U, 440.0, 0.5);
  samples[1800] = 1.0F;
  CHECK(seam::voicebank::writeMonoPcm16Wav(fixture.bankRoot / unit.audioPath, 48000U, samples));
  const auto hash = seam::core::sha256File(fixture.bankRoot / unit.audioPath);
  CHECK(hash);
  const seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash.value(), {{"a", 3600}}};
  const auto json = seam::synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash.value(), 24000);
  CHECK(json);
  std::filesystem::create_directories(fixture.bankRoot / "alignments");
  if (backend != seam::voicebank::RendererHint::Raw) {
    CHECK(seam::core::durableAtomicWriteText(fixture.bankRoot / "alignments" /
        (seam::core::sha256Hex(unit.id) + ".json"), json.value()));
  }
  const auto repeated = fixture.snapshot();
  const auto before = seam::rendering::PhraseRenderPipeline{}.render(repeated);
  CHECK(before);
  seam::application::SetLyricCommand continuation{lyric.id, U"-", seam::domain::Language::Japanese};
  CHECK(continuation.apply(fixture.project));
  const auto linked = fixture.snapshot();
  CHECK(!linked.compiledPerformance->notes()[1].reattack);
  const auto after = seam::rendering::PhraseRenderPipeline{}.render(linked);
  CHECK(after);
  CHECK(before.value().rendered.placements.size() == 2U);
  CHECK(after.value().rendered.placements.size() == 2U);
  CHECK(before.value().rendered.placements[1].vowelOnset == after.value().rendered.placements[1].vowelOnset);
  CHECK(before.value().rendered.audio.samples != after.value().rendered.audio.samples);
  CHECK(continuation.revert(fixture.project));
  CHECK(fixture.snapshot().contentHash == repeated.contentHash);
  }
}

TEST_CASE("aligned long unit follows both score pitches through normal classical snapshots") {
  for (const auto backend : {seam::voicebank::RendererHint::ClassicPsola, seam::voicebank::RendererHint::SpectralClassic,
      seam::voicebank::RendererHint::Stretch}) {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->notes.front().midiKey = 60U;
  region->notes.front().vibrato.enabled = false;
  CHECK(region->dynamicsAutomation.replacePoints({}));
  auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{2880}, seam::time::Tick{960},
      67U, U"あ", seam::domain::Language::Japanese);
  region->lyrics.push_back(lyric); region->notes.push_back(note);
  const auto samples = seam::test::support::sineWave(48000U, 440.0, 1.0);
  CHECK(seam::voicebank::writeMonoPcm16Wav(fixture.bankRoot / "audio/a.wav", 48000U, samples));
  auto& unit = fixture.bank.units.front();
  unit.phones = {"a", "a"};
  unit.renderer = backend;
  unit.markers.audioEnd = 48000;
  unit.markers.releaseStart = 45600;
  unit.markers.loopEnd = 44000;
  for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
  }
  const auto hash = seam::core::sha256File(fixture.bankRoot / unit.audioPath);
  CHECK(hash);
  const seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash.value(), {{"a", 3600}, {"a", 27000}}};
  const auto json = seam::synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash.value(), 48000);
  CHECK(json);
  std::filesystem::create_directories(fixture.bankRoot / "alignments");
  CHECK(seam::core::durableAtomicWriteText(fixture.bankRoot / "alignments" /
      (seam::core::sha256Hex(unit.id) + ".json"), json.value()));
  const auto snapshot = fixture.snapshot();
  CHECK(snapshot.sample().unitPlan->entries.size() == 1U);
  const auto rendered = seam::rendering::PhraseRenderPipeline{}.render(snapshot);
  CHECK(rendered);
  CHECK(!rendered.value().rendered.placements.front().usedFallback);
  CHECK(rendered.value().rendered.placements.front().diagnostic.find("Source-aligned") != std::string::npos);
  const auto& output = rendered.value().rendered.audio;
  const auto& spans = snapshot.compiledPerformance->notes();
  const auto measure = [&](std::size_t index) {
    const auto start = static_cast<std::size_t>(spans[index].startFrame - output.startFrame + 8000);
    const auto pitch = seam::voicebank::analyzePitch(std::span<const float>{output.samples.data() + start, 10000U}, 48000U);
    CHECK(pitch); return seam::voicebank::medianVoicedPitch(pitch.value());
  };
  CHECK_NEAR(measure(0U), 261.625565, 14.0);
  CHECK_NEAR(measure(1U), 391.995436, 18.0);
  seam::application::UpsertPhonemeOverrideCommand timingEdit{fixture.regionId,
      {.key = {note.id, 0U}, .timing = {.startOffset = 30000}, .locked = true}};
  CHECK(timingEdit.apply(fixture.project));
  const auto shiftedSnapshot = fixture.snapshot();
  CHECK(shiftedSnapshot.contentHash != snapshot.contentHash);
  const auto shifted = seam::rendering::PhraseRenderPipeline{}.render(shiftedSnapshot);
  CHECK(shifted);
  CHECK(shifted.value().timing.placements.front().phonemeTargets[1].nucleusFrame ==
      rendered.value().timing.placements.front().phonemeTargets[1].nucleusFrame + 1440);
  CHECK(shifted.value().rendered.audio.samples != output.samples);
  CHECK(timingEdit.revert(fixture.project));
  CHECK(fixture.snapshot().contentHash == snapshot.contentHash);
  }
}

TEST_CASE("phrase snapshot preserves effective dynamics with only interpolation anchors") {
  const PerformanceSnapshotFixture fixture;
  const auto snapshot = fixture.snapshot();
  const auto& dynamics = snapshot.project->findRegion(fixture.regionId)->dynamicsAutomation;
  CHECK(dynamics.points().size() == 4U);
  CHECK(dynamics.points().front().tick == seam::time::Tick{960});
  CHECK(dynamics.points().back().tick == seam::time::Tick{3840});
  const auto& original = fixture.project.findRegion(fixture.regionId)->dynamicsAutomation;
  for (const auto tick : {1920, 2040, 2280, 2760, 2880}) {
    CHECK_NEAR(dynamics.valueAt(seam::time::Tick{tick}),
        original.valueAt(seam::time::Tick{tick}), 1e-7);
  }
}

TEST_CASE("snapshot resolves continuation in full region context before phrase extraction") {
  PerformanceSnapshotFixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"ー";
  region->notes.front().phoneticHint.reset();
  auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{0},
      seam::time::Tick{240}, 69U, U"あ", seam::domain::Language::Japanese);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  region->sortNotes();
  const auto segments = seam::rendering::PhraseSegmenter{}.segment(*region);
  CHECK(segments);
  const auto found = std::find_if(segments.value().begin(), segments.value().end(),
      [&](const auto& segment) { return std::find(segment.noteIds.begin(), segment.noteIds.end(),
                                                  fixture.noteId) != segment.noteIds.end(); });
  CHECK(found != segments.value().end());
  CHECK(found->noteIds.size() == 1U);
  const auto snapshot = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
      fixture.trackId, *found, 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot);
  CHECK(snapshot);
  const auto full = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(full);
  CHECK(snapshot.value().pronunciationIdentity == full.value().identity);
  CHECK(snapshot.value().phonemes->tokens == full.value().pronunciation.tokensForNote(fixture.noteId));
  CHECK(snapshot.value().phonemes->tokens.front().symbol == "a");
  auto isolated = *region;
  std::erase_if(isolated.notes, [&](const auto& candidate) { return candidate.id != fixture.noteId; });
  const auto withoutPredecessor = seam::phonemizer::resolveJapanesePronunciation(isolated); CHECK(withoutPredecessor);
  CHECK(withoutPredecessor.value().pronunciation.tokens.front().symbol == "pau");
}

TEST_CASE("phrase projection rejects incomplete active relationships without changing saved edits") {
  for (bool seam : {false, true}) {
    PerformanceSnapshotFixture fixture;
    auto* region = fixture.project.findRegion(fixture.regionId);
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{0}, seam::time::Tick{240}, 69U, U"あ", seam::domain::Language::Japanese);
    region->lyrics.push_back(lyric); region->notes.push_back(note); region->sortNotes();
    if (seam) region->seamOverrides = {{.incomingStartKey = {fixture.noteId, 0U}, .seamAmount = 0.5F}};
    else {
      auto unit = fixture.bank.units.front();
      unit.id = "aa"; unit.phones = {"a", "a"};
      fixture.bank.units.push_back(unit);
      const auto hash = seam::core::sha256File(fixture.bankRoot / unit.audioPath);
      CHECK(hash);
      const seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash.value(),
          {{"a", 3600}, {"a", 15000}}};
      const auto json = seam::synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash.value(), 24000);
      CHECK(json);
      std::filesystem::create_directories(fixture.bankRoot / "alignments");
      CHECK(seam::core::durableAtomicWriteText(fixture.bankRoot / "alignments" /
          (seam::core::sha256Hex(unit.id) + ".json"), json.value()));
      region->unitSelectionOverrides = {{.startKey = {note.id, 0U}, .tokenCount = 2U, .unitId = "aa"}};
    }
    const auto before = fixture.project;
    auto withoutDependencies = *region;
    withoutDependencies.unitSelectionOverrides.clear();
    withoutDependencies.seamOverrides.clear();
    const auto segments = seam::rendering::PhraseSegmenter{}.segment(withoutDependencies);
    CHECK(segments); CHECK(segments.value().size() == 2U);
    const auto result = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
        fixture.trackId, segments.value()[seam ? 1U : 0U], 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot);
    CHECK(!result);
    CHECK(result.error().message.find("Phrase boundary splits") != std::string::npos);
    CHECK(fixture.project == before);
    const auto combined = seam::rendering::PhraseSegmenter{}.segment(*region);
    CHECK(combined); CHECK(combined.value().size() == 1U);
    const auto overLimit = seam::rendering::PhraseSegmenter{}.segment(*region, {.maximumDuration = seam::time::Tick{1000}});
    CHECK(!overLimit);
    CHECK(overLimit.error().message.find("maximum phrase duration") != std::string::npos);
    CHECK(fixture.project == before);
    const auto complete = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
        fixture.trackId, combined.value().front(), 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot);
    CHECK(complete);
    if (seam) region->seamOverrides.front().unresolved = true;
    else region->unitSelectionOverrides.front().unresolved = true;
    const auto retained = seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
        fixture.trackId, segments.value()[seam ? 1U : 0U], 1U, seam::rendering::RenderQuality::Preview, fixture.bankRoot);
    CHECK(retained);
  }
}

TEST_CASE("exact phrase boundary dynamics exclude unnecessary exterior anchors") {
  PerformanceSnapshotFixture fixture;
  auto& source = fixture.project.findRegion(fixture.regionId)->dynamicsAutomation;
  CHECK(source.replacePoints({
      {.tick = seam::time::Tick{0}, .linearGain = 0.1F},
      {.tick = seam::time::Tick{1920}, .linearGain = 0.4F},
      {.tick = seam::time::Tick{2400}, .linearGain = 0.8F},
      {.tick = seam::time::Tick{2880}, .linearGain = 0.6F},
      {.tick = seam::time::Tick{6000}, .linearGain = 0.2F}}));
  const auto snapshot = fixture.snapshot();
  const auto& cropped = snapshot.project->findRegion(fixture.regionId)->dynamicsAutomation;
  CHECK(cropped.points().size() == 3U);
  CHECK(cropped.points().front().tick == seam::time::Tick{1920});
  CHECK(cropped.points().back().tick == seam::time::Tick{2880});
  CHECK_NEAR(cropped.valueAt(seam::time::Tick{2760}), source.valueAt(seam::time::Tick{2760}), 1e-7);
}

TEST_CASE("phrase dynamics preserve empty curves clamping and spanning interpolation") {
  using Point = seam::domain::DynamicsAutomationPoint;
  PerformanceSnapshotFixture fixture;
  auto& source = fixture.project.findRegion(fixture.regionId)->dynamicsAutomation;
  const std::array curves{
      std::vector<Point>{},
      std::vector<Point>{{.tick = seam::time::Tick{0}, .linearGain = 0.1F},
                        {.tick = seam::time::Tick{960}, .linearGain = 0.5F}},
      std::vector<Point>{{.tick = seam::time::Tick{3840}, .linearGain = 0.2F},
                        {.tick = seam::time::Tick{6000}, .linearGain = 0.5F}},
      std::vector<Point>{{.tick = seam::time::Tick{960}, .linearGain = 0.2F},
                        {.tick = seam::time::Tick{3840}, .linearGain = 0.8F}}};
  constexpr std::array expectedCounts{0U, 1U, 1U, 2U};
  for (std::size_t index = 0U; index < curves.size(); ++index) {
    CHECK(source.replacePoints(curves[index]));
    const auto snapshot = fixture.snapshot();
    const auto& cropped = snapshot.project->findRegion(fixture.regionId)->dynamicsAutomation;
    CHECK(cropped.points().size() == expectedCounts[index]);
    for (const auto tick : {1920, 2400, 2880}) {
      CHECK_NEAR(cropped.valueAt(seam::time::Tick{tick}), source.valueAt(seam::time::Tick{tick}), 1e-7);
    }
  }
}

TEST_CASE("performance snapshots freeze note intent track style and cropped dynamics") {
  PerformanceSnapshotFixture fixture;
  const auto snapshot = fixture.snapshot();
  const auto frozen = *snapshot.project;
  const auto frozenHash = snapshot.contentHash;
  CHECK(snapshot.project->findNote(fixture.noteId)->vibrato == fixture.project.findNote(fixture.noteId)->vibrato);
  CHECK(snapshot.project->findNote(fixture.noteId)->phoneticHint == fixture.project.findNote(fixture.noteId)->phoneticHint);
  CHECK(snapshot.project->findVocalTrack(fixture.trackId)->styleSelection == fixture.project.findVocalTrack(fixture.trackId)->styleSelection);
  CHECK(snapshot.project->findRegion(fixture.regionId)->startTick == seam::time::Tick{4800});
  fixture.project.findNote(fixture.noteId)->vibrato.depthCents = 85.0F;
  fixture.project.findNote(fixture.noteId)->phoneticHint = "a a";
  fixture.project.findVocalTrack(fixture.trackId)->styleSelection.styleId = "soft";
  CHECK(fixture.project.findRegion(fixture.regionId)->dynamicsAutomation.upsert(
      {.tick = seam::time::Tick{2160}, .linearGain = 0.9F}));
  CHECK(*snapshot.project == frozen);
  CHECK(snapshot.contentHash == frozenHash);
  CHECK(fixture.snapshot().contentHash != frozenHash);
}

TEST_CASE("each persisted performance intent changes snapshot identity independently") {
  PerformanceSnapshotFixture fixture;
  const auto baselineProject = fixture.project;
  const auto baseline = fixture.snapshot();
  for (const auto field : {"vibrato", "hint", "style", "inside", "predecessor", "successor"}) {
    fixture.project = baselineProject;
    const std::string_view selected{field};
    if (selected == "vibrato") {
      fixture.project.findNote(fixture.noteId)->vibrato.phaseTurns = 0.5F;
    } else if (selected == "hint") {
      fixture.project.findNote(fixture.noteId)->phoneticHint = "a a";
    } else if (selected == "style") {
      fixture.project.findVocalTrack(fixture.trackId)->styleSelection.styleId = "soft";
    } else {
      const auto tick = selected == "inside" ? 2160 : selected == "predecessor" ? 960 : 3840;
      CHECK(fixture.project.findRegion(fixture.regionId)->dynamicsAutomation.upsert(
          {.tick = seam::time::Tick{tick}, .linearGain = 0.9F}));
    }
    CHECK(fixture.snapshot().contentHash != baseline.contentHash);
  }
}

TEST_CASE("faraway dynamics edits do not invalidate the extracted phrase") {
  PerformanceSnapshotFixture fixture;
  const auto before = fixture.snapshot();
  auto& dynamics = fixture.project.findRegion(fixture.regionId)->dynamicsAutomation;
  CHECK(dynamics.upsert({.tick = seam::time::Tick{0}, .linearGain = 3.0F}));
  CHECK(dynamics.upsert({.tick = seam::time::Tick{6000}, .linearGain = 0.3F}));
  const auto after = fixture.snapshot();
  CHECK(before.contentHash == after.contentHash);
  CHECK(before.project->findRegion(fixture.regionId)->dynamicsAutomation ==
        after.project->findRegion(fixture.regionId)->dynamicsAutomation);
}

TEST_CASE("snapshot factory rejects invalid source dynamics before cropping") {
  PerformanceSnapshotFixture fixture;
  const auto segments = seam::rendering::PhraseSegmenter{}.segment(*fixture.project.findRegion(fixture.regionId));
  CHECK(segments);
  fixture.project.findRegion(fixture.regionId)->durationTick = seam::time::Tick{5000};
  const auto result = seam::rendering::RenderSnapshotFactory{}.create(fixture.project,
      fixture.bank, fixture.trackId, segments.value().front(), 1U,
      seam::rendering::RenderQuality::Preview, fixture.bankRoot);
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::InvariantViolation);
}

TEST_CASE("default snapshot style follows saved intent without first style substitution") {
  PerformanceSnapshotFixture fixture;
  const auto original = fixture.snapshot();
  auto& selection = fixture.project.findVocalTrack(fixture.trackId)->styleSelection;
  selection = {seam::domain::VoiceStyleOrigin::Explicit, "soft"};
  const auto soft = fixture.snapshot();
  CHECK(soft.style == "soft");
  CHECK(soft.sample().unitPlan->entries.front().unitId == "soft-a");
  CHECK(soft.contentHash != original.contentHash);
  const auto segments = seam::rendering::PhraseSegmenter{}.segment(*fixture.project.findRegion(fixture.regionId));
  CHECK(segments);
  const auto snapshot = [&](std::string request = {}) {
    return seam::rendering::RenderSnapshotFactory{}.create(fixture.project, fixture.bank,
        fixture.trackId, segments.value().front(), 1U, seam::rendering::RenderQuality::Preview,
        fixture.bankRoot, 48000U, std::move(request));
  };
  selection.styleId = "missing";
  const auto missing = snapshot();
  CHECK(!missing);
  CHECK(missing.error().code == seam::core::ErrorCode::NotFound);
  selection = {};
  CHECK(!snapshot());
  selection = {seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution, {}};
  CHECK(!snapshot());
  const auto deliberate = snapshot("soft");
  CHECK(deliberate);
  CHECK(deliberate.value().style == "soft");
  CHECK(selection.origin == seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution);
  selection = {};
  fixture.bank.styles.resize(1U);
  fixture.bank.units.resize(1U);
  CHECK(snapshot());
}

TEST_CASE("project rendering honors saved style and reports missing styles without substitute units") {
  PerformanceSnapshotFixture fixture;
  fixture.project.findVocalTrack(fixture.trackId)->styleSelection = {
      seam::domain::VoiceStyleOrigin::Explicit, "soft"};
  const std::vector sources{seam::rendering::TrackVoicebankSource{
      .trackId = fixture.trackId, .manifest = fixture.bank, .bankRoot = fixture.bankRoot}};
  seam::rendering::ProductionProjectRenderer renderer;
  const auto soft = renderer.render(fixture.project, sources, fixture.trackId, fixture.regionId,
                                    1U, 48000U);
  CHECK(soft);
  CHECK(soft.value().diagnostics.empty());
  CHECK(!soft.value().activeUnitPlan.empty());
  CHECK(soft.value().activeUnitPlan.front().unitId == "soft-a");
  fixture.project.findVocalTrack(fixture.trackId)->styleSelection.styleId = "missing";
  const auto missing = renderer.render(fixture.project, sources, fixture.trackId, fixture.regionId,
                                       2U, 48000U);
  CHECK(!missing);
  CHECK(missing.error().code == seam::core::ErrorCode::NotFound);
  CHECK(missing.error().context.find("style") != std::string::npos);
}

TEST_CASE("accepted attack survives normal snapshots and manual ownership undo across sample renderers") {
  using namespace seam::domain;
  using seam::time::Tick;
  for (const auto backend : {seam::voicebank::RendererHint::Raw,
                            seam::voicebank::RendererHint::ClassicPsola,
                            seam::voicebank::RendererHint::SpectralClassic,
                            seam::voicebank::RendererHint::Stretch}) {
    PerformanceSnapshotFixture fixture;
    fixture.bank.units.front().renderer = backend;
    for (std::int64_t frame = 100; frame < 24000; frame += 109) {
      fixture.bank.units.front().pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
    }
    auto& region = *fixture.project.findRegion(fixture.regionId);
    region.notes.front().vibrato.enabled = false;
    CHECK(region.dynamicsAutomation.replacePoints({}));
    region.performance.takes = {{.id = "attack", .sourceRegionId = fixture.regionId,
        .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
        .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
        .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{9600}},
        .lanes = {{PerformanceChannel::Attack, {{Tick{0}, 100.0}}}}}};
    const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(region);
    CHECK(pronunciation);
    region.performance.takes[0].pronunciation = pronunciation.value().identity;
    const auto proposal = region.performance.takes[0];
    region.performance.takes.clear();
    const auto noProposalHash = fixture.snapshot().contentHash;
    seam::application::EditorSession acceptance{fixture.project};
    const auto job = acceptance.capturePerformanceJob();
    CHECK(job);
    CHECK(acceptance.executePerformanceResult(job.value(),
        std::make_unique<seam::application::AddPerformanceProposalCommand>(
            fixture.regionId, region.performance, proposal)));
    fixture.project = acceptance.project();
    CHECK(fixture.snapshot().contentHash == noProposalHash);
    CHECK(acceptance.execute(std::make_unique<seam::application::SetAcceptedPerformanceCommand>(
        fixture.regionId, fixture.project.findRegion(fixture.regionId)->performance, std::vector<AcceptedPerformanceSelection>{
            {"attack", PerformanceChannel::Attack, fixture.noteId, Tick{0}}})));
    fixture.project = acceptance.project();
    const auto original = fixture.project;
    const auto attacked = fixture.snapshot();
    const auto attackedPcm = seam::rendering::PhraseRenderPipeline{}.render(attacked);
    CHECK(attackedPcm);
    seam::rendering::PcmCache cache{fixture.bankRoot / "attack-ownership-cache"};
    const auto renderRegion = [&] {
      return seam::rendering::ProductionRegionRenderer{}.render(fixture.project, fixture.bank,
          fixture.bankRoot, fixture.trackId, fixture.regionId, 1U, 48000U,
          seam::rendering::RenderQuality::Preview, {}, {}, &cache);
    };
    const auto initialRegion = renderRegion();
    CHECK(initialRegion); CHECK(initialRegion.value().cacheHits == 0U);
    seam::application::EditorSession session{fixture.project};
    const auto& state = fixture.project.findRegion(fixture.regionId)->performance;
    CHECK(session.execute(std::make_unique<seam::application::EditPerformanceCommand>(
        std::vector<seam::application::NoteExpressionEdit>{},
        std::vector<seam::application::RegionDynamicsEdit>{},
        std::vector<seam::application::TrackStyleEdit>{},
        std::vector<seam::application::RegionOwnershipEdit>{{
            .regionId = fixture.regionId, .expectedRevision = state.revision,
            .expectedOwnership = state.ownership,
            .ownership = {{PerformanceChannel::Attack, fixture.noteId, ManualPerformanceMode::Replace, {}}}}})));
    fixture.project = session.project();
    const auto neutral = fixture.snapshot();
    CHECK(neutral.contentHash != attacked.contentHash);
    const auto neutralPcm = seam::rendering::PhraseRenderPipeline{}.render(neutral);
    CHECK(neutralPcm);
    const auto neutralRegion = renderRegion();
    CHECK(neutralRegion); CHECK(neutralRegion.value().cacheHits == 0U);
    CHECK(neutralRegion.value().mono != initialRegion.value().mono);
    const auto& shaped = attackedPcm.value().rendered.audio;
    const auto& plain = neutralPcm.value().rendered.audio;
    CHECK(shaped.startFrame == plain.startFrame);
    CHECK(shaped.samples.size() == plain.samples.size());
    CHECK(shaped.samples != plain.samples);
    for (std::size_t i = 0; i < shaped.samples.size(); ++i) {
      const auto gain = attacked.compiledPerformance->at(shaped.startFrame +
          static_cast<seam::time::SampleFrame>(i)).articulationGain;
      CHECK(shaped.samples[i] == static_cast<float>(static_cast<double>(plain.samples[i]) * gain));
    }
    CHECK(session.undo());
    fixture.project = session.project();
    CHECK(fixture.project == original);
    const auto restored = fixture.snapshot();
    CHECK(restored.contentHash == attacked.contentHash);
    const auto restoredPcm = seam::rendering::PhraseRenderPipeline{}.render(restored);
    CHECK(restoredPcm); CHECK(restoredPcm.value().rendered.audio.samples == shaped.samples);
    const auto restoredRegion = renderRegion();
    CHECK(restoredRegion); CHECK(restoredRegion.value().cacheHits == 1U);
    CHECK(restoredRegion.value().mono == initialRegion.value().mono);
    CHECK(session.redo());
    fixture.project = session.project();
    CHECK(fixture.snapshot().contentHash == neutral.contentHash);
    // Snapshot-owned evaluation remains immutable after live ownership edits.
    CHECK(seam::rendering::PhraseRenderPipeline{}.render(attacked).value().rendered.audio.samples == shaped.samples);
  }
}

TEST_CASE("snapshots exclude inert proposals but retain selected performance and manual ownership") {
  PerformanceSnapshotFixture fixture;
  const auto baseline = fixture.snapshot();
  auto& state = fixture.project.findRegion(fixture.regionId)->performance;
  seam::domain::PerformanceTake proposal{
      .id = "suggestion", .sourceRegionId = fixture.regionId,
      .resource = {seam::domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {seam::domain::Language::Japanese, "fixture", "1",
                         std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {seam::time::Tick{0}, seam::time::Tick{9600}},
      .lanes = {{seam::domain::PerformanceChannel::Pitch,
                  {{seam::time::Tick{0}, 6000.0}, {seam::time::Tick{9600}, 6200.0}}}}};
  state.takes.push_back(proposal);
  CHECK(fixture.snapshot().contentHash == baseline.contentHash);
  state.accepted.push_back({proposal.id, seam::domain::PerformanceChannel::Pitch,
                             fixture.noteId, seam::time::Tick{0}});
  state.ownership.push_back({seam::domain::PerformanceChannel::Pitch, fixture.noteId,
                              seam::domain::ManualPerformanceMode::PitchOffset, {}});
  const auto selected = fixture.snapshot();
  CHECK(selected.contentHash != baseline.contentHash);
  const auto& frozen = selected.project->findRegion(fixture.regionId)->performance;
  CHECK(frozen.accepted == state.accepted);
  CHECK(frozen.ownership == state.ownership);
  CHECK(frozen.takes == state.takes);
  proposal.id = "other-suggestion";
  proposal.seed = 999U;
  state.takes.push_back(proposal);
  CHECK(fixture.snapshot().contentHash == selected.contentHash);
}

TEST_CASE("data-only neural bundles own hash-bound assets without reinterpreting legacy models") {
  using namespace seam;
  using namespace synthesis;
  std::vector<std::byte> bytes{std::byte{1},std::byte{2}};
  const auto hash=core::sha256Hex(std::span<const std::byte>{bytes});
  std::vector<NeuralBundleAssetInput> assets{{NeuralAssetRole::Acoustic,"acoustic",bytes,hash},
      {NeuralAssetRole::Vocoder,"vocoder",bytes,hash},{NeuralAssetRole::Vocabulary,"vocabulary",bytes,hash},
      {NeuralAssetRole::Configuration,"configuration",bytes,hash}};
  const auto manifest=FrozenNeuralBundle::manifest(assets,8U); CHECK(manifest);
  domain::SingerResourceIdentity identity{domain::SingerResourceKind::Neural,"test-bundle","1",core::sha256Hex(manifest.value())};
  const auto frozen=FrozenNeuralBundle::freeze(identity,assets,8U); CHECK(frozen);
  CHECK(frozen.value().assets().size()==4U);
  auto shared=frozen.value();
  CHECK(shared.assets().data()==frozen.value().assets().data());
  CHECK(shared.assets()[0].data->bytes().data()!=bytes.data());
  std::reverse(assets.begin(),assets.end());
  CHECK(FrozenNeuralBundle::manifest(assets,8U).value()==manifest.value());
  CHECK(!FrozenNeuralBundle::freeze(identity,assets,7U));
  auto bad=assets; bad.front().name="../model"; CHECK(!FrozenNeuralBundle::manifest(bad,8U));
  bad=assets; bad.front().name=bad.back().name; CHECK(!FrozenNeuralBundle::manifest(bad,8U));
  bad=assets; bad.front().role=NeuralAssetRole::Acoustic; CHECK(!FrozenNeuralBundle::manifest(bad,8U));
  bad=assets; bad.front().role=static_cast<NeuralAssetRole>(99); CHECK(!FrozenNeuralBundle::manifest(bad,8U));
  auto wrong=identity; wrong.contentHash=std::string(64U,'0'); CHECK(!FrozenNeuralBundle::freeze(wrong,assets,8U));
  std::stop_source stop; stop.request_stop(); CHECK(!FrozenNeuralBundle::freeze(identity,assets,8U,stop.get_token()));
  bytes.front()=std::byte{9};
  CHECK(frozen.value().assets()[0].data->bytes().front()==std::byte{1});
  CHECK(!FrozenNeuralBundle::freeze(identity,assets,8U));
  // Arbitrary fixture bytes prove ownership only, never graph validity.
}
