// A render now publishes the phone partition it actually sang from.
//
// A character presentation may only draw what the listener hears, so the timing it draws has to come
// from the same prepared phrase the audio came from instead of being re-derived at presentation time.
// These cases render a real procedural region through the production project renderer and check the
// published partition: ordered, non-overlapping, inside the phrase, in absolute project frames, with
// vowels, consonants and the product's own pause symbol classified. They also check that the timeline
// is refused by cause when it cannot be an ordered partition, and that a request naming another active
// region publishes none.
//
// No listener hears anything here and no phonetic claim is made: the kinds are the dock's own
// drawing vocabulary, not a transcription, and no roadmap unit is accepted by this suite.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/render_performance.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

using namespace seam;

voice_design::VoiceRecipe sungRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "published-partition";
  recipe.poses = {
      {"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}},
      {"k", "neutral", 0.0, {{320.0, 60.0, 0.0}, {1100.0, 90.0, -3.0}, {2400.0, 120.0, -6.0}}},
  };
  recipe.plosives = {{"k", "neutral", {.seed = 42U}, 10.0}};
  return recipe;
}

// One two-note phrase: a vowel, then a stop plus vowel. The region starts at tick zero so every
// assertion below can compare absolute frames directly with the musical position.
struct SungProject final {
  domain::Project project;
  domain::TrackId track;
  domain::RegionId region;
  synthesis::ProceduralSingerResource resource;
};

SungProject makeSungProject() {
  application::ProjectFactory factory{48000U};
  auto project = factory.createProject("Published partition");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{1920});
  const auto recipe = sungRecipe();
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe);
  CHECK(resource.hasValue());
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  {
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 69U, U"a",
                                          domain::Language::Japanese);
    note.phoneticHint = "a";
    target->lyrics.push_back(lyric);
    target->notes.push_back(note);
  }
  {
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{960}, 71U, U"ka",
                                          domain::Language::Japanese);
    note.phoneticHint = "k a";
    target->lyrics.push_back(lyric);
    target->notes.push_back(note);
  }
  return SungProject{std::move(project), track, region, resource.value()};
}

rendering::ProjectRenderResult renderActive(const SungProject& sung) {
  const std::vector<rendering::TrackSingerSource> sources{
      rendering::TrackProceduralSource{sung.track, sung.resource, "neutral"}};
  auto rendered = rendering::ProductionProjectRenderer{}.renderWithSources(
      sung.project, sources, sung.track, sung.region, 1U, 48000U, rendering::RenderQuality::Final);
  if (!rendered) throw test::Failure{"project render failed: " + rendered.error().message};
  return std::move(rendered.value());
}

TEST_CASE("A published render carries the phone partition it sang from") {
  const auto sung = makeSungProject();
  const auto rendered = renderActive(sung);
  CHECK(!rendered.performanceCues.empty());
  bool sawVowel = false;
  bool sawConsonant = false;
  time::SampleFrame previousEnd = 0;
  for (const auto& cue : rendered.performanceCues) {
    CHECK(!cue.symbol.empty());
    CHECK(cue.endFrame > cue.startFrame);
    CHECK(cue.startFrame >= 0);
    CHECK(cue.startFrame >= previousEnd);
    previousEnd = cue.endFrame;
    sawVowel = sawVowel || cue.kind == rendering::RenderedCueKind::Vowel;
    sawConsonant = sawConsonant || cue.kind == rendering::RenderedCueKind::Consonant ||
                                  cue.kind == rendering::RenderedCueKind::Closure;
  }
  CHECK(sawVowel);
  CHECK(sawConsonant);
  // 1920 ticks at the default 120 bpm and 960 ppq is two quarter notes: 48000 frames at 48 kHz. The
  // partition cannot run past the music that produced it.
  CHECK(rendered.performanceCues.back().endFrame <= 48000);
  // The identity a presentation binds to comes from the same render: the resource the audio was made
  // from, the style it was rendered in, and a digest over the pronunciation that produced it.
  CHECK(rendered.performanceIdentity.has_value());
  CHECK(rendered.performanceIdentity->complete());
  CHECK(rendered.performanceIdentity->resourceId == sung.resource.identity.id);
  CHECK(rendered.performanceIdentity->resourceVersion == sung.resource.identity.version);
  CHECK(rendered.performanceIdentity->resourceContentHash == sung.resource.identity.contentHash);
  CHECK(rendered.performanceIdentity->style == "neutral");
  CHECK(rendered.performanceIdentity->renderRevision == 1U);
  CHECK(rendered.performanceIdentity->sampleRate == 48000U);
  CHECK(rendered.performanceIdentity->pronunciationIdentity.size() == 64U);
  // The second note begins at tick 960, which is frame 24000. Its stop has to be placed after the
  // first note's material, not at the start of the published mix.
  CHECK(rendered.performanceCues.back().startFrame >= 0);
  CHECK(std::any_of(rendered.performanceCues.begin(), rendered.performanceCues.end(),
                    [](const auto& cue) { return cue.startFrame >= 20000; }));
}

TEST_CASE("A request that names another active region publishes no partition") {
  const auto sung = makeSungProject();
  const std::vector<rendering::TrackSingerSource> sources{
      rendering::TrackProceduralSource{sung.track, sung.resource, "neutral"}};
  // No region carries this identity, so the render still produces the full mix while publishing no
  // partition: a dock that is not following the region that was sung has nothing to draw.
  auto rendered = rendering::ProductionProjectRenderer{}.renderWithSources(
      sung.project, sources, sung.track, domain::RegionId{}, 1U, 48000U,
      rendering::RenderQuality::Final);
  CHECK(rendered.hasValue());
  CHECK(rendered.value().performanceCues.empty());
  CHECK(!rendered.value().performanceIdentity.has_value());
}

TEST_CASE("A region's pronunciation identity is framed, not concatenated") {
  const std::array<std::string, 2> split{"aa", "bbb"};
  const std::array<std::string, 2> other{"aab", "bb"};
  const auto first = rendering::renderedPronunciationIdentity(split);
  const auto second = rendering::renderedPronunciationIdentity(other);
  CHECK(first.size() == 64U);
  CHECK(second.size() == 64U);
  // Two different phrase splits must not hash to one identity, which is what a bare concatenation
  // would do.
  CHECK(first != second);
  CHECK(rendering::renderedPronunciationIdentity(split) == first);
  CHECK(rendering::renderedPronunciationIdentity({}).empty());
}

TEST_CASE("Sample performance identity uses the prepared sole style on cold and cached renders") {
  auto sung = makeSungProject();
  CHECK(sung.project.findVocalTrack(sung.track)->styleSelection.styleId.empty());
  const auto root = test::support::temporaryDirectory("performance-sole-style");
  const auto wave = test::support::sineWave(48000U, 440.0, 0.5);
  CHECK(voicebank::writeMonoPcm16Wav(root / "unit.wav", 48000U, wave));
  const auto manifest = test::support::makeManifest({
      test::support::makeUnit("a", {"a"}, "unit.wav"),
      test::support::makeUnit("ka", {"k", "a"}, "unit.wav")});
  const std::array sources{rendering::TrackVoicebankSource{
      sung.track, manifest, root, std::string(64U, 'a'), voicebank::VoicebankTrust::DevelopmentFixture}};
  rendering::PcmCache cache{root / "cache"};
  for (const bool cached : {false, true}) {
    const auto rendered = rendering::ProductionProjectRenderer{}.render(
        sung.project, sources, sung.track, sung.region, 1U, 48000U,
        rendering::RenderQuality::Final, {}, &cache);
    CHECK(rendered);
    CHECK(!rendered.value().interleaved.empty());
    CHECK(!rendered.value().performanceCues.empty());
    CHECK(rendered.value().performanceIdentity.has_value());
    CHECK(rendered.value().performanceIdentity->complete());
    CHECK(rendered.value().performanceIdentity->style == "original");
    CHECK((rendered.value().cacheHits > 0U) == cached);
    CHECK(sung.project.findVocalTrack(sung.track)->styleSelection.styleId.empty());
  }
}

TEST_CASE("The product's own symbols decide the shape the dock may draw") {
  CHECK(rendering::renderedCueKindForSymbol("a", domain::PhonemeRole::Nucleus) ==
        rendering::RenderedCueKind::Vowel);
  CHECK(rendering::renderedCueKindForSymbol("i", domain::PhonemeRole::Nucleus) ==
        rendering::RenderedCueKind::Vowel);
  CHECK(rendering::renderedCueKindForSymbol("n", domain::PhonemeRole::Coda) ==
        rendering::RenderedCueKind::Nasal);
  CHECK(rendering::renderedCueKindForSymbol("N", domain::PhonemeRole::Nucleus) ==
        rendering::RenderedCueKind::Nasal);
  CHECK(rendering::renderedCueKindForSymbol("s", domain::PhonemeRole::Onset) ==
        rendering::RenderedCueKind::Consonant);
  CHECK(rendering::renderedCueKindForSymbol("cl", domain::PhonemeRole::Onset) ==
        rendering::RenderedCueKind::Closure);
  CHECK(rendering::renderedCueKindForSymbol("pau", domain::PhonemeRole::Silence) ==
        rendering::RenderedCueKind::Silence);
  CHECK(rendering::renderedCueKindForSymbol("a", domain::PhonemeRole::Breath) ==
        rendering::RenderedCueKind::Silence);
}

TEST_CASE("A partition that cannot describe its own render is refused by cause") {
  const auto sung = makeSungProject();
  rendering::RenderSnapshotFactory factory;
  auto snapshot = factory.createProcedural(sung.project, sung.resource, sung.track, sung.region, 1U,
                                           rendering::RenderQuality::Final, 48000U);
  CHECK(snapshot.hasValue());
  CHECK(snapshot.value().compiledPerformance != nullptr);

  // The anchors exist but no resolved phone can name them: an unnameable span must not become a
  // guessed mouth shape.
  auto unnamed = rendering::collectPhrasePerformanceCues(*snapshot.value().compiledPerformance, {});
  CHECK(!unnamed.hasValue());
  CHECK(unnamed.error().code == core::ErrorCode::InvariantViolation);

  auto bounded = rendering::collectPhrasePerformanceCues(*snapshot.value().compiledPerformance,
                                                         snapshot.value().phonemes->tokens, 0U);
  CHECK(!bounded.hasValue());
  CHECK(bounded.error().code == core::ErrorCode::InvalidArgument);

  auto tooSmall = rendering::collectPhrasePerformanceCues(*snapshot.value().compiledPerformance,
                                                          snapshot.value().phonemes->tokens, 1U);
  CHECK(!tooSmall.hasValue());
  CHECK(tooSmall.error().code == core::ErrorCode::InvalidArgument);

  auto named = rendering::collectPhrasePerformanceCues(*snapshot.value().compiledPerformance,
                                                       snapshot.value().phonemes->tokens);
  CHECK(named.hasValue());
  CHECK(!named.value().empty());
}

}  // namespace
