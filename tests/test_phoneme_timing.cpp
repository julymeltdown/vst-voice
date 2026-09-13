#include "test_framework.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"
#include "seam/synthesis/source_phoneme_alignment.hpp"
#include "seam/synthesis/source_target_map.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "test_support.hpp"
#include "seam/formats/json_value.hpp"
#include <limits>

namespace {
struct TimingFixture {
  seam::application::ProjectFactory factory{500U};
  seam::domain::Project project{factory.createProject("Timing contract")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  seam::domain::RegionId id{factory.addRegion(project, track, "Phrase", seam::time::Tick{480}, seam::time::Tick{3840})};
  TimingFixture() {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{960}, seam::time::Tick{960}, 60U, U"かき", seam::domain::Language::Japanese);
    region().lyrics.push_back(lyric); region().notes.push_back(note);
  }
  seam::domain::VocalRegion& region() { return *project.findRegion(id); }
  std::vector<seam::domain::PhonemeToken> tokens() {
    const auto result = seam::phonemizer::resolveJapanesePronunciation(region());
    CHECK(result); return result.value().pronunciation.tokens;
  }
};
}

TEST_CASE("short CV marker map preserves vowel anchor and reserves bounded sustain and release") {
  using namespace seam;
  auto unit = test::support::makeUnit("short-cv", {"k", "o"}, "short.wav", 67,
      voicebank::UnitKind::Cv, 24000);
  unit.markers.audioOffset = 0; unit.markers.vowelOnset = 2200;
  unit.markers.consonantEnd = 1200; unit.markers.stableStart = 5292;
  unit.markers.loopStart = 6000; unit.markers.loopEnd = 16000;
  unit.markers.releaseStart = 22000; unit.markers.audioEnd = 24000;
  for (auto rate : {44100U, 48000U, 96000U}) {
    const auto onset = static_cast<time::SampleFrame>(2200ULL * rate / 44100U);
    const auto length = static_cast<time::SampleFrame>(rate / 16U);
    const auto map = synthesis::compileShortUnitMarkerMap(unit, 0, onset, onset + length, 44100U, rate, 24000);
    CHECK(map); CHECK(map.value().validate(24000));
    CHECK(map.value().targetAt(2200.0) == static_cast<double>(onset));
    CHECK(map.value().knots[2].targetFrame < map.value().knots[3].targetFrame);
    CHECK(map.value().knots.back().targetFrame == onset + length);
    std::vector<float> audio(24000U);
    for (std::size_t i = 0; i < audio.size(); ++i) audio[i] = static_cast<float>(i) / 24000.0F;
    const auto rendered = synthesis::applySourceTargetMap(audio, map.value()); CHECK(rendered);
    CHECK(rendered.value().samples.size() == static_cast<std::size_t>(onset + length));
    CHECK(rendered.value().samples[static_cast<std::size_t>(onset)] == audio[2200]);
    const auto again = synthesis::applySourceTargetMap(audio, map.value()); CHECK(again);
    CHECK(rendered.value().samples == again.value().samples);
  }
  CHECK(!synthesis::compileShortUnitMarkerMap(unit, 0, 2200, 2202, 44100, 44100, 24000));
  CHECK(!synthesis::compileShortUnitMarkerMap(unit, 0, 0, 3000, 44100, 44100, 24000));
  CHECK(!synthesis::compileShortUnitMarkerMap(unit, 0, 2200, 5200, 0, 44100, 24000));
  CHECK(!synthesis::compileShortUnitMarkerMap(unit, 0, 2200, 5200, 44100, 44100, 23000));
  unit.kind = voicebank::UnitKind::Vcv;
  CHECK(!synthesis::compileShortUnitMarkerMap(unit, 0, 2200, 5200, 44100, 44100, 24000));
}

TEST_CASE("short-transition timing requires opt-in and classical renderers consume its map without Raw fallback") {
  using namespace seam;
  TimingFixture f; f.region().lyrics.front().surface = U"か";
  f.region().notes.front().durationTick = time::Tick{30};
  const auto tokens = f.tokens();
  auto unit = test::support::makeUnit("short", {"k", "a"}, "short.wav", 69, voicebank::UnitKind::Cv, 24000);
  for (time::SampleFrame frame = 4800; frame < 19200; frame += 109)
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F});
  const auto manifest = test::support::makeManifest({unit});
  auto plan = synthesis::DeterministicUnitSelector{}.select(manifest, f.region(), tokens, "original"); CHECK(plan);
  CHECK(!synthesis::TimingSolver{}.solve(f.project, f.region(), tokens, plan.value(), manifest, 48000U));
  const auto timing = synthesis::TimingSolver{}.solve(f.project, f.region(), tokens, plan.value(), manifest, 48000U, {}, true);
  CHECK(timing); CHECK(timing.value().placements.front().compressShortTransition);
  auto audio = std::make_shared<voicebank::AudioBuffer>();
  audio->sampleRate = 48000U; audio->channels = 1U;
  audio->interleaved = test::support::sineWave(48000U, 440.0, 0.5);
  const std::vector<synthesis::FrozenUnitAudio> sources{{.unitId = unit.id, .audio = audio}};
  const auto compiled = synthesis::compileScorePerformance(f.project, f.region(), 48000U, tokens); CHECK(compiled);
  synthesis::PhraseRenderOptions options;
  const auto performance = std::make_shared<const synthesis::CompiledScorePerformance>(compiled.value());
  options.renderer.psola.performance = performance;
  options.renderer.raw.performance = performance;
  options.renderer.spectral.performance = performance;
  options.renderer.stretch.performance = performance;
  for (auto renderer : {domain::UnitRendererKind::Raw, domain::UnitRendererKind::ClassicPsola, domain::UnitRendererKind::SpectralClassic, domain::UnitRendererKind::Stretch}) {
    plan.value().entries.front().renderer = renderer;
    const auto rendered = synthesis::ConcatenativePhraseRenderer{}.render(manifest, f.project, f.region(), plan.value(), timing.value(), 48000U, options, sources);
    if (!rendered) throw test::Failure{rendered.error().message + " / " + rendered.error().context};
    CHECK(!rendered.value().placements.front().usedFallback);
    CHECK(rendered.value().placements.front().diagnostic.find("Short transition") != std::string::npos);
    CHECK(rendered.value().placements.front().vowelOnset == timing.value().placements.front().desiredVowelOnset);
    CHECK(std::all_of(rendered.value().audio.samples.begin(), rendered.value().audio.samples.end(), [](float value) { return std::isfinite(value); }));
    CHECK(std::any_of(rendered.value().audio.samples.begin(), rendered.value().audio.samples.end(), [](float value) { return value != 0.0F; }));
  }
}

TEST_CASE("mapped Raw sustain retains pitch-ratio stepping gain and exact extent") {
  using namespace seam;
  auto unit = test::support::makeUnit("raw-short", {"k", "a"}, "raw.wav", 69, voicebank::UnitKind::Cv, 24000);
  unit.gainDb = -6.0F;
  voicebank::AudioBuffer audio; audio.sampleRate = 48000U; audio.channels = 1U;
  audio.interleaved = test::support::sineWave(48000U, 440.0, 0.5);
  const auto map = synthesis::compileShortUnitMarkerMap(unit, 10000, 12000, 16000, 48000U, 48000U, 24000); CHECK(map);
  synthesis::RawRenderParameters parameters; parameters.sourceMap = map.value(); parameters.performanceStartFrame = 10000;
  parameters.performanceVowelFrame = 12000;
  const auto pre = static_cast<time::SampleFrame>(std::llround(map.value().targetAt(7200.0))) - 10000;
  for (const int midi : {69, 81}) {
    const auto output = synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 6000, midi, parameters); CHECK(output);
    CHECK(output.value().samples.size() == 6000U); CHECK(output.value().vowelOnsetOffset == 2000);
    const auto step = std::exp2(static_cast<double>(midi - 69) / 12.0);
    const auto sample = [&](double position) {
      const auto left = static_cast<std::size_t>(std::floor(position));
      const auto mix = static_cast<float>(position - static_cast<double>(left));
      return audio.interleaved[left] * (1.0F - mix) + audio.interleaved[left + 1U] * mix;
    };
    const auto gain = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
    for (time::SampleFrame i = 100; i < 200; ++i) {
      const auto index = static_cast<std::size_t>(pre + i);
      const auto expected = (sample(7200.0 + static_cast<double>(i + 1) * step) -
                             sample(7200.0 + static_cast<double>(i) * step)) * gain;
      // First difference cancels DC correction while checking the actual loop phase step.
      CHECK_NEAR(output.value().samples[index + 1U] - output.value().samples[index], expected, 1.0e-6);
    }
  }
  CHECK(!synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 5999, 69, parameters));
  parameters.performanceVowelFrame = 12001;
  CHECK(!synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 6000, 69, parameters));
}

TEST_CASE("procedural timing allocates untimed onsets without rewriting explicit timing") {
  using namespace seam;
  TimingFixture fixture;
  const auto phones = fixture.tokens(); CHECK(phones.size() == 4U);
  const auto baseline = synthesis::compilePhonemeTimingPlan(fixture.project, fixture.region(), phones, 48000U); CHECK(baseline);
  const auto planned = synthesis::compilePhonemeTimingPlan(fixture.project, fixture.region(), phones, 48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(planned);
  CHECK(!baseline.value()[0].inferredStartFrame); CHECK(!planned.value()[0].explicitStartFrame);
  for (const auto i : {0U, 2U}) {
    CHECK(planned.value()[i].inferredStartFrame == std::optional{baseline.value()[i].nucleusFrame});
    CHECK(planned.value()[i + 1U].nucleusFrame == baseline.value()[i + 1U].nucleusFrame + 2880);
    CHECK(planned.value()[i].nucleusFrame == planned.value()[i + 1U].nucleusFrame);
  }
  CHECK(planned.value()[1].endFrame == *planned.value()[2].inferredStartFrame);
  auto edited = phones; edited[0].timing.startOffset = 0; edited[1].timing.startOffset = 100000;
  const auto manual = synthesis::compilePhonemeTimingPlan(fixture.project, fixture.region(), edited, 48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(manual);
  CHECK(!manual.value()[0].inferredStartFrame); CHECK(manual.value()[0].explicitStartFrame);
  CHECK(manual.value()[1].nucleusFrame == baseline.value()[0].nucleusFrame + 4800);
  CHECK(manual.value()[2].inferredStartFrame);
  // A partially edited syllable is not silently assigned a different policy.
  edited = phones; edited[1].timing.endOffset = 200000;
  const auto partial = synthesis::compilePhonemeTimingPlan(fixture.project, fixture.region(), edited, 48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(partial);
  CHECK(!partial.value()[0].inferredStartFrame);
  CHECK(partial.value()[1].nucleusFrame == baseline.value()[1].nucleusFrame);
  fixture.region().notes.front().durationTick = time::Tick{96};
  const auto shortNote = synthesis::compilePhonemeTimingPlan(fixture.project, fixture.region(), phones, 48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(shortNote);
  CHECK(shortNote.value()[1].nucleusFrame - *shortNote.value()[0].inferredStartFrame == 300);
  CHECK(phones == fixture.tokens());
}

TEST_CASE("procedural voiced onsets receive distinct timing without changing voicing or manual ownership") {
  using namespace seam;
  TimingFixture fixture; fixture.region().lyrics.front().surface=U"まな";
  const auto phones=fixture.tokens(); CHECK(phones.size()==4U);
  CHECK(phones[0].symbol=="m"); CHECK(phones[2].symbol=="n"); CHECK(phones[0].voiced && phones[2].voiced);
  const auto source=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),phones,48000U); CHECK(source);
  const auto planned=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),phones,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(planned);
  for (const auto i:{0U,2U}) {
    CHECK(!source.value()[i].inferredStartFrame);
    CHECK(planned.value()[i].voiced==std::optional{true});
    CHECK(planned.value()[i].inferredStartFrame==std::optional{source.value()[i].nucleusFrame});
    CHECK(planned.value()[i+1U].nucleusFrame==source.value()[i+1U].nucleusFrame+2880);
    CHECK(planned.value()[i].nucleusKey==std::optional{phones[i+1U].key});
    CHECK(!planned.value()[i].explicitStartFrame);
  }
  CHECK(planned.value()[1].endFrame==*planned.value()[2].inferredStartFrame);
  auto manual=phones; manual[0].timing.startOffset=0; manual[1].timing.startOffset=90000;
  const auto edited=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),manual,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(edited);
  CHECK(!edited.value()[0].inferredStartFrame); CHECK(edited.value()[0].explicitStartFrame);
  CHECK(edited.value()[1].nucleusFrame==source.value()[1].nucleusFrame+4320);
  CHECK(edited.value()[2].inferredStartFrame);
  fixture.region().notes.front().durationTick=time::Tick{96};
  const auto shortNote=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),phones,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(shortNote);
  CHECK(shortNote.value()[1].nucleusFrame-*shortNote.value()[0].inferredStartFrame==300);
  CHECK(phones==fixture.tokens());
}

TEST_CASE("procedural coda defaults reserve a tail while consonant clusters stay unresolved") {
  using namespace seam;
  TimingFixture fixture; fixture.region().lyrics.front().surface=U"あん";
  const auto coda=fixture.tokens(); CHECK(coda.size()==2U); CHECK(coda.back().role==domain::PhonemeRole::Coda);
  const auto codaTiming=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),coda,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(codaTiming);
  CHECK(!codaTiming.value().front().inferredStartFrame);
  CHECK(codaTiming.value().back().inferredStartFrame==std::optional{codaTiming.value().back().endFrame-2880});
  CHECK(codaTiming.value().front().endFrame==*codaTiming.value().back().inferredStartFrame);
  fixture.region().lyrics.front().surface=U"まん";
  const auto cvc=fixture.tokens(); CHECK(cvc.size()==3U);
  const auto cvcTiming=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),cvc,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(cvcTiming);
  CHECK(cvcTiming.value()[0].inferredStartFrame); CHECK(cvcTiming.value()[2].inferredStartFrame);
  CHECK(cvcTiming.value()[1].nucleusFrame==*cvcTiming.value()[0].inferredStartFrame+2880);
  CHECK(cvcTiming.value()[1].endFrame==*cvcTiming.value()[2].inferredStartFrame);
  auto authored=cvc; authored[2].timing.startOffset=400000;
  const auto owned=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),authored,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(owned);
  for (const auto& anchor:owned.value()) CHECK(!anchor.inferredStartFrame);
  fixture.region().lyrics.front().surface=U"まな";
  auto cluster=fixture.tokens(); cluster[1].symbol="r"; cluster[1].role=domain::PhonemeRole::Onset;
  const auto clusterTiming=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),cluster,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(clusterTiming);
  for (const auto& anchor:clusterTiming.value()) CHECK(!anchor.inferredStartFrame);
}

TEST_CASE("procedural coda allocation follows the resolved next syllable rather than an obsolete boundary") {
  using namespace seam;
  TimingFixture fixture; fixture.region().lyrics.front().surface=U"かんき";
  auto phones=fixture.tokens(); CHECK(phones.size()==5U); CHECK(phones[2].role==domain::PhonemeRole::Coda);
  const auto inferred=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),phones,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(inferred);
  CHECK(inferred.value()[2].endFrame==*inferred.value()[3].inferredStartFrame);
  CHECK(inferred.value()[1].endFrame==*inferred.value()[2].inferredStartFrame);
  const auto start=*inferred.value()[0].inferredStartFrame;
  phones[4].timing.startOffset=200000;
  const auto edited=synthesis::compilePhonemeTimingPlan(fixture.project,fixture.region(),phones,48000U,
      synthesis::PhonemeTimingPolicy::ProceduralInNote); CHECK(edited);
  CHECK(edited.value()[2].endFrame==start+9600);
  CHECK(edited.value()[2].inferredStartFrame==std::optional{start+7200});
  CHECK(!edited.value()[3].inferredStartFrame);
}

TEST_CASE("source phone alignment requires exact audio identity ordered coverage and decoded bounds") {
  const auto unit = seam::test::support::makeUnit("kaki", {"k", "a", "k", "i"}, "kaki.wav", 60,
      seam::voicebank::UnitKind::Cv, 24000);
  const std::string hash(64U, 'a');
  const seam::synthesis::SourcePhonemeAlignment valid{unit.id, hash,
      {{"k", 0}, {"a", 3600}, {"k", 12000}, {"i", 15000}}};
  CHECK(valid.validate(unit, hash, 24000));
  CHECK(!valid.validate(unit, std::string(64U, 'b'), 24000));
  CHECK(!valid.validate(unit, hash, 23999));
  for (unsigned scenario = 0U; scenario < 7U; ++scenario) {
    auto broken = valid;
    if (scenario == 0U) broken.unitId = "other";
    if (scenario == 1U) broken.audioSha256 = "unverified";
    if (scenario == 2U) broken.landmarks.pop_back();
    if (scenario == 3U) broken.landmarks[2].frame = 3600;
    if (scenario == 4U) broken.landmarks[0].frame = -1;
    if (scenario == 5U) broken.landmarks.back().frame = 24000;
    if (scenario == 6U) broken.landmarks[2].phone = "s";
    CHECK(!broken.validate(unit, hash, 24000));
  }
}

TEST_CASE("source alignment persistence round trips and rejects stale audio and malformed schema") {
  const auto unit = seam::test::support::makeUnit("ka", {"k", "a"}, "ka.wav", 60,
      seam::voicebank::UnitKind::Cv, 24000);
  const std::string hash(64U, 'a');
  const seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash, {{"k", 0}, {"a", 3600}}};
  const auto encoded = seam::synthesis::encodeSourcePhonemeAlignment(alignment, unit, hash, 24000);
  CHECK(encoded);
  const auto decoded = seam::synthesis::decodeSourcePhonemeAlignment(encoded.value(), unit, hash, 24000);
  CHECK(decoded); CHECK(decoded.value() == alignment);
  CHECK(!seam::synthesis::decodeSourcePhonemeAlignment(encoded.value(), unit, std::string(64U, 'b'), 24000));
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto json = seam::formats::parseJson(encoded.value());
    CHECK(json);
    if (scenario == 0U) *json.value().find("schemaVersion") = seam::formats::JsonValue{std::int64_t{2}};
    if (scenario == 1U) *json.value().find("landmarks")->asArray().front().find("frame") = seam::formats::JsonValue{0.5};
    if (scenario == 2U) json.value().asObject().emplace("unknown", seam::formats::JsonValue{true});
    if (scenario == 3U) json.value().find("landmarks")->asArray().clear();
    CHECK(!seam::synthesis::decodeSourcePhonemeAlignment(seam::formats::stringifyJson(json.value()), unit, hash, 24000));
  }
  CHECK(!seam::synthesis::decodeSourcePhonemeAlignment(std::string(512U * 1024U + 1U, ' '), unit, hash, 24000));
}

TEST_CASE("source target mapping retains multiple nuclei and rejects contradictory boundaries") {
  TimingFixture f;
  const auto tokens = f.tokens();
  const auto timing = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(timing);
  const auto unit = seam::test::support::makeUnit("kaki", {"k", "a", "k", "i"}, "kaki.wav", 60,
      seam::voicebank::UnitKind::Cv, 24000);
  const std::string hash(64U, 'a');
  const seam::synthesis::SourcePhonemeAlignment alignment{unit.id, hash, {{"k", 0}, {"a", 3600}, {"k", 12000}, {"i", 15000}}};
  seam::synthesis::TimedUnitPlacement placement{.unitId = unit.id, .startKey = tokens.front().key,
      .tokenCount = 4U, .destinationStart = timing.value()[1].nucleusFrame - 3600,
      .destinationEnd = timing.value().back().endFrame, .phonemeTargets = timing.value()};
  const auto map = seam::synthesis::compileSourceTargetMap(alignment, unit, placement, hash, 24000);
  CHECK(map); CHECK(map.value().knots.size() == 4U);
  CHECK(map.value().knots[1].sourceFrame == 3600);
  CHECK(map.value().knots[1].targetFrame == timing.value()[1].nucleusFrame);
  CHECK(map.value().knots[2].sourceFrame == 15000);
  CHECK(map.value().knots[2].targetFrame == timing.value()[3].nucleusFrame);
  CHECK(map.value().validate(24000));
  CHECK(map.value().voicedAtSource(0.0) == false);
  CHECK(map.value().voicedAtSource(3600.0) == true);
  CHECK(map.value().voicedAtSource(12000.0) == false);
  CHECK(map.value().voicedAtSource(15000.0) == true);
  CHECK(!map.value().voicedAtSource(24000.0).has_value());
  CHECK(map.value().sourceAt(static_cast<double>(timing.value()[3].nucleusFrame)) == 15000.0);
  CHECK(map.value().targetAt(15000.0) == static_cast<double>(timing.value()[3].nucleusFrame));
  CHECK_NEAR(map.value().sourceAt(map.value().targetAt(8000.5)), 8000.5, 0.000001);
  auto invalidMap = map.value();
  invalidMap.knots[2].sourceFrame = invalidMap.knots[1].sourceFrame;
  CHECK(!invalidMap.validate(24000));
  CHECK(!map.value().validate(1000));
  std::vector<float> source(24000U, 0.0F);
  source[3600] = 0.5F; source[15000] = -0.75F;
  const auto rendered = seam::synthesis::renderAlignedRawUnit(source, alignment, unit, placement, hash);
  CHECK(rendered);
  const auto firstIndex = static_cast<std::size_t>(timing.value()[1].nucleusFrame - rendered.value().startFrame);
  const auto secondIndex = static_cast<std::size_t>(timing.value()[3].nucleusFrame - rendered.value().startFrame);
  CHECK(rendered.value().samples[firstIndex] == source[3600]);
  CHECK(rendered.value().samples[secondIndex] == source[15000]);
  auto editedPlacement = placement;
  editedPlacement.phonemeTargets[3].nucleusFrame += 1440;
  const auto moved = seam::synthesis::renderAlignedRawUnit(source, alignment, unit, editedPlacement, hash);
  CHECK(moved);
  CHECK(moved.value().samples[firstIndex] == source[3600]);
  CHECK(moved.value().samples[secondIndex + 1440U] == source[15000]);
  auto gainedUnit = unit; gainedUnit.gainDb = -6.0206F;
  const auto gained = seam::synthesis::renderAlignedRawUnit(source, alignment, gainedUnit, placement, hash);
  CHECK(gained); CHECK_NEAR(gained.value().samples[firstIndex], 0.25F, 1.0e-6);
  editedPlacement.targetMidi = 72;
  CHECK(!seam::synthesis::renderAlignedRawUnit(source, alignment, unit, editedPlacement, hash));
  CHECK(!seam::synthesis::renderAlignedRawUnit(source, alignment, unit, placement, std::string(64U, 'b')));
  placement.phonemeTargets[3].nucleusFrame = timing.value()[1].nucleusFrame;
  CHECK(!seam::synthesis::compileSourceTargetMap(alignment, unit, placement, hash, 24000));
  placement.phonemeTargets = timing.value();
  placement.phonemeTargets[1].endExplicit = true;
  placement.phonemeTargets[1].endFrame = timing.value()[3].nucleusFrame - 1000;
  placement.phonemeTargets[2].explicitStartFrame = timing.value()[3].nucleusFrame - 2000;
  CHECK(!seam::synthesis::compileSourceTargetMap(alignment, unit, placement, hash, 24000));
}

TEST_CASE("source mapping places multiple waveform landmarks exactly with bounded output") {
  std::vector<float> source(16U, 0.0F);
  source[3] = 0.7F; source[10] = -0.8F;
  const auto before = source;
  seam::synthesis::SourceTargetMap map{{{0, -4}, {3, 0}, {10, 12}, {16, 24}}};
  const auto output = seam::synthesis::applySourceTargetMap(source, map);
  CHECK(output); CHECK(output.value().startFrame == -4);
  CHECK(output.value().samples.size() == 28U);
  CHECK(output.value().samples[4] == source[3]);
  CHECK(output.value().samples[16] == source[10]);
  map.knots[2].targetFrame += 3;
  const auto edited = seam::synthesis::applySourceTargetMap(source, map);
  CHECK(edited); CHECK(edited.value().samples[19] == source[10]);
  CHECK(edited.value().samples[4] == source[3]);
  CHECK(source == before);
  std::stop_source stop; stop.request_stop();
  CHECK(!seam::synthesis::applySourceTargetMap(source, map, stop.get_token()));
  map.knots[2].targetFrame = 0;
  CHECK(!seam::synthesis::applySourceTargetMap(source, map));
  map.knots[2].targetFrame = 12;
  map.knots.back().sourceFrame = 17;
  CHECK(!seam::synthesis::applySourceTargetMap(source, map));
  map.knots.back().sourceFrame = 16;
  map.knots.back().targetFrame = std::numeric_limits<seam::time::SampleFrame>::max();
  CHECK(!seam::synthesis::applySourceTargetMap(source, map));
  map.knots.back().targetFrame = 24;
  source[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(!seam::synthesis::applySourceTargetMap(source, map));
}

TEST_CASE("timing contract allocates elapsed time across tempo changes without losing note endpoints") {
  TimingFixture f;
  CHECK(f.project.tempoMap().addOrReplace(seam::time::Tick{1920}, 60.0));
  const auto tokens = f.tokens();
  for (auto rate : {8000U, 44100U, 48000U, 192000U}) {
    const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, rate);
    CHECK(result);
    const auto first = f.project.tempoMap().sampleFrameAt(seam::time::Tick{1440}, static_cast<double>(rate));
    const auto last = f.project.tempoMap().sampleFrameAt(seam::time::Tick{2400}, static_cast<double>(rate));
    CHECK(result.value()[1].nucleusFrame == first);
    CHECK(result.value()[3].nucleusFrame == first + (last - first) / 2);
    CHECK(result.value()[1].endFrame == result.value()[3].nucleusFrame);
    CHECK(result.value()[3].endFrame == last);
  }
}

TEST_CASE("timing contract rounds note-relative edits once at the output sample rate") {
  TimingFixture f;
  auto tokens = f.tokens();
  tokens[3].timing.startOffset = 280000;
  for (const auto rate : {44100U, 48000U}) {
    const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, rate);
    CHECK(result);
    const auto start = f.project.tempoMap().sampleFrameAt(seam::time::Tick{1440}, static_cast<double>(rate));
    CHECK(result.value()[3].nucleusFrame == start + static_cast<seam::time::SampleFrame>(rate * 28U / 100U));
  }
}

TEST_CASE("timing contract validates automatic ends after all nucleus edits resolve") {
  TimingFixture f;
  auto tokens = f.tokens();
  tokens[1].timing.startOffset = 300000;
  tokens[3].timing.startOffset = 400000;
  const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(result);
  CHECK(result.value()[1].endFrame == result.value()[3].nucleusFrame);
  CHECK(result.value()[1].endFrame - result.value()[1].nucleusFrame == 4800);
  tokens[1].timing.endOffset = 250000;
  CHECK(!seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U));
  tokens[1].timing.endOffset.reset();
  tokens[3].timing.startOffset = 200000;
  CHECK(!seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U));
}

TEST_CASE("timing contract distinguishes onset edits from the shared nucleus anchor") {
  TimingFixture f;
  auto tokens = f.tokens();
  tokens[0].timing.startOffset = -45000;
  tokens[1].timing.startOffset = 30000;
  const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(result);
  const auto noteStart = f.project.tempoMap().sampleFrameAt(seam::time::Tick{1440}, 48000.0);
  CHECK(result.value()[0].explicitStartFrame == noteStart - 2160);
  CHECK(result.value()[0].nucleusFrame == noteStart + 1440);
  CHECK(result.value()[0].nucleusFrame == result.value()[1].nucleusFrame);
  CHECK(!result.value()[2].explicitStartFrame);
  CHECK(result.value()[2].nucleusFrame == result.value()[3].nucleusFrame);
}

TEST_CASE("generated timing proposals displace syllable anchors through the ordered plan") {
  using namespace seam;
  TimingFixture f;
  const auto tokens = f.tokens();
  const auto baseline = synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(baseline);
  const auto noteStartFrame = f.project.tempoMap().sampleFrameAt(seam::time::Tick{1440}, 48000.0);
  CHECK(baseline.value()[1].nucleusFrame == noteStartFrame);
  CHECK(baseline.value()[3].nucleusFrame > baseline.value()[1].nucleusFrame);

  const auto pronunciation = phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(pronunciation);
  const auto noteId = f.region().notes.front().id;
  domain::PerformanceTake take{};
  take.id = "timing-take";
  take.sourceRegionId = f.id;
  take.capturedRevision = f.region().performance.revision;
  take.resource = {domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')};
  take.pronunciation = pronunciation.value().identity;
  take.generatorId = "fixture";
  take.generatorVersion = "1";
  take.range = {time::Tick{0}, f.region().durationTick};
  take.lanes = {{domain::PerformanceChannel::Timing, {{time::Tick{0}, 30000.0}}}};
  auto& performance = f.region().performance;
  performance.takes.push_back(take);
  performance.accepted = {{take.id, domain::PerformanceChannel::Timing, noteId, time::Tick{0}}};

  // A thirty millisecond proposal moves both syllables by 1440 frames at 48 kHz, and
  // the automatic end follows the moved nucleus instead of an obsolete boundary.
  const auto displaced = synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(displaced);
  CHECK(displaced.value()[1].nucleusFrame == baseline.value()[1].nucleusFrame + 1440);
  CHECK(displaced.value()[3].nucleusFrame == baseline.value()[3].nucleusFrame + 1440);
  CHECK(displaced.value()[1].endFrame == displaced.value()[3].nucleusFrame);
  // Generated timing is not an authored edit: provenance stays honest.
  CHECK(!displaced.value()[1].explicitStartFrame);
  CHECK(!displaced.value()[3].explicitStartFrame);

  // Manual replacement wins, exactly as it does for pitch.
  domain::ManualPerformanceOwnership owner{};
  owner.channel = domain::PerformanceChannel::Timing;
  owner.mode = domain::ManualPerformanceMode::Replace;
  owner.scope = noteId;
  performance.ownership.push_back(owner);
  const auto manual = synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(manual);
  CHECK(manual.value()[1].nucleusFrame == baseline.value()[1].nucleusFrame);
  CHECK(manual.value()[3].nucleusFrame == baseline.value()[3].nucleusFrame);
  performance.ownership.clear();

  // An authored offset stays absolute from the note start and applies only to the
  // token the creator wrote it for; the other syllable still follows the proposal.
  auto authored = tokens;
  authored[1].timing.startOffset = time::Microseconds{-10000};
  const auto composed = synthesis::compilePhonemeTimingPlan(f.project, f.region(), authored, 48000U);
  CHECK(composed);
  CHECK(composed.value()[1].nucleusFrame == noteStartFrame - 480);
  CHECK(composed.value()[3].nucleusFrame == baseline.value()[3].nucleusFrame + 1440);

  // A proposal that would reorder the nuclei is refused, not clamped.
  performance.takes.back().lanes = {{domain::PerformanceChannel::Timing,
      {{time::Tick{480}, 0.0}, {time::Tick{960}, -1000000.0}}}};
  const auto refused = synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(!refused);
  CHECK(refused.error().code == core::ErrorCode::Conflict);
}

TEST_CASE("timing contract keeps a trailing coda with its preceding final nucleus") {
  TimingFixture f;
  auto tokens = f.tokens();
  auto coda = tokens.back();
  coda.key.ordinal = 4U; coda.symbol = "N"; coda.role = seam::domain::PhonemeRole::Coda;
  tokens.push_back(coda);
  const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(result);
  CHECK(result.value()[4].nucleusFrame == result.value()[3].nucleusFrame);
  CHECK(result.value()[4].endFrame == result.value()[3].endFrame);
}

TEST_CASE("timing contract rejects noncontiguous ordinals reversed nuclei and invalid rates") {
  TimingFixture f;
  auto tokens = f.tokens();
  const auto before = f.project;
  tokens[3].key.ordinal = 5U;
  CHECK(!seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U));
  tokens = f.tokens(); tokens[3].timing.startOffset = -1;
  CHECK(!seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U));
  tokens = f.tokens();
  CHECK(!seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 0U));
  CHECK(f.project == before);
}

TEST_CASE("timing contract permits note releases inside the region and rejects region overrun") {
  TimingFixture f;
  auto tokens = f.tokens();
  tokens.back().timing.endOffset = 1000000;
  const auto valid = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(valid);
  const auto noteStart = f.project.tempoMap().sampleFrameAt(seam::time::Tick{1440}, 48000.0);
  CHECK(valid.value().back().endFrame == noteStart + 48000);
  const auto before = tokens;
  tokens.back().timing.endOffset = 2000000;
  const auto saved = tokens;
  const auto invalid = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(!invalid);
  CHECK(invalid.error().message.find("release exceeds the region") != std::string::npos);
  CHECK(tokens == saved);
  tokens = before;
  f.region().durationTick = seam::time::Tick{1440};
  CHECK(!seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U));
}

TEST_CASE("timing contract retains independent anchors for overlapping notes") {
  TimingFixture f;
  auto [lyric, note] = f.factory.makeNote(seam::time::Tick{1200}, seam::time::Tick{960}, 64U, U"あ", seam::domain::Language::Japanese);
  f.region().lyrics.push_back(lyric); f.region().notes.push_back(note);
  const auto tokens = f.tokens();
  const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(result);
  const auto secondStart = f.project.tempoMap().sampleFrameAt(seam::time::Tick{1680}, 48000.0);
  CHECK(result.value().back().nucleusFrame == secondStart);
  CHECK(result.value()[3].endFrame > secondStart);
}

TEST_CASE("timing contract exposes syllable membership and explicit boundary provenance") {
  TimingFixture f;
  auto tokens = f.tokens();
  tokens[0].timing.startOffset = -30000;
  tokens[1].timing.endOffset = 200000;
  const auto plan = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(plan);
  for (std::size_t i = 0U; i < tokens.size(); ++i) {
    CHECK(plan.value()[i].syllableIndex == i / 2U);
    CHECK(plan.value()[i].nucleusKey == tokens[i < 2U ? 1U : 3U].key);
    CHECK(plan.value()[i].endExplicit == (i == 1U));
    CHECK(plan.value()[i].explicitStartFrame.has_value() == (i == 0U));
  }
  tokens.resize(1U);
  tokens.front().role = seam::domain::PhonemeRole::Breath;
  tokens.front().symbol = "br";
  tokens.front().voiced = false;
  tokens.front().timing = {};
  const auto breath = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), tokens, 48000U);
  CHECK(breath);
  CHECK(!breath.value().front().nucleusKey);
  CHECK(!breath.value().front().endExplicit);
}

TEST_CASE("timing contract rejects a note with fewer frames than nuclei") {
  TimingFixture f;
  CHECK(f.project.tempoMap().addOrReplace(seam::time::Tick{0}, 300.0));
  f.region().notes.front().durationTick = seam::time::Tick{1};
  f.region().lyrics.front().surface = U"あいう";
  const auto result = seam::synthesis::compilePhonemeTimingPlan(f.project, f.region(), f.tokens(), 8000U);
  CHECK(!result);
  CHECK(result.error().message.find("too short") != std::string::npos);
}
