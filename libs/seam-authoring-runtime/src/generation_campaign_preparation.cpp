#include "seam/authoring/generation_campaign.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>

namespace seam::authoring {
core::Result<PreparedCampaignBatch> prepareGenerationCampaignBatch(
    std::string_view definition, std::string_view campaignSha256, std::size_t batchIndex,
    const voicebank_production::VoicebankProductionProject& producer, const std::filesystem::path& directory,
    std::optional<voicebank_production::ProductionCommitReceipt> predecessor, std::stop_token stop) {
  using Output = PreparedCampaignBatch;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::Conflict, std::move(message)); };
  const auto valid = verifyGenerationCampaign(definition, campaignSha256, stop);
  if (!valid) return core::Result<Output>{valid.error()};
  const auto parsed = formats::parseJson(definition);
  const auto& plan = parsed.value();
  if (batchIndex >= static_cast<std::size_t>(plan.find("batchCount")->asInt64())) return fail("Campaign batch index is out of range");
  const auto producerJson = voicebank_production::encodeProductionProject(producer);
  const auto producerHash = core::sha256Hex(producerJson);
  const auto initial = voicebank_production::decodeProductionProject(plan.find("initialProducerJson")->asString());
  if (batchIndex == 0U) {
    if (predecessor || producerHash != plan.find("initialProducerSha256")->asString()) return fail("First campaign batch has a stale producer");
  } else if (!predecessor || !predecessor->durabilityConfirmed || predecessor->committedProjectSha256 != producerHash ||
      predecessor->committedGeneration != producer.lastDurableGeneration ||
      producer.lastDurableGeneration < initial.value().lastDurableGeneration ||
      producer.lastDurableGeneration - initial.value().lastDurableGeneration != batchIndex)
    return fail("Later campaign batch requires its exact confirmed predecessor receipt");
  const auto recipe = voice_design::decodeVoiceRecipe(plan.find("recipeJson")->asString());
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe.value(), stop);
  if (!resource) return core::Result<Output>{resource.error()};
  const auto intent = formats::stringifyJson(formats::JsonValue::Object{
      {"campaignSha256", std::string{campaignSha256}}, {"batchIndex", static_cast<std::int64_t>(batchIndex)},
      {"producerJson", producerJson}}, true);
  std::error_code error;
  const auto directoryStatus = std::filesystem::symlink_status(directory, error);
  const bool missing = error == std::errc::no_such_file_or_directory || (!error && directoryStatus.type() == std::filesystem::file_type::not_found);
  if (stop.stop_requested()) return fail("Campaign batch preparation cancelled");
  if (missing) {
    error.clear();
    if (!std::filesystem::create_directory(directory, error) || error) return fail("Cannot create new campaign batch directory");
  } else if (error || !std::filesystem::is_directory(directoryStatus) || std::filesystem::is_symlink(directoryStatus)) return fail("Campaign batch directory is unsafe");
  core::ExclusiveFileLock lock;
  const auto locked = lock.acquire(directory / ".batch-prepare.lock");
  if (!locked) return core::Result<Output>{locked.error()};
  if (missing) {
    const auto written = core::durableAtomicWriteTextNew(directory / "batch-inputs.json", intent);
    if (!written) return core::Result<Output>{written.error()};
  } else {
    const auto retained = core::readTextFileLimited(directory / "batch-inputs.json", 64U * 1024U * 1024U);
    if (!retained || retained.value() != intent) return fail("Campaign batch original inputs differ or are missing");
  }
  Output output;
  for (const auto& row : plan.find("jobs")->asArray()) {
    if (static_cast<std::size_t>(row.find("batchIndex")->asInt64()) != batchIndex) continue;
    if (stop.stop_requested()) return fail("Campaign batch preparation cancelled; retained jobs may be resumed");
    const auto& takeId = row.find("takeId")->asString();
    auto score = buildInventoryGenerationScore(producer, takeId);
    if (!score) return core::Result<Output>{score.error()};
    const auto scoreJson = formats::ProjectJsonCodec{}.encode(score.value().project);
    if (!scoreJson || scoreJson.value() != row.find("scoreJson")->asString()) return fail("Campaign score differs from frozen template");
    const auto assignment = std::find_if(producer.unitAssignments.begin(), producer.unitAssignments.end(),
        [&](const auto& item) { return item.plannedTakeId == takeId; });
    score.value().project.findVocalTrack(score.value().trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
        resource.value().identity, "recipe.json", assignment->style};
    const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(score.value().project, resource.value(),
        score.value().trackId, score.value().regionId, 0U, rendering::RenderQuality::Final,
        static_cast<std::uint32_t>(score.value().project.settings().sampleRate), assignment->style);
    if (!snapshot) return core::Result<Output>{snapshot.error()};
    const auto jobId = "inventory-v1-" + core::sha256Hex(score.value().templateIdentity + "\n" + takeId);
    const auto jobDirectory = directory / jobId;
    const voicebank_production::RawTakeInput take{.takeId = takeId, .promptId = assignment->promptId,
        .coverageKey = assignment->coverageKey, .pitchLayer = assignment->pitchLayer,
        .supersedesTakeId = assignment->takeId, .style = assignment->style};
    error.clear();
    const auto jobStatus = std::filesystem::symlink_status(jobDirectory, error);
    const bool absent = error == std::errc::no_such_file_or_directory || (!error && jobStatus.type() == std::filesystem::file_type::not_found);
    if (error && !absent) return fail("Cannot inspect retained campaign job");
    const auto job = absent ? prepareGenerationJob(jobDirectory, jobId, snapshot.value(), producer, take)
                            : resumeGenerationJobPreparation(jobDirectory, jobId, snapshot.value(), producer, take);
    if (!job) return core::Result<Output>{job.error()};
    output.jobs.push_back({std::filesystem::absolute(jobDirectory), job.value().manifestSha256});
  }
  const GenerationBatchLimits limits{static_cast<std::size_t>(plan.find("batchMaximumJobs")->asInt64()),
      static_cast<std::uint64_t>(plan.find("batchMaximumFrames")->asInt64())};
  const auto manifestPath = directory / "batch.json";
  error.clear();
  if (std::filesystem::exists(manifestPath, error)) {
    const auto hash = core::sha256File(manifestPath);
    if (!hash) return core::Result<Output>{hash.error()};
    const auto retained = loadGenerationBatch(manifestPath, hash.value());
    if (!retained || retained.value().size() != output.jobs.size()) return fail("Retained campaign batch differs");
    for (std::size_t index = 0; index < output.jobs.size(); ++index)
      if (retained.value()[index].directory.lexically_normal() != output.jobs[index].directory.lexically_normal() ||
          retained.value()[index].manifestSha256 != output.jobs[index].manifestSha256) return fail("Retained campaign batch job differs");
    output.batchSha256 = hash.value();
  } else {
    if (error) return fail("Cannot inspect campaign batch manifest");
    const auto saved = saveGenerationBatch(manifestPath, output.jobs, limits, stop, producerHash);
    if (!saved) return core::Result<Output>{saved.error()};
    output.batchSha256 = saved.value();
  }
  return output;
}
}
