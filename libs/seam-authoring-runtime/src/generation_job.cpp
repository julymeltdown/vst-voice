#include "seam/authoring/generation_job.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/voice_design/procedural_candidate.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include <cmath>
#include <algorithm>
#include <charconv>

namespace seam::authoring {
namespace {
bool validHash(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool validJobId(std::string_view value) {
  return !value.empty() && value.size() <= 128U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}
core::Result<PreparedGenerationJob> fail() {
  return core::failure<PreparedGenerationJob>(core::ErrorCode::Conflict, "Generation job is incomplete, invalid or differs from its frozen request");
}
}

core::Result<PreparedGenerationJob> prepareGenerationJobFromScore(
    const std::filesystem::path& directory, std::string jobId,
    const std::filesystem::path& scorePath, domain::TrackId trackId, domain::RegionId regionId,
    const voicebank_production::VoicebankProductionProject& producer, std::string_view plannedTakeId, std::stop_token stopToken,
    std::string_view expectedScoreSha256, std::optional<GenerationRecipeSelection> selectedRecipe) {
  using Output = PreparedGenerationJob;
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Generation preparation cancelled"); };
  if (stopToken.stop_requested()) return cancelled();
  if (!trackId.valid() || !regionId.valid() || !validJobId(jobId))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation job/track/region identity is invalid");
  const auto& assignments = producer.unitAssignments;
  const auto matches = [&](const auto& assignment) { return assignment.plannedTakeId == plannedTakeId; };
  if (plannedTakeId.empty() || std::count_if(assignments.begin(), assignments.end(), matches) != 1)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Take must identify exactly one planned producer assignment");
  const auto& assignment = *std::find_if(assignments.begin(), assignments.end(), matches);
  std::error_code error;
  const auto path = std::filesystem::absolute(scorePath, error);
  if (error) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Cannot resolve generation score path");
  const auto scoreBytes = core::readTextFileLimited(path, 16U * 1024U * 1024U);
  if (!scoreBytes) return core::Result<Output>{scoreBytes.error()};
  if (!expectedScoreSha256.empty() && (!validHash(expectedScoreSha256) || core::sha256Hex(scoreBytes.value()) != expectedScoreSha256))
    return core::failure<Output>(core::ErrorCode::Conflict, "Score changed after generation region selection");
  auto project = formats::ProjectJsonCodec{}.decode(scoreBytes.value());
  if (!project) return core::Result<Output>{project.error()};
  auto* track = project.value().findVocalTrack(trackId);
  if (!track || (!selectedRecipe && !track->proceduralRecipe) || !track->findRegion(regionId))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Requested region must belong to a track with a saved procedural recipe");
  if (selectedRecipe) {
    const auto valid = voice_design::decodeVoiceRecipeResource(selectedRecipe->resource, stopToken);
    if (!valid) return core::Result<Output>{valid.error()};
    if (selectedRecipe->style.empty() || selectedRecipe->style.size() > 128U ||
        std::none_of(valid.value().poses.begin(), valid.value().poses.end(), [&](const auto& pose) { return pose.style == selectedRecipe->style; }))
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Selected Designer style is not present in the frozen recipe");
    // Only this owned score copy changes. The job package carries its verified
    // resource bytes; the user song and original external recipe stay untouched.
    track->proceduralRecipe = domain::ProceduralRecipeReference{selectedRecipe->resource.identity, "recipe.json", selectedRecipe->style};
  }
  const auto& reference = *track->proceduralRecipe;
  const auto rate = project.value().settings().sampleRate;
  if (!std::isfinite(rate) || rate != std::floor(rate) || rate < 8000.0 || rate > 384000.0)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Job sample rate must be an integer from 8000 to 384000");
  const auto recipe = selectedRecipe ? core::Result<synthesis::ProceduralSingerResource>{selectedRecipe->resource}
                                    : voice_design::loadVoiceRecipeResource(path.parent_path() / reference.path, reference.resource);
  if (!recipe) return core::Result<Output>{recipe.error()};
  if (stopToken.stop_requested()) return cancelled();
  const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(project.value(), recipe.value(), trackId, regionId,
      0U, rendering::RenderQuality::Final, static_cast<std::uint32_t>(rate), reference.style);
  if (!snapshot) return core::Result<Output>{snapshot.error()};
  if (stopToken.stop_requested()) return cancelled();
  return prepareGenerationJob(directory, std::move(jobId), snapshot.value(), producer,
      {.takeId = std::string{plannedTakeId}, .promptId = assignment.promptId, .coverageKey = assignment.coverageKey,
       .pitchLayer = assignment.pitchLayer, .supersedesTakeId = assignment.takeId, .style = assignment.style});
}

static core::Result<GenerationJobOutput> runGenerationJobImpl(const std::filesystem::path& directory,
    std::string_view expectedManifestSha256, std::stop_token stopToken,
    std::function<bool(ExportPublicationPhase)> publicationFaultInjector, bool existingOnly) {
  using Output = GenerationJobOutput;
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Generation job cancelled"); };
  if (stopToken.stop_requested()) return cancelled();
  const auto job = loadGenerationJob(directory, expectedManifestSha256);
  if (!job) return core::Result<Output>{job.error()};
  core::ExclusiveFileLock lock;
  const auto acquired = lock.acquire(directory / ".worker.lock");
  if (!acquired) return core::Result<Output>{acquired.error()};
  if (stopToken.stop_requested()) return cancelled();
  const auto destination = directory / "output";
  const auto recovered = ExportService{}.recoverSet(destination);
  if (!recovered && recovered.error().code != core::ErrorCode::NotFound) return core::Result<Output>{recovered.error()};
  std::error_code error;
  const bool reused = std::filesystem::is_directory(destination, error);
  if (error && error != std::errc::no_such_file_or_directory) return core::failure<Output>(core::ErrorCode::IoError, "Cannot inspect generation output");
  const auto& snapshot = job.value().snapshot;
  const auto& resource = std::get<synthesis::ProceduralSingerResource>(snapshot.resource);
  if (!reused) {
    if (existingOnly) return core::failure<Output>(core::ErrorCode::NotFound, "Generation job has no committed output to collect");
    ExportSettings settings; settings.sampleRate = snapshot.sampleRate;
    settings.includeMaster = false; settings.includeStems = false; settings.includeProceduralCandidates = true;
    settings.publicationFaultInjector = std::move(publicationFaultInjector);
    const std::vector<rendering::TrackSingerSource> sources{rendering::TrackProceduralSource{snapshot.trackId, resource, snapshot.style}};
    const auto exported = ExportService{}.exportSetWithSources(*snapshot.project, sources, snapshot.trackId, snapshot.segment.regionId,
        0U, destination, settings, {}, stopToken);
    if (!exported) return core::Result<Output>{exported.error()};
    if (exported.value().state != ExportState::Committed) return core::failure<Output>(
        core::ErrorCode::Conflict, "Generation publication requires recovery before output can be returned");
  }
  if (stopToken.stop_requested()) return cancelled();
  const auto prefix = destination / "candidates" / (snapshot.trackId.toString() + "-" + snapshot.segment.regionId.toString());
  const auto metadata = std::filesystem::path{prefix.string() + ".json"}, audio = std::filesystem::path{prefix.string() + ".wav"};
  const auto candidate = voice_design::loadProceduralCandidate(metadata, audio, resource, stopToken);
  if (!candidate) return core::Result<Output>{candidate.error()};
  const auto& expected = job.value().expectation;
  const auto notes = snapshot.compiledPerformance->notes();
  auto markers = rendering::projectProceduralMarkers(snapshot);
  if (!markers) return core::Result<Output>{markers.error()};
  for (auto& marker : markers.value()) {
    marker.ownedSpan.start -= notes.front().startFrame;
    marker.ownedSpan.end -= notes.front().startFrame;
  }
  if (candidate.value().renderContentHash != expected.renderContentHash || candidate.value().sampleRate != expected.sampleRate ||
      candidate.value().frameCount != expected.frameCount || candidate.value().style != expected.style ||
      candidate.value().scoreOriginFrame != notes.front().startFrame || candidate.value().markers != markers.value())
    return core::failure<Output>(core::ErrorCode::Conflict, "Published generation output differs from its frozen request");
  if (stopToken.stop_requested()) return cancelled();
  return Output{metadata, audio, candidate.value().audioSha256, reused};
}

core::Result<GenerationJobOutput> runGenerationJob(const std::filesystem::path& directory,
    std::string_view expectedManifestSha256, std::stop_token stopToken,
    std::function<bool(ExportPublicationPhase)> publicationFaultInjector) {
  return runGenerationJobImpl(directory, expectedManifestSha256, stopToken, std::move(publicationFaultInjector), false);
}

core::Result<GenerationJobOutput> verifyGenerationJobOutput(const std::filesystem::path& directory,
    std::string_view expectedManifestSha256, std::stop_token stopToken) {
  return runGenerationJobImpl(directory, expectedManifestSha256, stopToken, {}, true);
}

core::Result<GenerationJobReference> loadGenerationJobReference(const std::filesystem::path& path) {
  const auto invalid = [] { return core::failure<GenerationJobReference>(core::ErrorCode::InvalidArgument, "Generation job reference is invalid"); };
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) return invalid();
  const auto text = core::readTextFileLimited(absolute, 8192U);
  if (!text) return core::Result<GenerationJobReference>{text.error()};
  const auto parsed = formats::parseJson(text.value(), {.maximumInputBytes = 8192U, .maximumDepth = 2U,
      .maximumNodes = 12U, .maximumStringBytes = 4096U, .maximumCollectionEntries = 4U});
  if (!parsed || !parsed.value().isObject() || parsed.value().asObject().size() != 4U) return invalid();
  const auto& root = parsed.value();
  for (const auto* key : {"formatId", "directory", "manifestSha256"}) if (!root.find(key) || !root.find(key)->isString()) return invalid();
  if (root.find("formatId")->asString() != "com.project-seam.generation-job-reference" || !root.find("schemaVersion") ||
      !root.find("schemaVersion")->isInteger() || root.find("schemaVersion")->asInt64() != 1 || !validHash(root.find("manifestSha256")->asString())) return invalid();
  const auto& directory = root.find("directory")->asString();
  if (directory.empty() || std::any_of(directory.begin(), directory.end(), [](unsigned char c) { return c < 32U || c == 127U; })) return invalid();
  return GenerationJobReference{(absolute.parent_path() / directory).lexically_normal(), root.find("manifestSha256")->asString()};
}

core::Result<PreparedGenerationJob> loadGenerationJob(const std::filesystem::path& directory, std::string_view expectedManifestSha256) {
  if (!validHash(expectedManifestSha256)) return fail();
  const auto text = core::readTextFileLimited(directory / "job.json", 4096U);
  if (!text || core::sha256Hex(text.value()) != expectedManifestSha256) return fail();
  const auto parsed = formats::parseJson(text.value(), {.maximumInputBytes = 4096U, .maximumDepth = 2U,
      .maximumNodes = 16U, .maximumStringBytes = 128U, .maximumCollectionEntries = 7U});
  if (!parsed || !parsed.value().isObject() || parsed.value().asObject().size() != 7U) return fail();
  const auto& root = parsed.value();
  for (const auto* key : {"formatId", "jobId", "projectSha256", "recipeSha256", "expectationSha256", "sourceProjectId"})
    if (!root.find(key) || !root.find(key)->isString()) return fail();
  if (!root.find("schemaVersion") || !root.find("schemaVersion")->isInteger() || root.find("schemaVersion")->asInt64() != 1 ||
      root.find("formatId")->asString() != "com.project-seam.generation-job" || !validJobId(root.find("jobId")->asString())) return fail();
  for (const auto* key : {"projectSha256", "recipeSha256", "expectationSha256"}) if (!validHash(root.find(key)->asString())) return fail();
  const auto& sourceText = root.find("sourceProjectId")->asString();
  std::uint64_t sourceId = 0U;
  const auto id = std::from_chars(sourceText.data(), sourceText.data() + sourceText.size(), sourceId);
  if (id.ec != std::errc{} || id.ptr != sourceText.data() + sourceText.size() || sourceId == 0U || std::to_string(sourceId) != sourceText) return fail();
  auto expectation = voicebank_production::loadGenerationImportExpectation(directory / "expectation.json", root.find("expectationSha256")->asString());
  if (!expectation) return core::Result<PreparedGenerationJob>{expectation.error()};
  const auto projectText = core::readTextFileLimited(directory / "project.json", 16U * 1024U * 1024U);
  const auto recipeText = core::readTextFileLimited(directory / "recipe.json", 1024U * 1024U);
  if (!projectText || !recipeText || core::sha256Hex(projectText.value()) != root.find("projectSha256")->asString() ||
      core::sha256Hex(recipeText.value()) != root.find("recipeSha256")->asString()) return fail();
  const auto project = formats::ProjectJsonCodec{}.decode(projectText.value());
  const auto recipe = voice_design::decodeVoiceRecipe(recipeText.value());
  if (!project || !recipe || project.value().vocalTracks().size() != 1U || project.value().vocalTracks().front().regions.size() != 1U) return fail();
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe.value());
  if (!resource || resource.value().identity.id != expectation.value().recipeId || resource.value().identity.version != expectation.value().recipeVersion ||
      resource.value().identity.contentHash != expectation.value().recipeHash || resource.value().identity.contentHash != root.find("recipeSha256")->asString()) return fail();
  const auto& track = project.value().vocalTracks().front();
  auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(project.value(), resource.value(), track.id, track.regions.front().id,
      0U, rendering::RenderQuality::Final, expectation.value().sampleRate, expectation.value().style);
  if (!snapshot || snapshot.value().contentHash != expectation.value().renderContentHash) return fail();
  const auto notes = snapshot.value().compiledPerformance->notes();
  if (notes.back().endFrame - notes.front().startFrame != expectation.value().frameCount) return fail();
  snapshot.value().sourceProjectId = domain::ProjectId{sourceId};
  return PreparedGenerationJob{root.find("jobId")->asString(), std::string{expectedManifestSha256}, std::move(snapshot.value()), std::move(expectation.value())};
}

core::Result<PreparedGenerationJob> prepareGenerationJob(const std::filesystem::path& directory, std::string jobId,
    const rendering::RenderSnapshot& snapshot, const voicebank_production::VoicebankProductionProject& producer,
    const voicebank_production::RawTakeInput& take) {
  if (!validJobId(jobId) || snapshot.quality != rendering::RenderQuality::Final || snapshot.ownedFrames || !snapshot.sourceProjectId.valid()) return fail();
  const auto valid = rendering::validateProceduralSnapshot(snapshot);
  if (!valid) return core::Result<PreparedGenerationJob>{valid.error()};
  const auto& resource = std::get<synthesis::ProceduralSingerResource>(snapshot.resource);
  const auto notes = snapshot.compiledPerformance->notes();
  if (std::any_of(notes.begin(), notes.end(), [&](const auto& note) { return note.midiKey != take.pitchLayer; })) return fail();
  const auto expectation = voicebank_production::captureGenerationImportExpectation(producer, take, resource,
      snapshot.style, snapshot.contentHash, snapshot.sampleRate, notes.back().endFrame - notes.front().startFrame);
  if (!expectation) return core::Result<PreparedGenerationJob>{expectation.error()};
  const auto projectText = formats::ProjectJsonCodec{}.encode(*snapshot.project);
  if (!projectText || projectText.value().size() > 16U * 1024U * 1024U || resource.patch->bytes().size() > 1024U * 1024U) return fail();
  std::error_code error;
  if (!std::filesystem::create_directory(directory, error) || error) return core::failure<PreparedGenerationJob>(
      core::ErrorCode::Conflict, "Generation job directory must be new with an existing parent");
  const auto scoreWritten = core::durableAtomicWriteTextNew(directory / "project.json", projectText.value());
  if (!scoreWritten) return core::Result<PreparedGenerationJob>{scoreWritten.error()};
  const auto bytes = resource.patch->bytes();
  const auto recipeWritten = core::durableAtomicWriteTextNew(directory / "recipe.json", std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  if (!recipeWritten) return core::Result<PreparedGenerationJob>{recipeWritten.error()};
  const auto expectationHash = voicebank_production::saveGenerationImportExpectation(directory / "expectation.json", expectation.value());
  if (!expectationHash) return core::Result<PreparedGenerationJob>{expectationHash.error()};
  using formats::JsonValue;
  const auto manifest = formats::stringifyJson(JsonValue{JsonValue::Object{
      {"formatId", JsonValue{"com.project-seam.generation-job"}}, {"schemaVersion", JsonValue{std::int64_t{1}}},
      {"jobId", JsonValue{jobId}}, {"projectSha256", JsonValue{core::sha256Hex(projectText.value())}},
      {"recipeSha256", JsonValue{resource.identity.contentHash}}, {"expectationSha256", JsonValue{expectationHash.value()}},
      {"sourceProjectId", JsonValue{std::to_string(snapshot.sourceProjectId.value())}}}});
  const auto manifestHash = core::sha256Hex(manifest);
  const auto reference = formats::stringifyJson(JsonValue{JsonValue::Object{
      {"formatId", JsonValue{"com.project-seam.generation-job-reference"}}, {"schemaVersion", JsonValue{std::int64_t{1}}},
      {"directory", JsonValue{"."}}, {"manifestSha256", JsonValue{manifestHash}}}});
  const auto referenceWritten = core::durableAtomicWriteTextNew(directory / "job.seamjob", reference);
  if (!referenceWritten) return core::Result<PreparedGenerationJob>{referenceWritten.error()};
  const auto published = core::durableAtomicWriteTextNew(directory / "job.json", manifest);
  if (!published) return core::Result<PreparedGenerationJob>{published.error()};
  return loadGenerationJob(directory, manifestHash);
}
}
