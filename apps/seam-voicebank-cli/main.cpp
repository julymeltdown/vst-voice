#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/waveform.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/neural_synthesis/model_contract.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/authoring/generation_job.hpp"
#include "signal_cancellation.hpp"
#include "sample_review_commands.hpp"
#include "campaign_commands.hpp"
#include "seam/formats/project_json.hpp"
#include <charconv>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void printError(const seam::core::Error& error) {
  std::cerr << "error: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

seam::core::Result<void> writeJson(const std::filesystem::path& path,
                                   const seam::formats::JsonValue& value) {
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return seam::core::failure(seam::core::ErrorCode::IoError,
                                 "Unable to create JSON output directory",
                                 error.message());
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to create JSON output", path.string());
  }
  stream << seam::formats::stringifyJson(value, true) << '\n';
  stream.flush();
  if (!stream) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to write JSON output", path.string());
  }
  return seam::core::success();
}

int validateCommand(const std::filesystem::path& manifestPath,
                    const std::filesystem::path& requestedRoot) {
  seam::voicebank::ManifestJsonCodec codec;
  const auto manifest = codec.load(manifestPath);
  if (!manifest) {
    printError(manifest.error());
    return 2;
  }
  const auto root = requestedRoot.empty() ? manifestPath.parent_path() : requestedRoot;
  seam::voicebank::BankValidator validator;
  const auto report = validator.validate(manifest.value(), root);
  seam::formats::JsonValue::Array issues;
  for (const auto& issue : report.issues) {
    issues.emplace_back(seam::formats::JsonValue::Object{
        {"severity", std::string{seam::voicebank::issueSeverityName(issue.severity)}},
        {"code", std::string{seam::voicebank::issueCodeName(issue.code)}},
        {"unitId", issue.unitId},
        {"message", issue.message},
    });
  }
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"voicebankId", manifest.value().id},
      {"version", manifest.value().version},
      {"root", root.generic_string()},
      {"unitsChecked", static_cast<std::int64_t>(report.unitsChecked)},
      {"errors", static_cast<std::int64_t>(report.errorCount())},
      {"warnings", static_cast<std::int64_t>(report.warningCount())},
      {"ok", report.ok()},
      {"issues", std::move(issues)},
  }};
  std::cout << seam::formats::stringifyJson(result, true) << '\n';
  return report.ok() ? 0 : 3;
}

int inspectCommand(const std::filesystem::path& manifestPath) {
  seam::voicebank::ManifestJsonCodec codec;
  const auto manifest = codec.load(manifestPath);
  if (!manifest) {
    printError(manifest.error());
    return 2;
  }
  seam::formats::JsonValue::Array units;
  for (const auto& unit : manifest.value().units) {
    seam::formats::JsonValue::Array phones;
    for (const auto& phone : unit.phones) phones.emplace_back(phone);
    units.emplace_back(seam::formats::JsonValue::Object{
        {"id", unit.id},
        {"alias", unit.alias},
        {"phones", std::move(phones)},
        {"kind", std::string{seam::voicebank::unitKindName(unit.kind)}},
        {"rootMidi", std::int64_t{unit.rootMidi}},
        {"style", unit.style},
        {"take", std::int64_t{unit.take}},
        {"renderer", std::string{seam::voicebank::rendererHintName(unit.renderer)}},
        {"audio", unit.audioPath.generic_string()},
    });
  }
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"id", manifest.value().id},
      {"version", manifest.value().version},
      {"displayName", manifest.value().displayName},
      {"sampleRate", static_cast<std::int64_t>(manifest.value().expectedSampleRate)},
      {"unitCount", static_cast<std::int64_t>(manifest.value().units.size())},
      {"units", std::move(units)},
  }};
  std::cout << seam::formats::stringifyJson(result, true) << '\n';
  return 0;
}

int analyzeCommand(const std::filesystem::path& wavPath,
                   const std::filesystem::path& outputDirectory) {
  const auto audio = seam::voicebank::readWav(wavPath);
  if (!audio) {
    printError(audio.error());
    return 2;
  }
  const auto mono = audio.value().monoMix();
  const auto waveform = seam::voicebank::WaveformPyramid::build(mono, 64, 14);
  if (!waveform) {
    printError(waveform.error());
    return 3;
  }
  const auto spectrogram = seam::voicebank::buildSpectrogram(
      mono, seam::voicebank::SpectrogramConfig{
                .fftSize = 1024,
                .hopSize = 256,
                .minimumDb = -90.0F,
                .maximumDb = -6.0F,
            });
  if (!spectrogram) {
    printError(spectrogram.error());
    return 4;
  }
  const auto pitch = seam::voicebank::analyzePitch(mono, audio.value().sampleRate);
  if (!pitch) {
    printError(pitch.error());
    return 5;
  }
  std::error_code error;
  std::filesystem::create_directories(outputDirectory, error);
  if (error) {
    std::cerr << "error: unable to create output directory (" << error.message() << ")\n";
    return 6;
  }
  const auto waveformResult = seam::voicebank::writeWaveformSvg(
      outputDirectory / "waveform.svg", waveform.value().levelFor(64.0),
      1400.0, 320.0, wavPath.filename().generic_string());
  if (!waveformResult) {
    printError(waveformResult.error());
    return 7;
  }
  const auto spectrogramResult = seam::voicebank::writeSpectrogramPgm(
      outputDirectory / "spectrogram.pgm", spectrogram.value());
  if (!spectrogramResult) {
    printError(spectrogramResult.error());
    return 8;
  }

  seam::formats::JsonValue::Array pitchFrames;
  for (const auto& frame : pitch.value()) {
    pitchFrames.emplace_back(seam::formats::JsonValue::Object{
        {"sourceFrame", static_cast<std::int64_t>(frame.sourceFrame)},
        {"f0Hz", frame.f0Hz},
        {"confidence", frame.confidence},
        {"voiced", frame.voiced},
    });
  }
  const auto stats = seam::voicebank::analyzeAudio(mono);
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"source", wavPath.generic_string()},
      {"sampleRate", static_cast<std::int64_t>(audio.value().sampleRate)},
      {"channels", static_cast<std::int64_t>(audio.value().channels)},
      {"frames", static_cast<std::int64_t>(audio.value().frameCount())},
      {"peak", static_cast<double>(stats.peak)},
      {"rms", stats.rms},
      {"dcOffset", stats.dcOffset},
      {"clippedSamples", static_cast<std::int64_t>(stats.clippedSamples)},
      {"medianPitchHz", seam::voicebank::medianVoicedPitch(pitch.value())},
      {"pitchFrames", std::move(pitchFrames)},
  }};
  const auto jsonResult = writeJson(outputDirectory / "analysis.json", result);
  if (!jsonResult) {
    printError(jsonResult.error());
    return 9;
  }
  std::cout << "analysis written to " << outputDirectory << '\n';
  return 0;
}

int inspectWavCommand(const std::filesystem::path& wavPath) {
  const auto audio = seam::voicebank::readWav(wavPath);
  if (!audio) {
    printError(audio.error());
    return 2;
  }
  const auto mono = audio.value().monoMix();
  const auto stats = seam::voicebank::analyzeAudio(mono);
  const auto finite = std::all_of(
      audio.value().interleaved.begin(), audio.value().interleaved.end(),
      [](const float value) { return std::isfinite(value); });
  const auto audible = stats.peak > 1.0e-5F && stats.rms > 1.0e-6;
  const auto frames = audio.value().frameCount();
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"source", wavPath.generic_string()},
      {"sampleRate", static_cast<std::int64_t>(audio.value().sampleRate)},
      {"channels", static_cast<std::int64_t>(audio.value().channels)},
      {"frames", static_cast<std::int64_t>(frames)},
      {"durationSeconds", static_cast<double>(frames) /
                              static_cast<double>(audio.value().sampleRate)},
      {"peak", static_cast<double>(stats.peak)},
      {"rms", stats.rms},
      {"dcOffset", stats.dcOffset},
      {"clippedSamples", static_cast<std::int64_t>(stats.clippedSamples)},
      {"finiteDecodedSamples", finite},
      {"nonSilent", audible},
  }};
  std::cout << seam::formats::stringifyJson(result, true) << '\n';
  return finite && audible ? 0 : 3;
}

int bakeProjectCommand(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "bake-project requires PROJECT OUTPUT_DIRECTORY [SAMPLE_RATE]\n";
    return 1;
  }
  std::uint32_t rate = 48000U;
  if (argc == 5) {
    const std::string_view value{argv[4]};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), rate);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || rate < 8000U || rate > 384000U) {
      std::cerr << "error: sample rate must be an integer from 8000 to 384000\n"; return 1;
    }
  }
  std::error_code error;
  const auto path = std::filesystem::absolute(argv[2], error);
  if (error) { std::cerr << "error: cannot resolve project path\n"; return 1; }
  const auto project = seam::formats::ProjectJsonCodec{}.load(path);
  if (!project) { printError(project.error()); return 1; }
  std::vector<seam::rendering::TrackSingerSource> sources;
  seam::domain::TrackId activeTrack;
  seam::domain::RegionId activeRegion;
  std::size_t candidates = 0U;
  for (const auto& track : project.value().vocalTracks()) {
    const auto nonempty = std::count_if(track.regions.begin(), track.regions.end(), [](const auto& region) { return !region.notes.empty(); });
    if (nonempty == 0) continue;
    if (!track.proceduralRecipe) {
      std::cerr << "error: every nonempty vocal track must have a saved procedural recipe for bake-project\n"; return 1;
    }
    candidates += static_cast<std::size_t>(nonempty);
    sources.emplace_back(seam::rendering::TrackRecipeFileSource{track.id, *track.proceduralRecipe, path.parent_path()});
    if (!activeTrack.valid()) {
      activeTrack = track.id;
      activeRegion = std::find_if(track.regions.begin(), track.regions.end(), [](const auto& region) { return !region.notes.empty(); })->id;
    }
  }
  if (!activeTrack.valid()) { std::cerr << "error: project has no procedural material to bake\n"; return 1; }
  seam::authoring::ExportSettings settings;
  settings.sampleRate = rate; settings.includeMaster = false; settings.includeStems = false;
  settings.includeProceduralCandidates = true;
  const auto exported = seam::authoring::ExportService{}.exportSetWithSources(project.value(), sources, activeTrack, activeRegion,
      0U, argv[3], settings);
  if (!exported) { printError(exported.error()); return 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"state", seam::formats::JsonValue{"Committed"}}, {"approval", seam::formats::JsonValue{"unapproved"}},
      {"candidates", seam::formats::JsonValue{static_cast<std::int64_t>(candidates)}},
      {"receipt", seam::formats::JsonValue{exported.value().receiptPath.string()}}}}) << '\n';
  return 0;
}

int prepareGenerationBatchCommand(int argc, char** argv) {
  if (argc < 4 || argc > 67) {
    std::cerr << "prepare-generation-batch requires OUTPUT_JSON and 1 to 64 JOB_REFERENCE files\n"; return 1;
  }
  std::vector<seam::authoring::GenerationJobReference> jobs;
  for (int index = 3; index < argc; ++index) {
    const auto reference = seam::authoring::loadGenerationJobReference(argv[index]);
    if (!reference) { printError(reference.error()); return 1; }
    jobs.push_back(reference.value());
  }
  seam::voicebank_cli::SignalCancellation cancellation;
  if (!cancellation.install()) { std::cerr << "error: cannot install cancellation handlers\n"; return 1; }
  const auto saved = seam::authoring::saveGenerationBatch(argv[2], jobs, {}, cancellation.token());
  if (!saved) { printError(saved.error()); return 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"state", "BatchPrepared"}, {"manifestSha256", saved.value()}, {"jobs", static_cast<std::int64_t>(jobs.size())}}}) << '\n';
  return 0;
}

int prepareGenerationCommand(int argc, char** argv) {
  if (argc != 9) {
    std::cerr << "prepare-generation requires WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY\n"; return 1;
  }
  const auto parseId = [](std::string_view text) -> std::uint64_t {
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0U ||
        seam::domain::TrackId{value}.toString() != text) return 0U;
    return value;
  };
  const seam::domain::TrackId trackId{parseId(argv[4])};
  const seam::domain::RegionId regionId{parseId(argv[5])};
  if (!trackId.valid() || !regionId.valid()) { std::cerr << "error: track and region IDs must be canonical nonzero hexadecimal IDs\n"; return 1; }
  seam::voicebank_production::ProductionProjectRepository repository{argv[2]};
  const auto producer = repository.recover();
  if (!producer) { printError(producer.error()); return 1; }
  const auto job = seam::authoring::prepareGenerationJobFromScore(argv[8], argv[7], argv[3], trackId, regionId,
      producer.value(), argv[6]);
  if (!job) { printError(job.error()); return 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"state", seam::formats::JsonValue{"Prepared"}}, {"jobId", seam::formats::JsonValue{job.value().jobId}},
      {"manifestSha256", seam::formats::JsonValue{job.value().manifestSha256}}}}) << '\n';
  return 0;
}

int importGeneratedBatchCommand(int argc, char** argv) {
  if (argc != 7 && argc != 8) {
    std::cerr << "import-generated-batch requires WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]\n"; return 1;
  }
  seam::authoring::GenerationBatchLimits limits;
  if (argc == 8) {
    const std::string_view text{argv[7]};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), limits.maximumFrames);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) { std::cerr << "error: invalid batch frame budget\n"; return 1; }
  }
  if (limits.maximumFrames == 0U || limits.maximumFrames > 64ULL * 32ULL * 1024ULL * 1024ULL) {
    std::cerr << "error: batch collection frame budget exceeds bounds\n"; return 1;
  }
  seam::voicebank_cli::SignalCancellation cancellation;
  if (!cancellation.install()) { std::cerr << "error: cannot install batch collection cancellation handlers\n"; return 1; }
  const auto jobs = seam::authoring::loadGenerationBatch(argv[3], argv[4]);
  if (!jobs) { printError(jobs.error()); return 1; }
  const auto inputs = seam::authoring::inspectGenerationBatch(jobs.value(), limits, cancellation.token());
  if (!inputs) { printError(inputs.error()); return 1; }
  namespace production = seam::voicebank_production;
  production::ProductionProjectRepository repository{argv[2]};
  auto project = repository.recover();
  if (!project) { printError(project.error()); return 1; }
  seam::formats::JsonValue::Array history;
  for (const auto& input : inputs.value()) {
    if (cancellation.signal() != 0) return 128 + cancellation.signal();
    const auto found = repository.findCollectedGeneration(input.expectation);
    if (!found) { printError(found.error()); return 1; }
    if (!found.value()) continue;
    const auto& value = *found.value();
    if (value.generation != project.value().lastDurableGeneration) { std::cerr << "error: producer changed during batch recognition\n"; return 1; }
    history.emplace_back(seam::formats::JsonValue::Object{{"takeId", seam::formats::JsonValue{value.takeId}},
        {"state", seam::formats::JsonValue{production::toString(value.state)}}, {"active", seam::formats::JsonValue{value.active}}});
  }
  if (!history.empty()) {
    if (history.size() != inputs.value().size()) { std::cerr << "error: partially collected batch requires explicit reconciliation\n"; return 1; }
    std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
        {"result", seam::formats::JsonValue{"AlreadyCollected"}}, {"takes", seam::formats::JsonValue{std::move(history)}},
        {"generation", seam::formats::JsonValue{std::to_string(project.value().lastDurableGeneration)}}}}) << '\n';
    return 0;
  }
  const auto stateHash = seam::core::sha256Hex(production::encodeProductionProject(project.value()));
  for (const auto& input : inputs.value()) if (input.expectation.projectStateSha256 != stateHash) {
    std::cerr << "error: batch producer expectation is stale\n"; return 1;
  }
  for (const auto& job : jobs.value()) {
    const auto verified = seam::authoring::verifyGenerationJobOutput(job.directory, job.manifestSha256, cancellation.token());
    if (!verified) { printError(verified.error()); return 1; }
  }
  const auto imported = repository.importGeneratedBatch(project.value(), inputs.value(),
      {.action = "import-generated-batch", .subjectId = argv[4], .operatorId = argv[5], .occurredAtUtc = argv[6]},
      limits.maximumFrames, cancellation.token());
  if (!imported) { printError(imported.error()); return cancellation.signal() != 0 ? 128 + cancellation.signal() : 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"result", seam::formats::JsonValue{"Collected"}}, {"approval", seam::formats::JsonValue{"unapproved"}},
      {"count", seam::formats::JsonValue{static_cast<std::int64_t>(imported.value().assets.size())}},
      {"generation", seam::formats::JsonValue{std::to_string(imported.value().committedGeneration)}},
      {"projectSha256", seam::formats::JsonValue{imported.value().committedProjectSha256}},
      {"durabilityConfirmed", seam::formats::JsonValue{imported.value().durabilityConfirmed}},
      {"diagnostic", seam::formats::JsonValue{imported.value().diagnostic}},
      {"cancellationRequested", seam::formats::JsonValue{cancellation.signal() != 0}}}}) << '\n';
  return 0;
}

int runGenerationBatchCommand(int argc, char** argv) {
  if (argc != 4 && argc != 5) { std::cerr << "run-generation-batch requires BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]\n"; return 1; }
  seam::authoring::GenerationBatchLimits limits;
  if (argc == 5) {
    const std::string_view text{argv[4]};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), limits.maximumFrames);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
      std::cerr << "error: batch frame budget must be an unsigned integer\n"; return 1;
    }
  }
  seam::voicebank_cli::SignalCancellation cancellation;
  if (!cancellation.install()) { std::cerr << "error: cannot install batch cancellation handlers\n"; return 1; }
  const auto jobs = seam::authoring::loadGenerationBatch(argv[2], argv[3]);
  if (!jobs) { printError(jobs.error()); return 1; }
  const auto outputs = seam::authoring::runGenerationBatch(jobs.value(), limits, cancellation.token());
  if (cancellation.signal() != 0) {
    std::cerr << "batch cancelled; completed outputs remain available for verified reuse\n";
    return 128 + cancellation.signal();
  }
  if (!outputs) { printError(outputs.error()); return 1; }
  seam::formats::JsonValue::Array entries;
  for (const auto& output : outputs.value()) entries.emplace_back(seam::formats::JsonValue::Object{
      {"audio", seam::formats::JsonValue{output.audioPath.string()}}, {"metadata", seam::formats::JsonValue{output.metadataPath.string()}},
      {"audioSha256", seam::formats::JsonValue{output.audioSha256}}, {"reused", seam::formats::JsonValue{output.reused}}});
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"approval", seam::formats::JsonValue{"unapproved"}}, {"outputs", seam::formats::JsonValue{std::move(entries)}}}}) << '\n';
  return 0;
}

int runGenerationCommand(int argc, char** argv) {
  if (argc != 4) { std::cerr << "run-generation requires JOB_DIRECTORY MANIFEST_SHA256\n"; return 1; }
  seam::voicebank_cli::SignalCancellation cancellation;
  if (!cancellation.install()) { std::cerr << "error: cannot install generation cancellation handlers\n"; return 1; }
  const auto output = seam::authoring::runGenerationJob(argv[2], argv[3], cancellation.token());
  if (cancellation.signal() != 0) {
    std::cerr << "generation cancelled; any committed output remains available for verified reuse\n";
    return 128 + cancellation.signal();
  }
  if (!output) { printError(output.error()); return 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"metadata", seam::formats::JsonValue{output.value().metadataPath.string()}},
      {"audio", seam::formats::JsonValue{output.value().audioPath.string()}},
      {"audioSha256", seam::formats::JsonValue{output.value().audioSha256}},
      {"reused", seam::formats::JsonValue{output.value().reused}},
      {"approval", seam::formats::JsonValue{"unapproved"}}}}) << '\n';
  return 0;
}

int importGeneratedCommand(int argc, char** argv) {
  if (argc != 10) {
    std::cerr << "import-generated requires WORKSPACE METADATA WAV RECIPE EXPECTATION SHA256 OPERATOR UTC\n";
    return 1;
  }
  namespace production = seam::voicebank_production;
  const auto expectation = production::loadGenerationImportExpectation(argv[6], argv[7]);
  if (!expectation) { printError(expectation.error()); return 1; }
  production::ProductionProjectRepository repository{argv[2]};
  const auto collected = repository.findCollectedGeneration(expectation.value());
  if (!collected) { printError(collected.error()); return 1; }
  if (collected.value()) {
    const auto& existing = *collected.value();
    std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
        {"result", seam::formats::JsonValue{"AlreadyCollected"}}, {"takeId", seam::formats::JsonValue{existing.takeId}},
        {"state", seam::formats::JsonValue{production::toString(existing.state)}}, {"active", seam::formats::JsonValue{existing.active}},
        {"audioSha256", seam::formats::JsonValue{existing.audioSha256}},
        {"generation", seam::formats::JsonValue{std::to_string(existing.generation)}}}}) << '\n';
    return 0;
  }
  auto project = repository.recover();
  if (!project) { printError(project.error()); return 1; }
  const auto recipe = seam::voice_design::loadVoiceRecipeResource(argv[5]);
  if (!recipe) { printError(recipe.error()); return 1; }
  const auto& request = expectation.value();
  const auto imported = repository.importProceduralCandidate(project.value(), argv[3], argv[4], recipe.value(),
      {.takeId = request.takeId, .promptId = request.promptId, .coverageKey = request.coverageKey,
       .pitchLayer = request.pitchLayer, .supersedesTakeId = request.supersedesTakeId,
       .style = project.value().schemaVersion >= production::kProductionStyleSchemaVersion ? request.style : ""},
      {.action = request.supersedesTakeId.empty() ? "import-procedural" : "retake", .subjectId = request.takeId,
       .operatorId = argv[8], .occurredAtUtc = argv[9]}, {}, &request);
  if (!imported) { printError(imported.error()); return 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"result", seam::formats::JsonValue{"Collected"}},
      {"takeId", seam::formats::JsonValue{request.takeId}}, {"state", seam::formats::JsonValue{"MarkerReview"}},
      {"audioSha256", seam::formats::JsonValue{imported.value().sha256}},
      {"generation", seam::formats::JsonValue{std::to_string(imported.value().committedGeneration)}},
      {"projectSha256", seam::formats::JsonValue{imported.value().committedProjectSha256}},
      {"durabilityConfirmed", seam::formats::JsonValue{imported.value().durabilityConfirmed}},
      {"diagnostic", seam::formats::JsonValue{imported.value().diagnostic}}}}) << '\n';
  return 0;
}

int importProceduralCommand(int argc, char** argv) {
  if (argc != 12 && argc != 13) {
    std::cerr << "import-procedural requires WORKSPACE METADATA WAV RECIPE TAKE PROMPT COVERAGE MIDI OPERATOR UTC [SUPERSEDES]\n";
    return 1;
  }
  const std::string_view pitchText{argv[9]};
  std::int32_t pitch = 0;
  const auto parsed = std::from_chars(pitchText.data(), pitchText.data() + pitchText.size(), pitch);
  if (parsed.ec != std::errc{} || parsed.ptr != pitchText.data() + pitchText.size() || pitch < 0 || pitch > 127) {
    std::cerr << "error: MIDI layer must be an integer from 0 to 127\n";
    return 1;
  }
  seam::voicebank_production::ProductionProjectRepository repository{argv[2]};
  auto project = repository.recover();
  if (!project) { printError(project.error()); return 1; }
  const auto recipe = seam::voice_design::loadVoiceRecipeResource(argv[5]);
  if (!recipe) { printError(recipe.error()); return 1; }
  const auto imported = repository.importProceduralCandidate(project.value(), argv[3], argv[4], recipe.value(),
      {.takeId = argv[6], .promptId = argv[7], .coverageKey = argv[8], .pitchLayer = pitch,
       .supersedesTakeId = argc == 13 ? argv[12] : "", .initialState = seam::voicebank_production::UnitQueueState::MarkerReview,
       .review = std::nullopt},
      {.action = argc == 13 ? "retake" : "import-procedural", .subjectId = argv[6],
       .operatorId = argv[10], .occurredAtUtc = argv[11]});
  if (!imported) { printError(imported.error()); return 1; }
  std::cout << seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"takeId", seam::formats::JsonValue{argv[6]}}, {"state", seam::formats::JsonValue{"MarkerReview"}},
      {"audioSha256", seam::formats::JsonValue{imported.value().sha256}},
      {"generation", seam::formats::JsonValue{std::to_string(imported.value().committedGeneration)}},
      {"projectSha256", seam::formats::JsonValue{imported.value().committedProjectSha256}},
      {"durabilityConfirmed", seam::formats::JsonValue{imported.value().durabilityConfirmed}},
      {"diagnostic", seam::formats::JsonValue{imported.value().diagnostic}}}}) << '\n';
  return 0;
}

void printUsage() {
  std::cout
      << "SEAM Voicebank CLI\n\n"
      << "Usage:\n"
      << "  seam_voicebank_cli validate MANIFEST [BANK_ROOT]\n"
      << "  seam_voicebank_cli inspect MANIFEST\n"
      << "  seam_voicebank_cli inspect-wav WAV\n"
      << "  seam_voicebank_cli convert-neural-vocabulary SOURCE_JSON SOURCE_SHA256 NEW_OUTPUT_JSON\n"
      << "  seam_voicebank_cli inspect-neural-bundle DIRECTORY MODEL_ID MODEL_VERSION MANIFEST_SHA256 MAX_PAYLOAD_BYTES\n"
      << "  seam_voicebank_cli prepare-neural-bundle DIRECTORY MODEL_ID MODEL_VERSION MAX_PAYLOAD_BYTES\n"
      << "  seam_voicebank_cli analyze WAV OUTPUT_DIRECTORY\n"
      << "  seam_voicebank_cli bake-project PROJECT OUTPUT_DIRECTORY [SAMPLE_RATE]\n"
      << "  seam_voicebank_cli run-generation JOB_DIRECTORY MANIFEST_SHA256\n"
      << "  seam_voicebank_cli run-generation-batch BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]\n"
      << "  seam_voicebank_cli import-generated-batch WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]\n"
      << "  seam_voicebank_cli prepare-generation WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY\n"
      << "  seam_voicebank_cli prepare-generation-batch OUTPUT_JSON JOB_REFERENCE [JOB_REFERENCE ...]\n"
      << "    Bakes unapproved candidates using saved recipe references; does not assign or approve takes.\n"
      << "  seam_voicebank_cli import-procedural WORKSPACE METADATA WAV RECIPE TAKE PROMPT COVERAGE MIDI OPERATOR UTC [SUPERSEDES]\n"
      << "  seam_voicebank_cli import-generated WORKSPACE METADATA WAV RECIPE EXPECTATION SHA256 OPERATOR UTC\n"
      << "    Collects generated output only against its retained expectation; never refreshes a stale request.\n"
      << "    Imports into an existing procedural producer workspace as unapproved MarkerReview material.\n";
  seam::voicebank_cli::printSampleReviewUsage();
  seam::voicebank_cli::printCampaignUsage();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc>=2 && std::string_view{argv[1]}=="prepare-neural-bundle") {
    if (argc!=6) {std::cerr<<"prepare-neural-bundle requires DIRECTORY MODEL_ID MODEL_VERSION MAX_PAYLOAD_BYTES\n"; return 1;}
    const std::string_view limitText{argv[5]}; std::size_t limit=0;
    const auto [end,error]=std::from_chars(limitText.data(),limitText.data()+limitText.size(),limit);
    if (error!=std::errc{} || end!=limitText.data()+limitText.size() || limit==0 || limit>512U*1024U*1024U) {
      std::cerr<<"Invalid payload limit\n"; return 1;
    }
    std::error_code ec;
    const auto root=std::filesystem::canonical(argv[2],ec);
    if (ec || !std::filesystem::is_directory(root,ec) || ec) {std::cerr<<"Bundle directory is unavailable\n"; return 1;}
    std::vector<std::vector<std::byte>> owned; owned.reserve(4);
    std::vector<seam::synthesis::NeuralBundleAssetInput> inputs; inputs.reserve(4);
    const std::array names{"acoustic","vocoder","vocabulary","configuration"};
    std::size_t total=0;
    for (std::size_t index=0;index<names.size();++index) {
      const auto path=root/names[index];
      const auto status=std::filesystem::symlink_status(path,ec);
      if (ec || !std::filesystem::is_regular_file(status) || total>=limit) {std::cerr<<"Missing regular asset or exhausted payload budget\n"; return 1;}
      const auto perAsset=index>=2?4U*1024U*1024U:256U*1024U*1024U;
      auto bytes=seam::core::readFileBytesLimited(path,std::min<std::size_t>(perAsset,limit-total));
      if (!bytes) {printError(bytes.error()); return 1;}
      total+=bytes.value().size(); owned.push_back(std::move(bytes.value()));
      inputs.push_back({static_cast<seam::synthesis::NeuralAssetRole>(index),names[index],owned.back(),seam::core::sha256Hex(owned.back())});
    }
    const auto manifest=seam::synthesis::FrozenNeuralBundle::manifest(inputs,limit);
    if (!manifest) {printError(manifest.error()); return 1;}
    const seam::domain::SingerResourceIdentity identity{seam::domain::SingerResourceKind::Neural,argv[3],argv[4],seam::core::sha256Hex(manifest.value())};
    const auto bundle=seam::synthesis::FrozenNeuralBundle::freeze(identity,inputs,limit);
    if (!bundle) {printError(bundle.error()); return 1;}
    const auto metadata=seam::neural_synthesis::inspectNeuralBundleMetadata(bundle.value());
    if (!metadata) {printError(metadata.error()); return 1;}
    const auto written=seam::core::durableAtomicWriteTextNew(root/"manifest.json",manifest.value());
    if (!written) {printError(written.error()); return 1;}
    const auto verified=seam::neural_synthesis::loadNeuralBundleDirectory(root,identity,limit);
    if (!verified) {printError(verified.error()); return 1;}
    using J=seam::formats::JsonValue;
    std::cout<<seam::formats::stringifyJson(J{J::Object{{"status","DATA_BUNDLE_PREPARED_UNAPPROVED"},
        {"manifestSha256",identity.contentHash},{"executionAdmitted",false},{"releaseEligible",false}}});
    return std::cout?0:1;
  }
  if (argc>=2 && std::string_view{argv[1]}=="inspect-neural-bundle") {
    if (argc!=7) {std::cerr<<"inspect-neural-bundle requires DIRECTORY MODEL_ID MODEL_VERSION MANIFEST_SHA256 MAX_PAYLOAD_BYTES\n"; return 1;}
    const std::string_view limitText{argv[6]}; std::size_t limit=0;
    const auto [end,error]=std::from_chars(limitText.data(),limitText.data()+limitText.size(),limit);
    if (error!=std::errc{} || end!=limitText.data()+limitText.size() || limit==0) {std::cerr<<"Invalid payload limit\n"; return 1;}
    const auto bundle=seam::neural_synthesis::loadNeuralBundleDirectory(argv[2],
        {seam::domain::SingerResourceKind::Neural,argv[3],argv[4],argv[5]},limit);
    if (!bundle) {printError(bundle.error()); return 1;}
    const auto metadata=seam::neural_synthesis::inspectNeuralBundleMetadata(bundle.value());
    if (!metadata) {printError(metadata.error()); return 1;}
    using J=seam::formats::JsonValue;
    std::cout<<seam::formats::stringifyJson(J{J::Object{{"status","METADATA_INSPECTED_ONLY"},
        {"modelId",metadata.value().model.modelId},{"modelVersion",metadata.value().model.modelVersion},
        {"manifestSha256",bundle.value().identity().contentHash},{"vocabularySha256",metadata.value().model.vocabularyHash},
        {"vocabularySize",static_cast<std::int64_t>(metadata.value().vocabulary.size())},
        {"configurationVersion",static_cast<std::int64_t>(metadata.value().configurationVersion)},
        {"assetCount",static_cast<std::int64_t>(bundle.value().assets().size())},
        {"sampleRate",static_cast<std::int64_t>(metadata.value().model.sampleRate)},
        {"hopSize",static_cast<std::int64_t>(metadata.value().model.hopSize)},
        {"stepsLayout",metadata.value().stepsLayout},{"vocoderOutput",metadata.value().vocoderOutput},
        {"executionAdmitted",false},{"releaseEligible",false}}});
    return std::cout?0:1;
  }
  if (argc>=2 && std::string_view{argv[1]}=="convert-neural-vocabulary") {
    if (argc!=5) {std::cerr<<"convert-neural-vocabulary requires SOURCE_JSON SOURCE_SHA256 NEW_OUTPUT_JSON\n"; return 1;}
    const auto source=seam::core::readTextFileLimited(argv[2],4U*1024U*1024U);
    if (!source) {printError(source.error()); return 1;}
    const auto sourceHash=seam::core::sha256Hex(source.value());
    if (sourceHash!=argv[3]) {std::cerr<<"Source vocabulary digest mismatch\n"; return 1;}
    const auto converted=seam::neural_synthesis::convertDiffSingerVocabulary(source.value());
    if (!converted) {printError(converted.error()); return 1;}
    const auto written=seam::core::durableAtomicWriteTextNew(argv[4],converted.value());
    if (!written) {printError(written.error()); return 1;}
    using J=seam::formats::JsonValue;
    std::cout<<seam::formats::stringifyJson(J{J::Object{{"status","CONVERTED_UNAPPROVED"},
        {"sourceSha256",sourceHash},{"vocabularySha256",seam::core::sha256Hex(converted.value())},
        {"releaseEligible",false}}});
    return std::cout?0:1;
  }
  if (argc < 2) {
    printUsage();
    return 1;
  }
  const std::string_view command{argv[1]};
  if (const auto result = seam::voicebank_cli::runCampaignCommand(argc, argv)) return *result;
  if (const auto result=seam::voicebank_cli::runSampleReviewCommand(argc,argv)) return *result;
  if (command == "import-procedural") return importProceduralCommand(argc, argv);
  if (command == "import-generated") return importGeneratedCommand(argc, argv);
  if (command == "run-generation") return runGenerationCommand(argc, argv);
  if (command == "run-generation-batch") return runGenerationBatchCommand(argc, argv);
  if (command == "import-generated-batch") return importGeneratedBatchCommand(argc, argv);
  if (command == "prepare-generation") return prepareGenerationCommand(argc, argv);
  if (command == "prepare-generation-batch") return prepareGenerationBatchCommand(argc, argv);
  if (command == "bake-project") return bakeProjectCommand(argc, argv);
  if (command == "--help" || command == "-h" || command == "help") {
    printUsage();
    return 0;
  }
  if (command == "validate") {
    if (argc < 3 || argc > 4) {
      printUsage();
      return 1;
    }
    return validateCommand(argv[2], argc == 4 ? std::filesystem::path{argv[3]}
                                               : std::filesystem::path{});
  }
  if (command == "inspect") {
    if (argc != 3) {
      printUsage();
      return 1;
    }
    return inspectCommand(argv[2]);
  }
  if (command == "analyze") {
    if (argc != 4) {
      printUsage();
      return 1;
    }
    return analyzeCommand(argv[2], argv[3]);
  }
  if (command == "inspect-wav") {
    if (argc != 3) {
      printUsage();
      return 1;
    }
    return inspectWavCommand(argv[2]);
  }
  std::cerr << "unknown command: " << command << '\n';
  printUsage();
  return 1;
}
