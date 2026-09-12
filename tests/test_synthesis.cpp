#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/synthesis/classic_psola.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/synthesis/seam_composer.hpp"
#include "seam/synthesis/spectral_classic.hpp"
#include "seam/synthesis/stretch_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

namespace {

struct SynthesisFixture final {
  static constexpr std::uint32_t sampleRate = 48000;

  seam::application::ProjectFactory factory{500};
  seam::domain::Project project{factory.createProject("Synthesis fixture")};
  seam::domain::TrackId trackId{factory.addVocalTrack(project, "Voice")};
  seam::domain::RegionId regionId{
      factory.addRegion(project, trackId, "Phrase", seam::time::Tick{0},
                        seam::time::Tick{15360})};

  SynthesisFixture() {
    static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 120.0));
  }

  seam::domain::NoteId add(std::u32string lyric, seam::time::Tick start,
                           std::uint8_t midi = 69) {
    auto [token, note] = factory.makeNote(start, seam::time::Tick{960}, midi,
                                          std::move(lyric),
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

TEST_CASE("timing solver places multiple syllables sequentially within one note") {
  SynthesisFixture fixture;
  fixture.add(U"かき", seam::time::Tick{1920});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  auto phonemes = seam::phonemizer::JapaneseKanaPhonemizer{}.phonemize(*region);
  CHECK(phonemes.tokens.size() == 4U);
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("ka", {"k", "a"}, "ka.wav", 69, seam::voicebank::UnitKind::Cv, 24000),
      seam::test::support::makeUnit("ki", {"k", "i"}, "ki.wav", 69, seam::voicebank::UnitKind::Cv, 24000)});
  const auto selected = seam::synthesis::DeterministicUnitSelector{}.select(manifest, *region, phonemes.tokens, "original");
  CHECK(selected); CHECK(selected.value().entries.size() == 2U);
  seam::synthesis::TimingSolver solver;
  const auto timing = solver.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(timing); CHECK(timing.value().placements.size() == 2U);
  CHECK(timing.value().placements[1].desiredVowelOnset - timing.value().placements[0].desiredVowelOnset == 12000);
  CHECK(timing.value().placements[0].destinationEnd == timing.value().placements[1].desiredVowelOnset);
  phonemes.tokens[3].timing.startOffset = 280000; // Default second nucleus 250 ms + 30 ms.
  const auto edited = solver.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(edited);
  CHECK(edited.value().placements[1].desiredVowelOnset - timing.value().placements[1].desiredVowelOnset == 1440);
  phonemes.tokens[3].timing.startOffset = 200000;
  const auto earlier = solver.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(earlier);
  CHECK(earlier.value().placements[0].destinationEnd == earlier.value().placements[1].desiredVowelOnset);
  phonemes.tokens[1].timing.endOffset = 230000;
  const auto crossedEnd = solver.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(!crossedEnd);
  CHECK(crossedEnd.error().message.find("crosses the next nucleus") != std::string::npos);
  phonemes.tokens[1].timing.endOffset.reset();
  phonemes.tokens[3].timing.startOffset = -1000;
  const auto reversed = solver.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(!reversed);
  CHECK(reversed.error().message.find("strictly ordered") != std::string::npos);
  auto invalid = selected.value();
  invalid.entries.front().tokenCount = 0U;
  CHECK(!solver.solve(fixture.project, *region, phonemes.tokens, invalid, manifest, 48000U));
  phonemes.tokens[3].timing.startOffset = 600000;
  CHECK(!solver.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U));
}

TEST_CASE("interior timing edits select compatible smaller units and reject forced long units") {
  for (bool startEdit : {false, true}) {
    SynthesisFixture fixture;
    fixture.add(U"かき", seam::time::Tick{1920});
    const auto* region = fixture.project.findRegion(fixture.regionId);
    auto phonemes = seam::phonemizer::JapaneseKanaPhonemizer{}.phonemize(*region);
    auto manifest = seam::test::support::makeManifest({
        seam::test::support::makeUnit("long", {"k", "a", "k", "i"}, "long.wav", 69, seam::voicebank::UnitKind::Cv, 24000),
        seam::test::support::makeUnit("ka", {"k", "a"}, "ka.wav", 69, seam::voicebank::UnitKind::Cv, 24000),
        seam::test::support::makeUnit("ki", {"k", "i"}, "ki.wav", 69, seam::voicebank::UnitKind::Cv, 24000)});
    seam::synthesis::DeterministicUnitSelector selector;
    const auto original = selector.select(manifest, *region, phonemes.tokens, "original");
    CHECK(original); CHECK(original.value().entries.size() == 1U);
    const auto originalTiming = seam::synthesis::TimingSolver{}.solve(fixture.project, *region,
        phonemes.tokens, original.value(), manifest, 48000U);
    CHECK(originalTiming);
    const auto& targets = originalTiming.value().placements.front().phonemeTargets;
    CHECK(targets.size() == 4U);
    for (std::size_t i = 0U; i < targets.size(); ++i) CHECK(targets[i].key == phonemes.tokens[i].key);
    CHECK(targets[1].nucleusKey == phonemes.tokens[1].key);
    CHECK(targets[3].nucleusKey == phonemes.tokens[3].key);
    CHECK(targets[3].nucleusFrame - targets[1].nucleusFrame == 12000);
    CHECK(targets[1].endFrame == targets[3].nucleusFrame);
    auto duplicated = original.value();
    duplicated.entries.push_back(duplicated.entries.front());
    CHECK(!seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, duplicated, manifest, 48000U));
    if (startEdit) phonemes.tokens[2].timing.startOffset = 180000;
    else phonemes.tokens[1].timing.endOffset = 200000;
    const auto selected = selector.select(manifest, *region, phonemes.tokens, "original");
    CHECK(selected); CHECK(selected.value().entries.size() == 2U);
    CHECK(selected.value().entries[0].unitId == "ka");
    CHECK(selected.value().entries[1].unitId == "ki");
    const auto timing = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
    CHECK(timing);
    const auto noteOn = fixture.project.tempoMap().sampleFrameAt(region->notes.front().startTick, 48000.0);
    if (startEdit) CHECK(timing.value().placements[1].destinationStart == noteOn + 8640);
    else CHECK(timing.value().placements[0].destinationEnd == noteOn + 9600);
    const auto root = seam::test::support::temporaryDirectory("interior-timing-audio");
    const auto source = seam::test::support::sineWave(48000U, 440.0, 0.5);
    for (const auto& unit : manifest.units) CHECK(seam::voicebank::writeMonoPcm16Wav(root / unit.audioPath, 48000U, source));
    const auto audio = seam::synthesis::RawPhraseRenderer{}.render(manifest, root, timing.value(), 48000U,
        {}, {.sampleRate = 48000U});
    CHECK(audio);
    CHECK(audio.value().placements.size() == 2U);
    if (startEdit) CHECK(audio.value().placements[1].alignedStart == noteOn + 8640);
    else CHECK(audio.value().placements[0].alignedStart + audio.value().placements[0].frameCount == noteOn + 9600);
    CHECK(!seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, original.value(), manifest, 48000U));
    const std::vector<seam::domain::UnitSelectionOverride> forced{{.startKey = phonemes.tokens.front().key,
        .tokenCount = 4U, .unitId = "long"}};
    const auto conflict = selector.select(manifest, *region, phonemes.tokens, "original", forced);
    CHECK(!conflict);
    CHECK(conflict.error().message.find("interior phoneme timing") != std::string::npos);
  }
}

TEST_CASE("timing solver rejects short transitions instead of extending the target span") {
  SynthesisFixture fixture;
  const auto id = fixture.add(U"か", seam::time::Tick{1920});
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->findNote(id)->durationTick = seam::time::Tick{10};
  auto phonemes = seam::phonemizer::JapaneseKanaPhonemizer{}.phonemize(*region);
  auto manifest = seam::test::support::makeManifest({seam::test::support::makeUnit(
      "ka", {"k", "a"}, "ka.wav", 69, seam::voicebank::UnitKind::Cv, 24000)});
  const auto selected = seam::synthesis::DeterministicUnitSelector{}.select(manifest, *region, phonemes.tokens, "original");
  CHECK(selected);
  const auto before = fixture.project;
  const auto failed = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(!failed);
  CHECK(failed.error().message.find("too short") != std::string::npos);
  CHECK(fixture.project == before);
  phonemes.tokens.front().timing.startOffset = -200000;
  const auto earlyOnset = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(!earlyOnset);
  CHECK(earlyOnset.error().message.find("too short") != std::string::npos);
  region->findNote(id)->durationTick = seam::time::Tick{96};
  phonemes.tokens.front().timing.startOffset = -1000;
  const auto compressedOnset = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(compressedOnset);
  CHECK(compressedOnset.value().placements.front().explicitOnsetStart);
  phonemes.tokens.front().timing.startOffset.reset();
  region->findNote(id)->durationTick = seam::time::Tick{960};
  const auto valid = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U);
  CHECK(valid);
  CHECK(valid.value().placements.front().destinationEnd == fixture.project.tempoMap().sampleFrameAt(region->findNote(id)->endTick(), 48000.0));
  manifest.expectedSampleRate = 0U;
  CHECK(!seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 48000U));
}

TEST_CASE("timing marker conversion rounds exactly and rejects oversized metadata before conversion") {
  SynthesisFixture fixture;
  fixture.add(U"か", seam::time::Tick{1920});
  const auto* region = fixture.project.findRegion(fixture.regionId);
  const auto phonemes = seam::phonemizer::JapaneseKanaPhonemizer{}.phonemize(*region);
  auto manifest = seam::test::support::makeManifest({seam::test::support::makeUnit(
      "ka", {"k", "a"}, "ka.wav", 69, seam::voicebank::UnitKind::Cv, 24000)});
  manifest.units.front().markers.vowelOnset = 3601;
  const auto selected = seam::synthesis::DeterministicUnitSelector{}.select(manifest, *region, phonemes.tokens, "original");
  CHECK(selected);
  const auto normal = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 44100U);
  CHECK(normal);
  CHECK(normal.value().placements.front().desiredVowelOnset - normal.value().placements.front().destinationStart == 3308);
  auto& markers = manifest.units.front().markers;
  markers.audioEnd = std::numeric_limits<seam::time::SampleFrame>::max();
  markers.vowelOnset = markers.audioEnd / 2;
  markers.stableStart = markers.vowelOnset + 1000;
  markers.loopStart.reset(); markers.loopEnd.reset(); markers.releaseStart.reset();
  CHECK(manifest.units.front().validate());
  manifest.expectedSampleRate = 8000U;
  const auto oversized = seam::synthesis::TimingSolver{}.solve(fixture.project, *region, phonemes.tokens, selected.value(), manifest, 384000U);
  CHECK(!oversized);
  CHECK(oversized.error().message.find("conversion exceeds frame range") != std::string::npos);
}

TEST_CASE("deterministic unit selector prefers longer lower-score coverage") {
  SynthesisFixture fixture;
  fixture.add(U"か", seam::time::Tick{1920});
  fixture.add(U"ー", seam::time::Tick{2880});
  const auto* region = fixture.project.findRegion(fixture.regionId);

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  CHECK(phonemes.tokens.size() == 3);

  auto cvA = seam::test::support::makeUnit(
      "cv-a", {"k", "a"}, "audio/cv-a.wav", 69,
      seam::voicebank::UnitKind::Cv);
  auto cvB = cvA;
  cvB.id = "cv-b";
  cvB.take = 2;
  auto sustain = seam::test::support::makeUnit(
      "sustain-a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain);
  const auto manifest = seam::test::support::makeManifest({cvA, cvB, sustain});

  seam::synthesis::DeterministicUnitSelector selector;
  const auto first = selector.select(manifest, *region, phonemes.tokens, "original");
  const auto second = selector.select(manifest, *region, phonemes.tokens, "original");
  CHECK(first);
  CHECK(second);
  CHECK(first.value().entries.size() == 2);
  CHECK(first.value().entries.front().unitId == "cv-a");
  CHECK(first.value().entries.front().tokenCount == 2);
  CHECK(first.value().entries.front().alternatives ==
        (std::vector<std::string>{"cv-b"}));
  CHECK(first.value().entries == second.value().entries);
  CHECK_NEAR(first.value().totalScore, second.value().totalScore, 1.0e-12);
}

TEST_CASE("raw loop renderer preserves frame count and exposes vowel onset") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto sourceSamples = seam::test::support::sineWave(sampleRate, 440.0, 0.5);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = sourceSamples,
  };
  auto unit = seam::test::support::makeUnit(
      "k-a", {"k", "a"}, "audio/k-a.wav", 69,
      seam::voicebank::UnitKind::Cv, sourceSamples.size());

  seam::synthesis::RawLoopRenderer renderer;
  const auto rendered = renderer.render(
      unit, source, sampleRate, 36000, 69,
      seam::synthesis::RawRenderParameters{
          .loopPrint = 1.0F,
          .additionalGainDb = -3.0F,
      });
  CHECK(rendered);
  CHECK(rendered.value().samples.size() == 36000);
  CHECK(rendered.value().vowelOnsetOffset == unit.markers.vowelOnset);
  CHECK(std::any_of(rendered.value().samples.begin(), rendered.value().samples.end(),
                    [](float value) { return std::abs(value) > 0.01F; }));
  CHECK(std::all_of(rendered.value().samples.begin(), rendered.value().samples.end(),
                    [](float value) { return std::isfinite(value); }));
}

TEST_CASE("seam composer bounds negative preutterance without signed extent overflow") {
  seam::synthesis::PlacedRenderedUnit first{.destinationStart = -2,
      .unit = {.unitId = "first", .samples = {0.1F, 0.2F, 0.3F, 0.4F}, .vowelOnsetOffset = 2}};
  seam::synthesis::PlacedRenderedUnit second{.destinationStart = 2,
      .unit = {.unitId = "second", .samples = {0.5F, 0.6F}, .vowelOnsetOffset = 0}};
  seam::synthesis::SeamComposer composer;
  const auto normal = composer.compose(std::vector{first, second}, {.sampleRate = 48000U});
  CHECK(normal);
  CHECK(normal.value().startFrame == -2);
  CHECK(normal.value().samples.size() == 6U);
  first.destinationStart = std::numeric_limits<seam::time::SampleFrame>::min();
  const auto impossible = composer.compose(std::vector{first, second}, {.sampleRate = 48000U});
  CHECK(!impossible);
  CHECK(impossible.error().message.find("supported length") != std::string::npos);
}

TEST_CASE("seam overlap window bounds crossfading without shifting unit placement") {
  const seam::synthesis::PlacedRenderedUnit outgoing{.destinationStart = 0,
      .unit = {.unitId = "out", .samples = std::vector<float>(1024U, 0.25F), .vowelOnsetOffset = 0}};
  seam::synthesis::PlacedRenderedUnit incoming{.destinationStart = 512,
      .unit = {.unitId = "in", .samples = std::vector<float>(1024U, -0.5F), .vowelOnsetOffset = 32},
      .incomingBoundary = seam::synthesis::BoundarySeamSettings{.seamAmount = 0.0F,
          .maxOverlapFrames = 64, .phaseReset = 1.0F, .envelopeBlend = 0.0F}};
  seam::synthesis::SeamComposer composer;
  const auto shortWindow = composer.compose(std::vector{outgoing, incoming}, {.sampleRate = 48000U});
  CHECK(shortWindow);
  incoming.incomingBoundary->maxOverlapFrames = 256;
  const auto longWindow = composer.compose(std::vector{outgoing, incoming}, {.sampleRate = 48000U});
  CHECK(longWindow);
  CHECK(shortWindow.value().startFrame == 0);
  CHECK(shortWindow.value().startFrame == longWindow.value().startFrame);
  CHECK(shortWindow.value().samples.size() == 1536U);
  CHECK(shortWindow.value().samples.size() == longWindow.value().samples.size());
  CHECK_NEAR(shortWindow.value().samples[512U], 0.25F, 1.0e-6);
  CHECK_NEAR(shortWindow.value().samples[575U], -0.5F, 1.0e-6);
  CHECK_NEAR(shortWindow.value().samples[640U], -0.5F, 1.0e-6);
  CHECK(longWindow.value().samples[640U] > shortWindow.value().samples[640U]);
  incoming.incomingBoundary->maxOverlapFrames = 0;
  const auto noFade = composer.compose(std::vector{outgoing, incoming}, {.sampleRate = 48000U});
  CHECK(noFade);
  CHECK_NEAR(noFade.value().samples[512U], -0.5F, 1.0e-6);
  CHECK(noFade.value().samples.size() == shortWindow.value().samples.size());
  incoming.incomingBoundary->maxOverlapFrames = -1;
  CHECK(!composer.compose(std::vector{outgoing, incoming}, {.sampleRate = 48000U}));
  CHECK(incoming.destinationStart == 512);
  CHECK(incoming.unit.vowelOnsetOffset == 32);
  CHECK(incoming.unit.samples == std::vector<float>(1024U, -0.5F));
}

TEST_CASE("seam composer changes overlap character without changing bounds") {
  seam::synthesis::PlacedRenderedUnit left{
      .destinationStart = 0,
      .unit = seam::synthesis::RenderedUnit{
          .unitId = "left",
          .samples = std::vector<float>(32, 0.8F),
          .vowelOnsetOffset = 0,
      },
      .incomingBoundary = std::nullopt,
  };
  seam::synthesis::PlacedRenderedUnit right{
      .destinationStart = 16,
      .unit = seam::synthesis::RenderedUnit{
          .unitId = "right",
          .samples = std::vector<float>(32, -0.8F),
          .vowelOnsetOffset = 0,
      },
      .incomingBoundary = std::nullopt,
  };
  const std::vector<seam::synthesis::PlacedRenderedUnit> units{left, right};
  seam::synthesis::SeamComposer composer;
  const auto smooth = composer.compose(
      units, seam::synthesis::SeamSettings{.seamAmount = 0.0F, .sampleRate = 48000});
  const auto hard = composer.compose(
      units, seam::synthesis::SeamSettings{.seamAmount = 1.0F, .sampleRate = 48000});
  CHECK(smooth);
  CHECK(hard);
  CHECK(smooth.value().startFrame == hard.value().startFrame);
  CHECK(smooth.value().samples.size() == hard.value().samples.size());
  CHECK(smooth.value().samples[20] != hard.value().samples[20]);
}

TEST_CASE("raw phrase pipeline aligns vowels and renders inspectable audio") {
  SynthesisFixture fixture;
  const auto firstNote = fixture.add(U"か", seam::time::Tick{1920});
  const auto secondNote = fixture.add(U"ー", seam::time::Tick{2880});
  const auto directory = seam::test::support::temporaryDirectory("phrase-render");
  std::filesystem::create_directories(directory / "audio");
  const auto source = seam::test::support::sineWave(
      SynthesisFixture::sampleRate, 440.0, 0.5);
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "k-a.wav", SynthesisFixture::sampleRate, source));
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "a-sustain.wav", SynthesisFixture::sampleRate, source));

  auto cv = seam::test::support::makeUnit(
      "k-a", {"k", "a"}, "audio/k-a.wav", 69,
      seam::voicebank::UnitKind::Cv, source.size());
  auto sustain = seam::test::support::makeUnit(
      "a-sustain", {"a"}, "audio/a-sustain.wav", 69,
      seam::voicebank::UnitKind::Sustain, source.size());
  const auto manifest = seam::test::support::makeManifest({cv, sustain});
  const auto* region = fixture.project.findRegion(fixture.regionId);

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  seam::synthesis::DeterministicUnitSelector selector;
  const auto selected = selector.select(manifest, *region, phonemes.tokens, "original");
  CHECK(selected);

  seam::synthesis::TimingSolver timingSolver;
  const auto timing = timingSolver.solve(
      fixture.project, *region, phonemes.tokens, selected.value(), manifest,
      SynthesisFixture::sampleRate);
  CHECK(timing);
  CHECK(timing.value().placements.size() == 2);
  CHECK(timing.value().placements[0].noteOn ==
        fixture.project.tempoMap().sampleFrameAt(
            fixture.project.findNote(firstNote)->startTick,
            static_cast<double>(SynthesisFixture::sampleRate)));
  CHECK(timing.value().placements[1].noteOn ==
        fixture.project.tempoMap().sampleFrameAt(
            fixture.project.findNote(secondNote)->startTick,
            static_cast<double>(SynthesisFixture::sampleRate)));

  seam::synthesis::RawPhraseRenderer renderer;
  const auto rendered = renderer.render(
      manifest, directory, timing.value(), SynthesisFixture::sampleRate,
      seam::synthesis::RawRenderParameters{},
      seam::synthesis::SeamSettings{.seamAmount = 0.75F,
                                    .sampleRate = SynthesisFixture::sampleRate});
  CHECK(rendered);
  CHECK(rendered.value().placements.size() == 2);
  for (std::size_t index = 0; index < rendered.value().placements.size(); ++index) {
    CHECK(rendered.value().placements[index].vowelOnset ==
          timing.value().placements[index].desiredVowelOnset);
  }
  CHECK(!rendered.value().audio.samples.empty());
  CHECK(std::any_of(rendered.value().audio.samples.begin(),
                    rendered.value().audio.samples.end(),
                    [](float value) { return std::abs(value) > 0.01F; }));
  // Exercise a committed timing edit through resolution, selection, placement
  // and WAV-backed audio, then require exact audio restoration on Undo.
  seam::application::EditorSession editSession{fixture.project};
  const auto renderCurrent = [&](bool dispatched = false) {
    const auto* current = editSession.project().findRegion(fixture.regionId);
    const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(*current);
    CHECK(pronunciation);
    const auto plan = selector.select(manifest, *current, pronunciation.value().pronunciation.tokens, "original");
    CHECK(plan);
    const auto placement = timingSolver.solve(editSession.project(), *current,
        pronunciation.value().pronunciation.tokens, plan.value(), manifest, SynthesisFixture::sampleRate);
    CHECK(placement);
    if (dispatched) {
      auto audio = seam::synthesis::ConcatenativePhraseRenderer{}.render(manifest, directory,
          editSession.project(), *current, plan.value(), placement.value(), SynthesisFixture::sampleRate,
          seam::synthesis::PhraseRenderOptions{});
      CHECK(audio);
      return std::move(audio).value();
    }
    auto audio = renderer.render(manifest, directory, placement.value(), SynthesisFixture::sampleRate,
        seam::synthesis::RawRenderParameters{}, seam::synthesis::SeamSettings{.seamAmount = 0.75F,
            .sampleRate = SynthesisFixture::sampleRate});
    CHECK(audio);
    return std::move(audio).value();
  };
  const auto baselineAudio = renderCurrent();
  CHECK(editSession.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(fixture.regionId,
      seam::domain::PhonemeOverride{.key = {firstNote, 1U}, .timing = {.startOffset = 30000}, .locked = true})));
  const auto editedAudio = renderCurrent();
  CHECK(editedAudio.placements.front().vowelOnset - baselineAudio.placements.front().vowelOnset == 1440);
  CHECK(editedAudio.placements.front().alignedStart - baselineAudio.placements.front().alignedStart == 1440);
  CHECK(editedAudio.audio.startFrame - baselineAudio.audio.startFrame == 1440);
  CHECK(editSession.undo());
  const auto restoredAudio = renderCurrent();
  CHECK(restoredAudio.audio.startFrame == baselineAudio.audio.startFrame);
  CHECK(restoredAudio.audio.samples == baselineAudio.audio.samples);
  CHECK(editSession.redo());
  const auto redoneAudio = renderCurrent();
  CHECK(redoneAudio.audio.startFrame == editedAudio.audio.startFrame);
  CHECK(redoneAudio.audio.samples == editedAudio.audio.samples);
  CHECK(editSession.undo());
  for (bool dispatched : {false, true}) {
    const auto beforeOnset = renderCurrent(dispatched);
    CHECK(editSession.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(fixture.regionId,
        seam::domain::PhonemeOverride{.key = {firstNote, 0U}, .timing = {.startOffset = -45000}, .locked = true})));
    const auto onsetAudio = renderCurrent(dispatched);
    CHECK(onsetAudio.placements.front().vowelOnset == beforeOnset.placements.front().vowelOnset);
    CHECK(onsetAudio.placements.front().alignedStart == onsetAudio.placements.front().requestedStart);
    CHECK(onsetAudio.placements.front().alignedStart == beforeOnset.placements.front().vowelOnset - 2160);
    CHECK(onsetAudio.audio.samples != beforeOnset.audio.samples);
    CHECK(editSession.undo());
    const auto undoneOnset = renderCurrent(dispatched);
    CHECK(undoneOnset.audio.startFrame == beforeOnset.audio.startFrame);
    CHECK(undoneOnset.audio.samples == beforeOnset.audio.samples);
  }
  auto withSeam = *region;
  withSeam.seamOverrides = {{.incomingStartKey = {secondNote, 0U},
                             .seamAmount = 0.99F, .unresolved = true}};
  seam::synthesis::ConcatenativePhraseRenderer concatenative;
  seam::synthesis::PhraseRenderOptions options;
  options.defaultSeam.seamAmount = 0.25F;
  const auto inactive = concatenative.render(manifest, directory, fixture.project,
      withSeam, selected.value(), timing.value(), SynthesisFixture::sampleRate, options);
  CHECK(inactive);
  CHECK_NEAR(inactive.value().placements.back().seamAmount, 0.25, 1.0e-6);
  withSeam.unitSelectionOverrides = {{.startKey = {firstNote, 0U}, .tokenCount = 2U,
      .unitId = "retained-missing-unit", .loopPrint = 1.0F,
      .sourcePitchResidual = 1.0F, .unresolved = true}};
  const auto retained = concatenative.render(manifest, directory, fixture.project,
      withSeam, selected.value(), timing.value(), SynthesisFixture::sampleRate, options);
  CHECK(retained);
  CHECK(retained.value().audio.samples == inactive.value().audio.samples);
  withSeam.seamOverrides.front().unresolved = false;
  const auto active = concatenative.render(manifest, directory, fixture.project,
      withSeam, selected.value(), timing.value(), SynthesisFixture::sampleRate, options);
  CHECK(active);
  CHECK_NEAR(active.value().placements.back().seamAmount, 0.99, 1.0e-6);
}


TEST_CASE("onset waveform retiming preserves endpoints and relocates the vowel landmark") {
  seam::synthesis::RenderedUnit unit{.unitId = "test", .samples = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}, .vowelOnsetOffset = 2};
  const auto original = unit.samples;
  CHECK(!seam::synthesis::retimeRenderedOnset(unit, 0));
  CHECK(unit.samples == original);
  CHECK(seam::synthesis::retimeRenderedOnset(unit, 3));
  CHECK(unit.samples.size() == original.size());
  CHECK(unit.samples.front() == original.front());
  CHECK(unit.samples.back() == original.back());
  CHECK(unit.samples[3] == original[2]);
  CHECK(unit.vowelOnsetOffset == 3);
}

TEST_CASE("explicit unit selection persists renderer choice and alternatives") {
  SynthesisFixture fixture;
  const auto noteId = fixture.add(U"か", seam::time::Tick{1920});
  auto* region = fixture.project.findRegion(fixture.regionId);
  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  auto cvA = seam::test::support::makeUnit(
      "cv-a", {"k", "a"}, "audio/cv-a.wav", 69,
      seam::voicebank::UnitKind::Cv);
  auto cvB = cvA;
  cvB.id = "cv-b";
  cvB.take = 2;
  const auto manifest = seam::test::support::makeManifest({cvA, cvB});
  region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
      .startKey = seam::domain::PhonemeKey{noteId, 0},
      .tokenCount = 2,
      .unitId = "cv-b",
      .renderer = seam::domain::UnitRendererKind::ClassicPsola,
      .locked = true,
  });
  seam::synthesis::DeterministicUnitSelector selector;
  const auto selected = selector.select(manifest, *region, phonemes.tokens, "original");
  CHECK(selected);
  CHECK(selected.value().entries.size() == 1);
  CHECK(selected.value().entries.front().unitId == "cv-b");
  CHECK(selected.value().entries.front().forced);
  CHECK(selected.value().entries.front().renderer ==
        seam::domain::UnitRendererKind::ClassicPsola);
  CHECK(selected.value().entries.front().alternatives ==
        (std::vector<std::string>{"cv-a"}));
  region->unitSelectionOverrides.front().unresolved = true;
  region->unitSelectionOverrides.front().unitId = "retained-missing-unit";
  const auto automatic = selector.select(manifest, *region, phonemes.tokens, "original");
  CHECK(automatic);
  CHECK(automatic.value().entries.front().unitId == "cv-a");
  CHECK(!automatic.value().entries.front().forced);
}

TEST_CASE("classic PSOLA follows target pitch and keeps consonant frames finite") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto sourceSamples = seam::test::support::sineWave(sampleRate, 440.0, 0.75);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = sourceSamples,
  };
  auto unit = seam::test::support::makeUnit(
      "a-psola", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, sourceSamples.size());
  unit.renderer = seam::voicebank::RendererHint::ClassicPsola;
  unit.pitchMarks.clear();
  constexpr seam::time::SampleFrame period = 109;
  for (auto frame = unit.markers.stableStart;
       frame < unit.markers.releaseStart.value(); frame += period) {
    unit.pitchMarks.push_back(seam::voicebank::PitchMark{
        .frame = frame,
        .confidence = 1.0F,
        .locked = false,
    });
  }
  CHECK(unit.validate());

  seam::synthesis::ClassicPsolaRenderer renderer;
  const auto rendered = renderer.render(
      unit, source, sampleRate, 36000, 72,
      seam::synthesis::PsolaRenderParameters{
          .sourcePitchResidual = 0.0F,
          .additionalGainDb = 0.0F,
          .pitchCurve = seam::synthesis::PitchCurve{
              std::vector<seam::synthesis::PitchPoint>{
                  {.frame = 0, .cents = 0.0F},
                  {.frame = 35999, .cents = 0.0F},
              }},
      });
  CHECK(rendered);
  CHECK(rendered.value().samples.size() == 36000);
  CHECK(std::all_of(rendered.value().samples.begin(), rendered.value().samples.end(),
                    [](float value) { return std::isfinite(value); }));
  const std::span<const float> sustain{
      rendered.value().samples.data() + 8000, 18000};
  const auto pitch = seam::voicebank::analyzePitch(sustain, sampleRate);
  CHECK(pitch);
  CHECK_NEAR(seam::voicebank::medianVoicedPitch(pitch.value()), 523.25, 22.0);
}



TEST_CASE("phase reset and envelope blend are real boundary operations") {
  constexpr std::size_t total = 256;
  constexpr std::size_t overlap = 128;
  constexpr double period = 32.0;
  std::vector<float> leftSamples(total, 0.0F);
  std::vector<float> rightSamples(total, 0.0F);
  for (std::size_t index = 0; index < total; ++index) {
    const auto phase = 2.0 * std::numbers::pi *
                       static_cast<double>(index) / period;
    leftSamples[index] = 0.8F * static_cast<float>(std::sin(phase));
    rightSamples[index] = 0.18F * static_cast<float>(
        std::sin(phase + std::numbers::pi * 0.5));
  }
  const auto makeUnits = [&](float phaseReset, float envelopeBlend) {
    return std::vector<seam::synthesis::PlacedRenderedUnit>{
        seam::synthesis::PlacedRenderedUnit{
            .destinationStart = 0,
            .unit = seam::synthesis::RenderedUnit{
                .unitId = "left-phase",
                .samples = leftSamples,
                .vowelOnsetOffset = 0,
            },
            .incomingBoundary = std::nullopt,
        },
        seam::synthesis::PlacedRenderedUnit{
            .destinationStart = static_cast<seam::time::SampleFrame>(total - overlap),
            .unit = seam::synthesis::RenderedUnit{
                .unitId = "right-phase",
                .samples = rightSamples,
                .vowelOnsetOffset = 0,
            },
            .incomingBoundary = seam::synthesis::BoundarySeamSettings{
                .seamAmount = 1.0F,
                .curve = seam::domain::SeamCurve::HardCharacter,
                .maxOverlapFrames = static_cast<seam::time::SampleFrame>(overlap),
                .phaseReset = phaseReset,
                .envelopeBlend = envelopeBlend,
            },
        },
    };
  };
  seam::synthesis::SeamComposer composer;
  const auto reset = composer.compose(makeUnits(1.0F, 0.0F));
  const auto aligned = composer.compose(makeUnits(0.0F, 1.0F));
  CHECK(reset);
  CHECK(aligned);
  CHECK(reset.value().samples.size() == aligned.value().samples.size());
  const auto switchFrame = total - overlap + overlap / 2U;
  const auto resetJump = std::abs(reset.value().samples[switchFrame] -
                                  reset.value().samples[switchFrame - 1U]);
  const auto alignedJump = std::abs(aligned.value().samples[switchFrame] -
                                    aligned.value().samples[switchFrame - 1U]);
  CHECK(alignedJump < resetJump);
  CHECK(reset.value().samples != aligned.value().samples);
}

TEST_CASE("renderer dispatcher executes spectral and stretch backends explicitly") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 440.0, 0.5);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  auto unit = seam::test::support::makeUnit(
      "stretch-request", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  seam::synthesis::UnitRendererDispatcher dispatcher;

  unit.renderer = seam::voicebank::RendererHint::Stretch;
  const auto stretched = dispatcher.render(unit, source, sampleRate, 24000, 72);
  CHECK(stretched);
  CHECK(stretched.value().requested == seam::voicebank::RendererHint::Stretch);
  CHECK(stretched.value().actual == seam::voicebank::RendererHint::Stretch);
  CHECK(!stretched.value().usedFallback);
  CHECK(stretched.value().diagnostic.empty());

  unit.renderer = seam::voicebank::RendererHint::SpectralClassic;
  const auto spectral = dispatcher.render(unit, source, sampleRate, 24000, 72);
  CHECK(spectral);
  CHECK(spectral.value().requested == seam::voicebank::RendererHint::SpectralClassic);
  CHECK(spectral.value().actual == seam::voicebank::RendererHint::SpectralClassic);
  CHECK(!spectral.value().usedFallback);
}

TEST_CASE("renderer dispatcher reports an actual raw fallback") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 440.0, 0.5);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  auto unit = seam::test::support::makeUnit(
      "short-loop", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, samples.size());
  unit.renderer = seam::voicebank::RendererHint::Stretch;
  unit.markers.loopStart = 8000;
  unit.markers.loopEnd = 8010;
  seam::synthesis::UnitRendererDispatcher dispatcher;
  const auto rendered = dispatcher.render(unit, source, sampleRate, 24000, 69);
  CHECK(rendered);
  CHECK(rendered.value().requested == seam::voicebank::RendererHint::Stretch);
  CHECK(rendered.value().actual == seam::voicebank::RendererHint::Raw);
  CHECK(rendered.value().usedFallback);
  CHECK(!rendered.value().diagnostic.empty());
}

TEST_CASE("spectral classic preserves exact length and moves harmonic energy") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 220.0, 0.6, 0.35F);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  auto unit = seam::test::support::makeUnit(
      "spectral-a3", {"a"}, "audio/a.wav", 57,
      seam::voicebank::UnitKind::Sustain, samples.size());
  unit.renderer = seam::voicebank::RendererHint::SpectralClassic;
  unit.markers.loopStart = 8000;
  unit.markers.loopEnd = 22000;
  unit.markers.releaseStart = 25000;
  unit.markers.audioEnd = static_cast<seam::time::SampleFrame>(samples.size());

  seam::synthesis::SpectralClassicRenderer renderer;
  const auto rendered = renderer.render(
      unit, source, sampleRate, 30000, 69,
      seam::synthesis::SpectralRenderParameters{
          .fftSize = 1024,
          .hopSize = 256,
          .formantFollow = 0.45F,
          .phaseReset = 0.0F,
          .additionalGainDb = -2.0F,
          .pitchCurve = {},
      });
  CHECK(rendered);
  CHECK(rendered.value().samples.size() == 30000);
  CHECK(std::all_of(rendered.value().samples.begin(), rendered.value().samples.end(),
                    [](float value) { return std::isfinite(value); }));
  const auto begin = rendered.value().samples.begin() + 9000;
  const auto end = rendered.value().samples.begin() + 19000;
  const std::vector<float> sustain(begin, end);
  const auto pitch = seam::voicebank::analyzePitch(
      sustain, sampleRate,
      seam::voicebank::PitchConfig{.frameSize = 2048,
                                   .hopSize = 256,
                                   .minimumHz = 250.0,
                                   .maximumHz = 700.0,
                                   .voicingThreshold = 0.20});
  CHECK(pitch);
  const auto median = seam::voicebank::medianVoicedPitch(pitch.value());
  const auto gram = seam::voicebank::buildSpectrogram(
      sustain, seam::voicebank::SpectrogramConfig{.fftSize = 2048, .hopSize = 256,
          .minimumDb = -120.0F, .maximumDb = 0.0F});
  double dominantHz = 0.0;
  if (gram) {
    std::size_t bestBin = 1;
    double bestScore = -1.0e30;
    for (std::size_t bin = 1; bin < gram.value().bins; ++bin) {
      double score = 0.0;
      for (std::size_t column = 0; column < gram.value().columns; ++column) {
        score += gram.value().at(column, bin);
      }
      if (score > bestScore) { bestScore = score; bestBin = bin; }
    }
    dominantHz = static_cast<double>(bestBin) * 48000.0 / 2048.0;
  }
  CHECK(median > 360.0);
  CHECK(median < 520.0);
  CHECK(dominantHz > 390.0);
  CHECK(dominantHz < 510.0);
}

TEST_CASE("granular stretch is finite deterministic and unit scoped") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 329.627556, 0.6, 0.32F);
  seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate,
      .channels = 1,
      .interleaved = samples,
  };
  auto unit = seam::test::support::makeUnit(
      "stretch-e4", {"i"}, "audio/i.wav", 64,
      seam::voicebank::UnitKind::Sustain, samples.size());
  unit.markers.loopStart = 7200;
  unit.markers.loopEnd = 23000;
  unit.markers.releaseStart = 25000;
  unit.markers.audioEnd = static_cast<seam::time::SampleFrame>(samples.size());
  seam::synthesis::StretchUnitRenderer renderer;
  const seam::synthesis::StretchRenderParameters parameters{
      .grainSize = 1024,
      .hopSize = 256,
      .transientPreservation = 0.20F,
      .sourceDrift = 0.35F,
      .additionalGainDb = -1.0F,
      .pitchCurve = {},
  };
  const auto first = renderer.render(unit, source, sampleRate, 42000, 67, parameters);
  const auto second = renderer.render(unit, source, sampleRate, 42000, 67, parameters);
  CHECK(first);
  CHECK(second);
  CHECK(first.value().samples == second.value().samples);
  CHECK(first.value().samples.size() == 42000);
  CHECK(std::any_of(first.value().samples.begin(), first.value().samples.end(),
                    [](float value) { return std::abs(value) > 0.01F; }));
  CHECK(std::all_of(first.value().samples.begin(), first.value().samples.end(),
                    [](float value) { return std::isfinite(value); }));
}
