#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
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

TEST_CASE("voicebank content identity binds bounded per-unit alignment bytes") {
  const auto root = seam::test::support::temporaryDirectory("alignment-bank-identity");
  const auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "a.wav", 69,
          seam::voicebank::UnitKind::Sustain, 24000)});
  CHECK(seam::voicebank::writeMonoPcm16Wav(root / "a.wav", 48000U,
      seam::test::support::sineWave(48000U, 440.0, 0.5)));
  const auto identity = [&] { return seam::voicebank::computeVoicebankContentHash(manifest, root); };
  const auto legacy = identity();
  CHECK(legacy);
  std::filesystem::create_directories(root / "alignments");
  CHECK(identity().value() == legacy.value());
  const auto path = root / "alignments" / (seam::core::sha256Hex("a") + ".json");
  // Identity binds bytes; semantic validation is independently required by loading.
  CHECK(seam::core::durableAtomicWriteText(path, "{\"landmarks\":[]}"));
  const auto aligned = identity();
  CHECK(aligned);
  CHECK(aligned.value() != legacy.value());
  CHECK(seam::core::durableAtomicWriteText(path, "{\"landmarks\":[] }"));
  CHECK(identity().value() != aligned.value());
  CHECK(seam::core::durableAtomicWriteText(path, std::string(512U * 1024U + 1U, 'x')));
  CHECK(!identity());
  CHECK(std::filesystem::remove(path));
  CHECK(identity().value() == legacy.value());
  std::filesystem::create_symlink(root / "a.wav", path);
  CHECK(!identity());
  CHECK(std::filesystem::remove(path));
  CHECK(std::filesystem::remove(root / "alignments"));
  std::filesystem::create_directory_symlink(root, root / "alignments");
  CHECK(!identity());
}

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
  const auto acceptedAudio = seam::voicebank::readWav(acceptedPath);
  CHECK(acceptedAudio);
  if (acceptedAudio) {
    const auto expectedStatistics = seam::voicebank::analyzeAudio(acceptedAudio.value().monoMix());
    CHECK(accepted.value().peak == expectedStatistics.peak);
    CHECK(accepted.value().rms == expectedStatistics.rms);
    CHECK(accepted.value().dcOffset == expectedStatistics.dcOffset);
  }

  std::stop_source cancellation;
  cancellation.request_stop();
  const auto cancelledInspection = seam::voicebank::inspectDryTake(acceptedPath, 69, cancellation.get_token());
  CHECK(!cancelledInspection);
  if (!cancelledInspection) CHECK(cancelledInspection.error().code == seam::core::ErrorCode::Conflict);
  const auto cancelledDecode = seam::voicebank::readWav(acceptedPath, {}, cancellation.get_token());
  CHECK(!cancelledDecode);
  if (!cancelledDecode) CHECK(cancelledDecode.error().code == seam::core::ErrorCode::Conflict);
  const auto cancelledHash = seam::core::sha256File(acceptedPath,
      seam::voicebank::kMaximumSupportedWavBytes, cancellation.get_token());
  CHECK(!cancelledHash);
  if (!cancelledHash) CHECK(cancelledHash.error().code == seam::core::ErrorCode::Conflict);

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

// U15 scenario 3: replacing a unit's audio must invalidate the analysis that was
// measured from the audio it replaced.
//
// The fixture stores real marks produced by the production generator, so the
// stored analysis agrees with the bytes beside it. It then overwrites the WAV
// with a different take of the same name and length, which is the edit that used
// to be invisible: every structural rule on a pitch mark (ascending, inside the
// unit, confidence in range) still held, and the marks still satisfied the
// renderer, because none of those rules can see which audio they came from.
// The bank must now say so rather than let a stale measurement reach a release.
TEST_CASE("replacing unit audio invalidates the pitch marks measured from it") {
  constexpr std::uint32_t kRate = 48000U;
  constexpr std::size_t kFrames = 24000U;
  const auto directory = seam::test::support::temporaryDirectory("pitch-marks-stale");
  std::filesystem::create_directories(directory / "audio");
  const auto audioPath = directory / "audio" / "a.wav";

  const auto original = seam::test::support::sineWave(kRate, 220.0, 0.5);
  CHECK(original.size() == kFrames);
  CHECK(seam::voicebank::writeMonoPcm16Wav(audioPath, kRate, original));

  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 57,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const seam::voicebank::PitchMarkGenerationConfig generation{
      .pitch = {.frameSize = 2048U, .hopSize = 256U, .minimumHz = 60.0,
                .maximumHz = 1200.0, .voicingThreshold = 0.32,
                .correlationMethod = seam::voicebank::PitchCorrelationMethod::Fft}};
  const auto marks = seam::voicebank::generatePitchMarks(
      original, kRate, unit.markers.audioOffset, unit.markers.audioEnd, generation);
  CHECK(marks);
  CHECK(marks.value().size() >= 6U);
  unit.pitchMarks = marks.value();
  unit.renderer = seam::voicebank::RendererHint::ClassicPsola;
  const auto manifest = seam::test::support::makeManifest({unit});

  const auto hasStale = [](const seam::voicebank::ValidationReport& report) {
    return std::any_of(report.issues.begin(), report.issues.end(),
        [](const auto& issue) {
          return issue.code == seam::voicebank::IssueCode::PitchMarksStale;
        });
  };

  // Marks measured from these bytes: nothing to report.
  const auto before = seam::voicebank::BankValidator{}.validate(manifest, directory);
  CHECK(!hasStale(before));

  // A different take, same file name, same frame count, half the frequency.
  const auto replacement = seam::test::support::sineWave(kRate, 110.0, 0.5);
  CHECK(replacement.size() == original.size());
  CHECK(seam::voicebank::writeMonoPcm16Wav(audioPath, kRate, replacement));

  // The structural rules still hold, which is exactly why this went unnoticed.
  CHECK(unit.validate());
  const auto after = seam::voicebank::BankValidator{}.validate(manifest, directory);
  CHECK(hasStale(after));

  // Removing the marks is an honest way to resolve it, and must clear the finding.
  auto resolved = manifest;
  resolved.units.front().pitchMarks.clear();
  resolved.units.front().renderer = seam::voicebank::RendererHint::Raw;
  CHECK(!hasStale(seam::voicebank::BankValidator{}.validate(resolved, directory)));
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

// U15 scenario 1: voiced-to-fricative material must retain separate voicing
// states, and generated pitch marks must never bridge an unvoiced span.
//
// The fixture alternates sung vowel, unvoiced fricative, sung vowel. Every mark
// the generator emits must be owned by a frame the pitch analyser itself declared
// voiced: a mark is a claim that a glottal pulse belongs at that sample, and that
// claim is only supported where the analysis found periodicity. Before the guard,
// the nearest-frame lookup searched a voiced-only list and ignored the frames
// between, so a voiced span, a fricative and a second voiced span produced marks
// inside the fricative while the analysis had reported it unvoiced.
TEST_CASE("pitch marks never bridge an unvoiced span") {
  constexpr std::uint32_t kRate = 48000U;
  constexpr std::size_t kFrames = 48000U;
  constexpr std::size_t kVoicedEnd = 19200U;    // end of the first sung vowel
  constexpr std::size_t kUnvoicedEnd = 28800U;  // end of the fricative
  constexpr std::size_t kFrameSize = 2048U;
  constexpr std::size_t kHop = 256U;

  std::vector<float> samples(kFrames, 0.0F);
  unsigned seed = 12345U;
  auto noise = [&seed]() {
    seed = seed * 1103515245U + 12345U;
    return (static_cast<float>((seed >> 16U) & 0x7FFFU) / 16384.0F) - 1.0F;
  };
  for (std::size_t frame = 0U; frame < kFrames; ++frame) {
    const auto time = static_cast<double>(frame) / static_cast<double>(kRate);
    if (frame < kVoicedEnd)
      samples[frame] = 0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 200.0 * time));
    else if (frame < kUnvoicedEnd)
      samples[frame] = 0.35F * noise();
    else
      samples[frame] = 0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * time));
  }

  const seam::voicebank::PitchConfig pitch{
      .frameSize = kFrameSize, .hopSize = kHop, .minimumHz = 60.0,
      .maximumHz = 1200.0, .voicingThreshold = 0.32,
      .correlationMethod = seam::voicebank::PitchCorrelationMethod::Fft};
  const auto analysis = seam::voicebank::analyzePitch(samples, kRate, pitch);
  CHECK(analysis);

  seam::voicebank::PitchMarkGenerationConfig config;
  config.pitch = pitch;
  const auto marks = seam::voicebank::generatePitchMarks(
      samples, kRate, seam::time::SampleFrame{0}, seam::time::SampleFrame{kFrames}, config);
  CHECK(marks);
  CHECK(marks.value().size() >= 3U);

  // The analyser must actually have found the fricative unvoiced, or this fixture
  // proves nothing about bridging.
  std::size_t unvoicedFrames = 0U;
  for (const auto& frame : analysis.value()) {
    const auto start = static_cast<std::size_t>(frame.sourceFrame);
    if (start >= kVoicedEnd && start < kUnvoicedEnd && !frame.voiced) ++unvoicedFrames;
  }
  CHECK(unvoicedFrames >= 3U);

  // A mark is legitimate only where some voiced frame's window contains it, using
  // the same window convention the generator used. That is the real invariant: a
  // mark is a claim about a glottal pulse, and only a window the analyser found
  // periodic can support that claim. Counting marks inside a hand-drawn span would
  // be measuring the fixture rather than the generator, because a 2048-sample
  // window legitimately reaches past the last voiced sample it started inside.
  std::size_t unsupported = 0U;
  for (const auto& mark : marks.value()) {
    const auto at = static_cast<std::size_t>(mark.frame);
    const bool owned = std::any_of(
        analysis.value().begin(), analysis.value().end(), [&](const auto& frame) {
          return frame.voiced && at >= frame.sourceFrame &&
                 at < frame.sourceFrame + kFrameSize;
        });
    if (!owned) ++unsupported;
  }
  // Every mark must sit inside a window the analyser called voiced.
  CHECK(unsupported == 0U);

  // The fricative's middle must be left alone. The boundary is set by the analysis
  // windows, not by the fixture: the last voiced window of the first region ends at
  // 20480, and the first window the analyser already considers voiced in the second
  // region begins at 27392 -- its 2048-sample window has only just reached the
  // returning sine, which is why its confidence is the lowest in that run (0.450).
  // Marks may therefore appear in [20480, 27392) nowhere at all.
  std::size_t insideGap = 0U;
  for (const auto& mark : marks.value()) {
    const auto at = std::size_t(mark.frame);
    if (at >= kVoicedEnd + kFrameSize && at < 27392U) ++insideGap;
  }
  CHECK(insideGap == 0U);

  // And the generator must still reach the second sung vowel rather than giving up
  // at the fricative: a guard that simply stopped would also produce no marks in
  // the gap while silently truncating the take.
  CHECK(std::any_of(marks.value().begin(), marks.value().end(),
                    [](const auto& mark) { return std::size_t(mark.frame) >= kUnvoicedEnd; }));
}
