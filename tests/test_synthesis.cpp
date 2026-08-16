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
