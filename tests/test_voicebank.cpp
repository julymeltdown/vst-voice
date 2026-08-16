#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/marker_editor.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/waveform.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

TEST_CASE("PCM16 WAV round trip and audio statistics remain bounded") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto directory = seam::test::support::temporaryDirectory("wav");
  const auto path = directory / "tone.wav";
  const auto source = seam::test::support::sineWave(sampleRate, 440.0, 0.25);
  CHECK(seam::voicebank::writeMonoPcm16Wav(path, sampleRate, source));

  const auto loaded = seam::voicebank::readWav(path);
  CHECK(loaded);
  CHECK(loaded.value().sampleRate == sampleRate);
  CHECK(loaded.value().channels == 1);
  CHECK(loaded.value().frameCount() == source.size());
  const auto mono = loaded.value().monoMix();
  const auto statistics = seam::voicebank::analyzeAudio(mono);
  CHECK(statistics.peak > 0.30F);
  CHECK(statistics.peak < 0.36F);
  CHECK(statistics.clippedSamples == 0);
  CHECK(std::abs(statistics.dcOffset) < 0.001);
}

TEST_CASE("waveform spectrogram and pitch analysis produce inspectable data") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 440.0, 0.30);

  const auto pyramid = seam::voicebank::WaveformPyramid::build(samples, 64, 8);
  CHECK(pyramid);
  CHECK(!pyramid.value().levels().empty());
  CHECK(!pyramid.value().levelFor(100.0).buckets.empty());

  const auto spectrogram = seam::voicebank::buildSpectrogram(
      samples, seam::voicebank::SpectrogramConfig{
                   .fftSize = 512,
                   .hopSize = 128,
                   .minimumDb = -90.0F,
                   .maximumDb = -6.0F,
               });
  CHECK(spectrogram);
  CHECK(spectrogram.value().columns > 10);
  CHECK(spectrogram.value().bins == 257);
  CHECK(std::all_of(spectrogram.value().decibels.begin(),
                    spectrogram.value().decibels.end(),
                    [](float value) { return std::isfinite(value); }));

  const auto pitch = seam::voicebank::analyzePitch(samples, sampleRate);
  CHECK(pitch);
  const auto median = seam::voicebank::medianVoicedPitch(pitch.value());
  CHECK_NEAR(median, 440.0, 6.0);
}

TEST_CASE("marker editor enforces monotonic sample landmarks") {
  seam::voicebank::UnitMarkers markers{
      .audioOffset = 0,
      .consonantEnd = 100,
      .vowelOnset = 200,
      .stableStart = 300,
      .loopStart = 400,
      .loopEnd = 700,
      .releaseStart = 800,
      .audioEnd = 1000,
  };
  const auto moved = seam::voicebank::MarkerEditor::set(
      markers, seam::voicebank::MarkerKind::VowelOnset, 450, 1000);
  CHECK(moved);
  CHECK(moved.value().vowelOnset == 450);
  CHECK(moved.value().stableStart >= moved.value().vowelOnset);
  CHECK(moved.value().loopStart.value() >= moved.value().stableStart);
  CHECK(moved.value().validate(1000));

  const auto normalized = seam::voicebank::MarkerEditor::normalize(
      seam::voicebank::UnitMarkers{
          .audioOffset = -100,
          .consonantEnd = -50,
          .vowelOnset = 5000,
          .stableStart = 50,
          .loopStart = 10,
          .loopEnd = 9,
          .releaseStart = 2000,
          .audioEnd = 5000,
      },
      1000);
  CHECK(normalized.validate(1000));
}

TEST_CASE("voicebank manifest persists and validator checks real audio") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto directory = seam::test::support::temporaryDirectory("voicebank");
  std::filesystem::create_directories(directory / "audio");
  const auto tone = seam::test::support::sineWave(sampleRate, 440.0, 0.50);
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "k-a.wav", sampleRate, tone));
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "a-sustain.wav", sampleRate, tone));

  auto cv = seam::test::support::makeUnit(
      "ja.original.a4.k-a.01", {"k", "a"}, "audio/k-a.wav", 69,
      seam::voicebank::UnitKind::Cv, tone.size());
  cv.alias = "k a";
  auto sustain = seam::test::support::makeUnit(
      "ja.original.a4.a-sustain.01", {"a"}, "audio/a-sustain.wav", 69,
      seam::voicebank::UnitKind::Sustain, tone.size());
  sustain.alias = "a sustain";
  const auto manifest = seam::test::support::makeManifest({cv, sustain});

  seam::voicebank::ManifestJsonCodec codec;
  const auto path = directory / "manifest.json";
  CHECK(codec.save(manifest, path));
  const auto loaded = codec.load(path);
  CHECK(loaded);
  CHECK(loaded.value() == manifest);

  seam::voicebank::BankValidator validator;
  const auto report = validator.validate(loaded.value(), directory);
  CHECK(report.unitsChecked == 2);
  CHECK(report.errorCount() == 0);
  CHECK(report.ok());
}
