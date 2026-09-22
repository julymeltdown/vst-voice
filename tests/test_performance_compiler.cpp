#include "test_framework.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/synthesis/classic_psola.hpp"
#include "seam/synthesis/spectral_classic.hpp"
#include "seam/synthesis/stretch_renderer.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/voicebank/pitch.hpp"
#include "test_support.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace {
struct Fixture {
  seam::application::ProjectFactory factory{7700U};
  seam::domain::Project project{factory.createProject("Score performance")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  seam::domain::RegionId id{factory.addRegion(project, track, "Melody", seam::time::Tick{960}, seam::time::Tick{3840})};
  Fixture() {
    for (const auto pitch : {60, 67}) {
      auto [lyric, note] = factory.makeNote(seam::time::Tick{pitch == 60 ? 0 : 960},
          seam::time::Tick{960}, static_cast<std::uint8_t>(pitch), U"あ", seam::domain::Language::Japanese);
      region().lyrics.push_back(lyric); region().notes.push_back(note);
    }
  }
  seam::domain::VocalRegion& region() { return *project.findRegion(id); }
};
}

TEST_CASE("score performance follows both melody notes independently of acoustic units") {
  Fixture f;
  CHECK(f.project.tempoMap().addOrReplace(seam::time::Tick{1920}, 60.0));
  const auto compiled = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(compiled);
  CHECK(compiled.value().notes().size() == 2U);
  const auto& a = compiled.value().notes()[0];
  const auto& b = compiled.value().notes()[1];
  CHECK(a.endFrame == b.startFrame);
  CHECK(b.endFrame - b.startFrame == 2 * (a.endFrame - a.startFrame));
  CHECK_NEAR(*compiled.value().at(a.startFrame).scoreFrequencyHz, 261.625565, 0.00001);
  CHECK_NEAR(*compiled.value().at(b.startFrame).scoreFrequencyHz, 391.995436, 0.00001);
  CHECK(compiled.value().at(b.startFrame).dynamicsGain == 1.0F);
  CHECK(!compiled.value().at(a.startFrame - 1).scoreFrequencyHz);
  CHECK(!compiled.value().at(b.endFrame).scoreFrequencyHz);
  f.region().notes[0].midiKey = 72;
  CHECK_NEAR(*compiled.value().at(a.startFrame).scoreFrequencyHz, 261.625565, 0.00001);
}

TEST_CASE("phonetic context extends only owning-note pitch dynamics and authored gates") {
  using namespace seam;
  Fixture f;
  for (auto& note : f.region().notes) note.startTick += time::Tick{480};
  CHECK(f.project.tempoMap().addOrReplace(time::Tick{1920}, 90.0));
  CHECK(f.region().dynamicsAutomation.replacePoints({{time::Tick{0}, 0.2F}, {time::Tick{3840}, 0.8F}}));
  CHECK(f.region().formantAutomation.replacePoints({{time::Tick{0}, 3.0F}}));
  CHECK(f.region().breathinessAutomation.replacePoints({{time::Tick{0}, 0.5F}}));
  for (const auto rate : {8000U, 44100U, 48000U, 192000U}) {
    const auto score = synthesis::compileScorePerformance(f.project, f.region(), rate); CHECK(score);
    const auto& a = score.value().notes()[0];
    const auto& b = score.value().notes()[1];
    const auto check = [&](time::SampleFrame frame, time::SampleFrame edge) {
      const auto own = score.value().atPhonetic(frame, a.id);
      const auto expected = score.value().at(edge);
      CHECK(own.noteId == std::optional{a.id});
      CHECK(own.scoreFrequencyHz == expected.scoreFrequencyHz);
      CHECK(own.dynamicsGain == expected.dynamicsGain);
      CHECK(own.articulationGain == 1.0F);
      CHECK(own.formantSemitones == 0.0F);
      CHECK(own.breathiness == 0.0F); CHECK(!own.breathinessIsExplicit);
    };
    check(a.startFrame - 1, a.startFrame);
    check(b.startFrame + 17, a.endFrame - 1);
    CHECK(score.value().at(b.startFrame + 17).scoreFrequencyHz != score.value().atPhonetic(b.startFrame + 17, a.id).scoreFrequencyHz);
    CHECK(score.value().atPhonetic(a.startFrame, a.id).formantSemitones == 3.0F);
    CHECK(score.value().atPhonetic(a.startFrame, a.id).breathinessIsExplicit);
    CHECK(!score.value().at(a.startFrame - 1).noteId); // Score-only semantics did not change.
    auto stopped = f.project;
    stopped.findRegion(f.id)->notes.front().articulation = domain::NoteArticulation::Staccato;
    const auto gated = synthesis::compileScorePerformance(stopped, *stopped.findRegion(f.id), rate); CHECK(gated);
    CHECK(gated.value().atPhonetic(b.startFrame + 17, a.id).articulationGain == 0.0F);
    CHECK(gated.value().at(b.startFrame + 17).articulationGain == 1.0F);
    std::vector<float> carrier(127U, 1.0F);
    CHECK(synthesis::applyCompiledPerformanceGain(carrier, gated.value(), a.endFrame, {}, a.id));
    CHECK(std::all_of(carrier.begin(), carrier.end(), [](float x) { return x == 0.0F; }));
  }
}

TEST_CASE("inspection exposes selected generated dynamics without changing audio ownership or silence semantics") {
  using namespace seam; using namespace domain; using time::Tick;
  Fixture f; const auto note = f.region().notes.front().id;
  CHECK(f.project.tempoMap().addOrReplace(Tick{1200}, 90.0));
  auto& state = f.region().performance;
  state.takes = {{.id = "generated", .sourceRegionId = f.id,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Dynamics, {{Tick{960}, 0.4}, {Tick{1440}, 0.0}, {Tick{1919}, 0.7}}}}}};
  CHECK(f.region().dynamicsAutomation.upsert({Tick{0}, 0.9F}));
  const auto frame = [&](int tick) { return f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{tick}, 48000U); };
  const auto unselected = synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(unselected);
  CHECK(!unselected.value().inspectAt(frame(0)).selectedGeneratedDynamicsGain);
  state.accepted = {{"generated", PerformanceChannel::Dynamics, note, Tick{960}}};
  state.ownership = {{PerformanceChannel::Dynamics, note, ManualPerformanceMode::Replace, {}}};
  const auto owned = synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(owned);
  for (const auto tick : {0, 480, 959, 960}) {
    const auto audio = owned.value().at(frame(tick)); const auto inspected = owned.value().inspectAt(frame(tick));
    CHECK(!audio.selectedGeneratedDynamicsGain); CHECK(audio.dynamicsGain == inspected.dynamicsGain);
    CHECK(audio.scoreFrequencyHz == inspected.scoreFrequencyHz); CHECK(audio.articulationGain == inspected.articulationGain);
    CHECK(inspected.dynamicsGain == 0.9F);
  }
  CHECK(owned.value().inspectAt(frame(0)).selectedGeneratedDynamicsGain == 0.4F);
  CHECK(owned.value().inspectAt(frame(480)).selectedGeneratedDynamicsGain == 0.0F);
  CHECK(owned.value().inspectAt(frame(959)).selectedGeneratedDynamicsGain == 0.7F);
  CHECK(!owned.value().inspectAt(frame(960)).selectedGeneratedDynamicsGain);
  state.ownership.clear();
  const auto generated = synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(generated);
  CHECK(generated.value().at(frame(0)).dynamicsGain == 0.4F);
  CHECK(generated.value().inspectAt(frame(0)).dynamicsGain == 0.4F);
  CHECK(generated.value().inspectAt(frame(480)).dynamicsGain == 0.0F);
  state.takes.front().lanes.front().points[1].value.reset();
  CHECK(!synthesis::compileScorePerformance(f.project, f.region(), 48000U));
}

TEST_CASE("score vibrato uses absolute note phase independent of block boundaries") {
  Fixture f;
  f.region().notes[0].vibrato = {.enabled = true, .startFraction = 0.0F,
      .fadeInFraction = 0.0F, .fadeOutFraction = 0.0F, .depthCents = 100.0F,
      .periodMilliseconds = 200.0F, .phaseTurns = 0.0F};
  CHECK(f.region().pitchAutomation.upsert({.tick = seam::time::Tick{0}, .cents = 100.0F}));
  CHECK(f.region().dynamicsAutomation.replacePoints({{seam::time::Tick{0}, 0.5F}}));
  const auto result = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(result);
  const auto start = result.value().notes()[0].startFrame;
  CHECK_NEAR(result.value().at(start + 2400).vibratoCents, 100.0, 0.000001);
  CHECK_NEAR(*result.value().at(start + 2400).scoreFrequencyHz, 293.664768, 0.00001);
  CHECK(result.value().at(start).dynamicsGain == 0.5F);
  std::vector<double> reference;
  for (int offset = 0; offset < 24000; ++offset) {
    reference.push_back(*result.value().at(start + offset).scoreFrequencyHz);
  }
  for (const auto block : {1, 127, 512}) {
    // Visit blocks backward to expose any hidden mutable oscillator phase.
    for (int offset = (23999 / block) * block; offset >= 0; offset -= block) {
      for (int i = offset; i < std::min(24000, offset + block); ++i) {
        CHECK(*result.value().at(start + i).scoreFrequencyHz == reference[static_cast<std::size_t>(i)]);
      }
    }
  }
}

TEST_CASE("accepted timbre lanes retain native units source offsets and manual half-open ownership") {
  using namespace seam; using namespace domain; using time::Tick;
  using Sample = synthesis::ScorePerformanceSample;
  struct Channel final { PerformanceChannel channel; float Sample::*field; float manual; double first; double last; };
  const std::array channels{
      Channel{PerformanceChannel::Formant, &Sample::formantSemitones, -1.0F, -12.0, 12.0},
      Channel{PerformanceChannel::Breathiness, &Sample::breathiness, 0.125F, 0.25, 0.75},
      Channel{PerformanceChannel::Tension, &Sample::tension, 0.125F, 0.25, 0.75},
      Channel{PerformanceChannel::Airiness, &Sample::airiness, 0.125F, 0.25, 0.75},
      Channel{PerformanceChannel::Gender, &Sample::gender, -0.25F, -0.75, 0.75},
      Channel{PerformanceChannel::Growl, &Sample::growl, 0.125F, 0.25, 0.75}};
  for (const auto& channel : channels) {
    Fixture f;
    CHECK(f.project.tempoMap().addOrReplace(f.region().startTick + Tick{480}, 90.0));
    CHECK(f.region().formantAutomation.upsert({Tick{0}, -1.0F}));
    CHECK(f.region().breathinessAutomation.upsert({Tick{0}, 0.125F}));
    CHECK(f.region().tensionAutomation.upsert({Tick{0}, 0.125F}));
    CHECK(f.region().airinessAutomation.upsert({Tick{0}, 0.125F}));
    CHECK(f.region().genderAutomation.upsert({Tick{0}, -0.25F}));
    CHECK(f.region().growlAutomation.upsert({Tick{0}, 0.125F}));
    auto& state = f.region().performance;
    state.takes = {{.id = "timbre", .sourceRegionId = f.id,
        .resource = {SingerResourceKind::Procedural, "fixture", "1", std::string(64U, 'a')},
        .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
        .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{3840}},
        .lanes = {{channel.channel, {{Tick{1200}, channel.first}, {Tick{1680}, channel.last}}}}}};
    for (const auto rate : {8000U, 44100U, 192000U}) {
      const auto frame = [&](int tick) { return f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{tick}, rate); };
      state.accepted.clear(); state.ownership.clear();
      const auto proposed = synthesis::compileScorePerformance(f.project, f.region(), rate); CHECK(proposed);
      CHECK(proposed.value().at(frame(480)).*channel.field == channel.manual);
      state.accepted = {{"timbre", channel.channel, PerformanceTimeRange{Tick{240}, Tick{720}}, Tick{960}}};
      const auto generated = synthesis::compileScorePerformance(f.project, f.region(), rate); CHECK(generated);
      CHECK(generated.value().at(frame(240) - 1).*channel.field == channel.manual);
      CHECK_NEAR(generated.value().at(frame(240)).*channel.field, channel.first, 1e-6);
      CHECK_NEAR(generated.value().at(frame(480)).*channel.field, (channel.first + channel.last) * 0.5, 1e-6);
      CHECK_NEAR(generated.value().at(frame(720) - 1).*channel.field, channel.last, 1e-6);
      CHECK(generated.value().at(frame(720)).*channel.field == channel.manual);
      const auto mid = generated.value().at(frame(480));
      CHECK(mid.dynamicsGain == 1.0F); CHECK(mid.articulationGain == 1.0F);
      CHECK(mid.scoreFrequencyHz == proposed.value().at(frame(480)).scoreFrequencyHz);
      for (const auto& other : channels) if (other.channel != channel.channel)
        CHECK(mid.*other.field == other.manual);
      state.ownership = {{channel.channel, PerformanceTimeRange{Tick{360}, Tick{600}}, ManualPerformanceMode::Replace, {}}};
      const auto owned = synthesis::compileScorePerformance(f.project, f.region(), rate); CHECK(owned);
      CHECK(owned.value().at(frame(360) - 1).*channel.field != channel.manual);
      CHECK(owned.value().at(frame(360)).*channel.field == channel.manual);
      CHECK(owned.value().at(frame(600) - 1).*channel.field == channel.manual);
      CHECK(owned.value().at(frame(600)).*channel.field != channel.manual);
      // Immutable compiled intent is unaffected by subsequent live ownership changes.
      CHECK(generated.value().at(frame(480)).*channel.field == mid.*channel.field);
      CHECK(!owned.value().at(frame(1920)).noteId);
      CHECK(owned.value().at(frame(1920)).*channel.field == 0.0F);
    }
    state.takes.front().lanes.front().points.front().value.reset();
    CHECK(!synthesis::compileScorePerformance(f.project, f.region(), 48000U));
    state.takes.front().lanes.front().points.front().value = 1000.0;
    CHECK(!synthesis::compileScorePerformance(f.project, f.region(), 48000U));
  }
}

TEST_CASE("breathiness ownership distinguishes an explicit neutral from an unowned frame") {
  using namespace seam; using namespace domain; using time::Tick;
  Fixture f;
  const auto frame = [&](int tick) { return f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{tick}, 48000U); };
  auto& state = f.region().performance;
  state.takes = {{.id = "breath", .sourceRegionId = f.id,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{3840}},
      .lanes = {{PerformanceChannel::Breathiness, {{Tick{0}, 0.0}}}}}};
  state.accepted = {{"breath", PerformanceChannel::Breathiness, PerformanceTimeRange{Tick{240}, Tick{720}}, Tick{0}}};
  state.ownership = {{PerformanceChannel::Breathiness, PerformanceTimeRange{Tick{0}, Tick{120}}, ManualPerformanceMode::Replace, {}}};
  const auto selected = synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(selected);
  for (const auto tick : {0, 119, 240, 719}) {
    CHECK(selected.value().at(frame(tick)).breathinessIsExplicit);
    CHECK(selected.value().at(frame(tick)).breathiness == 0.0F);
  }
  for (const auto tick : {120, 239, 720, 1920})
    CHECK(!selected.value().at(frame(tick)).breathinessIsExplicit);
  CHECK(f.region().breathinessAutomation.upsert({Tick{480}, 0.0F}));
  const auto drawn = synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(drawn);
  CHECK(drawn.value().at(frame(120)).breathinessIsExplicit);
  CHECK(drawn.value().at(frame(720)).breathinessIsExplicit);
  CHECK(!drawn.value().at(frame(1920)).breathinessIsExplicit);
  // Style blend is not admitted as a scalar timbre control without paired resources.
  state.accepted.front().channel = PerformanceChannel::StyleBlend;
  state.takes.front().lanes.front().channel = PerformanceChannel::StyleBlend;
  const auto unsupported = synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(!unsupported); CHECK(unsupported.error().code == core::ErrorCode::Unsupported);
}

TEST_CASE("score compiler rejects invalid rates and ambiguous overlapping note voices") {
  Fixture f;
  CHECK(!seam::synthesis::compileScorePerformance(f.project, f.region(), 0U));
  f.region().notes[1].startTick = seam::time::Tick{480};
  CHECK(!seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U));
}

TEST_CASE("score voice allocation separates overlaps deterministically without moving notes") {
  Fixture f;
  f.region().notes[1].startTick = seam::time::Tick{480};
  const auto before = f.project;
  const auto plan = seam::synthesis::allocateScoreVoices(f.region());
  CHECK(plan); CHECK(plan.value().voices.size() == 2U);
  CHECK(plan.value().voices[0] == std::vector{f.region().notes[0].id});
  CHECK(plan.value().voices[1] == std::vector{f.region().notes[1].id});
  CHECK(f.project == before);
  const auto compiled = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U);
  CHECK(compiled); CHECK(compiled.value().size() == 2U);
  const auto frame = f.project.tempoMap().sampleFrameAt(f.region().startTick + seam::time::Tick{600}, 48000U);
  CHECK_NEAR(*compiled.value()[0].performance.at(frame).scoreFrequencyHz, 261.625565, 0.00001);
  CHECK_NEAR(*compiled.value()[1].performance.at(frame).scoreFrequencyHz, 391.995436, 0.00001);
  CHECK(f.project == before);
  CHECK(!seam::synthesis::allocateScoreVoices(f.region(), 1U));
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(pronunciation);
  CHECK(seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, pronunciation.value().pronunciation.tokens));
  const auto& allPhones = pronunciation.value().pronunciation.tokens;
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U,
      std::span<const seam::domain::PhonemeToken>{allPhones}.first(1U)));
  CHECK(!seam::synthesis::allocateScoreVoices(f.region(), 0U));
  std::reverse(f.region().notes.begin(), f.region().notes.end());
  CHECK(seam::synthesis::allocateScoreVoices(f.region()).value() == plan.value());
}

TEST_CASE("multi-voice compilation resolves English pronunciation per allocated voice") {
  Fixture f;
  using namespace seam;
  using namespace domain;
  using time::Tick;
  f.region().notes[0].phoneticHint = "ah1";
  f.region().notes[1].phoneticHint = "ah1";
  f.region().lyrics[0].language = Language::English;
  f.region().lyrics[1].language = Language::English;
  f.region().lyrics[0].surface = U"a";
  f.region().lyrics[1].surface = U"a";
  f.region().notes[1].startTick = Tick{480};
  const auto pronunciation = phonemizer::resolvePronunciation(f.region());
  CHECK(pronunciation);
  CHECK(pronunciation.value().identity.language == Language::English);
  CHECK(pronunciation.value().pronunciation.tokens.size() == 2U);
  const auto compiled = synthesis::compileScoreVoices(
      f.project, f.region(), 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(compiled);
  CHECK(compiled.value().size() == 2U);
  CHECK(compiled.value()[0].performance.phonemeTiming().size() == 1U);
  CHECK(compiled.value()[1].performance.phonemeTiming().size() == 1U);
  CHECK(compiled.value()[0].performance.notes().front().id == f.region().notes[0].id);
  CHECK(compiled.value()[1].performance.notes().front().id == f.region().notes[1].id);
}

TEST_CASE("voice compilation preserves forced unit and seam dependency boundaries") {
  Fixture f;
  f.region().notes[1].startTick = seam::time::Tick{480};
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(pronunciation);
  const auto& phones = pronunciation.value().pronunciation.tokens;
  CHECK(phones.size() == 2U);
  f.region().unitSelectionOverrides = {{.startKey = phones[0].key,
      .tokenCount = 2U, .unitId = "aa"}};
  const auto before = f.project;
  const auto crossed = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones);
  CHECK(!crossed); CHECK(crossed.error().code == seam::core::ErrorCode::Conflict);
  CHECK(f.project == before);
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U));
  f.region().unitSelectionOverrides.front().tokenCount = 1U;
  CHECK(seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones));
  f.region().unitSelectionOverrides.front().tokenCount = 3U;
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones));
  f.region().unitSelectionOverrides.front().unresolved = true;
  CHECK(seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones));
  f.region().seamOverrides = {{.incomingStartKey = phones[1].key, .seamAmount = 0.5F}};
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones));
  f.region().seamOverrides.front().unresolved = true;
  CHECK(seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones));
  f.region().seamOverrides.front().unresolved = false;
  f.region().seamOverrides.front().incomingStartKey = phones[0].key;
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, phones));
  // A consonant/vowel pair within one allocated note remains a valid unit
  // and seam even when a different note overlaps it.
  f.region().lyrics.front().surface = U"か";
  f.region().unitSelectionOverrides.clear();
  f.region().seamOverrides.clear();
  const auto syllable = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(syllable);
  const auto& syllablePhones = syllable.value().pronunciation.tokens;
  CHECK(syllablePhones.size() == 3U);
  f.region().unitSelectionOverrides = {{.startKey = syllablePhones[0].key,
      .tokenCount = 2U, .unitId = "ka"}};
  f.region().seamOverrides = {{.incomingStartKey = syllablePhones[1].key, .seamAmount = 0.5F}};
  CHECK(seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, syllablePhones));
}

TEST_CASE("multi-voice compilation uses independent vowel context without rewriting custom tokens") {
  Fixture f;
  f.region().notes[1].startTick = seam::time::Tick{480};
  f.region().notes[1].durationTick = seam::time::Tick{1920};
  f.region().lyrics[1].surface = U"お";
  auto [lyric, note] = f.factory.makeNote(seam::time::Tick{960}, seam::time::Tick{960},
      69U, U"-", seam::domain::Language::Japanese);
  f.region().lyrics.push_back(lyric); f.region().notes.push_back(note);
  const auto original = f.project;
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(pronunciation);
  CHECK(pronunciation.value().pronunciation.tokens.back().symbol == "o");
  const auto compiled = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(compiled); CHECK(compiled.value().size() == 2U);
  const auto& voice = compiled.value()[0].performance;
  CHECK(voice.notes().size() == 2U);
  const auto held = voice.at(voice.notes()[1].startFrame);
  CHECK(held.noteId == note.id); CHECK(!held.reattack);
  CHECK_NEAR(*held.scoreFrequencyHz, 261.625565, 0.00001);
  CHECK(f.project == original);
  auto custom = pronunciation.value().pronunciation.tokens;
  custom.back().symbol = "i";
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, custom));
  std::vector<std::vector<seam::domain::PhonemeToken>> independent{
      {custom[0], custom[2]}, {custom[1]}};
  independent[0][0].symbol = "custom-vowel";
  independent[0][1].symbol = "custom-vowel";
  const auto explicitContext = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U,
      custom, 16U, independent);
  CHECK(explicitContext);
  CHECK(!explicitContext.value()[0].performance.notes()[1].reattack);
  CHECK(f.project == original);
  auto incomplete = independent;
  incomplete[0].pop_back();
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, custom, 16U, incomplete));
  auto swapped = independent;
  std::swap(swapped[0], swapped[1]);
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, custom, 16U, swapped));
  auto missingVoice = independent;
  missingVoice.pop_back();
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, custom, 16U, missingVoice));
  auto duplicate = independent;
  duplicate[0].push_back(duplicate[0].back());
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, custom, 16U, duplicate));
  auto oversized = independent;
  oversized[0].resize(16385U, independent[0].front());
  CHECK(!seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U, custom, 16U, oversized));
  independent[0][1].symbol = "different-vowel";
  const auto mismatched = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U,
      custom, 16U, independent);
  CHECK(mismatched); CHECK(mismatched.value()[0].performance.notes()[1].reattack);
  // An authored override is resolved independently and remains authoritative:
  // an incompatible held vowel must not acquire the other voice's linkage.
  f.region().phonemeOverrides = {{.key = {note.id, 0U}, .symbol = "o", .locked = true}};
  const auto overridden = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(overridden);
  const auto explicitVoice = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U,
      overridden.value().pronunciation.tokens);
  CHECK(explicitVoice);
  CHECK(explicitVoice.value()[0].performance.notes()[1].reattack);
}

TEST_CASE("score voice allocation preserves a unique shared-lyric predecessor") {
  Fixture f;
  f.region().notes[0].durationTick = seam::time::Tick{480};
  f.region().notes[1].startTick = seam::time::Tick{240};
  f.region().notes[1].durationTick = seam::time::Tick{480};
  f.region().notes[1].articulation = seam::domain::NoteArticulation::Legato;
  auto [lyric, note] = f.factory.makeNote(seam::time::Tick{720}, seam::time::Tick{480}, 69U, U"あ", seam::domain::Language::Japanese);
  note.lyricTokenId = f.region().notes[1].lyricTokenId;
  note.articulation = seam::domain::NoteArticulation::Legato;
  f.region().notes.push_back(note);
  const auto plan = seam::synthesis::allocateScoreVoices(f.region());
  CHECK(plan); CHECK(plan.value().voices.size() == 2U);
  CHECK(plan.value().voices[0].size() == 1U);
  CHECK(plan.value().voices[1] == (std::vector{f.region().notes[1].id, note.id}));
  lyric.surface = U"-";
  f.region().lyrics.push_back(lyric);
  f.region().notes.back().lyricTokenId = lyric.id;
  CHECK(seam::synthesis::allocateScoreVoices(f.region()).value() == plan.value());
  f.region().notes[0].durationTick = seam::time::Tick{720};
  f.region().notes[0].lyricTokenId = note.lyricTokenId;
  f.region().notes[0].articulation = seam::domain::NoteArticulation::Legato;
  CHECK(!seam::synthesis::allocateScoreVoices(f.region()));
}

TEST_CASE("staccato release stays closed through gaps and follows tempo-resolved duration") {
  Fixture f;
  f.region().notes.front().articulation = seam::domain::NoteArticulation::Staccato;
  f.region().notes[1].startTick = seam::time::Tick{1920};
  CHECK(f.project.tempoMap().addOrReplace(seam::time::Tick{1440}, 60.0));
  for (const auto rate : {8000U, 44100U, 48000U}) {
    const auto compiled = seam::synthesis::compileScorePerformance(f.project, f.region(), rate);
    CHECK(compiled);
    const auto& first = compiled.value().notes()[0];
    const auto& second = compiled.value().notes()[1];
    CHECK(first.closesPhoneticTail); CHECK(!second.closesPhoneticTail);
    CHECK(first.gateEndFrame - first.startFrame == (first.endFrame - first.startFrame) / 2);
    CHECK(first.gateEndFrame - first.releaseStartFrame <= rate / 100U);
    CHECK(compiled.value().at(first.gateEndFrame).articulationGain == 0.0F);
    CHECK(compiled.value().at(first.endFrame).articulationGain == 0.0F);
    CHECK(compiled.value().at(second.startFrame - 1).articulationGain == 0.0F);
    CHECK(compiled.value().at(second.startFrame).articulationGain == 1.0F);
  }
}

TEST_CASE("compatible explicit vowel continuation links pitch while repeated syllables reattack") {
  Fixture f;
  f.region().notes[1].articulation = seam::domain::NoteArticulation::Legato;
  const auto compile = [&] {
    const auto phones = seam::phonemizer::resolveJapanesePronunciation(f.region());
    CHECK(phones);
    return seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U, phones.value().pronunciation.tokens);
  };
  const auto repeated = compile();
  CHECK(repeated);
  CHECK(repeated.value().notes()[1].reattack);
  f.region().lyrics[1].surface = U"-";
  const auto linked = compile();
  CHECK(linked);
  const auto& note = linked.value().notes()[1];
  CHECK(!note.reattack);
  CHECK(note.transitionEndFrame - note.startFrame == 960);
  CHECK_NEAR(*linked.value().at(note.startFrame).scoreFrequencyHz, 261.625565, 0.00001);
  CHECK_NEAR(*linked.value().at(note.transitionEndFrame).scoreFrequencyHz, 391.995436, 0.00001);
  CHECK(*linked.value().at(note.startFrame + 480).scoreFrequencyHz > 261.7);
  CHECK(*linked.value().at(note.startFrame + 480).scoreFrequencyHz < 391.9);
  f.region().notes.front().articulation = seam::domain::NoteArticulation::Staccato;
  CHECK(compile().value().notes()[1].reattack);
  f.region().notes.front().articulation = seam::domain::NoteArticulation::Normal;
  f.region().notes[1].startTick = seam::time::Tick{1200};
  CHECK(compile().value().notes()[1].reattack);
}

TEST_CASE("PSOLA consumes both compiled melody plateaus inside one rendered unit") {
  Fixture f;
  const auto score = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(score);
  const auto samples = seam::test::support::sineWave(48000U, 440.0, 1.0);
  seam::voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("aa", {"a", "a"}, "a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  // This pure-vowel fixture sustains across both score notes. The generic
  // fixture's fixed 19,200-frame release would put the second note in a raw tail.
  unit.markers.releaseStart = 45600;
  unit.markers.loopEnd = 44000;
  for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
  }
  seam::synthesis::PsolaRenderParameters options;
  options.sourcePitchResidual = 0.0F;
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(score.value());
  options.performanceStartFrame = score.value().notes().front().startFrame;
  const auto rendered = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(rendered);
  const auto measure = [&](std::size_t start) {
    const auto pitch = seam::voicebank::analyzePitch(
        std::span<const float>{rendered.value().samples.data() + start, 10000U}, 48000U);
    CHECK(pitch);
    return seam::voicebank::medianVoicedPitch(pitch.value());
  };
  CHECK_NEAR(measure(10000U), 261.625565, 12.0);
  CHECK_NEAR(measure(29000U), 391.995436, 16.0);
  seam::synthesis::SpectralRenderParameters spectralOptions;
  spectralOptions.performance = options.performance;
  spectralOptions.performanceStartFrame = options.performanceStartFrame;
  const auto spectral = seam::synthesis::SpectralClassicRenderer{}.render(unit, audio, 48000U, 48000, 60, spectralOptions);
  CHECK(spectral);
  seam::synthesis::StretchRenderParameters stretchOptions;
  stretchOptions.performance = options.performance;
  stretchOptions.performanceStartFrame = options.performanceStartFrame;
  const auto stretched = seam::synthesis::StretchUnitRenderer{}.render(unit, audio, 48000U, 48000, 60, stretchOptions);
  CHECK(stretched);
  seam::synthesis::RawRenderParameters rawOptions;
  rawOptions.performance = options.performance;
  rawOptions.performanceStartFrame = options.performanceStartFrame;
  const auto raw = seam::synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 48000, 60, rawOptions);
  CHECK(raw);
  for (const auto& [start, expected] : std::vector<std::pair<std::size_t, double>>{{10000U, 261.625565}, {29000U, 391.995436}}) {
    const auto pitch = seam::voicebank::analyzePitch(std::span<const float>{spectral.value().samples.data() + start, 10000U}, 48000U);
    CHECK(pitch);
    CHECK_NEAR(seam::voicebank::medianVoicedPitch(pitch.value()), expected, 16.0);
    const auto stretchPitch = seam::voicebank::analyzePitch(std::span<const float>{stretched.value().samples.data() + start, 10000U}, 48000U);
    CHECK(stretchPitch);
    CHECK_NEAR(seam::voicebank::medianVoicedPitch(stretchPitch.value()), expected, 16.0);
    const auto rawPitch = seam::voicebank::analyzePitch(std::span<const float>{raw.value().samples.data() + start, 10000U}, 48000U);
    CHECK(rawPitch);
    CHECK_NEAR(seam::voicebank::medianVoicedPitch(rawPitch.value()), expected, 16.0);
  }
  f.region().lyrics[1].surface = U"-";
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(pronunciation);
  const auto legatoScore = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(legatoScore);
  CHECK(!legatoScore.value().notes()[1].reattack);
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(legatoScore.value());
  const auto legato = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(legato);
  CHECK(!std::equal(legato.value().samples.begin() + 24000, legato.value().samples.begin() + 26000,
      rendered.value().samples.begin() + 24000));
  f.region().lyrics[1].surface = U"あ";
  f.region().notes.front().articulation = seam::domain::NoteArticulation::Staccato;
  const auto shortScore = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(shortScore);
  const auto& shortNote = shortScore.value().notes().front();
  CHECK(shortNote.gateEndFrame - shortNote.startFrame == 12000);
  CHECK(shortNote.gateEndFrame - shortNote.releaseStartFrame == 480);
  CHECK(shortScore.value().at(shortNote.releaseStartFrame).articulationGain == 1.0F);
  CHECK(shortScore.value().at(shortNote.releaseStartFrame + 240).articulationGain == 0.5F);
  CHECK(shortScore.value().at(shortNote.gateEndFrame).articulationGain == 0.0F);
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(shortScore.value());
  const auto shortened = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(shortened);
  for (std::size_t i = 0; i < shortened.value().samples.size(); ++i) {
    const auto gain = shortScore.value().at(options.performanceStartFrame + static_cast<seam::time::SampleFrame>(i)).articulationGain;
    CHECK(shortened.value().samples[i] == static_cast<float>(static_cast<double>(rendered.value().samples[i]) * gain));
  }
  f.region().notes.front().articulation = seam::domain::NoteArticulation::Normal;
  CHECK(f.region().dynamicsAutomation.replacePoints({{seam::time::Tick{0}, 1.0F}}));
  const auto unityScore = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(unityScore);
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(unityScore.value());
  const auto unity = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(unity);
  CHECK(unity.value().samples == rendered.value().samples);
  CHECK(f.region().dynamicsAutomation.replacePoints({{seam::time::Tick{0}, 0.25F}, {seam::time::Tick{1920}, 0.75F}}));
  const auto gainScore = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(gainScore);
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(gainScore.value());
  const auto gained = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(gained);
  rawOptions.performance = options.performance;
  const auto rawGained = seam::synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 48000, 60, rawOptions);
  CHECK(rawGained);
  for (std::size_t i = 0; i < gained.value().samples.size(); ++i) {
    const auto gain = gainScore.value().at(options.performanceStartFrame + static_cast<seam::time::SampleFrame>(i)).dynamicsGain;
    CHECK(gained.value().samples[i] == static_cast<float>(static_cast<double>(rendered.value().samples[i]) * gain));
    CHECK(rawGained.value().samples[i] == static_cast<float>(static_cast<double>(raw.value().samples[i]) * gain));
  }
  std::stop_source cancelled;
  cancelled.request_stop();
  CHECK(!seam::synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 48000, 60, rawOptions, cancelled.get_token()));
  options.pitchCurve = seam::synthesis::PitchCurve{{{0, 100.0F}}};
  CHECK(!seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options));
}

TEST_CASE("aligned PSOLA preserves an unvoiced source interval without periodic grains") {
  Fixture f;
  const auto score = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(score);
  auto samples = seam::test::support::sineWave(48000U, 440.0, 1.0);
  for (int i = 12000; i < 18000; ++i) samples[static_cast<std::size_t>(i)] = static_cast<float>((i * 17) % 31 - 15) / 100.0F;
  seam::voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("asa", {"a", "s", "a"}, "a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  unit.markers.releaseStart = 45600;
  unit.markers.loopEnd = 44000;
  for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
  }
  seam::synthesis::PsolaRenderParameters options;
  options.sourcePitchResidual = 0.0F;
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(score.value());
  options.performanceStartFrame = score.value().notes().front().startFrame;
  options.sourceMap = seam::synthesis::SourceTargetMap{
      {{0, options.performanceStartFrame}, {48000, options.performanceStartFrame + 48000}},
      {{0, 12000, true}, {12000, 18000, false}, {18000, 48000, true}}};
  const auto unvoiced = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(unvoiced);
  // DC removal may change a constant offset; relative unvoiced samples must
  // remain source-exact rather than being reconstructed from periodic grains.
  for (std::size_t i = 13001; i < 17000; ++i) {
    CHECK_NEAR(unvoiced.value().samples[i] - unvoiced.value().samples[13000], samples[i] - samples[13000], 0.000001);
  }
  options.sourceMap->voicing[1].voiced = true;
  const auto periodic = seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options);
  CHECK(periodic);
  CHECK(periodic.value().samples != unvoiced.value().samples);
  options.sourceMap->voicing[1].voiced = false;
  seam::synthesis::SpectralRenderParameters spectralOptions;
  spectralOptions.performance = options.performance;
  spectralOptions.performanceStartFrame = options.performanceStartFrame;
  spectralOptions.sourceMap = options.sourceMap;
  const auto spectral = seam::synthesis::SpectralClassicRenderer{}.render(unit, audio, 48000U, 48000, 60, spectralOptions);
  CHECK(spectral);
  for (std::size_t i = 13001; i < 17000; ++i) {
    CHECK_NEAR(spectral.value().samples[i] - spectral.value().samples[13000], samples[i] - samples[13000], 0.000001);
  }
  seam::synthesis::StretchRenderParameters stretchOptions;
  stretchOptions.performance = options.performance;
  stretchOptions.performanceStartFrame = options.performanceStartFrame;
  stretchOptions.sourceMap = options.sourceMap;
  const auto stretched = seam::synthesis::StretchUnitRenderer{}.render(unit, audio, 48000U, 48000, 60, stretchOptions);
  CHECK(stretched);
  for (std::size_t i = 13001; i < 17000; ++i) {
    CHECK_NEAR(stretched.value().samples[i] - stretched.value().samples[13000], samples[i] - samples[13000], 0.000001);
  }
  stretchOptions.sourceDrift = 0.5F;
  CHECK(!seam::synthesis::StretchUnitRenderer{}.render(unit, audio, 48000U, 48000, 60, stretchOptions));
  options.sourceMap->voicing[1].start = 11000;
  CHECK(!seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 48000, 60, options));
}

TEST_CASE("separate vowel continuation suppresses the recorded attack without moving its landmark") {
  Fixture f;
  const auto compile = [&] {
    const auto phones = seam::phonemizer::resolveJapanesePronunciation(f.region());
    CHECK(phones);
    const auto result = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U, phones.value().pronunciation.tokens);
    CHECK(result);
    return std::make_shared<const seam::synthesis::CompiledScorePerformance>(result.value());
  };
  const auto repeated = compile();
  f.region().lyrics[1].surface = U"-";
  const auto continued = compile();
  auto samples = seam::test::support::sineWave(48000U, 440.0, 0.5);
  samples[1800] = 1.0F;
  seam::voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("a", {"a"}, "a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  for (auto frame = unit.markers.stableStart; frame < *unit.markers.releaseStart; frame += 109) {
    unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = false});
  }
  const auto origin = repeated->notes()[1].startFrame - unit.markers.vowelOnset;
  auto rawSamples = samples;
  std::fill(rawSamples.begin() + 1700, rawSamples.begin() + 1900, 1.0F);
  const seam::voicebank::AudioBuffer rawAudio{.sampleRate = 48000U, .channels = 1U, .interleaved = rawSamples};
  seam::synthesis::RawRenderParameters rawParameters;
  rawParameters.performanceVowelFrame = repeated->notes()[1].startFrame;
  rawParameters.performance = repeated;
  const auto rawAttack = seam::synthesis::RawLoopRenderer{}.render(unit, rawAudio, 48000U, 27600, 67, rawParameters);
  rawParameters.performance = continued;
  const auto rawSustain = seam::synthesis::RawLoopRenderer{}.render(unit, rawAudio, 48000U, 27600, 67, rawParameters);
  CHECK(rawAttack); CHECK(rawSustain);
  CHECK(*std::max_element(rawAttack.value().samples.begin(), rawAttack.value().samples.begin() + 6000) > 0.8F);
  CHECK(*std::max_element(rawSustain.value().samples.begin(), rawSustain.value().samples.begin() + 6000) < 0.5F);
  CHECK(rawAttack.value().vowelOnsetOffset == rawSustain.value().vowelOnsetOffset);
  CHECK(rawAttack.value().samples.size() == rawSustain.value().samples.size());
  for (const auto backend : {seam::voicebank::RendererHint::ClassicPsola, seam::voicebank::RendererHint::SpectralClassic,
      seam::voicebank::RendererHint::Stretch}) {
    for (const bool aligned : {false, true}) {
    const auto render = [&](const auto& score) {
      if (backend == seam::voicebank::RendererHint::SpectralClassic) {
        seam::synthesis::SpectralRenderParameters parameters;
        parameters.performance = score; parameters.performanceStartFrame = origin;
        if (aligned) parameters.sourceMap = seam::synthesis::SourceTargetMap{
            {{0, origin}, {3600, origin + 3600}, {24000, origin + 27600}}, {{0, 24000, true}}};
        return seam::synthesis::SpectralClassicRenderer{}.render(unit, audio, 48000U, 27600, 67, parameters);
      }
      if (backend == seam::voicebank::RendererHint::Stretch) {
        seam::synthesis::StretchRenderParameters parameters;
        parameters.performance = score; parameters.performanceStartFrame = origin;
        if (aligned) parameters.sourceMap = seam::synthesis::SourceTargetMap{
            {{0, origin}, {3600, origin + 3600}, {24000, origin + 27600}}, {{0, 24000, true}}};
        return seam::synthesis::StretchUnitRenderer{}.render(unit, audio, 48000U, 27600, 67, parameters);
      }
      seam::synthesis::PsolaRenderParameters parameters;
      parameters.performance = score; parameters.performanceStartFrame = origin;
      if (aligned) {
        parameters.sourceMap = seam::synthesis::SourceTargetMap{
            {{0, origin}, {3600, origin + 3600}, {24000, origin + 27600}},
            {{0, 24000, true}}};
      }
      return seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 27600, 67, parameters);
    };
    const auto attacked = render(repeated);
    const auto sustained = render(continued);
    CHECK(attacked); CHECK(sustained);
    CHECK(attacked.value().samples[1800] > 0.9F);
    CHECK(std::abs(sustained.value().samples[1800]) < 0.5F);
    CHECK(attacked.value().vowelOnsetOffset == sustained.value().vowelOnsetOffset);
    CHECK(attacked.value().samples.size() == sustained.value().samples.size());
    if (backend != seam::voicebank::RendererHint::ClassicPsola && aligned) {
      const auto heldPitch = seam::voicebank::analyzePitch(
          std::span<const float>{sustained.value().samples.data() + 5200, 2400U}, 48000U);
      CHECK(heldPitch);
      CHECK_NEAR(seam::voicebank::medianVoicedPitch(heldPitch.value()), 391.995436, 12.0);
    }
    }
  }
}

TEST_CASE("raw performance gain uses the transposed rendered vowel origin") {
  Fixture f;
  const auto normal = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(normal);
  CHECK(f.region().dynamicsAutomation.replacePoints({{seam::time::Tick{0}, 0.5F}}));
  const auto quiet = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(quiet);
  const auto samples = seam::test::support::sineWave(48000U, 440.0, 0.5);
  seam::voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = samples};
  const auto unit = seam::test::support::makeUnit("a", {"a"}, "a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  seam::synthesis::RawRenderParameters options;
  options.performanceVowelFrame = normal.value().notes()[0].startFrame;
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(normal.value());
  const auto baseline = seam::synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 30000, 60, options);
  CHECK(baseline);
  CHECK(baseline.value().vowelOnsetOffset != unit.markers.vowelOnset);
  options.performance = std::make_shared<const seam::synthesis::CompiledScorePerformance>(quiet.value());
  const auto rendered = seam::synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 30000, 60, options);
  CHECK(rendered);
  const auto origin = *options.performanceVowelFrame - rendered.value().vowelOnsetOffset;
  for (std::size_t i = 0; i < rendered.value().samples.size(); ++i) {
    const auto gain = quiet.value().at(origin + static_cast<seam::time::SampleFrame>(i)).dynamicsGain;
    CHECK(rendered.value().samples[i] == static_cast<float>(static_cast<double>(baseline.value().samples[i]) * gain));
  }
}

TEST_CASE("accepted attack shapes reattacks with absolute-time ownership and neutral zero") {
  Fixture f;
  using namespace seam::domain;
  using seam::time::Tick;
  auto& state = f.region().performance;
  const auto noteId = f.region().notes.front().id;
  state.takes = {{.id = "attack", .sourceRegionId = f.id,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Attack, {{Tick{0}, 100.0}, {Tick{1920}, 100.0}}}}}};
  const auto baseline = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(baseline);
  CHECK(!baseline.value().at(baseline.value().notes()[0].startFrame).attackMilliseconds);
  state.accepted = {{"attack", PerformanceChannel::Attack, noteId, Tick{0}}};
  for (const auto rate : {8000U, 44100U, 192000U}) {
    const auto score = seam::synthesis::compileScorePerformance(f.project, f.region(), rate);
    CHECK(score);
    const auto start = score.value().notes()[0].startFrame;
    CHECK(score.value().at(start).articulationGain == 0.0F);
    CHECK(score.value().at(start + rate / 20U).articulationGain == 0.5F);
    CHECK(score.value().at(start + rate / 10U).articulationGain == 1.0F);
    CHECK(!score.value().at(score.value().notes()[1].startFrame).attackMilliseconds);
  }
  const auto attack = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(attack);
  const auto samples = seam::test::support::sineWave(48000U, 440.0, 0.5);
  seam::voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("a", {"a"}, "a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  unit.pitchMarks.clear();
  for (std::int64_t i = 100; i < 24000; i += 109) unit.pitchMarks.push_back({i});
  for (int backend = 0; backend < 4; ++backend) {
    const auto render = [&](const auto& performance) {
      const auto score = std::make_shared<const seam::synthesis::CompiledScorePerformance>(performance);
      const auto origin = performance.notes()[0].startFrame;
      if (backend == 0) {
        seam::synthesis::RawRenderParameters options;
        options.performance = score; options.performanceStartFrame = origin;
        return seam::synthesis::RawLoopRenderer{}.render(unit, audio, 48000U, 24000, 60, options);
      }
      if (backend == 1) {
        seam::synthesis::PsolaRenderParameters options;
        options.performance = score; options.performanceStartFrame = origin;
        return seam::synthesis::ClassicPsolaRenderer{}.render(unit, audio, 48000U, 24000, 60, options);
      }
      if (backend == 2) {
        seam::synthesis::SpectralRenderParameters options;
        options.performance = score; options.performanceStartFrame = origin;
        return seam::synthesis::SpectralClassicRenderer{}.render(unit, audio, 48000U, 24000, 60, options);
      }
      seam::synthesis::StretchRenderParameters options;
      options.performance = score; options.performanceStartFrame = origin;
      return seam::synthesis::StretchUnitRenderer{}.render(unit, audio, 48000U, 24000, 60, options);
    };
    const auto neutral = render(baseline.value());
    const auto shaped = render(attack.value());
    CHECK(neutral); CHECK(shaped);
    CHECK(neutral.value().samples.size() == shaped.value().samples.size());
    for (std::size_t i = 0; i < shaped.value().samples.size(); ++i) {
      const auto gain = attack.value().at(attack.value().notes()[0].startFrame +
          static_cast<seam::time::SampleFrame>(i)).articulationGain;
      CHECK(shaped.value().samples[i] == static_cast<float>(static_cast<double>(neutral.value().samples[i]) * gain));
    }
  }
  state.ownership = {{PerformanceChannel::Attack, noteId, ManualPerformanceMode::Replace, {}}};
  const auto manual = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(manual); CHECK(manual.value().at(manual.value().notes()[0].startFrame).articulationGain == 1.0F);
  state.ownership.clear();
  state.takes[0].lanes[0].points = {{Tick{0}, 1000.0}};
  f.region().notes[0].articulation = NoteArticulation::Staccato;
  const auto gated = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(gated);
  const auto& gatedNote = gated.value().notes()[0];
  CHECK_NEAR(gated.value().at(gatedNote.startFrame + 4800).articulationGain, 0.1, 0.000001);
  CHECK(gated.value().at(gatedNote.gateEndFrame).articulationGain == 0.0F);
  f.region().notes[0].articulation = NoteArticulation::Normal;
  state.takes[0].lanes[0].points = {{Tick{0}, 0.0}};
  const auto zero = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(zero); CHECK(zero.value().at(zero.value().notes()[0].startFrame).articulationGain == 1.0F);
  state.takes[0].lanes[0].points = {{Tick{0}, 100.0}};
  state.accepted[0].scope = f.region().notes[1].id;
  f.region().lyrics[1].surface = U"-";
  const auto phones = seam::phonemizer::resolveJapanesePronunciation(f.region());
  CHECK(phones);
  const auto legato = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U,
      phones.value().pronunciation.tokens);
  CHECK(legato);
  const auto held = legato.value().at(legato.value().notes()[1].startFrame);
  CHECK(!held.reattack); CHECK(held.attackMilliseconds == 100.0); CHECK(held.articulationGain == 1.0F);
}

TEST_CASE("accepted release fades to note end preserves continuation and keeps source tails closed") {
  Fixture f;
  using namespace seam::domain;
  using seam::time::Tick;
  auto& state = f.region().performance;
  const auto noteId = f.region().notes.back().id;
  state.takes = {{.id = "release", .sourceRegionId = f.id,
      .resource = {SingerResourceKind::Procedural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Release, {{Tick{0}, 100.0}, {Tick{1920}, 100.0}}}}}};
  state.accepted = {{"release", PerformanceChannel::Release, noteId, Tick{0}}};
  for (const auto rate : {8000U, 44100U, 192000U}) {
    const auto score = seam::synthesis::compileScorePerformance(f.project, f.region(), rate); CHECK(score);
    const auto end = score.value().notes().back().endFrame;
    CHECK(score.value().notes().back().closesPhoneticTail);
    CHECK(score.value().at(end - rate / 10U).articulationGain == 1.0F);
    CHECK(score.value().at(end - rate / 20U).articulationGain == 0.5F);
    CHECK(score.value().at(end).articulationGain == 0.0F);
    CHECK(score.value().at(end + 100).articulationGain == 0.0F);
    CHECK(!score.value().at(score.value().notes().front().startFrame).releaseMilliseconds);
    std::vector<float> samples(rate / 10U + 100U, 1.0F);
    CHECK(seam::synthesis::applyCompiledPerformanceGain(samples, score.value(), end - rate / 10U));
    CHECK(samples[rate / 20U] == 0.5F); CHECK(samples.back() == 0.0F);
  }
  state.ownership = {{PerformanceChannel::Release, noteId, ManualPerformanceMode::Replace, {}}};
  const auto manual = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(manual);
  const auto end = manual.value().notes().back().endFrame;
  CHECK(!manual.value().at(end - 1).releaseMilliseconds); CHECK(manual.value().at(end - 1).articulationGain == 1.0F);
  CHECK(!manual.value().notes().back().closesPhoneticTail);
  CHECK(manual.value().at(end + 100).articulationGain == 1.0F);
  state.ownership.clear(); state.takes[0].lanes[0].points = {{Tick{0}, 0.0}};
  const auto zero = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U); CHECK(zero);
  CHECK(!zero.value().notes().back().closesPhoneticTail);
  CHECK(zero.value().at(end - 1).articulationGain == 1.0F); CHECK(zero.value().at(end).articulationGain == 1.0F);
  state.takes[0].lanes[0].points = {{Tick{0}, 100.0}};
  state.accepted[0].scope = f.region().notes.front().id;
  f.region().lyrics[1].surface = U"-";
  const auto phones = seam::phonemizer::resolveJapanesePronunciation(f.region()); CHECK(phones);
  const auto linked = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U, phones.value().pronunciation.tokens); CHECK(linked);
  CHECK(!linked.value().notes()[1].reattack);
  CHECK(!linked.value().notes().front().closesPhoneticTail);
  CHECK(linked.value().at(linked.value().notes()[0].endFrame - 1).articulationGain == 1.0F);
}

TEST_CASE("compiled expression indexes preserve dense unordered selections and boundaries") {
  Fixture f;
  using namespace seam::domain;
  using seam::time::Tick;
  f.region().notes.resize(1U);
  f.region().notes[0].durationTick = Tick{3840};
  auto& state = f.region().performance;
  for (int i = 0; i < 2; ++i) {
    state.takes.push_back({.id = i == 0 ? "quiet" : "loud", .sourceRegionId = f.id,
        .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
        .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
        .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{3840}},
        .lanes = {{PerformanceChannel::Dynamics, {{Tick{0}, i == 0 ? 0.25 : 0.75}}}}});
  }
  for (int i = 0; i < 2048; ++i) {
    const PerformanceTimeRange range{Tick{i}, Tick{i + 1}};
    state.accepted.push_back({i % 2 == 0 ? "quiet" : "loud", PerformanceChannel::Dynamics, range, Tick{0}});
    if (i % 3 == 0) state.ownership.push_back({PerformanceChannel::Dynamics, range, ManualPerformanceMode::Replace, {}});
  }
  std::reverse(state.accepted.begin(), state.accepted.end());
  std::reverse(state.ownership.begin(), state.ownership.end());
  CHECK(f.project.tempoMap().addOrReplace(f.region().startTick + Tick{1024}, 90.0));
  const auto original = f.project;
  for (const auto rate : {8000U, 44100U, 192000U}) {
    auto compiled = seam::synthesis::compileScorePerformance(f.project, f.region(), rate);
    CHECK(compiled);
    const auto copied = compiled.value();
    const auto moved = std::move(compiled.value());
    for (int i = 2047; i >= 0; --i) {
      const auto frame = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{i}, rate);
      const auto end = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{i + 1}, rate);
      const auto expected = i % 3 == 0 ? 1.0F : i % 2 == 0 ? 0.25F : 0.75F;
      CHECK(copied.at(frame).dynamicsGain == expected);
      CHECK(moved.at(end - 1).dynamicsGain == expected);
    }
    const auto after = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{2048}, rate);
    CHECK(moved.at(after).dynamicsGain == 1.0F);
  }
  CHECK(f.project == original);
}

TEST_CASE("an accepted channel the audio path does not consume never becomes amplitude") {
  using namespace seam; using namespace domain; using time::Tick;
  Fixture f; const auto note = f.region().notes.front().id;
  auto& state = f.region().performance;
  // A timing proposal carries microseconds, not gain. It is consumed by the ordered
  // timing plan, so the per-frame audio path must leave the amplitude alone.
  state.takes = {{.id = "timing", .sourceRegionId = f.id,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Timing, {{Tick{0}, 30000.0}}}}}};
  state.accepted = {{"timing", PerformanceChannel::Timing, note, Tick{0}}};
  const auto compiled = synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(compiled);
  const auto& spans = compiled.value().notes();
  CHECK(spans.size() == 2U);
  CHECK(compiled.value().at(spans[0].startFrame).dynamicsGain == 1.0F);
  CHECK(compiled.value().at(spans[1].startFrame).dynamicsGain == 1.0F);
  CHECK(compiled.value().at(spans[0].startFrame).scoreFrequencyHz.has_value());
  CHECK(!compiled.value().at(spans[0].startFrame).selectedGeneratedDynamicsGain);
}

TEST_CASE("accepted pitch obeys explicit offset replacement and manual vibrato ownership") {
  Fixture f;
  using namespace seam::domain;
  using seam::time::Tick;
  auto& state = f.region().performance;
  const auto noteId = f.region().notes.front().id;
  state.takes = {{.id = "generated", .sourceRegionId = f.id,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{1920}},
      .lanes = {{PerformanceChannel::Pitch, {{Tick{0}, 6900.0}, {Tick{960}, std::nullopt}}},
                {PerformanceChannel::Dynamics, {{Tick{0}, 0.25}, {Tick{1920}, 0.25}}}}}};
  CHECK(f.region().pitchAutomation.upsert({.tick = Tick{0}, .cents = 100.0F}));
  const auto frequency = [&] {
    const auto compiled = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
    CHECK(compiled);
    return compiled.value().at(compiled.value().notes().front().startFrame);
  };
  // A proposal alone never affects the selected base.
  CHECK_NEAR(*frequency().scoreFrequencyHz, 277.182631, 0.00001);
  state.accepted = {{"generated", PerformanceChannel::Pitch, noteId, Tick{0}},
                    {"generated", PerformanceChannel::Dynamics, noteId, Tick{0}}};
  CHECK_NEAR(*frequency().scoreFrequencyHz, 440.0, 0.00001);
  CHECK(frequency().dynamicsGain == 0.25F);
  state.ownership = {{PerformanceChannel::Pitch, noteId, ManualPerformanceMode::PitchOffset, {}}};
  CHECK_NEAR(*frequency().scoreFrequencyHz, 466.163762, 0.00001);
  state.ownership.front().mode = ManualPerformanceMode::Replace;
  CHECK_NEAR(*frequency().scoreFrequencyHz, 277.182631, 0.00001);
  state.ownership.clear();
  state.ownership = {{PerformanceChannel::Pitch, PerformanceTimeRange{Tick{0}, Tick{480}}, ManualPerformanceMode::Replace, {}},
      {PerformanceChannel::Dynamics, noteId, ManualPerformanceMode::Replace, {}}};
  const auto ranged = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(ranged);
  const auto start = ranged.value().notes().front().startFrame;
  CHECK_NEAR(*ranged.value().at(start).scoreFrequencyHz, 277.182631, 0.00001);
  CHECK_NEAR(*ranged.value().at(start + 11999).scoreFrequencyHz, 277.182631, 0.00001);
  CHECK_NEAR(*ranged.value().at(start + 12000).scoreFrequencyHz, 440.0, 0.00001);
  CHECK(ranged.value().at(start).dynamicsGain == 1.0F);
  state.ownership.front().scope = PerformanceTimeRange{Tick{120}, Tick{480}};
  CHECK(f.project.tempoMap().addOrReplace(Tick{1200}, 90.0));
  for (const auto rate : {44100U, 48000U, 192000U}) {
    const auto exact = seam::synthesis::compileScorePerformance(f.project, f.region(), rate);
    CHECK(exact);
    const auto begin = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{120}, rate);
    const auto end = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{480}, rate);
    CHECK_NEAR(*exact.value().at(begin - 1).scoreFrequencyHz, 440.0, 0.00001);
    CHECK_NEAR(*exact.value().at(begin).scoreFrequencyHz, 277.182631, 0.00001);
    CHECK_NEAR(*exact.value().at(end - 1).scoreFrequencyHz, 277.182631, 0.00001);
    CHECK_NEAR(*exact.value().at(end).scoreFrequencyHz, 440.0, 0.00001);
  }
  state.ownership.clear();
  state.accepted.front().scope = PerformanceTimeRange{Tick{120}, Tick{480}};
  const auto scoped = seam::synthesis::compileScorePerformance(f.project, f.region(), 48000U);
  CHECK(scoped);
  const auto acceptedStart = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{120}, 48000U);
  const auto acceptedEnd = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{480}, 48000U);
  CHECK_NEAR(*scoped.value().at(acceptedStart - 1).scoreFrequencyHz, 277.182631, 0.00001);
  CHECK_NEAR(*scoped.value().at(acceptedStart).scoreFrequencyHz, 440.0, 0.00001);
  CHECK_NEAR(*scoped.value().at(acceptedEnd - 1).scoreFrequencyHz, 440.0, 0.00001);
  CHECK_NEAR(*scoped.value().at(acceptedEnd).scoreFrequencyHz, 277.182631, 0.00001);
  state.accepted.front().scope = noteId;
  state.ownership.clear();
  state.takes.front().lanes.front().points = {{Tick{0}, 6900.0}, {Tick{480}, std::nullopt}, {Tick{720}, 6700.0}};
  for (const auto offset : {0, 120}) {
  state.accepted.front().sourceTickOffset = Tick{offset};
  for (const auto rate : {44100U, 48000U, 192000U}) {
    const auto voiced = seam::synthesis::compileScorePerformance(f.project, f.region(), rate);
    CHECK(voiced);
    const auto off = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{480 - offset}, rate);
    const auto on = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{720 - offset}, rate);
    CHECK(voiced.value().at(off - 1).scoreFrequencyHz);
    CHECK_NEAR(*voiced.value().at(off - 1).scoreFrequencyHz, 440.0, 0.00001);
    CHECK(!voiced.value().at(off).scoreFrequencyHz);
    CHECK(!voiced.value().at(on - 1).scoreFrequencyHz);
    CHECK(voiced.value().at(on).scoreFrequencyHz);
    CHECK_NEAR(*voiced.value().at(on).scoreFrequencyHz, 391.995436, 0.00001);
  }
  }
  state.accepted.front().sourceTickOffset = Tick{0};
  state.takes.front().lanes.front().points = {{Tick{0}, 6900.0}, {Tick{960}, std::nullopt}};
  f.region().notes.front().vibrato.enabled = true;
  CHECK_NEAR(*frequency().scoreFrequencyHz, 277.182631, 0.00001);
  f.region().notes.front().vibrato.enabled = false;
  state.accepted.front().sourceTickOffset = Tick{960};
  CHECK(!frequency().scoreFrequencyHz);
  CHECK(frequency().noteId == noteId);
  state.accepted.front().sourceTickOffset = Tick{0};
  CHECK(f.region().pitchAutomation.erase(Tick{0}));
  f.region().notes[1].startTick = Tick{480};
  const auto beforeVoices = f.project;
  const auto voices = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U);
  CHECK(voices); CHECK(voices.value().size() == 2U);
  const auto overlapFrame = f.project.tempoMap().sampleFrameAt(f.region().startTick + Tick{600}, 48000U);
  CHECK_NEAR(*voices.value()[0].performance.at(overlapFrame).scoreFrequencyHz, 440.0, 0.00001);
  CHECK_NEAR(*voices.value()[1].performance.at(overlapFrame).scoreFrequencyHz, 391.995436, 0.00001);
  CHECK(voices.value()[0].performance.at(overlapFrame).dynamicsGain == 0.25F);
  CHECK(voices.value()[1].performance.at(overlapFrame).dynamicsGain == 1.0F);
  CHECK(f.project == beforeVoices);
  state.ownership = {{PerformanceChannel::Pitch, noteId, ManualPerformanceMode::Replace, {}}};
  const auto manualVoices = seam::synthesis::compileScoreVoices(f.project, f.region(), 48000U);
  CHECK(manualVoices);
  CHECK_NEAR(*manualVoices.value()[0].performance.at(overlapFrame).scoreFrequencyHz, 261.625565, 0.00001);
  CHECK_NEAR(*manualVoices.value()[1].performance.at(overlapFrame).scoreFrequencyHz, 391.995436, 0.00001);
}
