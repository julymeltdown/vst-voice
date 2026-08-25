#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/marker_editor.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/pitch_marks.hpp"
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

TEST_CASE("voicebank manifest codec rejects symlink and non-file paths") {
  const auto root = seam::test::support::temporaryDirectory("manifest-paths");
  const auto outside = root / "outside.json";
  const auto link = root / "manifest-link.json";
  const auto directory = root / "manifest-directory";
  std::filesystem::create_directories(directory);
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  const auto manifest = seam::test::support::makeManifest({unit});
  seam::voicebank::ManifestJsonCodec codec;
  CHECK(codec.save(manifest, outside));
  std::error_code error;
  std::filesystem::create_symlink(outside, link, error);
  CHECK(!error);

  CHECK(!codec.load(link));
  CHECK(!codec.save(manifest, link));
  CHECK(codec.load(outside));
  CHECK(!codec.load(directory));
  CHECK(!codec.save(manifest, directory));

  const auto target = root / "target.json";
  CHECK(codec.save(manifest, target));
  const auto backupSource = root / "backup-outside.json";
  CHECK(codec.save(manifest, backupSource));
  const auto backupLink = target.string() + ".bak";
  std::filesystem::create_symlink(backupSource, backupLink, error);
  CHECK(!error);
  CHECK(!codec.save(manifest, target));
  CHECK(std::filesystem::is_symlink(backupLink));
  CHECK(codec.load(backupSource));
}

TEST_CASE("dry take inspection enforces format and acoustic quality") {
  const auto root = seam::test::support::temporaryDirectory("dry-take-inspection");
  const auto acceptedPath = root / "accepted.wav";
  auto acceptedWriter = seam::voicebank::WavStreamWriter::create(
      acceptedPath, seam::voicebank::WavOutputFormat{
                        48000U, 1U, seam::voicebank::WavSampleFormat::Pcm24});
  CHECK(acceptedWriter);
  CHECK(acceptedWriter.value()->writeFrames(
      seam::test::support::sineWave(48000U, 440.0, 0.4)));
  CHECK(acceptedWriter.value()->finalize());
  const auto accepted = seam::voicebank::inspectDryTake(acceptedPath, 69);
  CHECK(accepted);
  CHECK(!accepted.value().sourceSha256.empty());
  CHECK(accepted.value().formatValid);
  CHECK(accepted.value().finite);
  CHECK(accepted.value().clippingFree);
  CHECK(accepted.value().silenceFree);
  CHECK(accepted.value().dcOffsetFree);
  CHECK(accepted.value().rootPitchValid);
  CHECK(accepted.value().accepted());
  CHECK(accepted.value().bitsPerSample == 24U);
  CHECK(accepted.value().analyzedRootMidi.has_value());

  const auto wrongFormat = root / "wrong-format.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      wrongFormat, 48000U,
      seam::test::support::sineWave(48000U, 440.0, 0.4)));
  const auto wrongFormatReport = seam::voicebank::inspectDryTake(wrongFormat, 69);
  CHECK(wrongFormatReport);
  CHECK(!wrongFormatReport.value().formatValid);
  CHECK(!wrongFormatReport.value().accepted());

  const auto silentPath = root / "silent.wav";
  auto silentWriter = seam::voicebank::WavStreamWriter::create(
      silentPath, seam::voicebank::WavOutputFormat{
                     48000U, 1U, seam::voicebank::WavSampleFormat::Pcm24});
  CHECK(silentWriter);
  CHECK(silentWriter.value()->writeFrames(
      std::vector<float>(48000U / 4U, 0.0F)));
  CHECK(silentWriter.value()->finalize());
  const auto silent = seam::voicebank::inspectDryTake(silentPath, 69);
  CHECK(silent);
  CHECK(!silent.value().silenceFree);
  CHECK(!silent.value().accepted());
}


TEST_CASE("pitch marks are generated validated and edited deterministically") {
  constexpr std::uint32_t sampleRate = 48000;
  const auto samples = seam::test::support::sineWave(sampleRate, 220.0, 0.60);
  const auto generated = seam::voicebank::generatePitchMarks(
      samples, sampleRate, 2400,
      static_cast<seam::time::SampleFrame>(samples.size() - 2400U));
  CHECK(generated);
  CHECK(generated.value().size() > 40);
  CHECK(seam::voicebank::validatePitchMarks(
      generated.value(), 2400,
      static_cast<seam::time::SampleFrame>(samples.size() - 2400U)));

  auto marks = generated.value();
  seam::voicebank::PitchMarkEditor editor;
  const auto originalFrame = marks[3].frame;
  CHECK(editor.move(marks, 3, originalFrame + 2, 2400,
                    static_cast<seam::time::SampleFrame>(samples.size() - 2400U)));
  CHECK(editor.setLocked(marks, 3, true));
  CHECK(!editor.move(marks, 3, originalFrame + 4, 2400,
                     static_cast<seam::time::SampleFrame>(samples.size() - 2400U)));
  CHECK(editor.setLocked(marks, 3, false));
  const auto before = marks.size();
  CHECK(editor.remove(marks, 3, 2400,
                      static_cast<seam::time::SampleFrame>(samples.size() - 2400U)));
  CHECK(marks.size() + 1U == before);
}

TEST_CASE("voicebank schema one migrates without pitch marks") {
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  unit.renderer = seam::voicebank::RendererHint::Raw;
  auto manifest = seam::test::support::makeManifest({unit});
  seam::voicebank::ManifestJsonCodec codec;
  auto encoded = codec.encode(manifest);
  CHECK(encoded);
  auto legacy = encoded.value();
  const auto schema = legacy.find("\"schemaVersion\": 3");
  CHECK(schema != std::string::npos);
  legacy.replace(schema, std::string{"\"schemaVersion\": 3"}.size(),
                 "\"schemaVersion\": 1");
  const auto marks = legacy.find(",\n      \"pitchMarks\": []");
  CHECK(marks != std::string::npos);
  legacy.erase(marks, std::string{",\n      \"pitchMarks\": []"}.size());
  const auto decoded = codec.decode(legacy);
  CHECK(decoded);
  CHECK(decoded.value() == manifest);
}

TEST_CASE("stereo PCM16 WAV writer round trips channel layout") {
  const auto directory = seam::test::support::temporaryDirectory("stereo-wav");
  const std::vector<float> interleaved{
      0.1F, -0.1F, 0.2F, -0.2F, 0.3F, -0.3F, 0.4F, -0.4F};
  const auto path = directory / "stereo.wav";
  CHECK(seam::voicebank::writePcm16Wav(path, 48000, 2, interleaved));
  const auto loaded = seam::voicebank::readWav(path);
  CHECK(loaded);
  CHECK(loaded.value().channels == 2);
  CHECK(loaded.value().frameCount() == 4);
  CHECK_NEAR(loaded.value().interleaved[0], 0.1F, 1.0e-4);
  CHECK_NEAR(loaded.value().interleaved[1], -0.1F, 1.0e-4);
}

TEST_CASE("voicebank schema three binds an optional character product identity") {
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  auto manifest = seam::test::support::makeManifest({unit});
  manifest.characterId = "official.character.01";
  manifest.characterVersion = "0.1.0";
  seam::voicebank::ManifestJsonCodec codec;
  const auto encoded = codec.encode(manifest);
  CHECK(encoded);
  CHECK(encoded.value().find("official.character.01") != std::string::npos);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value().characterId == "official.character.01");
  CHECK(decoded.value().characterVersion == "0.1.0");

  manifest.characterVersion.clear();
  CHECK(!manifest.validate());
}

TEST_CASE("voicebank schema two migrates without a character binding") {
  auto unit = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000);
  auto manifest = seam::test::support::makeManifest({unit});
  seam::voicebank::ManifestJsonCodec codec;
  auto encoded = codec.encode(manifest);
  CHECK(encoded);
  auto legacy = encoded.value();
  const auto schema = legacy.find("\"schemaVersion\": 3");
  CHECK(schema != std::string::npos);
  legacy.replace(schema, std::string{"\"schemaVersion\": 3"}.size(),
                 "\"schemaVersion\": 2");
  const auto decoded = codec.decode(legacy);
  CHECK(decoded);
  CHECK(decoded.value().characterId.empty());
  CHECK(decoded.value().characterVersion.empty());
}
