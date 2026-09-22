#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "target_voicing_experiment.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
using namespace seam;
using namespace seam::domain;
namespace voicing_experiment = seam::synthesis::experimental;
using time::Tick;
struct VoicingFixture {
  application::ProjectFactory factory{71931U};
  Project project{factory.createProject("Target voicing")};
  TrackId track{factory.addVocalTrack(project, "Original")};
  RegionId regionId{factory.addRegion(project, track, "Phrase", Tick{137}, Tick{3840})};
  explicit VoicingFixture(std::uint8_t midi = 69U) {
    auto [lyric, note] = factory.makeNote(Tick{0}, Tick{1920}, midi, U"あ", Language::Japanese);
    region().lyrics.push_back(lyric); region().notes.push_back(note);
    region().performance.takes = {{.id = "voicing", .sourceRegionId = regionId,
        .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
        .pronunciation = {Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
        .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, Tick{3840}},
        .lanes = {{PerformanceChannel::Pitch, {{Tick{0}, std::nullopt}}}}}};
    region().performance.accepted = {{"voicing", PerformanceChannel::Pitch, note.id, Tick{0}}};
  }
  VocalRegion& region() { return *project.findRegion(regionId); }
  synthesis::CompiledScorePerformance compile(std::uint32_t rate = 48000U) {
    const auto result = synthesis::compileScorePerformance(project, region(), rate); CHECK(result);
    return result.value();
  }
};
double energy(std::span<const float> values) {
  double result = 0.0;
  for (const auto value : values) result += static_cast<double>(value) * value;
  return result;
}
double periodicity(std::span<const float> values, std::uint32_t rate, double hz) {
  double result = 0.0;
  // Off-grid periods, especially at 8 kHz, need a fractional-delay oracle;
  // rounding by half a sample can decorrelate the upper formants of the SOURCE.
  const auto exactPeriod = rate / hz;
  const auto integral = static_cast<std::size_t>(std::floor(exactPeriod));
  std::array<double, 33U> kernel{};
  double kernelSum = 0.0;
  for (int tap = -16; tap <= 16; ++tap) {
    const auto distance = static_cast<double>(tap) + exactPeriod - static_cast<double>(integral);
    const auto sinc = std::abs(distance) < 1.0e-12 ? 1.0 : std::sin(std::numbers::pi * distance) / (std::numbers::pi * distance);
    const auto window = 0.5 + 0.5 * std::cos(std::numbers::pi * static_cast<double>(tap) / 17.0);
    kernel[static_cast<std::size_t>(tap + 16)] = sinc * window;
    kernelSum += sinc * window;
  }
  double cross = 0.0, first = 0.0, second = 0.0;
  for (std::size_t i = integral + 16U; i + 16U < values.size(); ++i) {
    double delayed = 0.0;
    for (std::size_t tap = 0U; tap < kernel.size(); ++tap)
      delayed += values[i - integral - 16U + tap] * kernel[tap] / kernelSum;
    cross += values[i] * delayed;
    first += static_cast<double>(values[i]) * values[i]; second += delayed * delayed;
  }
  result = cross / std::max(1.0e-24, std::sqrt(first * second));
  // Also catch a remaining octave/subharmonic rather than accepting a changed
  // periodic carrier merely because its original-period correlation fell.
  for (const auto multiple : {0.5, 1.0, 2.0, 3.0}) {
    const auto period = static_cast<std::size_t>(std::llround(rate / hz * multiple));
    const auto tolerance = std::max<std::size_t>(1U, period / 50U);
    for (auto lag = period - tolerance; lag <= period + tolerance; ++lag) {
      cross = 0.0; first = 0.0; second = 0.0;
      for (std::size_t i = lag; i < values.size(); ++i) {
        cross += static_cast<double>(values[i]) * values[i - lag];
        first += static_cast<double>(values[i]) * values[i];
        second += static_cast<double>(values[i - lag]) * values[i - lag];
      }
      result = std::max(result, cross / std::max(1.0e-24, std::sqrt(first * second)));
    }
  }
  return result;
}
std::vector<float> vowel(std::uint32_t rate, double hz) {
  std::vector<float> result(rate / 2U);
  const std::array<std::pair<double, double>, 3U> formants{{{650.0, 100.0}, {1150.0, 180.0}, {2450.0, 220.0}}};
  for (std::size_t harmonic = 1U; hz * static_cast<double>(harmonic) < rate * 0.45; ++harmonic) {
    const auto frequency = hz * static_cast<double>(harmonic);
    double amplitude = 0.0;
    for (const auto& [center, width] : formants) amplitude += std::exp(-0.5 * std::pow((frequency - center) / width, 2.0));
    amplitude /= std::pow(static_cast<double>(harmonic), 0.4);
    for (std::size_t i = 0U; i < result.size(); ++i)
      result[i] += static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
  }
  float peak = 0.0F;
  for (const auto sample : result) peak = std::max(peak, std::abs(sample));
  for (std::size_t i = 0U; i < result.size(); ++i) {
    const auto motion = 0.75 + 0.25 * std::sin(2.0 * std::numbers::pi * 4.0 * static_cast<double>(i) / rate);
    result[i] *= static_cast<float>(0.45 * motion / peak);
  }
  return result;
}
std::array<double, 5U> bands(std::span<const float> values, std::uint32_t rate) {
  std::size_t size = 256U;
  while (size < static_cast<std::size_t>(rate) * 32U / 1000U && size < 8192U) size *= 2U;
  const auto spectrum = voicebank::buildSpectrogram(values,
      {.fftSize = size, .hopSize = size / 2U, .minimumDb = -150.0F, .maximumDb = 20.0F}); CHECK(spectrum);
  constexpr std::array<double, 4U> boundaries{400.0, 900.0, 1800.0, 3200.0};
  std::array<double, 5U> result{};
  double total = 0.0;
  for (std::size_t bin = 0U; bin < spectrum.value().bins; ++bin) {
    const auto hz = static_cast<double>(bin) * rate / static_cast<double>(size);
    const auto band = static_cast<std::size_t>(std::upper_bound(boundaries.begin(), boundaries.end(), hz) - boundaries.begin());
    for (std::size_t column = 0U; column < spectrum.value().columns; ++column) {
      const auto power = std::pow(10.0, spectrum.value().at(column, bin) / 10.0);
      result[band] += power; total += power;
    }
  }
  for (auto& value : result) value /= total;
  return result;
}
}

TEST_CASE("sample target unvoicing retains broad formant energy while reducing periodicity") {
  // Fixed before measurement: middle 300 ms, RMS ratio .7..1.3, source-lag
  // correlation < .65, each broad spectral-energy share within .20 absolute.
  // These synthetic controls are an engineering diagnostic, not singer quality.
  for (const auto rate : {8000U, 44100U, 48000U, 192000U}) {
    for (const auto midi : {48U, 60U, 72U}) {
      VoicingFixture fixture{static_cast<std::uint8_t>(midi)};
      const auto performance = fixture.compile(rate);
      const auto hz = 440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0);
      const auto input = vowel(rate, hz);
      auto output = input;
      CHECK(voicing_experiment::applySampleTargetVoicing(output, performance, performance.notes().front().startFrame, hz, "vowel"));
      const auto a = std::span<const float>{input}.subspan(rate / 10U, rate * 3U / 10U);
      const auto b = std::span<const float>{output}.subspan(rate / 10U, rate * 3U / 10U);
      const auto ratio = std::sqrt(energy(b) / energy(a));
      const auto correlation = periodicity(b, rate, hz);
      const auto referenceBands = bands(a, rate), outputBands = bands(b, rate);
      double maximumBandError = 0.0;
      for (std::size_t i = 0U; i < referenceBands.size(); ++i)
        maximumBandError = std::max(maximumBandError, std::abs(referenceBands[i] - outputBands[i]));
      std::cout << "[VOICING] rate=" << rate << " midi=" << midi << " rms=" << ratio
                << " periodicity=" << correlation << " band_error=" << maximumBandError << '\n';
      CHECK(std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value); }));
      CHECK(ratio > 0.7); CHECK(ratio < 1.3);
      CHECK(periodicity(a, rate, hz) > 0.9); CHECK(correlation < 0.65);
      CHECK(maximumBandError < 0.20);
      auto repeated = input;
      CHECK(voicing_experiment::applySampleTargetVoicing(repeated, performance, performance.notes().front().startFrame, hz, "vowel"));
      CHECK(repeated == output);
    }
  }
}

TEST_CASE("sample target voicing masks offsets tempo ownership gaps and source classification exactly") {
  VoicingFixture fixture;
  auto& region = fixture.region();
  region.performance.takes.front().lanes.front().points = {{Tick{0}, 6900.0}, {Tick{360}, std::nullopt}, {Tick{1440}, 6900.0}};
  region.performance.accepted.front().sourceTickOffset = Tick{120};
  region.performance.ownership = {{PerformanceChannel::Pitch, PerformanceTimeRange{Tick{720}, Tick{721}}, ManualPerformanceMode::Replace, {}}};
  CHECK(fixture.project.tempoMap().addOrReplace(region.startTick + Tick{960}, 90.0));
  for (const auto rate : {8000U, 44100U, 48000U, 192000U}) {
    const auto performance = fixture.compile(rate);
    const auto origin = performance.notes().front().startFrame - 17;
    const auto frames = performance.notes().front().endFrame - origin + 19;
    auto input = test::support::sineWave(rate, 440.0, static_cast<double>(frames + 1) / rate, 0.35F);
    input.resize(static_cast<std::size_t>(frames));
    synthesis::SourceTargetMap map{{{0, origin}, {frames, origin + frames}},
        {{0, frames / 2, true}, {frames / 2, frames / 2 + 127, false}, {frames / 2 + 127, frames, true}}};
    auto output = input;
    CHECK(voicing_experiment::applySampleTargetVoicing(output, performance, origin, 440.0, "mask", &map));
    std::size_t changed = 0U, protectedCount = 0U;
    for (time::SampleFrame i = 0; i < frames; ++i) {
      const auto value = performance.at(origin + i);
      const bool eligible = value.noteId && !value.scoreFrequencyHz && map.voicedAtSource(map.sourceAt(static_cast<double>(origin + i))) != false;
      if (!eligible) { CHECK(output[static_cast<std::size_t>(i)] == input[static_cast<std::size_t>(i)]); ++protectedCount; }
      else if (output[static_cast<std::size_t>(i)] != input[static_cast<std::size_t>(i)]) ++changed;
    }
    CHECK(changed > rate / 10U); CHECK(protectedCount > rate / 10U);
  }
  region.notes.front().vibrato.enabled = true;
  const auto vibrato = fixture.compile();
  auto original = test::support::sineWave(48000U, 440.0, 0.5);
  auto unchanged = original;
  CHECK(voicing_experiment::applySampleTargetVoicing(unchanged, vibrato, vibrato.notes().front().startFrame, 440.0, "mask"));
  CHECK(unchanged == original);
}

TEST_CASE("speech experiment executes and records both frozen acoustic failures without promotion") {
  const std::filesystem::path source{SEAM_TARGET_VOICING_SPEECH_FIXTURE};
  const auto hash = core::sha256File(source); CHECK(hash);
  CHECK(hash.value() == "caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9");
  const auto decoded = voicebank::readWav(source); CHECK(decoded);
  const auto input = decoded.value().monoMix();
  const auto rate = decoded.value().sampleRate;
  const auto manifestPath = source.parent_path().parent_path() / "manifest.json";
  const auto manifestHash = core::sha256File(manifestPath); CHECK(manifestHash);
  CHECK(manifestHash.value() == "ea199788012edc818459a00504a6043829560b22abe46efd4572fe886df973a5");
  const auto manifest = voicebank::ManifestJsonCodec{}.load(manifestPath); CHECK(manifest);
  CHECK(!manifest.value().units.empty()); CHECK(manifest.value().expectedSampleRate == rate);
  CHECK(source == manifestPath.parent_path() / manifest.value().units.front().audioPath);
  // Match the actual production caller's declared carrier; pitch extraction is
  // not part of this stage's API. Retain its conflicting estimate as a diagnostic.
  const auto hz = 440.0 * std::exp2((static_cast<double>(manifest.value().units.front().rootMidi) - 69.0) / 12.0);
  const auto pitch = voicebank::analyzePitch(input, rate); CHECK(pitch);
  const auto measuredHz = voicebank::medianVoicedPitch(pitch.value());
  CHECK_NEAR(measuredHz, 991.014, 0.01);
  VoicingFixture fixture{static_cast<std::uint8_t>(manifest.value().units.front().rootMidi)};
  const auto performance = fixture.compile(rate);
  const auto a = std::span<const float>{input}.subspan(rate / 10U, rate * 3U / 10U);
  // Measurement remains at the ORIGINAL independently estimated lag set, not
  // the newly corrected synthesis parameter. Report both before and after.
  const auto sourceCorrelation = periodicity(a, rate, measuredHz);
  const auto referenceBands = bands(a, rate);
  const std::filesystem::path artifacts{SEAM_TARGET_VOICING_REPORT_DIRECTORY};
  using J = formats::JsonValue;
  J::Array cases;
  bool acousticPass = true;
  CHECK(sourceCorrelation > 0.9);
  CHECK(voicebank::writeWav(artifacts / "source.wav", {.sampleRate = rate, .channels = 1U,
      .sampleFormat = voicebank::WavSampleFormat::Float32}, input));
  for (const auto& [name, carrierHz] : std::array<std::pair<std::string, double>, 2U>{{
       {"declared-root-67", hz}, {"estimated-pitch", measuredHz}}}) {
    auto output = input;
    CHECK(voicing_experiment::applySampleTargetVoicing(output, performance, performance.notes().front().startFrame, carrierHz, "speech-fixture"));
    CHECK(std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value); }));
    const auto b = std::span<const float>{output}.subspan(rate / 10U, rate * 3U / 10U);
    const auto ratio = std::sqrt(energy(b) / energy(a));
    const auto correlation = periodicity(b, rate, measuredHz);
    const auto outputBands = bands(b, rate);
    double maximumBandError = 0.0;
    for (std::size_t i = 0U; i < referenceBands.size(); ++i)
      maximumBandError = std::max(maximumBandError, std::abs(referenceBands[i] - outputBands[i]));
    J::Array failures;
    if (!(ratio > 0.7 && ratio < 1.3)) failures.emplace_back("rms-ratio");
    if (!(correlation < 0.65 && correlation < sourceCorrelation)) failures.emplace_back("periodicity");
    if (!(maximumBandError < 0.20)) failures.emplace_back("spectral-share");
    const bool passed = failures.empty(); acousticPass = acousticPass && passed;
    const auto outputPath = artifacts / (name + ".wav");
    CHECK(voicebank::writeWav(outputPath, {.sampleRate = rate, .channels = 1U,
        .sampleFormat = voicebank::WavSampleFormat::Float32}, output));
    const auto outputHash = core::sha256File(outputPath); CHECK(outputHash);
    const auto reread = voicebank::readWav(outputPath); CHECK(reread); CHECK(reread.value().interleaved == output);
    cases.emplace_back(J::Object{{"id", J{name}}, {"carrierHz", J{carrierHz}},
        {"executionStatus", J{"PASS"}}, {"acousticAssessment", J{passed ? "PASS" : "FAIL"}},
        {"rmsRatio", J{ratio}}, {"periodicity", J{correlation}}, {"maximumBandShareError", J{maximumBandError}},
        {"failedCriteria", J{std::move(failures)}}, {"outputFile", J{name + ".wav"}}, {"outputSha256", J{outputHash.value()}}});
    std::cout << "[SPEECH-VOICING] case=" << name << " carrier_hz=" << carrierHz << " fixed_oracle_hz=" << measuredHz
              << " source_periodicity=" << sourceCorrelation << " rms=" << ratio << " periodicity=" << correlation
              << " band_error=" << maximumBandError << " acoustic=" << (passed ? "PASS" : "FAIL") << '\n';
  }
  const auto executableHash = core::sha256File(SEAM_TARGET_VOICING_EXECUTABLE); CHECK(executableHash);
  const J report{J::Object{{"formatId", J{"com.project-seam.sample-target-voicing-experiment"}},
      {"schemaVersion", J{std::int64_t{1}}}, {"algorithmRevision", J{std::int64_t{1}}},
      {"executionStatus", J{"PASS"}}, {"acousticAssessment", J{acousticPass ? "PASS" : "FAIL"}},
      {"productionEnabled", J{false}}, {"releaseEligible", J{false}}, {"listeningStatus", J{"NOT_REVIEWED"}},
      {"engineeringTests", J{"SEPARATE_CTEST_RESULT_NOT_ACOUSTIC_ACCEPTANCE"}},
      {"sourceSha256", J{hash.value()}}, {"manifestSha256", J{manifestHash.value()}},
      {"executableSha256", J{executableHash.value()}}, {"sampleRate", J{static_cast<std::int64_t>(rate)}},
      {"analysisStartFrame", J{static_cast<std::int64_t>(rate / 10U)}},
      {"analysisFrameCount", J{static_cast<std::int64_t>(rate * 3U / 10U)}},
      {"sourcePeriodicity", J{sourceCorrelation}}, {"oracleHz", J{measuredHz}},
      {"oracle", J{"source-lag-ncc-v1: fractional source period plus integer lags around 0.5/1/2/3 periods, +/- max(1 sample, 2 percent); normalized correlation maximum"}},
      {"thresholds", J{J::Object{{"rmsRatioMinimumExclusive", J{0.7}}, {"rmsRatioMaximumExclusive", J{1.3}},
          {"periodicityMaximumExclusive", J{0.65}}, {"periodicityMustImproveSource", J{true}},
          {"bandShareErrorMaximumExclusive", J{0.20}}}}},
      {"spectralBandBoundariesHz", J{J::Array{J{400.0}, J{900.0}, J{1800.0}, J{3200.0}}}},
      {"cases", J{std::move(cases)}}}};
  CHECK(core::durableAtomicWriteText(artifacts / "assessment.json", formats::stringifyJson(report)));
  std::cout << "[ACOUSTIC-ASSESSMENT] " << artifacts / "assessment.json" << " status=" << (acousticPass ? "PASS" : "FAIL") << '\n';
  // Verifying that FAIL is faithfully reported is an engineering test PASS,
  // not an acoustic PASS. Improvement requires a new pinned promotion review.
  CHECK(!acousticPass);
  CHECK(report.find("cases")->asArray()[0].find("acousticAssessment")->asString() == "FAIL");
  CHECK(report.find("cases")->asArray()[1].find("acousticAssessment")->asString() == "FAIL");
}

TEST_CASE("sample target voicing replaces tiny runs without touching adjacent protected samples") {
  VoicingFixture fixture;
  for (const auto rate : {8000U, 44100U, 48000U, 192000U, 384000U}) {
    const auto performance = fixture.compile(rate);
    const auto origin = performance.notes().front().startFrame + 733;
    std::size_t size = 256U;
    while (size < static_cast<std::size_t>(rate) * 32U / 1000U) size *= 2U;
    for (const auto length : {1U, 2U, 17U, static_cast<unsigned>(size / 4U - 1U),
         static_cast<unsigned>(size / 4U), static_cast<unsigned>(size / 4U + 1U)}) {
      auto input = test::support::sineWave(rate, 440.0, static_cast<double>(length + 256U) / rate, 0.35F);
      input.resize(length + 128U);
      synthesis::SourceTargetMap map{{{0, origin}, {static_cast<time::SampleFrame>(input.size()), origin + static_cast<time::SampleFrame>(input.size())}},
          {{0, 64, false}, {64, 64 + length, true}, {64 + length, static_cast<time::SampleFrame>(input.size()), false}}};
      auto output = input;
      CHECK(voicing_experiment::applySampleTargetVoicing(output, performance, origin, 440.0, "tiny", &map));
      CHECK(std::equal(output.begin(), output.begin() + 64, input.begin()));
      CHECK(std::equal(output.begin() + 64 + length, output.end(), input.begin() + 64 + length));
      CHECK(!std::equal(output.begin() + 64, output.begin() + 64 + length, input.begin() + 64));
      CHECK(std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value) && std::abs(value) < 1.4F; }));
    }
    std::vector<float> one{0.25F};
    CHECK(voicing_experiment::applySampleTargetVoicing(one, performance, origin, 440.0, "single"));
    CHECK(one.front() != 0.25F); CHECK(std::isfinite(one.front()));
  }
  const auto performance = fixture.compile();
  const auto origin = performance.notes().front().startFrame;
  std::vector<float> input(128U, 0.25F);
  synthesis::SourceTargetMap map{{{0, origin}, {128, origin + 128}}, {}};
  for (time::SampleFrame i = 0; i < 128; ++i) map.voicing.push_back({i, i + 1, i % 2 == 0});
  auto output = input;
  CHECK(voicing_experiment::applySampleTargetVoicing(output, performance, origin, 440.0, "alternating", &map));
  for (std::size_t i = 0U; i < input.size(); ++i) {
    if (i % 2U) CHECK(output[i] == input[i]);
    else CHECK(output[i] != input[i]);
  }
  CHECK(!map.voicedAtSource(-1.0)); CHECK(!map.voicedAtSource(128.0));
}

TEST_CASE("sample target voicing is local deterministic finite and cancellation bounded") {
  VoicingFixture fixture;
  const auto performance = fixture.compile();
  const auto origin = performance.notes().front().startFrame;
  const auto input = vowel(48000U, 440.0);
  auto output = input;
  CHECK(voicing_experiment::applySampleTargetVoicing(output, performance, origin, 440.0, "stable"));
  auto later = input;
  for (std::size_t i = 18000U; i < later.size(); ++i) later[i] *= 0.1F;
  CHECK(voicing_experiment::applySampleTargetVoicing(later, performance, origin, 440.0, "stable"));
  CHECK(std::equal(output.begin(), output.begin() + 12000, later.begin()));
  auto otherStream = input;
  CHECK(voicing_experiment::applySampleTargetVoicing(otherStream, performance, origin, 440.0, "other"));
  CHECK(otherStream != output);
  for (const float silence : {0.0F, 1.0e-20F}) {
    std::vector<float> quiet(4096U, silence);
    CHECK(voicing_experiment::applySampleTargetVoicing(quiet, performance, origin, 440.0, "quiet"));
    CHECK(energy(quiet) <= static_cast<double>(quiet.size()) * silence * silence);
  }
  std::stop_source stop; stop.request_stop();
  auto cancelled = input;
  CHECK(!voicing_experiment::applySampleTargetVoicing(cancelled, performance, origin, 440.0, "cancel", nullptr, stop.get_token()));
  CHECK(cancelled == input);
  auto invalid = input; invalid.back() = std::numeric_limits<float>::quiet_NaN();
  CHECK(!voicing_experiment::applySampleTargetVoicing(invalid, performance, origin, 440.0, "nan"));
  CHECK(std::equal(invalid.begin(), invalid.end() - 1, input.begin()));
  CHECK(!voicing_experiment::applySampleTargetVoicing(cancelled, performance, origin, std::numeric_limits<double>::infinity(), "bad"));
  CHECK(!voicing_experiment::applySampleTargetVoicing(cancelled, performance, std::numeric_limits<time::SampleFrame>::max(), 440.0, "overflow"));
  fixture.region().performance.accepted.clear();
  const auto neutral = fixture.compile(); CHECK(neutral.at(origin).scoreFrequencyHz);
  CHECK(voicing_experiment::applySampleTargetVoicing(cancelled, neutral, origin, 440.0, "neutral"));
  CHECK(cancelled == input);
}
