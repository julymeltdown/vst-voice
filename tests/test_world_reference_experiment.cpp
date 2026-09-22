#include "test_framework.hpp"
#include "test_support.hpp"
#include "target_voicing_experiment.hpp"
#include "target_voicing_test_metrics.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/pitch.hpp"
#include SEAM_WORLD_IDENTITY_HEADER
#include "world/cheaptrick.h"
#include "world/d4c.h"
#include "world/dio.h"
#include "world/stonemask.h"
#include "world/synthesis.h"
#include <bit>
#include <iomanip>
#include <limits>
#include <set>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
using namespace seam;
using namespace synthesis::experimental::diagnostics;
using J = formats::JsonValue;
constexpr double frameMs = 5.0;
constexpr std::size_t maximumCells = 512U * 1024U;

bool sourceInventoryMatches(const std::filesystem::path& directory,
    std::span<const std::pair<std::string_view, std::string_view>> files) {
  if (!std::filesystem::is_directory(std::filesystem::symlink_status(directory))) return false;
  std::set<std::filesystem::path> allowedFiles, allowedDirectories, observedFiles;
  for (const auto& [name, hash] : files) {
    (void)hash;
    const std::filesystem::path path{name}; allowedFiles.insert(path);
    for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path())
      allowedDirectories.insert(parent);
  }
  // Do not follow directory symlinks, and stop before descending into any
  // unlisted directory. Content hashing runs only after this closed inventory.
  for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
    const auto relative = entry.path().lexically_relative(directory);
    const auto status = entry.symlink_status();
    if (std::filesystem::is_directory(status)) {
      if (!allowedDirectories.contains(relative)) return false;
    } else {
      if (!std::filesystem::is_regular_file(status) || !allowedFiles.contains(relative)) return false;
      observedFiles.insert(relative);
    }
  }
  return observedFiles == allowedFiles;
}

bool geometry(std::uint32_t rate, std::size_t samples) {
  return (rate == 44100U || rate == 48000U) && samples >= rate * 4U / 10U && samples <= rate;
}
J retainBytes(const std::filesystem::path& directory, const std::string& name, std::span<const double> values) {
  CHECK(values.size() <= maximumCells);
  std::vector<std::byte> bytes;
  bytes.reserve(values.size() * sizeof(double));
  for (const auto value : values) {
    // Retain rejected analysis bytes too. Nonfinite values are counted and
    // refused before synthesis, never encoded as JSON numbers or sanitized.
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (unsigned i = 0U; i < 8U; ++i) bytes.push_back(static_cast<std::byte>((bits >> (8U * i)) & 0xffU));
  }
  CHECK(core::durableAtomicWrite(directory / name, bytes));
  return J{J::Object{{"file", J{name}}, {"sha256", J{core::sha256Hex(bytes)}},
      {"elements", J{static_cast<std::int64_t>(values.size())}}, {"encoding", J{"little-endian IEEE754 float64"}}}};
}
struct Analysis {
  int fft{};
  int rows{};
  int bins{};
  std::vector<double> times, f0, envelope, aperiodicity;
};
std::vector<double*> rows(std::vector<double>& data, const Analysis& analysis) {
  CHECK(data.size() == static_cast<std::size_t>(analysis.rows) * static_cast<std::size_t>(analysis.bins));
  std::vector<double*> result(static_cast<std::size_t>(analysis.rows));
  for (int i = 0; i < analysis.rows; ++i) result[static_cast<std::size_t>(i)] =
      data.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(analysis.bins);
  return result;
}
// All calls are serial: this upstream pin has mutable globally reseeded noise.
Analysis analyze(std::span<const float> input, std::uint32_t rate, double constantHz) {
  CHECK(geometry(rate, input.size()));
  CHECK(constantHz == 0.0 || (constantHz >= 71.0 && constantHz <= 1200.0));
  CHECK(std::all_of(input.begin(), input.end(), [](float value) { return std::isfinite(value); }));
  const auto sampleRate = static_cast<int>(rate), length = static_cast<int>(input.size());
  Analysis result;
  result.rows = GetSamplesForDIO(sampleRate, length, frameMs);
  CHECK(result.rows >= 2); CHECK(result.rows <= 202);
  CheapTrickOption cheap{}; InitializeCheapTrickOption(sampleRate, &cheap);
  cheap.q1 = -0.15; cheap.f0_floor = 71.0;
  cheap.fft_size = GetFFTSizeForCheapTrick(sampleRate, &cheap);
  result.fft = cheap.fft_size;
  CHECK(result.fft >= 256); CHECK(result.fft <= 4096);
  result.bins = result.fft / 2 + 1;
  const auto cells = static_cast<std::size_t>(result.rows) * static_cast<std::size_t>(result.bins);
  CHECK(cells <= maximumCells);
  result.times.resize(static_cast<std::size_t>(result.rows));
  result.f0.resize(static_cast<std::size_t>(result.rows));
  result.envelope.resize(cells); result.aperiodicity.resize(cells);
  const std::vector<double> source(input.begin(), input.end());
  if (constantHz > 0.0) {
    for (int i = 0; i < result.rows; ++i) {
      result.times[static_cast<std::size_t>(i)] = static_cast<double>(i) * frameMs / 1000.0;
      result.f0[static_cast<std::size_t>(i)] = constantHz;
    }
  } else {
    DioOption dio{}; InitializeDioOption(&dio);
    dio.f0_floor = 71.0; dio.f0_ceil = 1200.0; dio.channels_in_octave = 2.0;
    dio.frame_period = frameMs; dio.speed = 1; dio.allowed_range = 0.1;
    std::vector<double> initial(result.f0.size());
    Dio(source.data(), length, sampleRate, &dio, result.times.data(), initial.data());
    StoneMask(source.data(), length, sampleRate, result.times.data(), initial.data(), result.rows, result.f0.data());
  }
  CHECK(std::all_of(result.f0.begin(), result.f0.end(), [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1500.0; }));
  auto envelopeRows = rows(result.envelope, result), apRows = rows(result.aperiodicity, result);
  CheapTrick(source.data(), length, sampleRate, result.times.data(), result.f0.data(), result.rows, &cheap, envelopeRows.data());
  D4COption d4c{}; InitializeD4COption(&d4c); d4c.threshold = 0.85;
  D4C(source.data(), length, sampleRate, result.times.data(), result.f0.data(), result.rows, result.fft, &d4c, apRows.data());
  CHECK(std::all_of(result.envelope.begin(), result.envelope.end(), [](double value) { return std::isfinite(value) && value > 0.0; }));
  const auto nonfinite = std::count_if(result.aperiodicity.begin(), result.aperiodicity.end(), [](double value) { return !std::isfinite(value); });
  double minimum = std::numeric_limits<double>::infinity(), maximum = -std::numeric_limits<double>::infinity();
  for (const auto value : result.aperiodicity) if (std::isfinite(value)) { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
  std::cout << std::setprecision(17) << "[WORLD-AP-ADMISSION] rate=" << rate << " analysis_hz=" << constantHz
            << " nonfinite=" << nonfinite << " minimum=" << minimum << " maximum=" << maximum << '\n';
  return result;
}
std::vector<float> synthesize(Analysis& analysis, std::uint32_t rate, std::size_t count, bool unvoiced) {
  CHECK(geometry(rate, count)); CHECK(analysis.rows >= 2);
  auto f0 = analysis.f0, ap = analysis.aperiodicity;
  if (unvoiced) { std::fill(f0.begin(), f0.end(), 0.0); std::fill(ap.begin(), ap.end(), 1.0); }
  CHECK(std::all_of(ap.begin(), ap.end(), [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }));
  auto spRows = rows(analysis.envelope, analysis), apRows = rows(ap, analysis);
  std::vector<double> result(count);
  Synthesis(f0.data(), analysis.rows, spRows.data(), apRows.data(), analysis.fft,
      frameMs, static_cast<int>(rate), static_cast<int>(count), result.data());
  std::vector<float> output(count);
  for (std::size_t i = 0U; i < count; ++i) {
    CHECK(std::isfinite(result[i])); CHECK(std::abs(result[i]) <= std::numeric_limits<float>::max());
    output[i] = static_cast<float>(result[i]);
  }
  return output;
}
std::vector<float> convertSeam(std::span<const float> input, std::uint32_t rate, double carrierHz, bool speech) {
  application::ProjectFactory factory{71931U};
  auto project = factory.createProject("Target voicing");
  const auto track = factory.addVocalTrack(project, "Original");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{137}, time::Tick{3840});
  auto& region = *project.findRegion(regionId);
  const auto midi = static_cast<std::uint8_t>(std::lround(69.0 + 12.0 * std::log2(carrierHz / 440.0)));
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, speech ? 67U : midi, U"あ", domain::Language::Japanese);
  region.lyrics.push_back(lyric); region.notes.push_back(note);
  region.performance.takes = {{.id = "voicing", .sourceRegionId = regionId,
      .resource = {domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = {domain::Language::Japanese, "fixture", "1", std::string(64U, 'b'), std::string(64U, 'c'), std::string(64U, 'd')},
      .generatorId = "fixture", .generatorVersion = "1", .range = {time::Tick{0}, time::Tick{3840}},
      .lanes = {{domain::PerformanceChannel::Pitch, {{time::Tick{0}, std::nullopt}}}}}};
  region.performance.accepted = {{"voicing", domain::PerformanceChannel::Pitch, note.id, time::Tick{0}}};
  const auto performance = synthesis::compileScorePerformance(project, region, rate); CHECK(performance);
  std::vector<float> output(input.begin(), input.end());
  CHECK(synthesis::experimental::applySampleTargetVoicing(output, performance.value(),
      performance.value().notes().front().startFrame, carrierHz, speech ? "speech-fixture" : "vowel"));
  return output;
}
J measure(const std::filesystem::path& directory, const std::string& name,
    std::span<const float> input, const std::vector<float>& output, std::uint32_t rate, double oracleHz, bool unvoicingApplicable) {
  CHECK(output.size() == input.size());
  CHECK(std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value); }));
  const auto a = input.subspan(rate / 10U, rate * 3U / 10U);
  const auto b = std::span<const float>{output}.subspan(rate / 10U, rate * 3U / 10U);
  const auto ratio = std::sqrt(energy(b) / energy(a));
  const auto correlation = periodicity(b, rate, oracleHz), sourceCorrelation = periodicity(a, rate, oracleHz);
  const auto ab = bands(a, rate), bb = bands(b, rate);
  double maximumBandError = 0.0, peak = 0.0;
  for (std::size_t i = 0U; i < ab.size(); ++i) maximumBandError = std::max(maximumBandError, std::abs(ab[i] - bb[i]));
  for (const auto value : output) peak = std::max(peak, std::abs(static_cast<double>(value)));
  CHECK(std::isfinite(ratio)); CHECK(std::isfinite(correlation)); CHECK(std::isfinite(maximumBandError));
  CHECK(voicebank::writeWav(directory / (name + ".wav"), {.sampleRate = rate, .channels = 1U,
      .sampleFormat = voicebank::WavSampleFormat::Float32}, output));
  const auto reread = voicebank::readWav(directory / (name + ".wav")); CHECK(reread); CHECK(reread.value().interleaved == output);
  const auto hash = core::sha256File(directory / (name + ".wav")); CHECK(hash);
  J::Array failures;
  if (unvoicingApplicable) {
    if (!(ratio > 0.7 && ratio < 1.3)) failures.emplace_back("rms-ratio");
    if (!(correlation < 0.65 && correlation < sourceCorrelation)) failures.emplace_back("periodicity");
    if (!(maximumBandError < 0.20)) failures.emplace_back("spectral-share");
  }
  const auto assessment = !unvoicingApplicable ? "NOT_APPLICABLE_RECONSTRUCTION" : failures.empty() ? "PASS" : "FAIL";
  std::cout << "[WORLD-REFERENCE] " << directory.filename().string() << ' ' << name << " rms=" << ratio
            << " ncc=" << correlation << " bands=" << maximumBandError << " peak=" << peak << " acoustic=" << assessment << '\n';
  return J{J::Object{{"id", J{name}}, {"executionStatus", J{"PASS"}}, {"unvoicingCriterionApplicable", J{unvoicingApplicable}},
      {"acousticAssessment", J{assessment}}, {"failedCriteria", J{std::move(failures)}}, {"rmsRatio", J{ratio}},
      {"periodicity", J{correlation}}, {"sourcePeriodicity", J{sourceCorrelation}}, {"maximumBandShareError", J{maximumBandError}},
      {"peak", J{peak}}, {"nonfiniteFrames", J{std::int64_t{0}}}, {"outputSha256", J{hash.value()}}, {"outputFile", J{name + ".wav"}}}};
}
}

TEST_CASE("WORLD reference bounds reject unsupported geometry before inference") {
  CHECK(geometry(44100U, 22050U)); CHECK(geometry(48000U, 48000U));
  CHECK(!geometry(8000U, 4000U)); CHECK(!geometry(192000U, 96000U));
  CHECK(!geometry(44100U, 1U)); CHECK(!geometry(48000U, 48001U));
  CHECK(!geometry(48000U, std::numeric_limits<std::size_t>::max()));
}

TEST_CASE("WORLD execution rejects unlisted source entries before hashing or inference") {
  const std::array<std::pair<std::string_view, std::string_view>, 2U> files{{{"LICENSE.txt", "unused"}, {"src/unit.cpp", "unused"}}};
  const auto fresh = [&] {
    const auto directory = test::support::temporaryDirectory("world-inventory");
    CHECK(core::durableAtomicWriteText(directory / "LICENSE.txt", "fixture"));
    CHECK(core::durableAtomicWriteText(directory / "src/unit.cpp", "fixture"));
    CHECK(sourceInventoryMatches(directory, files));
    return directory;
  };
  const auto shadow = fresh();
  CHECK(core::durableAtomicWriteText(shadow / "src/math.h", "#error unpinned\n"));
  CHECK(!sourceInventoryMatches(shadow, files));
  const auto nested = fresh();
  std::filesystem::create_directory(nested / "src/unlisted");
  CHECK(!sourceInventoryMatches(nested, files));
  const auto missing = test::support::temporaryDirectory("world-missing");
  CHECK(!sourceInventoryMatches(missing, files));
#ifndef _WIN32
  const auto symlink = fresh();
  std::filesystem::create_directory_symlink(shadow, symlink / "src/linked");
  CHECK(!sourceInventoryMatches(symlink, files));
  const auto fifo = fresh();
  CHECK(::mkfifo((fifo / "src/pipe.h").c_str(), 0600) == 0);
  CHECK(!sourceInventoryMatches(fifo, files));
#endif
}

TEST_CASE("pinned WORLD comparison retains every raw reconstruction and unvoicing outcome") {
  CHECK(sourceInventoryMatches(SEAM_WORLD_SOURCE_DIR, world_reference::files));
  for (const auto& [path, expected] : world_reference::files) {
    const auto hash = core::sha256File(std::filesystem::path{SEAM_WORLD_SOURCE_DIR} / path, 256U * 1024U);
    CHECK(hash); CHECK(hash.value() == expected);
  }
  const auto executableHash = core::sha256File(SEAM_WORLD_EXECUTABLE); CHECK(executableHash);
  const auto root = std::filesystem::path{SEAM_WORLD_REPORT_DIRECTORY} / executableHash.value();
  J::Array results; bool successful = true;
  std::size_t knownAnalysisRejections = 0U, unexpectedErrors = 0U;
  const auto run = [&](const std::string& id, const std::vector<float>& input, std::uint32_t rate,
      double analysisHz, double carrierHz, double oracleHz, bool speech) {
    const auto directory = root / id;
    J::Array outputs;
    J::Object record{{"id", J{id}}, {"sampleRate", J{static_cast<std::int64_t>(rate)}},
        {"sourceFrames", J{static_cast<std::int64_t>(input.size())}},
        {"analysisPolicy", J{analysisHz > 0.0 ? (speech ? "constant-speech-hypothesis-not-original-F0" : "synthetic-ground-truth") : "DIO-then-StoneMask"}},
        {"analysisConstantHz", analysisHz > 0.0 ? J{analysisHz} : J{}}, {"seamCarrierHz", J{carrierHz}}, {"oracleHz", J{oracleHz}}};
    try {
      CHECK(geometry(rate, input.size()));
      CHECK(voicebank::writeWav(directory / "source.wav", {.sampleRate = rate, .channels = 1U,
          .sampleFormat = voicebank::WavSampleFormat::Float32}, input));
      const auto sourceHash = core::sha256File(directory / "source.wav"); CHECK(sourceHash);
      record.emplace("sourceWavSha256", J{sourceHash.value()});
      // The SEAM comparator does not depend on WORLD analysis admission.
      outputs.push_back(measure(directory, "seam-experiment", input,
          convertSeam(input, rate, carrierHz, speech), rate, oracleHz, true));
      auto analysis = analyze(input, rate, analysisHz);
      const auto repeated = analyze(input, rate, analysisHz);
      CHECK(analysis.times == repeated.times); CHECK(analysis.f0 == repeated.f0);
      CHECK(analysis.envelope == repeated.envelope);
      CHECK(std::equal(analysis.aperiodicity.begin(), analysis.aperiodicity.end(), repeated.aperiodicity.begin(),
          [](double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }));
      J::Object parameters{{"timeAxis", retainBytes(directory, "time-axis.f64le", analysis.times)},
          {"analysisF0", retainBytes(directory, "analysis-f0.f64le", analysis.f0)},
          {"spectralEnvelope", retainBytes(directory, "envelope.f64le", analysis.envelope)},
          {"analysisAperiodicity", retainBytes(directory, "aperiodicity.f64le", analysis.aperiodicity)}};
      std::vector<double> zeros(analysis.f0.size(), 0.0), ones(analysis.aperiodicity.size(), 1.0);
      parameters.emplace("unvoicedSynthesisF0", retainBytes(directory, "unvoiced-f0.f64le", zeros));
      parameters.emplace("unvoicedSynthesisAperiodicity", retainBytes(directory, "unvoiced-aperiodicity.f64le", ones));
      const auto nonfinite = std::count_if(analysis.aperiodicity.begin(), analysis.aperiodicity.end(), [](double value) { return !std::isfinite(value); });
      const auto outOfRange = std::count_if(analysis.aperiodicity.begin(), analysis.aperiodicity.end(), [](double value) { return std::isfinite(value) && (value < 0.0 || value > 1.0); });
      J::Array nanIndices, positiveInfIndices, negativeInfIndices, outOfRangeIndices;
      for (std::size_t i = 0U; i < analysis.aperiodicity.size(); ++i) {
        const auto value = analysis.aperiodicity[i];
        const J index{static_cast<std::int64_t>(i)};
        if (std::isnan(value)) nanIndices.push_back(index);
        else if (std::isinf(value)) (value > 0.0 ? positiveInfIndices : negativeInfIndices).push_back(index);
        else if (value < 0.0 || value > 1.0) outOfRangeIndices.push_back(index);
      }
      const J analysisReport{J::Object{{"schemaVersion", J{std::int64_t{1}}}, {"rows", J{static_cast<std::int64_t>(analysis.rows)}},
          {"bins", J{static_cast<std::int64_t>(analysis.bins)}}, {"aperiodicityNonfiniteCells", J{static_cast<std::int64_t>(nonfinite)}},
          {"aperiodicityFiniteCells", J{static_cast<std::int64_t>(analysis.aperiodicity.size()) - static_cast<std::int64_t>(nonfinite)}},
          {"nanCells", J{static_cast<std::int64_t>(nanIndices.size())}},
          {"positiveInfinityCells", J{static_cast<std::int64_t>(positiveInfIndices.size())}},
          {"negativeInfinityCells", J{static_cast<std::int64_t>(negativeInfIndices.size())}},
          {"aperiodicityOutOfRangeFiniteCells", J{static_cast<std::int64_t>(outOfRange)}}, {"parameters", J{parameters}},
          {"nanIndicesRowMajor", J{std::move(nanIndices)}}, {"positiveInfinityIndicesRowMajor", J{std::move(positiveInfIndices)}},
          {"negativeInfinityIndicesRowMajor", J{std::move(negativeInfIndices)}}, {"outOfRangeFiniteIndicesRowMajor", J{std::move(outOfRangeIndices)}},
          {"synthesisAdmission", J{nonfinite || outOfRange ? "REJECTED_INVALID_ANALYSIS" : "ADMITTED"}}}};
      CHECK(core::durableAtomicWriteText(directory / "analysis.json", formats::stringifyJson(analysisReport)));
      const auto analysisHash = core::sha256File(directory / "analysis.json"); CHECK(analysisHash);
      record.emplace("retainedAnalysisFile", J{id + "/analysis.json"});
      record.emplace("retainedAnalysisSha256", J{analysisHash.value()});
      record.emplace("analysisRows", J{static_cast<std::int64_t>(analysis.rows)});
      record.emplace("fftSize", J{static_cast<std::int64_t>(analysis.fft)});
      record.emplace("bins", J{static_cast<std::int64_t>(analysis.bins)});
      record.emplace("voicedAnalysisRows", J{static_cast<std::int64_t>(std::count_if(analysis.f0.begin(), analysis.f0.end(), [](double hz) { return hz > 0.0; }))});
      record.emplace("parameters", J{std::move(parameters)});
      if (nonfinite || outOfRange) {
        successful = false;
        if (world_reference::variant == "upstream" && !speech && nonfinite > 0 && outOfRange == 0) ++knownAnalysisRejections;
        else ++unexpectedErrors;
        for (const auto* name : {"world-reconstruction", "world-forced-unvoiced"})
          outputs.emplace_back(J::Object{{"id", J{name}}, {"executionStatus", J{"NOT_RUN"}},
              {"reason", J{"REJECTED_INVALID_ANALYSIS_AP"}}, {"acousticAssessment", J{"NOT_RUN"}}});
        record.emplace("executionStatus", J{"ERROR"});
        record.emplace("error", J{"REJECTED_INVALID_ANALYSIS_AP"});
        record.emplace("outputs", J{std::move(outputs)});
        results.emplace_back(std::move(record));
        return;
      }
      for (const bool unvoiced : {false, true}) {
        const auto output = synthesize(analysis, rate, input.size(), unvoiced);
        CHECK(output == synthesize(analysis, rate, input.size(), unvoiced));
        outputs.push_back(measure(directory, unvoiced ? "world-forced-unvoiced" : "world-reconstruction",
            input, output, rate, oracleHz, unvoiced));
      }
      record.emplace("executionStatus", J{"PASS"});
      record.emplace("outputs", J{std::move(outputs)});
      results.emplace_back(std::move(record));
    } catch (const std::exception& error) {
      successful = false; ++unexpectedErrors;
      for (const auto* name : {"world-reconstruction", "world-forced-unvoiced"}) {
        if (std::none_of(outputs.begin(), outputs.end(), [name](const J& item) { return item.find("id")->asString() == name; }))
          outputs.emplace_back(J::Object{{"id", J{name}}, {"executionStatus", J{"NOT_RUN"}}, {"reason", J{"EXECUTION_ERROR"}}});
      }
      record.emplace("executionStatus", J{"ERROR"}); record.emplace("error", J{error.what()});
      record.emplace("outputs", J{std::move(outputs)});
      results.emplace_back(std::move(record));
    }
  };
  for (const auto rate : {44100U, 48000U}) for (const auto midi : {48U, 60U, 72U}) {
    const auto hz = 440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0);
    run("synthetic-" + std::to_string(rate) + "-" + std::to_string(midi), vowel(rate, hz), rate, hz, hz, hz, false);
  }
  const auto sourceHash = core::sha256File(SEAM_WORLD_SPEECH_FIXTURE); CHECK(sourceHash);
  CHECK(sourceHash.value() == "caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9");
  const auto source = voicebank::readWav(SEAM_WORLD_SPEECH_FIXTURE); CHECK(source);
  const auto input = source.value().monoMix(); const auto rate = source.value().sampleRate;
  const auto pitch = voicebank::analyzePitch(input, rate); CHECK(pitch);
  const auto oracleHz = voicebank::medianVoicedPitch(pitch.value()); CHECK_NEAR(oracleHz, 991.014, 0.01);
  const auto rootHz = 440.0 * std::exp2(-2.0 / 12.0);
  run("speech-root-67", input, rate, rootHz, rootHz, oracleHz, true);
  run("speech-estimated-constant", input, rate, oracleHz, oracleHz, oracleHz, true);
  run("speech-dio-stonemask", input, rate, 0.0, rootHz, oracleHz, true);
  const J report{J::Object{{"formatId", J{"com.project-seam.world-reference-experiment"}}, {"schemaVersion", J{std::int64_t{1}}},
      {"comparisonRevision", J{std::int64_t{2}}}, {"executionStatus", J{successful ? "PASS" : "ERROR"}},
      {"variant", J{std::string(world_reference::variant)}},
      {"knownAnalysisRejections", J{static_cast<std::int64_t>(knownAnalysisRejections)}},
      {"unexpectedErrors", J{static_cast<std::int64_t>(unexpectedErrors)}},
      {"engineeringVerification", J{"SEPARATE_CTEST_RESULT; expected invalid upstream AP reporting is not successful WORLD execution"}},
      {"productionEnabled", J{false}}, {"releaseEligible", J{false}}, {"listeningStatus", J{"NOT_REVIEWED"}},
      {"comparisonScope", J{"complete configured pipelines; unmodified upstream or upstream + only extracted OpenUtau D4C guard as explicitly identified; not full OpenUtau renderer, isolated algorithm quality or perceptual gold standard"}},
      {"worldRevision", J{std::string(world_reference::revision)}}, {"sourceManifestSha256", J{std::string(world_reference::sourceManifestSha256)}},
      {"sourceLockSha256", J{std::string(world_reference::lockSha256)}}, {"compiler", J{std::string(world_reference::compiler)}},
      {"configuration", J{std::string(world_reference::configuration)}}, {"executableSha256", J{executableHash.value()}},
      {"originalSpeechWavSha256", J{sourceHash.value()}}, {"rawFloat32NoGainClippingAlignmentOrCropping", J{true}},
      {"analysisOptions", J{J::Object{{"frameMilliseconds", J{5.0}}, {"f0Floor", J{71.0}}, {"dioF0Ceiling", J{1200.0}},
          {"dioChannelsPerOctave", J{2.0}}, {"dioSpeed", J{std::int64_t{1}}}, {"dioAllowedRange", J{0.1}}, {"cheapTrickQ1", J{-0.15}}, {"d4cThreshold", J{0.85}}}}},
      {"measurement", J{"unchanged source-lag-ncc-v1, same source oracle for every output; original 100-400ms window; RMS (0.7,1.3), NCC<0.65 and <source, max band-share error<0.20; reconstruction NCC cutoff not applicable"}},
      {"unvoicedOverrides", J{"analysis retained unchanged; synthesis F0=0 and AP=1; same envelope; construction, not D4C inference"}},
      {"randomness", J{"serial upstream calls with pinned global reseeding; exact repeated analysis and synthesis verified; no independent WORLD noise seeds claimed"}},
      {"cases", J{std::move(results)}}}};
  CHECK(core::durableAtomicWriteText(root / "comparison.json", formats::stringifyJson(report)));
  std::cout << "[WORLD-COMPARISON] " << root / "comparison.json" << " execution=" << (successful ? "PASS" : "ERROR") << '\n';
  CHECK(report.find("cases")->asArray().size() == 9U); CHECK(unexpectedErrors == 0U);
  CHECK(knownAnalysisRejections == (world_reference::variant == "upstream" ? 6U : 0U));
}
