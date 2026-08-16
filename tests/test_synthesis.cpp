#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/synthesis/seam_composer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
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

TEST_CASE("seam composer changes overlap character without changing bounds") {
  seam::synthesis::PlacedRenderedUnit left{
      .destinationStart = 0,
      .unit = seam::synthesis::RenderedUnit{
          .unitId = "left",
          .samples = std::vector<float>(32, 0.8F),
          .vowelOnsetOffset = 0,
      },
  };
  seam::synthesis::PlacedRenderedUnit right{
      .destinationStart = 16,
      .unit = seam::synthesis::RenderedUnit{
          .unitId = "right",
          .samples = std::vector<float>(32, -0.8F),
          .vowelOnsetOffset = 0,
      },
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
}
