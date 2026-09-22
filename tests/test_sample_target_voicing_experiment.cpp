#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "target_voicing_experiment.hpp"
#include "target_voicing_test_metrics.hpp"
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
#include <random>

namespace {
using namespace seam;
using namespace seam::domain;
using namespace seam::synthesis::experimental::diagnostics;
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
  const auto executableHash = core::sha256File(SEAM_TARGET_VOICING_EXECUTABLE); CHECK(executableHash);
  // New executable identities must not overwrite the historical r1 assessment.
  const auto artifacts = std::filesystem::path{SEAM_TARGET_VOICING_REPORT_DIRECTORY} / executableHash.value();
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

TEST_CASE("source lag oracle calibration distinguishes construction labels from perceptual voicing") {
  // Prospective calibration v1, not a replacement oracle or threshold tuning.
  // Frozen grid: 2 rates x 3 centers x (1 tone + 3 seeds x
  // (1 white + 5 resonated-noise + 3 tone/noise mixtures)) = 168 controls.
  // Construction labels describe EXCITATION, not perceived voicing or quality.
  constexpr std::array<std::uint32_t, 2U> rates{44100U, 48000U};
  constexpr std::array<double, 3U> centers{220.0, 440.0, 991.01406569073924};
  constexpr std::array<std::uint32_t, 3U> seeds{91073U, 71931U, 26701U};
  constexpr std::array<double, 5U> widths{20.0, 50.0, 100.0, 200.0, 400.0};
  constexpr std::array<double, 3U> mixtures{0.25, 0.5, 0.75};
  using J = formats::JsonValue;
  J::Array cases;
  std::size_t aperiodicCount = 0U, periodicCount = 0U, mixedCount = 0U;
  std::size_t falsePositives = 0U, falseNegatives = 0U;
  const auto executableHash = core::sha256File(SEAM_TARGET_VOICING_EXECUTABLE); CHECK(executableHash);
  const auto artifacts = std::filesystem::path{SEAM_TARGET_VOICING_REPORT_DIRECTORY} /
      executableHash.value() / "oracle-calibration-v1";
  for (const auto rate : rates) {
    for (std::size_t centerIndex = 0U; centerIndex < centers.size(); ++centerIndex) {
      const auto hz = centers[centerIndex];
      const auto count = static_cast<std::size_t>(rate) * 8U / 10U;
      const auto first = static_cast<std::size_t>(rate) / 2U;
      const auto measured = static_cast<std::size_t>(rate) * 3U / 10U;
      const auto prefix = std::to_string(rate) + "-center-" + std::to_string(centerIndex);
      const auto record = [&](const std::string& suffix, std::vector<float> samples,
          const std::string& label, std::uint32_t seed, double width, double mixture) {
        CHECK(samples.size() == count);
        float peak = 0.0F;
        for (const auto sample : samples) { CHECK(std::isfinite(sample)); peak = std::max(peak, std::abs(sample)); }
        CHECK(peak > 0.0F);
        for (auto& sample : samples) sample *= 0.45F / peak;
        const auto correlation = periodicity(std::span<const float>{samples}.subspan(first, measured), rate, hz);
        CHECK(std::isfinite(correlation));
        const bool detectedPeriodic = correlation >= 0.65;
        // This audits ONLY the frozen NCC cutoff. The speech conjunction also
        // includes RMS, source improvement and spectral retention; it is unchanged.
        if (label == "aperiodic-excitation") { ++aperiodicCount; if (detectedPeriodic) ++falsePositives; }
        else if (label == "periodic-excitation") { ++periodicCount; if (!detectedPeriodic) ++falseNegatives; }
        else { CHECK(label == "mixed-excitation"); ++mixedCount; }
        const auto filename = prefix + "-" + suffix + ".wav";
        CHECK(voicebank::writeWav(artifacts / filename, {.sampleRate = rate, .channels = 1U,
            .sampleFormat = voicebank::WavSampleFormat::Float32}, samples));
        const auto reread = voicebank::readWav(artifacts / filename); CHECK(reread);
        CHECK(reread.value().interleaved == samples);
        const auto hash = core::sha256File(artifacts / filename); CHECK(hash);
        cases.emplace_back(J::Object{{"id", J{prefix + "-" + suffix}}, {"constructionLabel", J{label}},
            {"sampleRate", J{static_cast<std::int64_t>(rate)}}, {"oracleHz", J{hz}},
            {"seed", seed ? J{static_cast<std::int64_t>(seed)} : J{}},
            {"resonatorBandwidthHz", width > 0.0 ? J{width} : J{}},
            {"resonatorPoleRadius", width > 0.0 ? J{std::exp(-std::numbers::pi * width / rate)} : J{}},
            {"normalizedTonePowerWeight", mixture >= 0.0 ? J{mixture} : J{}},
            {"analysisStartFrame", J{static_cast<std::int64_t>(first)}},
            {"analysisFrameCount", J{static_cast<std::int64_t>(measured)}},
            {"correlation", J{correlation}}, {"cutoffDetectsPeriodic", J{detectedPeriodic}},
            {"outputFile", J{filename}}, {"outputSha256", J{hash.value()}}});
        std::cout << "[ORACLE-CONTROL] " << prefix << '-' << suffix << " label=" << label
                  << " correlation=" << correlation << " periodic_cutoff=" << detectedPeriodic << '\n';
        return correlation;
      };
      std::vector<float> tone(count);
      for (std::size_t i = 0U; i < count; ++i)
        tone[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
      CHECK(record("tone", tone, "periodic-excitation", 0U, 0.0, 1.0) > 0.9);
      for (const auto seed : seeds) {
        std::mt19937 random{seed};
        std::vector<float> white(count);
        for (auto& value : white)
          value = static_cast<float>((static_cast<double>(random()) + 0.5) * 0x1.0p-31 - 1.0);
        const auto seedLabel = "seed-" + std::to_string(seed);
        CHECK(record(seedLabel + "-white", white, "aperiodic-excitation", seed, 0.0, 0.0) < 0.15);
        for (const auto width : widths) {
          // Independent causal two-pole noise filter, not production synthesis,
          // FFT, phase randomization, or the envelope/noise conversion under test.
          const auto radius = std::exp(-std::numbers::pi * width / rate);
          const auto coefficient = 2.0 * radius * std::cos(2.0 * std::numbers::pi * hz / rate);
          std::vector<float> colored(count);
          double previous = 0.0, beforePrevious = 0.0;
          for (std::size_t i = 0U; i < count; ++i) {
            const auto value = white[i] + coefficient * previous - radius * radius * beforePrevious;
            colored[i] = static_cast<float>(value);
            beforePrevious = previous; previous = value;
          }
          record(seedLabel + "-bandwidth-" + std::to_string(static_cast<unsigned>(width)),
              std::move(colored), "aperiodic-excitation", seed, width, 0.0);
        }
        const auto toneRms = std::sqrt(energy(tone) / static_cast<double>(count));
        const auto noiseRms = std::sqrt(energy(white) / static_cast<double>(count));
        for (const auto mixture : mixtures) {
          std::vector<float> mixed(count);
          for (std::size_t i = 0U; i < count; ++i)
            mixed[i] = static_cast<float>(std::sqrt(mixture) * tone[i] / toneRms +
                std::sqrt(1.0 - mixture) * white[i] / noiseRms);
          record(seedLabel + "-tone-weight-" + std::to_string(static_cast<unsigned>(mixture * 100.0)),
              std::move(mixed), "mixed-excitation", seed, 0.0, mixture);
        }
      }
    }
  }
  const J report{J::Object{{"formatId", J{"com.project-seam.source-lag-oracle-calibration"}},
      {"schemaVersion", J{std::int64_t{1}}}, {"calibrationRevision", J{std::int64_t{1}}},
      {"executionStatus", J{"PASS"}},
      {"assessment", J{falsePositives ? "EXCITATION_CLASSIFIER_SCOPE_LIMITATION" : "NO_COUNTEREXAMPLE_IN_GRID"}},
      {"oracle", J{"source-lag-ncc-v1; exact unchanged C++ function used by the frozen speech experiment"}},
      {"cutoffPeriodicMinimumInclusive", J{0.65}}, {"durationSeconds", J{0.8}},
      {"analysisWindowSeconds", J{J::Array{J{0.5}, J{0.8}}}},
      {"constructionLabelsArePerceptualLabels", J{false}}, {"productionEnabled", J{false}},
      {"releaseEligible", J{false}}, {"listeningStatus", J{"NOT_REVIEWED"}},
      {"historicalSpeechAssessment", J{"UNCHANGED_FAIL"}}, {"replacementMetric", J{}},
      {"generator", J{"sine; std::mt19937(seed), uint32 half-bin uniform to Float32; causal y=x+2*r*cos(2*pi*hz/rate)*y[-1]-r*r*y[-2], r=exp(-pi*bandwidth/rate); no production DSP"}},
      {"mixtures", J{"sqrt(weight)*unit-RMS tone + sqrt(1-weight)*unit-RMS white noise; weight is nominal component power, not exact total-waveform proportion; mixed labels excluded from binary errors"}},
      {"waveformNormalization", J{"whole-control peak to 0.45, then Float32; measure serialized samples after 500 ms warmup"}},
      {"executableSha256", J{executableHash.value()}},
      {"aperiodicConstructionCount", J{static_cast<std::int64_t>(aperiodicCount)}},
      {"periodicConstructionCount", J{static_cast<std::int64_t>(periodicCount)}},
      {"mixedConstructionCount", J{static_cast<std::int64_t>(mixedCount)}},
      {"falsePositivesAgainstAperiodicConstruction", J{static_cast<std::int64_t>(falsePositives)}},
      {"falseNegativesAgainstPeriodicConstruction", J{static_cast<std::int64_t>(falseNegatives)}},
      {"cases", J{std::move(cases)}}}};
  CHECK(core::durableAtomicWriteText(artifacts / "calibration.json", formats::stringifyJson(report)));
  std::cout << "[ORACLE-CALIBRATION] " << artifacts / "calibration.json" << " false_positive="
            << falsePositives << '/' << aperiodicCount << " false_negative=" << falseNegatives
            << '/' << periodicCount << " mixed=" << mixedCount << '\n';
  CHECK(aperiodicCount == 108U); CHECK(periodicCount == 6U); CHECK(mixedCount == 54U);
  CHECK(falsePositives > 0U); CHECK(falseNegatives == 0U);
}
